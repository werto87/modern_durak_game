#include "game.hxx"
#include "durak/card.hxx"
#include "durak/game.hxx"
#include "durak/gameData.hxx"
#include "example_of_a_game_server/database.hxx"
#include "example_of_a_game_server/serialization.hxx"
#include "example_of_a_game_server/server/user.hxx"
#include "example_of_a_game_server/util.hxx"
#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
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
#include <cstdint>
#include <cstdlib>
#include <durak/gameOption.hxx>
#include <durak_computer_controlled_opponent/compressCard.hxx>
#include <durak_computer_controlled_opponent/solve.hxx>
#include <fmt/format.h>
#include <iostream>
#include <magic_enum.hpp>
#include <optional>
#include <pipes/dev_null.hpp>
#include <pipes/mux.hpp>
#include <pipes/pipes.hpp>
#include <pipes/push_back.hpp>
#include <pipes/transform.hpp>
#include <pipes/unzip.hpp>
#include <queue>
#include <range/v3/action/erase.hpp>
#include <range/v3/algorithm/find.hpp>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/for_each.hpp>
#include <range/v3/algorithm/remove.hpp>
#include <range/v3/algorithm/transform.hpp>
#include <range/v3/all.hpp>
#include <range/v3/iterator/insert_iterators.hpp>
#include <range/v3/numeric/accumulate.hpp>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
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
  GameDependencies (matchmaking_game::StartGame const &startGame, std::string const &gameName_, std::list<User> &&users_, boost::asio::io_context &ioContext_, boost::asio::ip::tcp::endpoint const &gameToMatchmakingEndpoint_) : users{ std::move (users_) }, isRanked{ startGame.ratedGame }, gameName{ gameName_ }, ioContext{ ioContext_ }, gameToMatchmakingEndpoint{ gameToMatchmakingEndpoint_ } {}
  TimerOption timerOption{};
  durak::Game game{};
  std::list<User> users{};
  PassAttackAndAssist passAttackAndAssist{};
  bool isRanked{};
  std::string gameName{};
  boost::asio::io_context &ioContext;
  boost::asio::ip::tcp::endpoint gameToMatchmakingEndpoint{};
};

auto const timerActive = [] (GameDependencies &gameDependencies) { return gameDependencies.timerOption.timerType != shared_class::TimerType::noTimer; };

boost::asio::awaitable<void>
sendGameOverToMatchmaking (matchmaking_game::GameOver gameOver, GameDependencies &gameDependencies)
{
  auto ws = std::make_shared<Websocket> (gameDependencies.ioContext);
  co_await ws->next_layer ().async_connect (gameDependencies.gameToMatchmakingEndpoint);
  ws->next_layer ().expires_never ();
  ws->set_option (boost::beast::websocket::stream_base::timeout::suggested (boost::beast::role_type::client));
  ws->set_option (boost::beast::websocket::stream_base::decorator ([] (boost::beast::websocket::request_type &req) { req.set (boost::beast::http::field::user_agent, std::string (BOOST_BEAST_VERSION_STRING) + " websocket-client-async"); }));
  co_await ws->async_handshake (gameDependencies.gameToMatchmakingEndpoint.address ().to_string () + std::to_string (gameDependencies.gameToMatchmakingEndpoint.port ()), "/");
  static size_t id = 0;
  auto myWebsocket = MyWebsocket<Websocket>{ std::move (ws), "sendGameOverToMatchmaking", fmt::fg (fmt::color::cornflower_blue), std::to_string (id++) };
  co_await myWebsocket.async_write_one_message (objectToStringWithObjectName (gameOver));
  auto msg = co_await myWebsocket.async_read_one_message ();
  std::vector<std::string> splitMessage{};
  boost::algorithm::split (splitMessage, msg, boost::is_any_of ("|"));
  if (splitMessage.size () == 2)
    {
      auto const &typeToSearch = splitMessage.at (0);
      auto const &objectAsString = splitMessage.at (1);
      if (typeToSearch == "GameOverSuccess")
        {
          // TODO maybe need to do something???
        }
      else if (typeToSearch == "GameOverError")
        {
          std::cout << "GameOverError: " << stringToObject<matchmaking_game::GameOverError> (objectAsString).error << std::endl;
        }
      else
        {
          std::cout << "matchmaking answered with: '" << msg << "' expected GameOverSuccess|{} or GameOverError|{} " << std::endl;
        }
    }
  else
    {
      std::cout << "Game server answered with: '" << msg << "' expected GameOverSuccess|{} or GameOverError|{} " << std::endl;
    }
  ranges::for_each (gameDependencies.users, [] (User &user) { user.sendMsgToUser (objectToStringWithObjectName (matchmaking_game::LeaveGameSuccess{})); });
}

auto const handleGameOver = [] (boost::optional<durak::Player> const &durak, GameDependencies &gameDependencies) {
  auto winners = std::vector<std::string>{};
  auto losers = std::vector<std::string>{};
  auto draws = std::vector<std::string>{};
  if (durak)
    {
      ranges::for_each (gameDependencies.users, [durak = durak->id, &winners, &losers] (User &user) {
        if (user.accountName == durak)
          {
            user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakGameOverLose{}));
            winners.push_back (user.accountName);
          }
        else
          {
            user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakGameOverWon{}));
            losers.push_back (user.accountName);
          }
      });
    }
  else
    {
      ranges::for_each (gameDependencies.users, [&draws] (auto &user) {
        user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakGameOverDraw{}));
        draws.push_back (user.accountName);
      });
    }
  auto gameOver = matchmaking_game::GameOver{ gameDependencies.gameName, gameDependencies.isRanked, std::move (winners), std::move (losers), std::move (draws) };
  co_spawn (gameDependencies.ioContext, sendGameOverToMatchmaking (std::move (gameOver), gameDependencies), printException);
};

inline void
removeUserFromGame (std::string const &userToRemove, GameDependencies &gameDependencies)
{
  if (not gameDependencies.game.checkIfGameIsOver ())
    {
      gameDependencies.game.removePlayer (userToRemove);
      handleGameOver (gameDependencies.game.durak (), gameDependencies);
    }
}

boost::asio::awaitable<void> inline runTimer (std::shared_ptr<boost::asio::system_timer> timer, std::string const &accountName, GameDependencies &gameDependencies)
{
  try
    {
      co_await timer->async_wait (boost::asio::use_awaitable);
      removeUserFromGame (accountName, gameDependencies);
      ranges::for_each (gameDependencies.users, [] (auto const &user) { user.timer->cancel (); });
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
            user.timer->get_executor (), [playersToResume = std::move (playersToResume), &gameDependencies, &user] () { return runTimer (user.timer, user.accountName, gameDependencies); }, printException);
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
          handleGameOver (gameDependencies.game.durak (), gameDependencies);
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
  // TODO i think this should be auto &[event, user] = leaveGameEventUser; in all places where this construct is used
  auto [event, user] = leaveGameEventUser;
  removeUserFromGame (user.accountName, gameDependencies);
  ranges::for_each (gameDependencies.users, [] (auto const &user_) { user_.timer->cancel (); });
};

std::optional<durak_computer_controlled_opponent::Action>
nextActionForRole (std::vector<std::tuple<uint8_t, durak_computer_controlled_opponent::Result> > const &nextActions, durak::PlayerRole const &playerRole)
{
  if (playerRole == durak::PlayerRole::attack || playerRole == durak::PlayerRole::defend)
    {
      if (auto winningAction = ranges::find_if (nextActions,
                                                [&playerRole] (std::tuple<uint8_t, durak_computer_controlled_opponent::Result> const &actionAsBinaryAndResult) {
                                                  auto const &[actionAsBinary, result] = actionAsBinaryAndResult;
                                                  if (playerRole == durak::PlayerRole::attack)
                                                    {
                                                      return result == durak_computer_controlled_opponent::Result::AttackWon;
                                                    }
                                                  else
                                                    {
                                                      return result == durak_computer_controlled_opponent::Result::DefendWon;
                                                    }
                                                });
          winningAction != nextActions.end ())
        {
          return { durak_computer_controlled_opponent::Action{ std::get<0> (*winningAction) } };
        }
      else
        {
          if (auto drawAction = ranges::find_if (nextActions,
                                                 [] (auto const &actionAsBinaryAndResult) {
                                                   auto const &[actionAsBinary, result] = actionAsBinaryAndResult;
                                                   return result == durak_computer_controlled_opponent::Result::Draw;
                                                 });
              drawAction != nextActions.end ())
            {
              return { durak_computer_controlled_opponent::Action{ std::get<0> (*drawAction) } };
            }
        }
    }
  return {};
}

shared_class::DurakNextMoveSuccess
calcNextMove (std::optional<durak_computer_controlled_opponent::Action> const &action, std::vector<shared_class::Move> const &moves, durak::PlayerRole const &playerRole, std::vector<std::tuple<uint8_t, durak::Card> > const &defendIdCardMapping, std::vector<std::tuple<uint8_t, durak::Card> > const &attackIdCardMapping)
{
  if (action.has_value ())
    {
      if (playerRole == durak::PlayerRole::attack)
        {
          if (ranges::find (moves, shared_class::Move::AddCards) != moves.end ())
            {
              if (auto idCard = ranges::find_if (attackIdCardMapping,
                                                 [value = action->value ()] (auto const &idAndCard) {
                                                   //
                                                   return value == std::get<0> (idAndCard);
                                                 });
                  idCard != attackIdCardMapping.end ())
                {
                  return { shared_class::Move::AddCards, std::get<1> (*idCard) };
                }
              else
                {
                  throw std::logic_error{ "attack play card but card could not be found in id card mapping" };
                  return {};
                }
            }
          else if (ranges::find (moves, shared_class::Move::AttackAssistPass) != moves.end ())
            {
              return { shared_class::Move::AttackAssistPass, {} };
            }
          else
            {
              throw std::logic_error{ "attack move should be add card or pass" };
              return {};
            }
        }
      else if (playerRole == durak::PlayerRole::defend)
        {
          if (ranges::find (moves, shared_class::Move::Defend) != moves.end ())
            {
              if (auto idCard = ranges::find_if (defendIdCardMapping, [value = action->value ()] (auto const &idAndCard) { return value == std::get<0> (idAndCard); }); idCard != attackIdCardMapping.end ())
                {
                  return { shared_class::Move::Defend, std::get<1> (*idCard) };
                }
              else
                {
                  throw std::logic_error{ "defend play card but card could not be found in id card mapping" };
                  return {};
                }
            }
          else if (ranges::find (moves, shared_class::Move::TakeCards) != moves.end ())
            {
              return { shared_class::Move::TakeCards, {} };
            }
          else
            {
              throw std::logic_error{ "defend move should be take or beat cards" };
              return {};
            }
        }
      else
        {
          throw std::logic_error{ "role should be attack or defend" };
          return {};
        }
    }
  else
    {
      return {};
    }
}

durak::PlayerRole
whoHasToMove (std::vector<std::pair<durak::Card, boost::optional<durak::Card> > > const &table)
{
  return (table.size () % 2 == 0) ? durak::PlayerRole::attack : durak::PlayerRole::defend;
}

std::tuple<std::vector<std::tuple<uint8_t, durak::Card> >, std::vector<std::tuple<uint8_t, durak::Card> > >
calcCompressedCardsForAttackAndDefend (durak::Game const &game)
{
  using namespace durak_computer_controlled_opponent;
  auto cards = game.getAttackingPlayer ()->getCards ();
  auto defendingCards = game.getDefendingPlayer ()->getCards ();
  cards.insert (cards.end (), defendingCards.begin (), defendingCards.end ());
  auto cardsAsIds = cardsToIds (compress (cards));
  auto attackingCardsAsIds = std::vector<uint8_t>{ cardsAsIds.begin (), cardsAsIds.begin () + 3 };
  auto attackingCardsAsIdsAndAsCards = std::vector<std::tuple<uint8_t, durak::Card> >{};
  pipes::mux (attackingCardsAsIds, game.getAttackingPlayer ()->getCards ()) >>= pipes::transform ([] (auto const &x, auto const &y) { return std::tuple<uint8_t, durak::Card>{ x, y }; }) >>= pipes::push_back (attackingCardsAsIdsAndAsCards);
  ranges::sort (attackingCardsAsIdsAndAsCards, [] (auto const &x, auto const &y) { return std::get<0> (x) < std::get<0> (y); });
  auto defendingCardsAsIds = std::vector<uint8_t>{ cardsAsIds.begin () + 3, cardsAsIds.end () };
  auto defendingCardsAsIdsAndAsCards = std::vector<std::tuple<uint8_t, durak::Card> >{};
  pipes::mux (defendingCardsAsIds, game.getDefendingPlayer ()->getCards ()) >>= pipes::transform ([] (auto const &x, auto const &y) { return std::tuple<uint8_t, durak::Card>{ x, y }; }) >>= pipes::push_back (defendingCardsAsIdsAndAsCards);
  ranges::sort (defendingCardsAsIdsAndAsCards, [] (auto const &x, auto const &y) { return std::get<0> (x) < std::get<0> (y); });
  return { attackingCardsAsIdsAndAsCards, defendingCardsAsIdsAndAsCards };
}

auto const nextMove = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakNextMove, User &> const &durakNextMoveUser) {
  auto &[event, user] = durakNextMoveUser;
  auto playerRole = gameDependencies.game.getRoleForName (user.accountName);
  if (whoHasToMove (gameDependencies.game.getTable ()) == playerRole)
    {
      using namespace durak;
      using namespace durak_computer_controlled_opponent;
      soci::session sql (soci::sqlite3, database::databaseName);
      auto const [compressedCardsForAttack, compressedCardsForDefend] = calcCompressedCardsForAttackAndDefend (gameDependencies.game);
      auto attackCardsCompressed = std::vector<uint8_t>{};
      compressedCardsForAttack >>= pipes::unzip (pipes::push_back (attackCardsCompressed), pipes::dev_null ());
      auto defendCardsCompressed = std::vector<uint8_t>{};
      compressedCardsForDefend >>= pipes::unzip (pipes::push_back (defendCardsCompressed), pipes::dev_null ());
      auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, gameDependencies.game.getTrump ()));
      if (someRound.has_value ())
        {
          auto moveResult = binaryToMoveResult (someRound.value ().combination);
          auto result = nextActions ({ 0 }, moveResult);
          auto actionForRole = nextActionForRole (result, playerRole);
          auto allowedMoves = calculateAllowedMoves (gameDependencies.game, playerRole);
          auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
          user.sendMsgToUser (objectToStringWithObjectName (calculatedNextMove));
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakNextMoveError{ "could not find a game for the cards." }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakNextMoveError{ "player does not have to make a move so wait for the other player." }));
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
          handleGameOver (gameDependencies.game.durak (), gameDependencies);
        }
    }
  else
    {
      process_event (sendTimerEv{});
    }
};

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

auto const handleDefendSuccess = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &defendAnswerEventAndUser) {
  auto [event, user] = defendAnswerEventAndUser;
  gameDependencies.game.nextRound (false);
  if (gameDependencies.game.checkIfGameIsOver ())
    {
      handleGameOver (gameDependencies.game.durak (), gameDependencies);
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
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassError{ "account role is not defined: " + user.accountName }));
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

auto const needsToBeDefendingPlayerError = [] (std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &askDefendWantToTakeCardsAnswerEventAndUser, GameDependencies &gameDependencies) {
  auto [askDefendWantToTakeCardsAnswerEvent, user] = askDefendWantToTakeCardsAnswerEventAndUser;
  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswerError{ "Wrong role error. To take or discard cards you need to have the role defend. Your role is: " + std::string{ magic_enum::enum_name (gameDependencies.game.getRoleForName (user.accountName)) } }));
};

auto const wantsToTakeCards = [] (std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &askDefendWantToTakeCardsAnswerEventAndUser) {
  auto [askDefendWantToTakeCardsAnswerEvent, user] = askDefendWantToTakeCardsAnswerEventAndUser;
  return askDefendWantToTakeCardsAnswerEvent.answer;
};

auto const sendStartGameToUser = [] (GameDependencies &gameDependencies) { ranges::for_each (gameDependencies.users, [] (User &user) { user.sendMsgToUser (objectToStringWithObjectName (matchmaking_game::StartGame{})); }); };

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
    using NextMove = std::tuple<DurakNextMove, User &>;
    return make_transition_table (

        // clang-format off
/*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
* "init"_s                  + event<start>                                                  
                            /(sendStartGameToUser,roundStartSendAllowedMovesAndGameData, process (sendTimerEv{}))                                 = state<Chill>   
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
, state<Chill>              + event<userRelogged>                                                   / userReloggedInChillState
// /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
, state<AskDef>             + on_entry<_>                                                           / startAskDef
, state<AskDef>             + event<DefendWantToTakeCardsAnswer> [not isDefendingPlayer]            / needsToBeDefendingPlayerError
, state<AskDef>             + event<DefendWantToTakeCardsAnswer> [wantsToTakeCards]                 / blockOnlyDef                                = state<AskAttackAndAssist>
, state<AskDef>             + event<DefendWantToTakeCardsAnswer>                                    / handleDefendSuccess                         = state<Chill>
, state<AskDef>             + event<userRelogged>                                                   / (userReloggedInAskDef)
// /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
, state<AskAttackAndAssist> + on_entry<_>                                                           / startAskAttackAndAssist
, state<AskAttackAndAssist> + event<AttackPass>                                                     /(setAttackAnswer,checkAttackAndAssistAnswer)
, state<AskAttackAndAssist> + event<AssistPass>                                                     /(setAssistAnswer,checkAttackAndAssistAnswer)
, state<AskAttackAndAssist> + event<Attack>                      [not isAttackingOrAssistingPlayer] / attackErrorUserHasWrongRole
, state<AskAttackAndAssist> + event<Attack>                                                         / doAttackAskAttackAndAssist
, state<AskAttackAndAssist> + event<chill>                                                                                                        =state<Chill>
, state<AskAttackAndAssist> + event<userRelogged>                                                   / userReloggedInAskAttackAssist
// /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
,*"leaveGameHandler"_s      + event<LeaveGame>                                                      / userLeftGame                                
,*"nextMove"_s              + event<NextMove>                                                       / nextMove                                
,*"timerHandler"_s          + event<initTimer>                   [timerActive]                      / initTimerHandler
, "timerHandler"_s          + event<nextRoundTimer>              [timerActive]                      / nextRoundTimerHandler
, "timerHandler"_s          + event<pauseTimer>                  [timerActive]                      / pauseTimerHandler
, "timerHandler"_s          + event<resumeTimer>                 [timerActive]                      / resumeTimerHandler
, "timerHandler"_s          + event<sendTimerEv>                 [timerActive]                      / sendTimer 
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
  StateMachineWrapper (Game *owner,matchmaking_game::StartGame const &startGame, std::string const &gameName, std::list<User> &&users, boost::asio::io_context &ioContext_,boost::asio::ip::tcp::endpoint const &gameToMatchmakingEndpoint_) : gameDependencies{startGame,gameName,std::move(users),ioContext_,gameToMatchmakingEndpoint_},
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



Game::Game (matchmaking_game::StartGame const &startGame, std::string const &gameName, std::list<User> &&users, boost::asio::io_context &ioContext_,boost::asio::ip::tcp::endpoint const &gameToMatchmakingEndpoint_): sm{ new StateMachineWrapper{this,startGame,gameName,std::move(users),ioContext_,gameToMatchmakingEndpoint_} } {
  auto userNames=std::vector<std::string>{};
  ranges::transform(sm->gameDependencies.users,ranges::back_inserter(userNames),[](User const& user){return user.accountName;});
  sm->gameDependencies.game=durak::Game{std::move(userNames),startGame.gameOption.gameOption};
  // TODO check if it necessary to send this 2 events. maybe game could be started with out this???
  sm->impl.process_event (initTimer{});
  sm->impl.process_event (start{});
}



std::optional<std::string> Game::processEvent (std::string const &event, std::string const &accountName) {
  std::vector<std::string> splitMessage{};
  boost::algorithm::split (splitMessage, event, boost::is_any_of ("|"));
  auto result=std::optional<std::string>{};
  if (splitMessage.size () == 2)
    {
      auto const &typeToSearch = splitMessage.at (0);
      auto const &objectAsString = splitMessage.at (1);
      bool typeFound = false;
      boost::hana::for_each (shared_class::gameTypes, [&] (const auto &x) {
            if (typeToSearch == confu_json::type_name<typename std::decay<decltype (x)>::type> ())
              {
                typeFound = true;
                boost::json::error_code ec{};
                auto messageAsObject=confu_json::read_json (objectAsString, ec);
                if (ec) result="read_json error: " + ec.message ();
                else if (not sm->impl.process_event (std::tuple<std::decay_t<decltype (x)>,User& > {confu_json::to_object<std::decay_t<decltype (x)> > (messageAsObject), user(accountName).value()} )) result="No transition found";
                return;
              }
          });
      if (not typeFound) result= "could not find a match for typeToSearch in shared_class::gameTypes '" + typeToSearch + "'";
    }
  else result= "Not supported event. event syntax: EventName|JsonObject";
  return result;
}




std::string const& Game::gameName () const{
  return sm->gameDependencies.gameName;
}

bool Game::isUserInGame (std::string const& userName) const {
    return  ranges::find(sm->gameDependencies.users,userName,[](User const& user){return user.accountName;})!=sm->gameDependencies.users.end();
}

bool Game::removeUser (std::string const &userName) {
   return std::erase_if(sm->gameDependencies.users,[&userName](User const&user){return userName==user.accountName;}) >0;
}

size_t Game::usersInGame () const {
  return sm->gameDependencies.users.size();
}

boost::optional<User &> Game::user (std::string const &userName) {
  auto userItr=ranges::find(sm->gameDependencies.users,userName,[](User const& user){return user.accountName;});
  return userItr!=sm->gameDependencies.users.end()?*userItr:boost::optional<User &>{};
}

