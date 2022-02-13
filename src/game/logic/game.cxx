#include "game.hxx"
#include "durak/card.hxx"
#include "durak/game.hxx"
#include "durak/gameData.hxx"
#include "gameEvent.hxx"
#include "src/serialization.hxx"
#include "src/server/user.hxx"
#include "src/util.hxx"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/optional.hpp>
#include <boost/sml.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <chrono>
#include <cmath>
#include <concepts>
#include <confu_json/concept.hxx>
#include <confu_json/confu_json.hxx>
#include <cstddef>
#include <cstdlib>
#include <durak/gameOption.hxx>
#include <fmt/format.h>
#include <iostream>
#include <magic_enum.hpp>
#include <optional>
#include <pipes/push_back.hpp>
#include <pipes/unzip.hpp>
#include <queue>
#include <range/v3/algorithm/find.hpp>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/for_each.hpp>
#include <range/v3/algorithm/transform.hpp>
#include <range/v3/all.hpp>
#include <range/v3/iterator/insert_iterators.hpp>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/transform.hpp>
#include <string>
#include <utility>
#include <vector>
struct Chill
{
};
struct chill
{
};

struct AskDef
{
};
struct AskAttackAndAssist
{
};

struct askAttackAndAssist
{
};
struct askDef
{
};

struct PassAttackAndAssist
{
  bool attack{};
  bool assist{};
};

struct gameOver
{
};

struct initTimer
{
};
struct nextRoundTimer
{
};
struct pauseTimer
{
  std::vector<std::string> playersToPause{};
};

struct resumeTimer
{
  std::vector<std::string> playersToResume{};
};
enum struct TimerType
{
  noTimer,
  resetTimeOnNewRound,
  addTimeOnNewRound
};

struct TimerOption
{
  shared_class::TimerType timerType{};
  std::chrono::seconds timeAtStart{};
  std::chrono::seconds timeForEachRound{};
};

struct userRelogged
{
  std::string accountName{};
};

struct start
{
};

struct sendTimerEv
{
};
using namespace boost::sml;

struct GameDependencies
{

  GameDependencies (matchmaking_game::StartGame const &startGame, std::string const &gameName_, std::list<User> &&users_) : users{ std::move (users_) }, isRanked{ startGame.ratedGame }, gameName{ gameName_ } {}
  TimerOption timerOption{};
  durak::Game game{};
  std::list<User> users{};
  PassAttackAndAssist passAttackAndAssist{};
  bool isRanked{};
  std::string gameName{};
};

auto const timerActive = [] (GameDependencies &gameDependencies) { return gameDependencies.timerOption.timerType != shared_class::TimerType::noTimer; };

auto const handleGameOver = [] (boost::optional<durak::Player> const &durak, std::list<User> &users, bool isRanked) {
  if (durak)
    {
      ranges::for_each (users, [durak = durak->id] (User &user) {
        if (user.accountName == durak) user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakGameOverLose{}));
        else
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakGameOverWon{}));
      });
    }
  else
    {
      ranges::for_each (users, [] (auto &user) { user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakGameOverDraw{})); });
    }
  if (isRanked)
    {
      if (durak)
        {
          // TODO send to matchmaking the winners and losers
          // TODO send to every user in game the winners and losers
        }
      else
        {
          // TODO send to matchmaking the player which have a draw
          // TODO send to every user in game the player which have a draw
        }
    }
};

inline void
removeUserFromGame (std::string const &userToRemove, durak::Game &game, std::list<User> &users, bool isRanked)
{
  if (not game.checkIfGameIsOver ())
    {
      game.removePlayer (userToRemove);
      handleGameOver (game.durak (), users, isRanked);
    }
}

boost::asio::awaitable<void> inline runTimer (std::shared_ptr<boost::asio::system_timer> timer, std::string const &accountName, durak::Game &game, std::list<User> &users, bool isRanked)
{
  try
    {
      co_await timer->async_wait (boost::asio::use_awaitable);
      removeUserFromGame (accountName, game, users, isRanked);
      ranges::for_each (users, [] (auto const &user) { user.timer->cancel (); });
    }
  catch (boost::system::system_error &e)
    {
      using namespace boost::system::errc;
      if (operation_canceled == e.code ())
        {
          // swallow cancel
        }
      else
        {
          std::cout << "error in timer boost::system::errc: " << e.code () << std::endl;
          abort ();
        }
    }
}

auto const sendTimer = [] (GameDependencies &gameDependencies) {
  if (gameDependencies.timerOption.timerType != shared_class::TimerType::noTimer)
    {
      auto durakTimers = shared_class::DurakTimers{};
      ranges::for_each (gameDependencies.users, [&durakTimers] (User const &user) {
        if (user.pausedTime)
          {
            durakTimers.pausedTimeUserDurationMilliseconds.push_back (std::make_pair (user.accountName, user.pausedTime->count ()));
          }
        else
          {
            using namespace std::chrono;
            durakTimers.runningTimeUserTimePointMilliseconds.push_back (std::make_pair (user.accountName, duration_cast<milliseconds> (user.timer->expiry ().time_since_epoch ()).count ()));
          }
      });
      ranges::for_each (gameDependencies.users, [&durakTimers] (User const &user) { user.sendMsgToUser (objectToStringWithObjectName (durakTimers)); });
    }
};

auto const initTimerHandler = [] (GameDependencies &gameDependencies, boost::sml::back::process<resumeTimer> process_event) {
  using namespace std::chrono;
  ranges::for_each (gameDependencies.users, [&gameDependencies] (auto &user) { user.pausedTime = gameDependencies.timerOption.timeAtStart; });
  if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer ())
    {
      process_event (resumeTimer{ { attackingPlayer->id } });
    }
};

auto const pauseTimerHandler = [] (GameDependencies &gameDependencies, pauseTimer const &pauseTimerEv) {
  ranges::for_each (gameDependencies.users, [&playersToPausetime = pauseTimerEv.playersToPause] (auto &user) {
    using namespace std::chrono;
    if (ranges::find (playersToPausetime, user.accountName) != playersToPausetime.end ())
      {
        user.pausedTime = duration_cast<milliseconds> (user.timer->expiry () - system_clock::now ());
        user.timer->cancel ();
      }
  });
};

auto const nextRoundTimerHandler = [] (GameDependencies &gameDependencies, boost::sml::back::process<resumeTimer, sendTimerEv> process_event) {
  using namespace std::chrono;
  ranges::for_each (gameDependencies.users, [&timerOption = gameDependencies.timerOption] (auto &user) {
    if (timerOption.timerType == shared_class::TimerType::addTimeOnNewRound)
      {
        user.pausedTime = timerOption.timeForEachRound + duration_cast<milliseconds> (user.timer->expiry () - system_clock::now ());
      }
    else
      {
        user.pausedTime = timerOption.timeForEachRound;
      }
    user.timer->cancel ();
  });
  if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer ())
    {
      process_event (resumeTimer{ { attackingPlayer->id } });
      process_event (sendTimerEv{});
    }
};

inline void
sendAllowedMovesForUserWithName (durak::Game &game, std::list<User> &users, std::string const &userName)
{
  if (auto user = ranges::find_if (users, [&userName] (auto const &user) { return user.accountName == userName; }); user != users.end ())
    {
      user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ calculateAllowedMoves (game, game.getRoleForName (userName)) }));
    }
}

auto const userReloggedInChillState = [] (GameDependencies &gameDependencies, userRelogged const &userReloggedEv) {
  if (auto user = ranges::find_if (gameDependencies.users, [userName = userReloggedEv.accountName] (auto const &user) { return user.accountName == userName; }); user != gameDependencies.users.end ())
    {
      user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ calculateAllowedMoves (gameDependencies.game, gameDependencies.game.getRoleForName (userReloggedEv.accountName)) }));
    }
};

auto const userReloggedInAskDef = [] (GameDependencies &gameDependencies, userRelogged const &userReloggedEv) {
  if (gameDependencies.game.getRoleForName (userReloggedEv.accountName) == durak::PlayerRole::defend)
    {
      if (auto user = ranges::find_if (gameDependencies.users, [userName = userReloggedEv.accountName] (auto const &user) { return user.accountName == userName; }); user != gameDependencies.users.end ())
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ { shared_class::Move::AnswerDefendWantsToTakeCardsYes, shared_class::Move::AnswerDefendWantsToTakeCardsNo } }));
        }
    }
};

auto const defendsWantsToTakeCardsSendMovesToAttackOrAssist = [] (durak::Game &game, durak::PlayerRole playerRole, User &user) {
  auto allowedMoves = calculateAllowedMoves (game, playerRole);
  if (ranges::find_if (allowedMoves, [] (auto allowedMove) { return allowedMove == shared_class::Move::AddCards; }) != allowedMoves.end ())

    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ { shared_class::Move::AttackAssistDoneAddingCards, shared_class::Move::AddCards } }));
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ { shared_class::Move::AttackAssistDoneAddingCards } }));
    }
};

auto const userReloggedInAskAttackAssist = [] (GameDependencies &gameDependencies, userRelogged const &userReloggedEv) {
  if (auto user = ranges::find_if (gameDependencies.users, [userName = userReloggedEv.accountName] (auto const &user) { return user.accountName == userName; }); user != gameDependencies.users.end ())
    {
      if ((not gameDependencies.passAttackAndAssist.assist && gameDependencies.game.getRoleForName (userReloggedEv.accountName) == durak::PlayerRole::assistAttacker) || (not gameDependencies.passAttackAndAssist.attack && gameDependencies.game.getRoleForName (userReloggedEv.accountName) == durak::PlayerRole::attack))
        {
          defendsWantsToTakeCardsSendMovesToAttackOrAssist (gameDependencies.game, gameDependencies.game.getRoleForName (userReloggedEv.accountName), *user);
        }
    }
};
auto const blockEverythingExceptStartAttack = AllowedMoves{ .defend = std::vector<shared_class::Move>{}, .attack = std::vector<shared_class::Move>{ shared_class::Move::AddCards }, .assist = std::vector<shared_class::Move>{} };
auto const roundStartSendAllowedMovesAndGameData = [] (GameDependencies &gameDependencies) {
  sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users);
  sendAvailableMoves (gameDependencies.game, gameDependencies.users, blockEverythingExceptStartAttack);
};

auto const resumeTimerHandler = [] (GameDependencies &gameDependencies, resumeTimer const &resumeTimerEv) {
  ranges::for_each (gameDependencies.users, [playersToResume = resumeTimerEv.playersToResume, &gameDependencies] (User &user) {
    if (ranges::find (playersToResume, user.accountName) != playersToResume.end ())
      {
        if (user.pausedTime)
          {
            user.timer->expires_after (user.pausedTime.value ());
          }
        else
          {
            std::cout << "tried to resume but pausedTime has no value" << std::endl;
            // abort ();
          }
        user.pausedTime = {};
        co_spawn (
            user.timer->get_executor (), [playersToResume = std::move (playersToResume), &gameDependencies, &user] () { return runTimer (user.timer, user.accountName, gameDependencies.game, gameDependencies.users, gameDependencies.isRanked); }, printException);
      }
  });
};

auto const isDefendingPlayer = [] (GameDependencies &gameDependencies, auto const &defendAndUser) {
  auto [defendEvent, user] = defendAndUser;
  return gameDependencies.game.getRoleForName (user.accountName) == durak::PlayerRole::defend;
};
auto const isAttackingOrAssistingPlayer = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAttack, User &> const &attackEvAndUser) {
  auto [event, user] = attackEvAndUser;
  auto const &playerRole = gameDependencies.game.getRoleForName (user.accountName);
  return playerRole == durak::PlayerRole::attack or playerRole == durak::PlayerRole::assistAttacker;
};
auto const isNotFirstRound = [] (GameDependencies &gameDependencies) { return gameDependencies.game.getRound () > 1; };

auto const setAttackAnswer = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAttackPass, User &> const &attackPassEventAndUser, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) {
  auto [attackPassEvent, user] = attackPassEventAndUser;
  if (not gameDependencies.passAttackAndAssist.attack)
    {
      if (gameDependencies.game.getRoleForName (user.accountName) == durak::PlayerRole::attack)
        {
          gameDependencies.passAttackAndAssist.attack = true;
          process_event (pauseTimer{ { user.accountName } });
          process_event (sendTimerEv{});
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ {} }));
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsSuccess{}));
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError{ "role is not attack" }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError{ "pass already set" }));
    }
};

// TODO
auto const setAssistAnswer = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAssistPass, User &> const &assistPassEventAndUser, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) {
  auto [assistPassEvent, user] = assistPassEventAndUser;
  if (not gameDependencies.passAttackAndAssist.assist)
    {
      if (gameDependencies.game.getRoleForName (user.accountName) == durak::PlayerRole::assistAttacker)
        {
          gameDependencies.passAttackAndAssist.assist = true;
          process_event (pauseTimer{ { user.accountName } });
          process_event (sendTimerEv{});
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ {} }));
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsSuccess{}));
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError{ "role is not assist" }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError{ "pass already set" }));
    }
};

auto const checkData = [] (GameDependencies &gameDependencies, boost::sml::back::process<askDef> process_event) {
  if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer (); not attackingPlayer || attackingPlayer->getCards ().empty ())
    {
      gameDependencies.passAttackAndAssist.attack = true;
    }
  if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer (); not assistingPlayer || assistingPlayer->getCards ().empty ())
    {
      gameDependencies.passAttackAndAssist.assist = true;
    }
  if (gameDependencies.passAttackAndAssist.assist && gameDependencies.passAttackAndAssist.attack && gameDependencies.game.countOfNotBeatenCardsOnTable () == 0)
    {
      process_event (askDef{});
    }
};

auto const checkAttackAndAssistAnswer = [] (GameDependencies &gameDependencies, boost::sml::back::process<chill, nextRoundTimer, sendTimerEv> process_event) {
  if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer (); not attackingPlayer || attackingPlayer->getCards ().empty ())
    {
      gameDependencies.passAttackAndAssist.attack = true;
    }
  if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer (); not assistingPlayer || assistingPlayer->getCards ().empty ())
    {
      gameDependencies.passAttackAndAssist.assist = true;
    }
  if (gameDependencies.passAttackAndAssist.attack && gameDependencies.passAttackAndAssist.assist)
    {
      gameDependencies.game.nextRound (true);
      if (gameDependencies.game.checkIfGameIsOver ())
        {
          handleGameOver (gameDependencies.game.durak (), gameDependencies.users, gameDependencies.isRanked);
        }
      process_event (chill{});
    }
};

auto const startAskDef = [] (GameDependencies &gameDependencies, boost::sml::back::process<pauseTimer, resumeTimer, sendTimerEv> process_event) {
  if (auto defendingPlayer = gameDependencies.game.getDefendingPlayer ())
    {
      process_event (resumeTimer{ { defendingPlayer->id } });
      if (auto user = ranges::find_if (gameDependencies.users, [&defendingPlayer] (auto const &user) { return user.accountName == defendingPlayer->id; }); user != gameDependencies.users.end ())
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCards{}));
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ { shared_class::Move::AnswerDefendWantsToTakeCardsYes, shared_class::Move::AnswerDefendWantsToTakeCardsNo } }));
        }
      if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer ())
        {
          process_event (pauseTimer{ { attackingPlayer->id } });
        }
      if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer ())
        {
          process_event (pauseTimer{ { assistingPlayer->id } });
        }
      process_event (sendTimerEv{});
    }
};

auto const userLeftGame = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakLeaveGame, User &> const &leaveGameEventUser) {
  auto [event, user] = leaveGameEventUser;
  removeUserFromGame (user.accountName, gameDependencies.game, gameDependencies.users, gameDependencies.isRanked);
  ranges::for_each (gameDependencies.users, [] (auto const &user_) { user_.timer->cancel (); });
  if (auto userItr = ranges::find_if (gameDependencies.users, [accountName = user.accountName] (User const &user_) { return user_.accountName == accountName; }); userItr != gameDependencies.users.end ())
    {
      gameDependencies.users.erase (userItr);
    }
};

auto const startAskAttackAndAssist = [] (GameDependencies &gameDependencies, boost::sml::back::process<pauseTimer, nextRoundTimer, resumeTimer, sendTimerEv> process_event) {
  if (auto defendingPlayer = gameDependencies.game.getDefendingPlayer ())
    {
      process_event (pauseTimer{ { defendingPlayer->id } });
    }
  gameDependencies.passAttackAndAssist = PassAttackAndAssist{};
  if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer (); attackingPlayer && not attackingPlayer->getCards ().empty ())
    {
      process_event (resumeTimer{ { attackingPlayer->id } });
      if (auto user = ranges::find_if (gameDependencies.users, [&attackingPlayer] (auto const &user) { return user.accountName == attackingPlayer->id; }); user != gameDependencies.users.end ())
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoYouWantToAddCards{}));
          defendsWantsToTakeCardsSendMovesToAttackOrAssist (gameDependencies.game, durak::PlayerRole::attack, *user);
        }
    }
  else
    {
      gameDependencies.passAttackAndAssist.attack = true;
    }
  if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer (); assistingPlayer && not assistingPlayer->getCards ().empty ())
    {
      process_event (resumeTimer{ { assistingPlayer->id } });
      if (auto user = ranges::find_if (gameDependencies.users, [&assistingPlayer] (auto const &user) { return user.accountName == assistingPlayer->id; }); user != gameDependencies.users.end ())
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoYouWantToAddCards{}));
          defendsWantsToTakeCardsSendMovesToAttackOrAssist (gameDependencies.game, durak::PlayerRole::assistAttacker, *user);
        }
    }
  else
    {
      gameDependencies.passAttackAndAssist.assist = true;
    }
  if (gameDependencies.passAttackAndAssist.assist && gameDependencies.passAttackAndAssist.attack)
    {
      gameDependencies.game.nextRound (true);
      if (gameDependencies.game.checkIfGameIsOver ())
        {
          handleGameOver (gameDependencies.game.durak (), gameDependencies.users, gameDependencies.isRanked);
        }
    }
  else
    {
      process_event (sendTimerEv{});
    }
};

// TODO
auto const doPass = [] (GameDependencies &gameDependencies, auto const &eventAndUser, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) {
  auto [event, user] = eventAndUser;
  auto playerName = user.accountName;
  using eventType = typename std::decay<decltype (event)>::type;
  using eventErrorType = typename std::decay<decltype ([&] {
    if constexpr (std::same_as<eventType, shared_class::DurakAttackPass>)
      {
        return shared_class::DurakAttackPassError{};
      }
    else
      {
        return shared_class::DurakAssistPassError{};
      }
  }())>::type;

  auto playerRole = gameDependencies.game.getRoleForName (playerName);
  if (gameDependencies.game.getAttackStarted ())
    {
      if (gameDependencies.game.countOfNotBeatenCardsOnTable () == 0)
        {
          if (playerRole == durak::PlayerRole::attack || playerRole == durak::PlayerRole::assistAttacker)
            {
              if (playerRole == durak::PlayerRole::attack)
                {
                  gameDependencies.passAttackAndAssist.attack = true;
                  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackPassSuccess{}));
                }
              else
                {
                  gameDependencies.passAttackAndAssist.assist = true;
                  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAssistPassSuccess{}));
                }
              user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{}));
              process_event (pauseTimer{ { playerName } });
              process_event (sendTimerEv{});
            }
          else
            {
              user.sendMsgToUser (objectToStringWithObjectName (eventErrorType{ "account role is not attack or assist: " + playerName }));
            }
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (eventErrorType{ "there are not beaten cards on the table" }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (eventErrorType{ "can not pass if attack is not started" }));
    }
};

auto const setAttackPass = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAttackPass, User &> const &durakAttackPassAndUser, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) { doPass (gameDependencies, durakAttackPassAndUser, process_event); };

auto const setAssistPass = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAssistPass, User &> const &durakAssistPassAndUser, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) { doPass (gameDependencies, durakAssistPassAndUser, process_event); };

// TODO DurakAskDefendWantToTakeCardsAnswer
auto const handleDefendSuccess = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &defendAnswerEventAndUser) {
  auto [event, user] = defendAnswerEventAndUser;
  gameDependencies.game.nextRound (false);
  if (gameDependencies.game.checkIfGameIsOver ())
    {
      handleGameOver (gameDependencies.game.durak (), gameDependencies.users, gameDependencies.isRanked);
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswerSuccess{}));
    }
};

auto const handleDefendPass = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakDefendPass, User &> const &durakDefendPassAndUser, boost::sml::back::process<askAttackAndAssist> process_event) {
  auto [defendPassEv, user] = durakDefendPassAndUser;
  if (gameDependencies.game.getRoleForName (user.accountName) == durak::PlayerRole::defend)
    {
      if (gameDependencies.game.getAttackStarted ())
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassSuccess{}));
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ {} }));
          process_event (askAttackAndAssist{});
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassError{ "attack is not started" }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassError{ "account role is not defiend: " + user.accountName }));
    }
};

auto const resetPassStateMachineData = [] (GameDependencies &gameDependencies) { gameDependencies.passAttackAndAssist = PassAttackAndAssist{}; };

auto const tryToAttackAndInformOtherPlayers = [] (GameDependencies &gameDependencies, shared_class::DurakAttack const &attackEv, durak::PlayerRole playerRole, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event, bool isChill, User &user) {
  if (gameDependencies.game.playerAssists (playerRole, attackEv.cards))
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackSuccess{}));
      if (isChill)
        {
          if (auto defendingPlayer = gameDependencies.game.getDefendingPlayer ())
            {
              process_event (resumeTimer{ { defendingPlayer->id } });
            }
          if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer ())
            {
              process_event (pauseTimer{ { attackingPlayer->id } });
            }
          if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer ())
            {
              process_event (pauseTimer{ { assistingPlayer->id } });
            }
          sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users);
          sendAvailableMoves (gameDependencies.game, gameDependencies.users);
          process_event (sendTimerEv{});
        }
      else
        {
          sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users);
          auto otherPlayerRole = (playerRole == durak::PlayerRole::attack) ? durak::PlayerRole::assistAttacker : durak::PlayerRole::attack;
          defendsWantsToTakeCardsSendMovesToAttackOrAssist (gameDependencies.game, playerRole, user);
          if (auto otherPlayer = (otherPlayerRole == durak::PlayerRole::attack) ? gameDependencies.game.getAttackingPlayer () : gameDependencies.game.getAssistingPlayer ())
            {
              if (auto otherPlayerItr = ranges::find_if (gameDependencies.users, [otherPlayerName = otherPlayer->id] (auto const &user) { return user.accountName == otherPlayerName; }); otherPlayerItr != gameDependencies.users.end ())
                {
                  defendsWantsToTakeCardsSendMovesToAttackOrAssist (gameDependencies.game, otherPlayerRole, *otherPlayerItr);
                }
              process_event (resumeTimer{ { otherPlayer->id } });
              process_event (sendTimerEv{});
            }
        }
      gameDependencies.passAttackAndAssist = PassAttackAndAssist{};
    }
};

auto const doAttack = [] (std::tuple<shared_class::DurakAttack, User &> const &durakAttackAndUser, GameDependencies &gameDependencies, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event, bool isChill) {
  auto [attackEv, user] = durakAttackAndUser;
  auto playerRole = gameDependencies.game.getRoleForName (user.accountName);
  if (not gameDependencies.game.getAttackStarted () && playerRole == durak::PlayerRole::attack)
    {
      if (gameDependencies.game.playerStartsAttack (attackEv.cards))
        {
          if (auto defendingPlayer = gameDependencies.game.getDefendingPlayer ())
            {
              process_event (resumeTimer{ { defendingPlayer->id } });
            }
          if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer ())
            {
              process_event (pauseTimer{ { attackingPlayer->id } });
            }
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackSuccess{}));
          sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users);
          sendAvailableMoves (gameDependencies.game, gameDependencies.users);
          process_event (sendTimerEv{});
          gameDependencies.passAttackAndAssist = PassAttackAndAssist{};
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackError{ "not allowed to play cards" }));
        }
    }
  else
    {
      tryToAttackAndInformOtherPlayers (gameDependencies, attackEv, playerRole, process_event, isChill, user);
    }
};

auto const doAttackChill = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAttack, User &> const &durakAttackAndUser, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event) { doAttack (durakAttackAndUser, gameDependencies, process_event, true); };
auto const doAttackAskAttackAndAssist = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAttack, User &> const &attackEventAndUser, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event) { doAttack (attackEventAndUser, gameDependencies, process_event, false); };
auto const doDefend = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakDefend, User &> const &defendAndUser, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event) {
  auto [defendEvent, user] = defendAndUser;
  auto playerRole = gameDependencies.game.getRoleForName (user.accountName);
  if (playerRole == durak::PlayerRole::defend)
    {
      if (gameDependencies.game.playerDefends (defendEvent.cardToBeat, defendEvent.card))
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendSuccess{}));
          sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users);
          sendAvailableMoves (gameDependencies.game, gameDependencies.users);
          if (gameDependencies.game.countOfNotBeatenCardsOnTable () == 0)
            {
              process_event (pauseTimer{ { user.accountName } });
              if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer (); attackingPlayer && not attackingPlayer->getCards ().empty ())
                {
                  process_event (resumeTimer{ { attackingPlayer->id } });
                }
              if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer (); assistingPlayer && not assistingPlayer->getCards ().empty ())
                {
                  process_event (resumeTimer{ { assistingPlayer->id } });
                }
              process_event (sendTimerEv{});
            }
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendError{ "Error while defending " + fmt::format ("CardToBeat: {},{} vs. Card: {},{}", defendEvent.cardToBeat.value, magic_enum::enum_name (defendEvent.cardToBeat.type), defendEvent.card.value, magic_enum::enum_name (defendEvent.card.type)) }));
        }
    }
};

auto const blockOnlyDef = [] (std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &defendWantToTakeCardsEventAndUser) {
  auto [event, user] = defendWantToTakeCardsEventAndUser;
  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ {} }));
};

auto const attackErrorUserHasWrongRole = [] (std::tuple<shared_class::DurakAttack, User &> const &durakAttackAndUser, GameDependencies &gameDependencies) {
  auto [event, user] = durakAttackAndUser;
  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackError{ "Wrong role error. To attack you need to have the role attack or assist. Your role is: " + std::string{ magic_enum::enum_name (gameDependencies.game.getRoleForName (user.accountName)) } }));
};

template <typename> struct is_tuple : std::false_type
{
};

template <typename... T> struct is_tuple<std::tuple<T...> > : std::true_type
{
};

auto const unhandledEvent = [] (auto const &event) {
  if constexpr (is_tuple<typename std::decay<decltype (event)>::type>::value)
    {
      using eventType = typename std::decay<decltype (std::get<0> (event))>::type;
      using userType = typename std::decay<decltype (std::get<1> (event))>::type;
      if constexpr (std::same_as<userType, User> &&
                    // orthogonal states lead to unhandled events even if they are handled in another state but not in both.
                    not std::same_as<eventType, shared_class::DurakLeaveGame>)
        {
          auto [durakEvent, user] = event;
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::UnhandledEventError{ boost::typeindex::type_id<typename std::decay<decltype (std::get<0> (event))>::type> ().pretty_name () }));
#ifdef LOG_FOR_STATE_MACHINE
          fmt::print (fmt::fg (fmt::color::orange_red), "[unhandled event]\t\t  {}", boost::typeindex::type_id<typename std::decay<decltype (std::get<0> (event))>::type> ().pretty_name ());
          std::cout << std::endl;
#endif
        }
    }
  else
    {
#ifdef LOG_FOR_STATE_MACHINE
      fmt::print (fmt::fg (fmt::color::orange_red), "[unhandled event]\t\t  {}", boost::typeindex::type_id<typename std::decay<decltype (event)>::type> ().pretty_name ());
      std::cout << std::endl;
#endif
    }
};

auto const needsToBeDefendingplayerError = [] (std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &askDefendWantToTakeCardsAnswerEventAndUser, GameDependencies &gameDependencies) {
  auto [askDefendWantToTakeCardsAnswerEvent, user] = askDefendWantToTakeCardsAnswerEventAndUser;

  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswerError{ "Wrong role error. To take or discard cards you need to have the role defend. Your role is: " + std::string{ magic_enum::enum_name (gameDependencies.game.getRoleForName (user.accountName)) } }));
};

auto const wantsToTakeCards = [] (std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &askDefendWantToTakeCardsAnswerEventAndUser) {
  auto [askDefendWantToTakeCardsAnswerEvent, user] = askDefendWantToTakeCardsAnswerEventAndUser;
  return askDefendWantToTakeCardsAnswerEvent.answer;
};

class StateMachineImpl
{
public:
  auto
  operator() () const noexcept
  {
    using namespace boost::sml;
    using namespace shared_class;
    using AttackPass = std::tuple<DurakAttackPass, User &>;
    using AssistPass = std::tuple<DurakAssistPass, User &>;
    using DefendPass = std::tuple<DurakDefendPass, User &>;
    using Attack = std::tuple<DurakAttack, User &>;
    using Attack = std::tuple<DurakAttack, User &>;
    using Defend = std::tuple<DurakDefend, User &>;
    using DefendWantToTakeCardsAnswer = std::tuple<DurakAskDefendWantToTakeCardsAnswer, User &>;
    using LeaveGame = std::tuple<DurakLeaveGame, User &>;
    return make_transition_table (

        // clang-format off
/*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
* "init"_s                  + event<start>                                                  
                            /(roundStartSendAllowedMovesAndGameData, process (sendTimerEv{}))                                                     = state<Chill>    
/*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
, state<Chill>              + on_entry<_>                        [isNotFirstRound]               
                            /(resetPassStateMachineData,process (nextRoundTimer{}),roundStartSendAllowedMovesAndGameData)           
, state<Chill>              + event<askDef>                                                                                                       = state<AskDef>
, state<Chill>              + event<askAttackAndAssist>                                                                                           = state<AskAttackAndAssist>
, state<Chill>              + event<AttackPass>                                                     /(setAttackPass,checkData)
, state<Chill>              + event<AssistPass>                                                     /(setAssistPass,checkData)
, state<Chill>              + event<DefendPass>                                                     / handleDefendPass
, state<Chill>              + event<Attack>                      [not isAttackingOrAssistingPlayer] / attackErrorUserHasWrongRole
, state<Chill>              + event<Attack>                                                         / doAttackChill
, state<Chill>              + event<Defend>                      [isDefendingPlayer]                / doDefend
, state<Chill>              + event<userRelogged>                                                   / (userReloggedInChillState)
, state<Chill>              + event<_>                                                              / unhandledEvent
// /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
, state<AskDef>             + on_entry<_>                                                           / startAskDef
, state<AskDef>             + event<DefendWantToTakeCardsAnswer> [not isDefendingPlayer]            / needsToBeDefendingplayerError
, state<AskDef>             + event<DefendWantToTakeCardsAnswer> [ wantsToTakeCards ]               / blockOnlyDef                                = state<AskAttackAndAssist>
, state<AskDef>             + event<DefendWantToTakeCardsAnswer>                                    / handleDefendSuccess                         = state<Chill>
, state<AskDef>             + event<userRelogged>                                                   / (userReloggedInAskDef)
, state<AskDef>             + event<_>                                                              / unhandledEvent
// /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
, state<AskAttackAndAssist> + on_entry<_>                                                           / startAskAttackAndAssist
, state<AskAttackAndAssist> + event<AttackPass>                                                     /(setAttackAnswer,checkAttackAndAssistAnswer)
, state<AskAttackAndAssist> + event<AssistPass>                                                     /(setAssistAnswer,checkAttackAndAssistAnswer)
, state<AskAttackAndAssist> + event<Attack>                      [not isAttackingOrAssistingPlayer] / attackErrorUserHasWrongRole
, state<AskAttackAndAssist> + event<Attack>                                                         / doAttackAskAttackAndAssist
, state<AskAttackAndAssist> + event<chill>                                                                                                        =state<Chill>
, state<AskAttackAndAssist> + event<userRelogged>                                                   / (userReloggedInAskAttackAssist)
, state<AskAttackAndAssist> + event<_>                                                              / unhandledEvent
// /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
,*"leaveGameHandler"_s      + event<LeaveGame>                                                      / userLeftGame                                
,*"timerHandler"_s          + event<initTimer>                   [timerActive]                      / (initTimerHandler)
, "timerHandler"_s          + event<nextRoundTimer>              [timerActive]                      / (nextRoundTimerHandler)
, "timerHandler"_s          + event<pauseTimer>                  [timerActive]                      / (pauseTimerHandler)
, "timerHandler"_s          + event<resumeTimer>                 [timerActive]                      / (resumeTimerHandler)
, "timerHandler"_s          + event<sendTimerEv>                 [timerActive]                      / (sendTimer)

// clang-format on   
    );
  }
};



struct my_logger
{
  template <class SM, class TEvent>
  void
  log_process_event (const TEvent &event)
  {
    if constexpr (confu_json::is_adapted_struct<TEvent>::value)
      {
        std::cout << "\n[" << aux::get_type_name<SM> () << "]"
                  << "[process_event] " << objectToStringWithObjectName (event)  << std::endl;
      }
    else
      {
        printf ("[%s][process_event] %s\n", aux::get_type_name<SM> (), aux::get_type_name<TEvent> ());
      }
  }

  template <class SM, class TGuard, class TEvent>
  void
  log_guard (const TGuard &, const TEvent &, bool result)
  {
    printf ("[%s][guard]\t  %s %s\n", aux::get_type_name<SM> (), aux::get_type_name<TGuard> (), (result ? "[OK]" : "[Reject]"));
  }

  template <class SM, class TAction, class TEvent>
  void
  log_action (const TAction &, const TEvent &)
  {
    printf ("[%s][action]\t  %s \n", aux::get_type_name<SM> (), aux::get_type_name<TAction> ());
  }

  template <class SM, class TSrcState, class TDstState>
  void
  log_state_change (const TSrcState &src, const TDstState &dst)
  {
    printf ("[%s][transition]\t  %s -> %s\n", aux::get_type_name<SM> (), src.c_str (), dst.c_str ());
  }
};



struct Game::StateMachineWrapper
{
  StateMachineWrapper (Game *owner,matchmaking_game::StartGame const &startGame, std::string const &gameName, std::list<User> &&users) : gameDependencies{startGame,gameName,std::move(users)},
  impl (owner,
#ifdef LOG_FOR_STATE_MACHINE
                                                                                              logger,
#endif
                                                                                              gameDependencies){}

  GameDependencies gameDependencies;

#ifdef LOG_FOR_STATE_MACHINE
  my_logger logger;
  boost::sml::sm<StateMachineImpl, boost::sml::logger<my_logger>,boost::sml::process_queue<std::queue> > impl;
#else
  boost::sml::sm<StateMachineImpl,boost::sml::process_queue<std::queue> > impl;
#endif
};

void // has to be after YourClass::StateMachineWrapper definition
Game::StateMachineWrapperDeleter::operator() (StateMachineWrapper *p)
{
  delete p;
}



Game::Game (matchmaking_game::StartGame const &startGame, std::string const &gameName, std::list<User> &&users): sm{ new StateMachineWrapper{this,startGame,gameName,std::move(users)} } {
  auto userNames=std::vector<std::string>{};
  ranges::transform(sm->gameDependencies.users,ranges::back_inserter(userNames),[](User const& user){return user.accountName;});
  sm->gameDependencies.game=durak::Game{std::move(userNames),startGame.gameOption.gameOption};
  // TODO check if it necessary to send this 2 events. maybe game could be started with out this???
  sm->impl.process_event (initTimer{});
  sm->impl.process_event (start{});
}



void Game::processEvent (std::string const &event, std::string const &accountName) {
  std::vector<std::string> splitMesssage{};
  boost::algorithm::split (splitMesssage, event, boost::is_any_of ("|"));
  if (splitMesssage.size () == 2)
    {
      auto const &typeToSearch = splitMesssage.at (0);
      auto const &objectAsString = splitMesssage.at (1);
      bool typeFound = false;
      boost::hana::for_each (shared_class::gameTypes, [&] (const auto &x) {
            if (typeToSearch == confu_json::type_name<typename std::decay<decltype (x)>::type> ())
              {
                typeFound = true;
                boost::json::error_code ec{};
                sm->impl.process_event (std::tuple<std::decay_t<decltype (x)>,User& > {confu_json::to_object<std::decay_t<decltype (x)> > (confu_json::read_json (objectAsString, ec)), user(accountName).value()} );
                if (ec) std::cout << "read_json error: " << ec.message () << std::endl;
                return;
              }
          });
      if (not typeFound) std::cout << "could not find a match for typeToSearch in shared_class::gameTypes or matchmaking_game::gameMessages '" << typeToSearch << "'" << std::endl;
    }
  else
    {
      std::cout << "Not supported event. event syntax: EventName|JsonObject. not handled event: '" << event << "'" << std::endl;
    }
}




std::string const& Game::gameName () const{
  return sm->gameDependencies.gameName;
}

bool Game::isGameRunning () const {
   return not sm->impl.is("init"_s);
}

bool Game::isUserInGame (std::string const& userName) const {
    return  ranges::find(sm->gameDependencies.users,userName,[](User const& user){return user.accountName;})!=sm->gameDependencies.users.end();
}

boost::optional<User &> Game::user (std::string const &userName) {
  auto userItr=ranges::find(sm->gameDependencies.users,userName,[](User const& user){return user.accountName;});
  return userItr!=sm->gameDependencies.users.end()?*userItr:boost::optional<User &>{};
}
