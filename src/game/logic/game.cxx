#include "game.hxx"
#include "durak/card.hxx"
#include "durak/game.hxx"
#include "durak/gameData.hxx"
#include "src/serialization.hxx"
#include "src/server/user.hxx"
#include "src/util.hxx"
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/system_timer.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <boost/optional.hpp>
#include <boost/sml.hpp>
#include <chrono>
#include <cmath>
#include <confu_json/concept.hxx>
#include <confu_json/confu_json.hxx>
#include <cstddef>
#include <cstdlib>
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
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/transform.hpp>
#include <string>
#include <utility>
#include <vector>
using namespace boost::sml;

struct TempUser
{
  boost::optional<std::string> accountName{};
  std::deque<std::string> msgQueue{};
};

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

struct attack
{
  std::string playerName{};
  std::vector<durak::Card> cards{};
};

struct defend
{
  std::string playerName{};
  durak::Card cardToBeat{};
  durak::Card card{};
};

struct attackPass
{
  std::string playerName{};
};

struct assistPass
{
  std::string playerName{};
};
struct defendPass
{
  std::string playerName{};
};

struct PassAttackAndAssist
{
  bool attack{};
  bool assist{};
};

struct defendAnswerYes
{
  std::string playerName{};
};
struct defendAnswerNo
{
  std::string playerName{};
};

struct leaveGame
{
  std::string accountName{};
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

struct GameDependencies
{
  TimerOption timerOption{};
  durak::Game game{};
  std::vector<User> users{};
  PassAttackAndAssist passAttackAndAssist{};
  bool isRanked{};
};

auto const timerActive = [] (GameDependencies &gameDependencies) { return gameDependencies.timerOption.timerType != shared_class::TimerType::noTimer; };

auto const handleGameOver = [] (boost::optional<durak::Player> const &durak, std::vector<User> &users, bool isRanked) {
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
removeUserFromGame (std::string const &userToRemove, durak::Game &game, std::vector<User> &users, bool isRanked)
{
  if (not game.checkIfGameIsOver ())
    {
      game.removePlayer (userToRemove);
      handleGameOver (game.durak (), users, isRanked);
    }
}

boost::asio::awaitable<void> inline runTimer (std::shared_ptr<boost::asio::system_timer> timer, std::string const &accountName, durak::Game &game, std::vector<User> &users, bool isRanked)
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
sendAllowedMovesForUserWithName (durak::Game &game, std::vector<User> &users, std::string const &userName)
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
            user.timer->get_executor (), [playersToResume = std::move (playersToResume), &gameDependencies, &user] () { return runTimer (user.timer, user.accountName, gameDependencies.game, gameDependencies.users, gameDependencies.isRanked); }, boost::asio::detached);
      }
  });
};

auto const isDefendingPlayer = [] (GameDependencies &gameDependencies, defend const &defendEv) { return gameDependencies.game.getRoleForName (defendEv.playerName) == durak::PlayerRole::defend; };
auto const isNotFirstRound = [] (GameDependencies &gameDependencies) { return gameDependencies.game.getRound () > 1; };
auto const setAttackAnswer = [] (GameDependencies &gameDependencies, attackPass const &attackPassEv, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) {
  if (auto user = ranges::find_if (gameDependencies.users, [accountName = attackPassEv.playerName] (auto const &user) { return user.accountName == accountName; }); user != gameDependencies.users.end ())
    {

      if (not gameDependencies.passAttackAndAssist.attack)
        {
          if (gameDependencies.game.getRoleForName (attackPassEv.playerName) == durak::PlayerRole::attack)
            {
              gameDependencies.passAttackAndAssist.attack = true;
              process_event (pauseTimer{ { attackPassEv.playerName } });
              process_event (sendTimerEv{});
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ {} }));
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsSuccess{}));
            }
          else
            {
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError{ "role is not attack" }));
            }
        }
      else
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError{ "pass already set" }));
        }
    }
};
auto const setAssistAnswer = [] (GameDependencies &gameDependencies, assistPass const &assistPassEv, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) {
  if (auto user = ranges::find_if (gameDependencies.users, [accountName = assistPassEv.playerName] (auto const &user) { return user.accountName == accountName; }); user != gameDependencies.users.end ())
    {
      if (not gameDependencies.passAttackAndAssist.assist)
        {
          if (gameDependencies.game.getRoleForName (assistPassEv.playerName) == durak::PlayerRole::assistAttacker)
            {
              gameDependencies.passAttackAndAssist.assist = true;
              process_event (pauseTimer{ { assistPassEv.playerName } });
              process_event (sendTimerEv{});
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ {} }));
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsSuccess{}));
            }
          else
            {
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError{ "role is not assist" }));
            }
        }
      else
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError{ "pass already set" }));
        }
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

auto const userLeftGame = [] (GameDependencies &gameDependencies, leaveGame const &leaveGameEv) {
  removeUserFromGame (leaveGameEv.accountName, gameDependencies.game, gameDependencies.users, gameDependencies.isRanked);
  ranges::for_each (gameDependencies.users, [] (auto const &user) { user.timer->cancel (); });
  if (auto user = ranges::find_if (gameDependencies.users, [accountName = leaveGameEv.accountName] (User const &user) { return user.accountName == accountName; }); user != gameDependencies.users.end ())
    {
      gameDependencies.users.erase (user);
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

auto const doPass = [] (GameDependencies &gameDependencies, std::string const &playerName, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) {
  if (auto user = ranges::find_if (gameDependencies.users, [&playerName] (auto const &user) { return user.accountName == playerName; }); user != gameDependencies.users.end ())
    {
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
                      user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackPassSuccess{}));
                    }
                  else
                    {
                      gameDependencies.passAttackAndAssist.assist = true;
                      user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAssistPassSuccess{}));
                    }
                  user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{}));
                  process_event (pauseTimer{ { playerName } });
                  process_event (sendTimerEv{});
                }
              else
                {
                  user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackPassError{ "account role is not attack or assist: " + playerName }));
                }
            }
          else
            {
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackPassError{ "there are not beaten cards on the table" }));
            }
        }
      else
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackPassError{ "can not pass if attack is not started" }));
        }
    }
};
auto const setAttackPass = [] (GameDependencies &gameDependencies, attackPass const &attackPassEv, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) { doPass (gameDependencies, attackPassEv.playerName, process_event); };
auto const setAssistPass = [] (GameDependencies &gameDependencies, assistPass const &assistPassEv, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) { doPass (gameDependencies, assistPassEv.playerName, process_event); };

auto const handleDefendSuccess = [] (GameDependencies &gameDependencies, defendAnswerNo const &defendAnswerNoEv) {
  if (auto user = ranges::find_if (gameDependencies.users, [accountName = defendAnswerNoEv.playerName] (auto const &user) { return user.accountName == accountName; }); user != gameDependencies.users.end ())
    {
      if (gameDependencies.game.getRoleForName (defendAnswerNoEv.playerName) == durak::PlayerRole::defend)
        {
          gameDependencies.game.nextRound (false);
          if (gameDependencies.game.checkIfGameIsOver ())
            {
              handleGameOver (gameDependencies.game.durak (), gameDependencies.users, gameDependencies.isRanked);
            }
          else
            {
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswerSuccess{}));
            }
        }
      else
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswerError{ "account role is not defend: " + defendAnswerNoEv.playerName }));
        }
    }
};

auto const handleDefendPass = [] (GameDependencies &gameDependencies, defendPass const &defendPassEv, boost::sml::back::process<askAttackAndAssist> process_event) {
  if (auto user = ranges::find_if (gameDependencies.users, [accountName = defendPassEv.playerName] (auto const &user) { return user.accountName == accountName; }); user != gameDependencies.users.end ())
    {
      if (gameDependencies.game.getRoleForName (defendPassEv.playerName) == durak::PlayerRole::defend)
        {
          if (gameDependencies.game.getAttackStarted ())
            {
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassSuccess{}));
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ {} }));
              process_event (askAttackAndAssist{});
            }
          else
            {
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassError{ "attack is not started" }));
            }
        }
      else
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassError{ "account role is not defiend: " + defendPassEv.playerName }));
        }
    }
};

auto const resetPassStateMachineData = [] (GameDependencies &gameDependencies) { gameDependencies.passAttackAndAssist = PassAttackAndAssist{}; };

auto const tryToAttackAndInformOtherPlayers = [] (GameDependencies &gameDependencies, attack const &attackEv, durak::PlayerRole playerRole, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event, bool isChill, User &user) {
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

auto const doAttack = [] (attack const &attackEv, GameDependencies &gameDependencies, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event, bool isChill) {
  if (auto user = ranges::find_if (gameDependencies.users, [accountName = attackEv.playerName] (auto const &user) { return user.accountName == accountName; }); user != gameDependencies.users.end ())
    {
      auto playerRole = gameDependencies.game.getRoleForName (attackEv.playerName);
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
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackSuccess{}));
              sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users);
              sendAvailableMoves (gameDependencies.game, gameDependencies.users);
              process_event (sendTimerEv{});
              gameDependencies.passAttackAndAssist = PassAttackAndAssist{};
            }
          else
            {
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackError{ "not allowed to play cards" }));
            }
        }
      else
        {
          tryToAttackAndInformOtherPlayers (gameDependencies, attackEv, playerRole, process_event, isChill, *user);
        }
    }
};

auto const doAttackChill = [] (GameDependencies &gameDependencies, attack const &attackEv, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event) { doAttack (attackEv, gameDependencies, process_event, true); };

auto const doAttackAskAttackAndAssist = [] (GameDependencies &gameDependencies, attack const &attackEv, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event) { doAttack (attackEv, gameDependencies, process_event, false); };

auto const doDefend = [] (GameDependencies &gameDependencies, defend const &defendEv, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event) {
  if (auto user = ranges::find_if (gameDependencies.users, [accountName = defendEv.playerName] (auto const &user) { return user.accountName == accountName; }); user != gameDependencies.users.end ())
    {

      auto playerRole = gameDependencies.game.getRoleForName (defendEv.playerName);
      if (playerRole == durak::PlayerRole::defend)
        {
          if (gameDependencies.game.playerDefends (defendEv.cardToBeat, defendEv.card))
            {
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendSuccess{}));
              sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users);
              sendAvailableMoves (gameDependencies.game, gameDependencies.users);
              if (gameDependencies.game.countOfNotBeatenCardsOnTable () == 0)
                {
                  process_event (pauseTimer{ { defendEv.playerName } });
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
              user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendError{ "Error while defending " + fmt::format ("CardToBeat: {},{} vs. Card: {},{}", defendEv.cardToBeat.value, magic_enum::enum_name (defendEv.cardToBeat.type), defendEv.card.value, magic_enum::enum_name (defendEv.card.type)) }));
            }
        }
    }
};

auto const blockOnlyDef = [] (GameDependencies &gameDependencies) {
  if (auto defendingPlayer = gameDependencies.game.getDefendingPlayer ())
    {
      if (auto user = ranges::find_if (gameDependencies.users, [&defendingPlayer] (User const &user_) { return user_.accountName == defendingPlayer->id; }); user != gameDependencies.users.end ())
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves{ {} }));
        }
    }
};

class StateMachineImpl
{
public:
  auto
  operator() () const noexcept
  {
    using namespace boost::sml;
    return make_transition_table (
        // clang-format off
/*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
* "doNotStartAtConstruction"_s  + event<start>                                            /(roundStartSendAllowedMovesAndGameData, process (sendTimerEv{}))                                      = state<Chill>    
, state<Chill>                  + on_entry<_>                   [isNotFirstRound]         /(resetPassStateMachineData,process (nextRoundTimer{}),roundStartSendAllowedMovesAndGameData)           
, state<Chill>                  + event<askDef>                                                                                                                           = state<AskDef>
, state<Chill>                  + event<askAttackAndAssist>                                                                                                               = state<AskAttackAndAssist>
, state<Chill>                  + event<attackPass>                                       /(setAttackPass,checkData)
, state<Chill>                  + event<assistPass>                                       /(setAssistPass,checkData)
, state<Chill>                  + event<defendPass>                                       / handleDefendPass                                         
, state<Chill>                  + event<attack>                                           / doAttackChill 
, state<Chill>                  + event<defend>                 [isDefendingPlayer]       / doDefend 
, state<Chill>                  + event<userRelogged>                                     / (userReloggedInChillState)
/*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
, state<AskDef>                 + on_entry<_>                                             / startAskDef
, state<AskDef>                 + event<defendAnswerYes>                                  / blockOnlyDef                                                                  = state<AskAttackAndAssist>
, state<AskDef>                 + event<defendAnswerNo>                                   / handleDefendSuccess                                                           = state<Chill>
, state<AskDef>                 + event<userRelogged>                                     / (userReloggedInAskDef)
/*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
, state<AskAttackAndAssist>     + on_entry<_>                                             / startAskAttackAndAssist
, state<AskAttackAndAssist>     + event<attackPass>                                       /(setAttackAnswer,checkAttackAndAssistAnswer)
, state<AskAttackAndAssist>     + event<assistPass>                                       /(setAssistAnswer,checkAttackAndAssistAnswer)
, state<AskAttackAndAssist>     + event<attack>                                           / doAttackAskAttackAndAssist 
, state<AskAttackAndAssist>     + event<chill>                                                                                                                            =state<Chill>
, state<AskAttackAndAssist>     + event<userRelogged>                                     / (userReloggedInAskAttackAssist)
/*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
,*"leaveGameHandler"_s          + event<leaveGame>                                        / userLeftGame                                
,*"timerHandler"_s              + event<initTimer>              [timerActive]             / (initTimerHandler)
, "timerHandler"_s              + event<nextRoundTimer>         [timerActive]             / (nextRoundTimerHandler)
, "timerHandler"_s              + event<pauseTimer>             [timerActive]             / (pauseTimerHandler)
, "timerHandler"_s              + event<resumeTimer>            [timerActive]             / (resumeTimerHandler)
, "timerHandler"_s              + event<sendTimerEv>            [timerActive]             / (sendTimer)

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
                  << "[process_event] '" << objectToStringWithObjectName (event) << "'" << std::endl;
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
    printf ("[%s][guard]\t  '%s' %s\n", aux::get_type_name<SM> (), aux::get_type_name<TGuard> (), (result ? "[OK]" : "[Reject]"));
  }

  template <class SM, class TAction, class TEvent>
  void
  log_action (const TAction &, const TEvent &)
  {
    printf ("[%s][action]\t '%s' \n", aux::get_type_name<SM> (), aux::get_type_name<TAction> ());
  }

  template <class SM, class TSrcState, class TDstState>
  void
  log_state_change (const TSrcState &src, const TDstState &dst)
  {
    printf ("[%s][transition]\t  '%s' -> '%s'\n", aux::get_type_name<SM> (), src.c_str (), dst.c_str ());
  }
};



struct Game::StateMachineWrapper
{
  StateMachineWrapper (Game *owner) :
  impl (owner,
#ifdef LOGGING_FOR_STATE_MACHINE
                                                                                              logger,
#endif
                                                                                              matchmakingGameDependencies){}

  GameDependencies matchmakingGameDependencies{};

#ifdef LOGGING_FOR_STATE_MACHINE
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


Game::Game(): sm{ new StateMachineWrapper{this} } {}



void Game::process_event (std::string const &event) {
{
  // TODO process_event
  std::vector<std::string> splitMesssage{};
  boost::algorithm::split (splitMesssage, event, boost::is_any_of ("|"));
  if (splitMesssage.size () == 2)
    {
      auto const &typeToSearch = splitMesssage.at (0);
      auto const &objectAsString = splitMesssage.at (1);
      bool typeFound = false;
      boost::hana::for_each (shared_class::sharedClasses, [&] (const auto &x) {
            if (typeToSearch == confu_json::type_name<typename std::decay<decltype (x)>::type> ())
              {
                typeFound = true;
                boost::json::error_code ec{};
                sm->impl.process_event (confu_json::to_object<std::decay_t<decltype (x)> > (confu_json::read_json (objectAsString, ec)));
                if (ec) std::cout << "read_json error: " << ec.message () << std::endl;
                return;
              }
          });
          if (not typeFound) std::cout << "could not find a match for typeToSearch in userMatchmaking '" << typeToSearch << "'" << std::endl;
      
    }
  else
    {
      std::cout << "Not supported event. event syntax: EventName|JsonObject" << std::endl;
    }
}

}
