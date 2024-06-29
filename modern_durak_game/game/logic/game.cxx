#include "modern_durak_game/game/logic/game.hxx"
#include "durak_computer_controlled_opponent/database.hxx"
#include "modern_durak_game/game/logic/allowedMoves.hxx"
#include "modern_durak_game/game/logic/gameAllowedTypes.hxx"
#include "modern_durak_game/server/user.hxx"
#include "modern_durak_game/util/util.hxx"
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
#include <durak/card.hxx>
#include <durak/game.hxx>
#include <durak/gameData.hxx>
#include <durak/gameOption.hxx>
#include <durak_computer_controlled_opponent/compressCard.hxx>
#include <durak_computer_controlled_opponent/solve.hxx>
#include <durak_computer_controlled_opponent/util.hxx>
#include <fmt/format.h>
#include <iostream>
#include <login_matchmaking_game_shared/matchmakingGameSerialization.hxx>
#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
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
  bool attack {};
  bool assist {};
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
  std::vector<std::string> playersToPause {};
};

struct resumeTimer
{
  std::vector<std::string> playersToResume {};
};
enum struct TimerType
{
  noTimer,
  resetTimeOnNewRound,
  addTimeOnNewRound
};

struct TimerOption
{
  shared_class::TimerType timerType {};
  std::chrono::seconds timeAtStart {};
  std::chrono::seconds timeForEachRound {};
};

struct userRelogged
{
  std::string accountName {};
};

struct start
{
};

struct sendTimerEv
{
};

durak::GameData
filterGameDataByAccountName (durak::GameData const &gameData, std::string const &accountName)
{
  auto filteredGameData = gameData;
  for (auto &player : filteredGameData.players | std::ranges::views::filter ([&accountName] (auto const &player) { return player.name != accountName; }))
    {
      std::ranges::transform (player.cards, player.cards.begin (), [] (boost::optional<durak::Card> const &) { return boost::optional<durak::Card> {}; });
    }
  return filteredGameData;
}

auto const &moveMapping = std::map<durak::Move, shared_class::Move> { { durak::Move::startAttack, shared_class::Move::AddCards }, { durak::Move::addCard, shared_class::Move::AddCards }, { durak::Move::pass, shared_class::Move::AttackAssistPass }, { durak::Move::defend, shared_class::Move::Defend }, { durak::Move::takeCards, shared_class::Move::TakeCards } };

std::vector<shared_class::Move>
calculateAllowedMoves (durak::Game const &game, durak::PlayerRole playerRole)
{
  auto result = std::vector<shared_class::Move> {};
  auto durakAllowedMoves = game.getAllowedMoves (playerRole);
  std::ranges::transform (durakAllowedMoves, std::back_inserter (result), [] (auto move) { return moveMapping.at (move); });
  return result;
}

std::vector<shared_class::Move>
calculateAllowedMovesWithPassState (durak::Game const &game, durak::PlayerRole playerRole, PassAttackAndAssist passAttackAndAssist)
{
  auto allowedMoves = calculateAllowedMoves (game, playerRole);
  if (auto takeCards = std::ranges::find (allowedMoves, shared_class::Move::TakeCards); takeCards != allowedMoves.end () and std::ranges::find (allowedMoves, shared_class::Move::Defend) == allowedMoves.end () and playerRole == durak::PlayerRole::defend and passAttackAndAssist.attack and passAttackAndAssist.assist)
    {
      allowedMoves.erase (takeCards);
      allowedMoves.emplace_back (shared_class::Move::AnswerDefendWantsToTakeCardsYes);
      allowedMoves.emplace_back (shared_class::Move::AnswerDefendWantsToTakeCardsNo);
    }
  return allowedMoves;
}

shared_class::DurakAllowedMoves
calcAllowedMoves (durak::Game const &game, durak::PlayerRole playerRole, std::optional<std::vector<shared_class::Move> > const &removeFromAllowedMoves, std::optional<std::vector<shared_class::Move> > const &addToAllowedMoves)
{
  auto allowedMoves = shared_class::DurakAllowedMoves { removeFromAllowedMoves.value_or (calculateAllowedMoves (game, playerRole)) };
  if (addToAllowedMoves && not addToAllowedMoves->empty ())
    {
      allowedMoves.allowedMoves.insert (allowedMoves.allowedMoves.end (), addToAllowedMoves.value ().begin (), addToAllowedMoves.value ().end ());
      std::ranges::sort (allowedMoves.allowedMoves);
      auto result = shared_class::DurakAllowedMoves {};
      std::ranges::unique_copy (allowedMoves.allowedMoves, std::back_inserter (result.allowedMoves));
      return result;
    }
  else
    {
      return allowedMoves;
    }
}

void
sendAvailableMoves (durak::Game const &game, std::list<User> const &users, AllowedMoves const &removeFromAllowedMoves = {}, AllowedMoves const &addToAllowedMoves = {})
{
  if (auto attackingPlayer = game.getAttackingPlayer ())
    {
      if (auto attackingUser = std::ranges::find_if (users, [attackingPlayerName = attackingPlayer->id] (User const &user) { return user.accountName == attackingPlayerName; }); attackingUser != users.end ())
        {
          attackingUser->sendMsgToUser (objectToStringWithObjectName (calcAllowedMoves (game, durak::PlayerRole::attack, removeFromAllowedMoves.attack, addToAllowedMoves.attack)));
        }
    }
  if (auto assistingPlayer = game.getAssistingPlayer ())
    {
      if (auto assistingUser = std::ranges::find_if (users, [assistingPlayerName = assistingPlayer->id] (User const &user) { return user.accountName == assistingPlayerName; }); assistingUser != users.end ())
        {
          assistingUser->sendMsgToUser (objectToStringWithObjectName (calcAllowedMoves (game, durak::PlayerRole::assistAttacker, removeFromAllowedMoves.assist, addToAllowedMoves.assist)));
        }
    }
  if (auto defendingPlayer = game.getDefendingPlayer ())
    {
      if (auto defendingUser = std::ranges::find_if (users, [defendingPlayerName = defendingPlayer->id] (User const &user) { return user.accountName == defendingPlayerName; }); defendingUser != users.end ())
        {
          defendingUser->sendMsgToUser (objectToStringWithObjectName (calcAllowedMoves (game, durak::PlayerRole::defend, removeFromAllowedMoves.defend, addToAllowedMoves.defend)));
        }
    }
}

void
sendGameDataToAccountsInGame (durak::Game const &game, std::list<User> const &users, shared_class::OpponentCards opponentCards)
{
  auto gameData = game.getGameData ();
  std::ranges::for_each (gameData.players, [] (auto &player) { std::ranges::sort (player.cards, [] (auto const &card1, auto const &card2) { return card1.value () < card2.value (); }); });
  std::ranges::for_each (users, [&gameData, opponentCards] (User const &user) { user.sendMsgToUser (objectToStringWithObjectName (opponentCards == shared_class::OpponentCards::showNumberOfOpponentCards ? filterGameDataByAccountName (gameData, user.accountName) : gameData)); });
}

using namespace boost::sml;

struct GameDependencies
{
  GameDependencies (matchmaking_game::StartGame const &startGame, std::string const &gameName_, std::list<User> &&users_, boost::asio::io_context &ioContext_, boost::asio::ip::tcp::endpoint const &gameToMatchmakingEndpoint_) : users { std::move (users_) }, isRanked { startGame.ratedGame }, gameName { gameName_ }, ioContext { ioContext_ }, gameToMatchmakingEndpoint { gameToMatchmakingEndpoint_ } {}

  TimerOption timerOption {};
  durak::Game game {};
  std::list<User> users {};
  PassAttackAndAssist passAttackAndAssist {};
  bool isRanked {};
  std::string gameName {};
  boost::asio::io_context &ioContext;
  boost::asio::ip::tcp::endpoint gameToMatchmakingEndpoint {};
  std::filesystem::path databasePath {};
  shared_class::OpponentCards opponentCards {};
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
  auto myWebsocket = MyWebsocket<Websocket> { std::move (ws), "sendGameOverToMatchmaking", fmt::fg (fmt::color::cornflower_blue), std::to_string (id++) };
  co_await myWebsocket.async_write_one_message (objectToStringWithObjectName (gameOver));
  auto msg = co_await myWebsocket.async_read_one_message ();
  std::vector<std::string> splitMessage {};
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
  std::ranges::for_each (gameDependencies.users, [] (User &user) { user.sendMsgToUser (objectToStringWithObjectName (matchmaking_game::LeaveGameSuccess {})); });
}

auto const handleGameOver = [] (boost::optional<durak::Player> const &durak, GameDependencies &gameDependencies) {
  auto winners = std::vector<std::string> {};
  auto losers = std::vector<std::string> {};
  auto draws = std::vector<std::string> {};
  if (durak)
    {
      std::ranges::for_each (gameDependencies.users, [durak = durak->id, &winners, &losers] (User &user) {
        if (user.accountName == durak)
          {
            user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakGameOverLose {}));
            winners.push_back (user.accountName);
          }
        else
          {
            user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakGameOverWon {}));
            losers.push_back (user.accountName);
          }
      });
    }
  else
    {
      std::ranges::for_each (gameDependencies.users, [&draws] (auto &user) {
        user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakGameOverDraw {}));
        draws.push_back (user.accountName);
      });
    }
  auto gameOver = matchmaking_game::GameOver { gameDependencies.gameName, gameDependencies.isRanked, std::move (winners), std::move (losers), std::move (draws) };
  co_spawn (gameDependencies.ioContext, sendGameOverToMatchmaking (std::move (gameOver), gameDependencies), printException);
};

void
removeUserFromGame (std::string const &userToRemove, GameDependencies &gameDependencies)
{
  if (not gameDependencies.game.checkIfGameIsOver ())
    {
      gameDependencies.game.removePlayer (userToRemove);
      handleGameOver (gameDependencies.game.durak (), gameDependencies);
    }
}

boost::asio::awaitable<void>
runTimer (std::shared_ptr<boost::asio::system_timer> timer, std::string const &accountName, GameDependencies &gameDependencies)
{
  try
    {
      co_await timer->async_wait (boost::asio::use_awaitable);
      removeUserFromGame (accountName, gameDependencies);
      std::ranges::for_each (gameDependencies.users, [] (auto const &_user) { _user.timer->cancel (); });
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
      auto durakTimers = shared_class::DurakTimers {};
      std::ranges::for_each (gameDependencies.users, [&durakTimers] (User const &user) {
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
      std::ranges::for_each (gameDependencies.users, [&durakTimers] (User const &user) { user.sendMsgToUser (objectToStringWithObjectName (durakTimers)); });
    }
};

auto const initTimerHandler = [] (GameDependencies &gameDependencies, boost::sml::back::process<resumeTimer> process_event) {
  using namespace std::chrono;
  std::ranges::for_each (gameDependencies.users, [&gameDependencies] (auto &user) { user.pausedTime = gameDependencies.timerOption.timeAtStart; });
  if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer ())
    {
      process_event (resumeTimer { { attackingPlayer->id } });
    }
};

auto const pauseTimerHandler = [] (GameDependencies &gameDependencies, pauseTimer const &pauseTimerEv) {
  std::ranges::for_each (gameDependencies.users, [&playersToPausetime = pauseTimerEv.playersToPause] (auto &user) {
    using namespace std::chrono;
    if (std::ranges::find (playersToPausetime, user.accountName) != playersToPausetime.end ())
      {
        user.pausedTime = duration_cast<milliseconds> (user.timer->expiry () - system_clock::now ());
        user.timer->cancel ();
      }
  });
};

auto const nextRoundTimerHandler = [] (GameDependencies &gameDependencies, boost::sml::back::process<resumeTimer, sendTimerEv> process_event) {
  using namespace std::chrono;
  std::ranges::for_each (gameDependencies.users, [&timerOption = gameDependencies.timerOption] (auto &user) {
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
      process_event (resumeTimer { { attackingPlayer->id } });
      process_event (sendTimerEv {});
    }
};

auto const userReloggedInChillState = [] (GameDependencies &gameDependencies, userRelogged const &userReloggedEv) {
  if (auto user = std::ranges::find_if (gameDependencies.users, [userName = userReloggedEv.accountName] (auto const &_user) { return _user.accountName == userName; }); user != gameDependencies.users.end ())
    {
      user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves { calculateAllowedMoves (gameDependencies.game, gameDependencies.game.getRoleForName (userReloggedEv.accountName)) }));
    }
};

auto const userReloggedInAskDef = [] (GameDependencies &gameDependencies, userRelogged const &userReloggedEv) {
  if (gameDependencies.game.getRoleForName (userReloggedEv.accountName) == durak::PlayerRole::defend)
    {
      if (auto user = std::ranges::find_if (gameDependencies.users, [userName = userReloggedEv.accountName] (auto const &_user) { return _user.accountName == userName; }); user != gameDependencies.users.end ())
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves { { shared_class::Move::AnswerDefendWantsToTakeCardsYes, shared_class::Move::AnswerDefendWantsToTakeCardsNo } }));
        }
    }
};

auto const defendsWantsToTakeCardsSendMovesToAttackOrAssist = [] (durak::Game &game, durak::PlayerRole playerRole, User &user) {
  auto allowedMoves = calculateAllowedMoves (game, playerRole);
  if (std::ranges::find_if (allowedMoves, [] (auto allowedMove) { return allowedMove == shared_class::Move::AddCards; }) != allowedMoves.end ())
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves { { shared_class::Move::AttackAssistDoneAddingCards, shared_class::Move::AddCards } }));
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves { { shared_class::Move::AttackAssistDoneAddingCards } }));
    }
};

auto const userReloggedInAskAttackAssist = [] (GameDependencies &gameDependencies, userRelogged const &userReloggedEv) {
  if (auto user = std::ranges::find_if (gameDependencies.users, [userName = userReloggedEv.accountName] (auto const &_user) { return _user.accountName == userName; }); user != gameDependencies.users.end ())
    {
      if ((not gameDependencies.passAttackAndAssist.assist && gameDependencies.game.getRoleForName (userReloggedEv.accountName) == durak::PlayerRole::assistAttacker) || (not gameDependencies.passAttackAndAssist.attack && gameDependencies.game.getRoleForName (userReloggedEv.accountName) == durak::PlayerRole::attack))
        {
          defendsWantsToTakeCardsSendMovesToAttackOrAssist (gameDependencies.game, gameDependencies.game.getRoleForName (userReloggedEv.accountName), *user);
        }
    }
};
auto const blockEverythingExceptStartAttack = AllowedMoves { .defend = std::vector<shared_class::Move> {}, .attack = std::vector<shared_class::Move> { shared_class::Move::AddCards }, .assist = std::vector<shared_class::Move> {} };
auto const roundStart = [] (GameDependencies &gameDependencies) {
  sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users, gameDependencies.opponentCards);
  sendAvailableMoves (gameDependencies.game, gameDependencies.users, blockEverythingExceptStartAttack);
};

auto const resumeTimerHandler = [] (GameDependencies &gameDependencies, resumeTimer const &resumeTimerEv) {
  std::ranges::for_each (gameDependencies.users, [playersToResume = resumeTimerEv.playersToResume, &gameDependencies] (User &user) {
    if (std::ranges::find (playersToResume, user.accountName) != playersToResume.end ())
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
          process_event (pauseTimer { { user.accountName } });
          process_event (sendTimerEv {});
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves { {} }));
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsSuccess {}));
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError { "role is not attack" }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError { "pass already set" }));
    }
};

auto const setAssistAnswer = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakAssistPass, User &> const &assistPassEventAndUser, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) {
  auto [assistPassEvent, user] = assistPassEventAndUser;
  if (not gameDependencies.passAttackAndAssist.assist)
    {
      if (gameDependencies.game.getRoleForName (user.accountName) == durak::PlayerRole::assistAttacker)
        {
          gameDependencies.passAttackAndAssist.assist = true;
          process_event (pauseTimer { { user.accountName } });
          process_event (sendTimerEv {});
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves { {} }));
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsSuccess {}));
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError { "role is not assist" }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError { "pass already set" }));
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
      process_event (askDef {});
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
      process_event (chill {});
    }
};

auto const startAskDef = [] (GameDependencies &gameDependencies, boost::sml::back::process<pauseTimer, resumeTimer, sendTimerEv> process_event) {
  if (auto defendingPlayer = gameDependencies.game.getDefendingPlayer ())
    {
      process_event (resumeTimer { { defendingPlayer->id } });
      if (auto user = std::ranges::find_if (gameDependencies.users, [&defendingPlayer] (auto const &_user) { return _user.accountName == defendingPlayer->id; }); user != gameDependencies.users.end ())
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCards {}));
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves { { shared_class::Move::AnswerDefendWantsToTakeCardsYes, shared_class::Move::AnswerDefendWantsToTakeCardsNo } }));
        }
      if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer ())
        {
          process_event (pauseTimer { { attackingPlayer->id } });
        }
      if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer ())
        {
          process_event (pauseTimer { { assistingPlayer->id } });
        }
      process_event (sendTimerEv {});
    }
};

auto const userLeftGame = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakLeaveGame, User &> const &leaveGameEventUser) {
  // TODO i think this should be auto &[event, user] = leaveGameEventUser; in all places where this construct is used
  auto [event, user] = leaveGameEventUser;
  removeUserFromGame (user.accountName, gameDependencies);
  std::ranges::for_each (gameDependencies.users, [] (auto const &user_) { user_.timer->cancel (); });
};

bool
hasToMove (durak::Game const &game, durak::PlayerRole playerRole, PassAttackAndAssist passAttackAndAssist, std::variant<AskAttackAndAssist, AskDef, Chill> const &currentState)
{
  using namespace durak;
  switch (playerRole)
    {
    case PlayerRole::attack:
      {
        if (passAttackAndAssist.attack == false and (game.getTableAsVector ().size () % 2 == 0 or std::holds_alternative<AskAttackAndAssist> (currentState)))
          {
            return true;
          }
        else
          {
            return false;
          }
      }
    case PlayerRole::assistAttacker:
      {

        if (game.getAttackStarted () and passAttackAndAssist.assist == false and (game.getTableAsVector ().size () % 2 == 0 or std::holds_alternative<AskAttackAndAssist> (currentState)))
          {
            return true;
          }
        else
          {
            return false;
          }
      }
    case PlayerRole::defend:
      {
        if (hasToMove (game, durak::PlayerRole::attack, passAttackAndAssist, currentState) or hasToMove (game, durak::PlayerRole::assistAttacker, passAttackAndAssist, currentState))
          {
            return false;
          }
        else
          {
            return true;
          }
      }
    case PlayerRole::waiting:
      {
        return false;
      }
    }
  return false;
}

std::optional<shared_class::DurakNextMoveSuccess>
calcNextMove (std::optional<durak_computer_controlled_opponent::Action> const &action, std::vector<shared_class::Move> const &moves, durak::PlayerRole const &playerRole, std::vector<std::tuple<uint8_t, durak::Card> > const &defendIdCardMapping, std::vector<std::tuple<uint8_t, durak::Card> > const &attackIdCardMapping, std::variant<AskAttackAndAssist, AskDef, Chill> const &currentState)
{
  if (playerRole == durak::PlayerRole::attack)
    {
      if (action.has_value ())
        {
          if (std::ranges::find (moves, shared_class::Move::AddCards) != moves.end ())
            {
              if (auto idCard = std::ranges::find_if (attackIdCardMapping, [value = action->value ()] (auto const &idAndCard) { return value == std::get<0> (idAndCard); }); idCard != attackIdCardMapping.end ())
                {
                  return shared_class::DurakNextMoveSuccess { shared_class::Move::AddCards, std::get<1> (*idCard) };
                }
              else
                {

                  if (std::holds_alternative<AskAttackAndAssist> (currentState))
                    {
                      return shared_class::DurakNextMoveSuccess { shared_class::Move::AttackAssistDoneAddingCards, {} };
                    }
                  else
                    {
                      return std::nullopt;
                    }
                }
            }
          else if (std::holds_alternative<AskAttackAndAssist> (currentState))
            {
              return shared_class::DurakNextMoveSuccess { shared_class::Move::AttackAssistDoneAddingCards, {} };
            }
          else
            {
              return std::nullopt;
            }
        }
      else
        {
          if (moves.size () != 1)
            {
              return std::nullopt;
            }
          else
            {
              return shared_class::DurakNextMoveSuccess { moves.front (), {} };
            }
        }
    }
  else if (playerRole == durak::PlayerRole::defend)
    {
      if (action and action.value ().value () == 253)
        {
          return shared_class::DurakNextMoveSuccess { shared_class::Move::TakeCards, std::nullopt };
        }
      else if (action and std::ranges::find (moves, shared_class::Move::Defend) != moves.end ())
        {
          if (auto idCard = std::ranges::find_if (defendIdCardMapping, [value = action->value ()] (auto const &idAndCard) { return value == std::get<0> (idAndCard); }); idCard != defendIdCardMapping.end ())
            {
              return shared_class::DurakNextMoveSuccess { shared_class::Move::Defend, std::get<1> (*idCard) };
            }
          else
            {
              return std::nullopt;
            }
        }
      else if (std::ranges::find (moves, shared_class::Move::TakeCards) != moves.end ())
        {
          return shared_class::DurakNextMoveSuccess { shared_class::Move::TakeCards, {} };
        }
      else if (std::ranges::find (moves, shared_class::Move::AnswerDefendWantsToTakeCardsNo) != moves.end ())
        {
          return shared_class::DurakNextMoveSuccess { shared_class::Move::AnswerDefendWantsToTakeCardsNo, {} };
        }
      else if (std::ranges::find (moves, shared_class::Move::AnswerDefendWantsToTakeCardsYes) != moves.end ())
        {
          return shared_class::DurakNextMoveSuccess { shared_class::Move::AnswerDefendWantsToTakeCardsYes, {} };
        }
      else
        {
          return std::nullopt;
        }
    }
  else
    {
      return std::nullopt;
    }
}

void
nextMove (GameDependencies &gameDependencies, std::tuple<shared_class::DurakNextMove, User &> const &durakNextMoveUser, std::variant<AskAttackAndAssist, AskDef, Chill> const &currentState)
{
  auto &[event, user] = durakNextMoveUser;
  if (durak_computer_controlled_opponent::tableValidForMoveLookUp (gameDependencies.game.getTable ()))
    {
      auto playerRole = gameDependencies.game.getRoleForName (user.accountName);
      if (hasToMove (gameDependencies.game, playerRole, gameDependencies.passAttackAndAssist, currentState))
        {
          using namespace durak;
          using namespace durak_computer_controlled_opponent;
          soci::session sql (soci::sqlite3, gameDependencies.databasePath);
          auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (gameDependencies.game);
          auto attackCardsCompressed = std::vector<uint8_t> {};
          std::ranges::transform (compressedCardsForAttack, std::back_inserter (attackCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
          auto defendCardsCompressed = std::vector<uint8_t> {};
          std::ranges::transform (compressedCardsForDefend, std::back_inserter (defendCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
          auto const &someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, gameDependencies.game.getTrump ()));
          if (someRound)
            {
              if (someRound->nodes.empty ())
                {
                  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakNextMoveError { "Unsupported card combination." }));
                }
              else
                {
                  auto const &actions = historyEventsToActionsCompressedCards (gameDependencies.game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (gameDependencies.game));
                  auto const &result = nextActionsAndResults (actions, small_memory_tree::SmallMemoryTree { durak_computer_controlled_opponent::database::binaryToSmallMemoryTree (someRound.value ().nodes) });
                  auto const &actionForRole = nextActionForRole (result, playerRole);
                  auto const &allowedMoves = calculateAllowedMovesWithPassState (gameDependencies.game, playerRole, gameDependencies.passAttackAndAssist);
                  auto const &calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack, currentState);
                  if (calculatedNextMove)
                    {
                      user.sendMsgToUser (objectToStringWithObjectName (*calculatedNextMove));
                    }
                  else
                    {
                      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakNextMoveError { "could not find a move. Are you sure you have to Move?" }));
                    }
                }
            }
          else if (attackCardsCompressed.size () == 1 and defendCardsCompressed.size () > 1)
            {
              if (playerRole == durak::PlayerRole::defend)
                {
                  auto nextMoveSuccess = shared_class::DurakNextMoveSuccess {};
                  nextMoveSuccess.nextMove = shared_class::Move::TakeCards;
                  user.sendMsgToUser (objectToStringWithObjectName (nextMoveSuccess));
                }
              else
                {
                  if (gameDependencies.game.getAttackStarted ())
                    {
                      auto nextMoveSuccess = shared_class::DurakNextMoveSuccess {};
                      nextMoveSuccess.nextMove = shared_class::Move::AttackAssistPass;
                      user.sendMsgToUser (objectToStringWithObjectName (nextMoveSuccess));
                    }
                  else
                    {
                      auto nextMoveSuccess = shared_class::DurakNextMoveSuccess {};
                      nextMoveSuccess.nextMove = shared_class::Move::AddCards;
                      nextMoveSuccess.card = gameDependencies.game.getAttackingPlayer ()->getCards ().front ();
                      user.sendMsgToUser (objectToStringWithObjectName (nextMoveSuccess));
                    }
                }
            }
          else
            {
              user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakNextMoveError { "could not find a game for the cards." }));
            }
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakNextMoveError { "Do not move. Game thinks you do not have to move. Please wait for other players to move." }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakNextMoveError { "table not in valid state. Game lookup is only supported for 1 vs. 1 players and if every player only place one card at once." }));
    }
};

auto const nextMoveChill = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakNextMove, User &> const &durakNextMoveUser) { nextMove (gameDependencies, durakNextMoveUser, Chill {}); };
auto const nextMoveAskDef = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakNextMove, User &> const &durakNextMoveUser) { nextMove (gameDependencies, durakNextMoveUser, AskDef {}); };
auto const nextMoveAskAttackAndAssist = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakNextMove, User &> const &durakNextMoveUser) { nextMove (gameDependencies, durakNextMoveUser, AskAttackAndAssist {}); };

auto const startAskAttackAndAssist = [] (GameDependencies &gameDependencies, boost::sml::back::process<pauseTimer, nextRoundTimer, resumeTimer, sendTimerEv> process_event) {
  if (auto defendingPlayer = gameDependencies.game.getDefendingPlayer ())
    {
      process_event (pauseTimer { { defendingPlayer->id } });
    }
  gameDependencies.passAttackAndAssist = PassAttackAndAssist {};
  if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer (); attackingPlayer && not attackingPlayer->getCards ().empty ())
    {
      process_event (resumeTimer { { attackingPlayer->id } });
      if (auto user = std::ranges::find_if (gameDependencies.users, [&attackingPlayer] (auto const &_user) { return _user.accountName == attackingPlayer->id; }); user != gameDependencies.users.end ())
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoYouWantToAddCards {}));
          defendsWantsToTakeCardsSendMovesToAttackOrAssist (gameDependencies.game, durak::PlayerRole::attack, *user);
        }
    }
  else
    {
      gameDependencies.passAttackAndAssist.attack = true;
    }
  if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer (); assistingPlayer && not assistingPlayer->getCards ().empty ())
    {
      process_event (resumeTimer { { assistingPlayer->id } });
      if (auto user = std::ranges::find_if (gameDependencies.users, [&assistingPlayer] (auto const &_user) { return _user.accountName == assistingPlayer->id; }); user != gameDependencies.users.end ())
        {
          user->sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendWantsToTakeCardsFromTableDoYouWantToAddCards {}));
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
      process_event (sendTimerEv {});
    }
};

auto const doPass = [] (GameDependencies &gameDependencies, auto const &eventAndUser, boost::sml::back::process<pauseTimer, sendTimerEv> process_event) {
  auto [event, user] = eventAndUser;
  auto playerName = user.accountName;
  using eventType = typename std::decay<decltype (event)>::type;
  using eventErrorType = typename std::decay<decltype ([&] {
    if constexpr (std::same_as<eventType, shared_class::DurakAttackPass>)
      {
        return shared_class::DurakAttackPassError {};
      }
    else
      {
        return shared_class::DurakAssistPassError {};
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
                  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackPassSuccess {}));
                }
              else
                {
                  gameDependencies.passAttackAndAssist.assist = true;
                  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAssistPassSuccess {}));
                }
              user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves {}));
              process_event (pauseTimer { { playerName } });
              process_event (sendTimerEv {});
            }
          else
            {
              user.sendMsgToUser (objectToStringWithObjectName (eventErrorType { "account role is not attack or assist: " + playerName }));
            }
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (eventErrorType { "there are not beaten cards on the table" }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (eventErrorType { "can not pass if attack is not started" }));
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
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswerSuccess {}));
    }
};

auto const handleDefendPass = [] (GameDependencies &gameDependencies, std::tuple<shared_class::DurakDefendPass, User &> const &durakDefendPassAndUser, boost::sml::back::process<askAttackAndAssist> process_event) {
  auto [defendPassEv, user] = durakDefendPassAndUser;
  if (gameDependencies.game.getRoleForName (user.accountName) == durak::PlayerRole::defend)
    {
      if (gameDependencies.game.getAttackStarted ())
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassSuccess {}));
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves { {} }));
          process_event (askAttackAndAssist {});
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassError { "attack is not started" }));
        }
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendPassError { "account role is not defined: " + user.accountName }));
    }
};

auto const resetPassStateMachineData = [] (GameDependencies &gameDependencies) { gameDependencies.passAttackAndAssist = PassAttackAndAssist {}; };

auto const tryToAttackAndInformOtherPlayers = [] (GameDependencies &gameDependencies, shared_class::DurakAttack const &attackEv, durak::PlayerRole playerRole, boost::sml::back::process<resumeTimer, pauseTimer, sendTimerEv> process_event, bool isChill, User &user) {
  if (gameDependencies.game.playerAssists (playerRole, attackEv.cards))
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackSuccess {}));
      if (isChill)
        {
          if (auto defendingPlayer = gameDependencies.game.getDefendingPlayer ())
            {
              process_event (resumeTimer { { defendingPlayer->id } });
            }
          if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer ())
            {
              process_event (pauseTimer { { attackingPlayer->id } });
            }
          if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer ())
            {
              process_event (pauseTimer { { assistingPlayer->id } });
            }
          sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users, gameDependencies.opponentCards);
          sendAvailableMoves (gameDependencies.game, gameDependencies.users);
          process_event (sendTimerEv {});
        }
      else
        {
          sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users, gameDependencies.opponentCards);
          auto otherPlayerRole = (playerRole == durak::PlayerRole::attack) ? durak::PlayerRole::assistAttacker : durak::PlayerRole::attack;
          defendsWantsToTakeCardsSendMovesToAttackOrAssist (gameDependencies.game, playerRole, user);
          if (auto otherPlayer = (otherPlayerRole == durak::PlayerRole::attack) ? gameDependencies.game.getAttackingPlayer () : gameDependencies.game.getAssistingPlayer ())
            {
              if (auto otherPlayerItr = std::ranges::find_if (gameDependencies.users, [otherPlayerName = otherPlayer->id] (auto const &_user) { return _user.accountName == otherPlayerName; }); otherPlayerItr != gameDependencies.users.end ())
                {
                  defendsWantsToTakeCardsSendMovesToAttackOrAssist (gameDependencies.game, otherPlayerRole, *otherPlayerItr);
                }
              process_event (resumeTimer { { otherPlayer->id } });
              process_event (sendTimerEv {});
            }
        }
      gameDependencies.passAttackAndAssist = PassAttackAndAssist {};
    }
  else
    {
      user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackError { "not allowed to play cards" }));
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
              process_event (resumeTimer { { defendingPlayer->id } });
            }
          if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer ())
            {
              process_event (pauseTimer { { attackingPlayer->id } });
            }
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackSuccess {}));
          sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users, gameDependencies.opponentCards);
          sendAvailableMoves (gameDependencies.game, gameDependencies.users);
          process_event (sendTimerEv {});
          gameDependencies.passAttackAndAssist = PassAttackAndAssist {};
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackError { "not allowed to play cards" }));
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
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendSuccess {}));
          sendGameDataToAccountsInGame (gameDependencies.game, gameDependencies.users, gameDependencies.opponentCards);
          sendAvailableMoves (gameDependencies.game, gameDependencies.users);
          if (gameDependencies.game.countOfNotBeatenCardsOnTable () == 0)
            {
              process_event (pauseTimer { { user.accountName } });
              if (auto attackingPlayer = gameDependencies.game.getAttackingPlayer (); attackingPlayer && not attackingPlayer->getCards ().empty ())
                {
                  process_event (resumeTimer { { attackingPlayer->id } });
                }
              if (auto assistingPlayer = gameDependencies.game.getAssistingPlayer (); assistingPlayer && not assistingPlayer->getCards ().empty ())
                {
                  process_event (resumeTimer { { assistingPlayer->id } });
                }
              process_event (sendTimerEv {});
            }
        }
      else
        {
          user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakDefendError { "Error while defending " + fmt::format ("CardToBeat: {},{} vs. Card: {},{}", defendEvent.cardToBeat.value, magic_enum::enum_name (defendEvent.cardToBeat.type), defendEvent.card.value, magic_enum::enum_name (defendEvent.card.type)) }));
        }
    }
};

auto const blockOnlyDef = [] (std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &defendWantToTakeCardsEventAndUser) {
  auto [event, user] = defendWantToTakeCardsEventAndUser;
  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAllowedMoves { {} }));
};

auto const attackErrorUserHasWrongRole = [] (std::tuple<shared_class::DurakAttack, User &> const &durakAttackAndUser, GameDependencies &gameDependencies) {
  auto [event, user] = durakAttackAndUser;
  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAttackError { "Wrong role error. To attack you need to have the role attack or assist. Your role is: " + std::string { magic_enum::enum_name (gameDependencies.game.getRoleForName (user.accountName)) } }));
};

auto const needsToBeDefendingPlayerError = [] (std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &askDefendWantToTakeCardsAnswerEventAndUser, GameDependencies &gameDependencies) {
  auto [askDefendWantToTakeCardsAnswerEvent, user] = askDefendWantToTakeCardsAnswerEventAndUser;
  user.sendMsgToUser (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswerError { "Wrong role error. To take or discard cards you need to have the role defend. Your role is: " + std::string { magic_enum::enum_name (gameDependencies.game.getRoleForName (user.accountName)) } }));
};

auto const wantsToTakeCards = [] (std::tuple<shared_class::DurakAskDefendWantToTakeCardsAnswer, User &> const &askDefendWantToTakeCardsAnswerEventAndUser) {
  auto [askDefendWantToTakeCardsAnswerEvent, user] = askDefendWantToTakeCardsAnswerEventAndUser;
  return askDefendWantToTakeCardsAnswerEvent.answer;
};

auto const sendStartGameToUser = [] (GameDependencies &gameDependencies) { std::ranges::for_each (gameDependencies.users, [&gameDependencies] (User &user) { user.sendMsgToUser (objectToStringWithObjectName (matchmaking_game::StartGameSuccess { gameDependencies.gameName })); }); };

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
                *"init"_s + event<start>
                            / (sendStartGameToUser, roundStart, process(sendTimerEv{})) = state<Chill>
/*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
                , state<Chill> + on_entry<_>[isNotFirstRound]
                                 / (resetPassStateMachineData, process(nextRoundTimer{}), roundStart),
                state<Chill> + event<askDef> = state<AskDef>,
                state<Chill> + event<askAttackAndAssist> = state<AskAttackAndAssist>,
                state<Chill> + event<AttackPass> / (setAttackPass, checkData),
                state<Chill> + event<AssistPass> / (setAssistPass, checkData),
                state<Chill> + event<DefendPass> / handleDefendPass,
                state<Chill> + event<Attack>[not isAttackingOrAssistingPlayer] / attackErrorUserHasWrongRole,
                state<Chill> + event<Attack> / doAttackChill,
                state<Chill> + event<Defend>[isDefendingPlayer] / doDefend,
                state<Chill> + event<userRelogged> / userReloggedInChillState,
                state<Chill> + event<NextMove> / nextMoveChill
// /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
                , state<AskDef> + on_entry<_> / startAskDef, state<AskDef> +
                                                             event<DefendWantToTakeCardsAnswer>[not isDefendingPlayer] /
                                                             needsToBeDefendingPlayerError, state<AskDef> +
                                                                                            event<DefendWantToTakeCardsAnswer>[wantsToTakeCards] /
                                                                                            blockOnlyDef = state<AskAttackAndAssist>,
                state<AskDef> + event<DefendWantToTakeCardsAnswer> / handleDefendSuccess = state<Chill>,
                state<AskDef> + event<userRelogged> / (userReloggedInAskDef),
                state<AskDef> + event<NextMove> / nextMoveAskDef
// /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
                , state<AskAttackAndAssist> + on_entry<_> / startAskAttackAndAssist,
                state<AskAttackAndAssist> + event<AttackPass> / (setAttackAnswer, checkAttackAndAssistAnswer),
                state<AskAttackAndAssist> + event<AssistPass> / (setAssistAnswer, checkAttackAndAssistAnswer),
                state<AskAttackAndAssist> +
                event<Attack>[not isAttackingOrAssistingPlayer] / attackErrorUserHasWrongRole,
                state<AskAttackAndAssist> + event<Attack> / doAttackAskAttackAndAssist,
                state<AskAttackAndAssist> + event<chill> = state<Chill>,
                state<AskAttackAndAssist> + event<userRelogged> / userReloggedInAskAttackAssist,
                state<AskAttackAndAssist> + event<NextMove> / nextMoveAskAttackAndAssist
// /*--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/      
                , *"leaveGameHandler"_s + event<LeaveGame> / userLeftGame
//,*"nextMove"_s
                , *"timerHandler"_s + event<initTimer>[timerActive] / initTimerHandler,
                "timerHandler"_s + event<nextRoundTimer>[timerActive] / nextRoundTimerHandler,
                "timerHandler"_s + event<pauseTimer>[timerActive] / pauseTimerHandler,
                "timerHandler"_s + event<resumeTimer>[timerActive] / resumeTimerHandler,
                "timerHandler"_s + event<sendTimerEv>[timerActive] / sendTimer
// clang-format on   
        );
    }
};


struct my_logger {
    template<class SM, class TEvent>
    void
    log_process_event(const TEvent
                      &event) {
        if constexpr (boost::fusion::traits::is_sequence<TEvent>::value) {
            std::cout <<
                      "\n[" << aux::get_type_name<SM>() << "]"
                      << "[process_event] " << objectToStringWithObjectName(event) << std::endl;
        } else {
            printf("[%s][process_event] %s\n", aux::get_type_name<SM>(), aux::get_type_name<TEvent>());
        }
    }

    template<class SM, class TGuard, class TEvent>
    void
    log_guard(const TGuard &, const TEvent &, bool result) {
        printf("[%s][guard]\t  %s %s\n", aux::get_type_name<SM>(), aux::get_type_name<TGuard>(),
               (result ? "[OK]" : "[Reject]"));
    }

    template<class SM, class TAction, class TEvent>
    void
    log_action(const TAction &, const TEvent &) {
        printf("[%s][action]\t  %s \n", aux::get_type_name<SM>(), aux::get_type_name<TAction>());
    }

    template<class SM, class TSrcState, class TDstState>
    void
    log_state_change(const TSrcState &src, const TDstState &dst) {
        printf("[%s][transition]\t  %s -> %s\n", aux::get_type_name<SM>(), src.c_str(), dst.c_str());
    }
};


struct Game::StateMachineWrapper {
    StateMachineWrapper(Game *owner, matchmaking_game::StartGame const &startGame, std::string const &gameName,
                        std::list<User> &&users, boost::asio::io_context &ioContext_,
                        boost::asio::ip::tcp::endpoint const &gameToMatchmakingEndpoint_) : gameDependencies{startGame,
                                                                                                             gameName,
                                                                                                             std::move(
                                                                                                                     users),
                                                                                                             ioContext_,
                                                                                                             gameToMatchmakingEndpoint_},
                                                                                            impl(owner,
#ifdef LOG_FOR_STATE_MACHINE
                                                                                                    logger,
#endif
                                                                                                 gameDependencies) {}

    GameDependencies gameDependencies;

#ifdef LOG_FOR_STATE_MACHINE
    my_logger logger;
    boost::sml::sm<StateMachineImpl, boost::sml::logger<my_logger>,boost::sml::process_queue<std::queue> > impl;
#else
    boost::sml::sm<StateMachineImpl, boost::sml::process_queue<std::queue> > impl;
#endif
};

void // has to be after YourClass::StateMachineWrapper definition
Game::StateMachineWrapperDeleter::operator()(StateMachineWrapper *p)const {
    delete p;
}


Game::Game(matchmaking_game::StartGame const &startGame, std::string const &gameName, std::list<User> &&users,
           boost::asio::io_context &ioContext_, boost::asio::ip::tcp::endpoint const &gameToMatchmakingEndpoint_,
           std::filesystem::path const &databasePath) : sm{
        new StateMachineWrapper{this, startGame, gameName, std::move(users), ioContext_, gameToMatchmakingEndpoint_}} {
    auto userNames = std::vector<std::string>{};
    std::ranges::transform(sm->gameDependencies.users, std::back_inserter(userNames),[](User const &user) { return user.accountName; });
    if(auto gameOption=toGameOption(startGame.gameOptionAsString.gameOptionAsString)){
        sm->gameDependencies.game = durak::Game{std::move(userNames), gameOption->gameOption};
        sm->gameDependencies.timerOption.timerType = gameOption->timerOption.timerType;
        sm->gameDependencies.opponentCards = gameOption->opponentCards;
        sm->gameDependencies.timerOption.timeAtStart = std::chrono::seconds{gameOption->timerOption.timeAtStartInSeconds};
        sm->gameDependencies.timerOption.timeForEachRound = std::chrono::seconds{gameOption->timerOption.timeForEachRoundInSeconds};
        sm->gameDependencies.databasePath = databasePath;
   }
   else
   {
    std::cout << "StartGameError: " << gameOption.error() << std::endl;
   }  
}



std::optional<std::string>
Game::processEvent (std::string const &event, std::string const &accountName)
{
  std::vector<std::string> splitMessage {};
  boost::algorithm::split (splitMessage, event, boost::is_any_of ("|"));
  auto result = std::optional<std::string> {};
  if (splitMessage.size () == 2)
    {
      auto const &typeToSearch = splitMessage.at (0);
      auto const &objectAsString = splitMessage.at (1);
      bool typeFound = false;
      boost::hana::for_each (shared_class::gameTypes, [&] (auto const &x) {
        if (typeToSearch == confu_json::type_name<typename std::decay<decltype (x)>::type> ())
          {
            typeFound = true;
            boost::system::error_code ec {};
            auto messageAsObject = confu_json::read_json (objectAsString, ec);
            if (ec) result = "read_json error: " + ec.message ();
            else if (not sm->impl.process_event (std::tuple<std::decay_t<decltype (x)>, User &> { confu_json::to_object<std::decay_t<decltype (x)> > (messageAsObject), user (accountName).value () }))
              result = "No transition found";
            return;
          }
      });
      if (not typeFound) result = "could not find a match for typeToSearch in shared_class::gameTypes '" + typeToSearch + "'";
    }
  else
    result = "Not supported event. event syntax: EventName|JsonObject";
  return result;
}

void
Game::startGame ()
{
  sm->impl.process_event (initTimer {});
  sm->impl.process_event (start {});
}

std::string const &
Game::gameName () const
{
  return sm->gameDependencies.gameName;
}

bool
Game::isUserInGame (std::string const &userName) const
{
  return std::ranges::find (sm->gameDependencies.users, userName, [] (User const &user) { return user.accountName; }) != sm->gameDependencies.users.end ();
}

bool
Game::removeUser (std::string const &userName)
{
  return std::erase_if (sm->gameDependencies.users, [&userName] (User const &user) { return userName == user.accountName; }) > 0;
}

size_t
Game::usersInGame () const
{
  return sm->gameDependencies.users.size ();
}

boost::optional<User &>
Game::user (std::string const &userName)
{
  auto userItr = std::ranges::find (sm->gameDependencies.users, userName, [] (User const &user) { return user.accountName; });
  return userItr != sm->gameDependencies.users.end () ? *userItr : boost::optional<User &> {};
}

durak::Game const &
Game::durakGame () const
{
  return sm->gameDependencies.game;
}
