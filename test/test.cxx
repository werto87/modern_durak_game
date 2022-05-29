#include "mockserver.hxx"
#include "src/serialization.hxx"
#include "src/server/server.hxx"
#include "src/util.hxx"
#include "test/constant.hxx"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json/src.hpp>
#include <catch2/catch.hpp>
#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
boost::asio::awaitable<void>
connectWebsocket (auto handleMsgFromGame, boost::asio::io_context &ioContext, boost::asio::ip::tcp::endpoint const &endpoint, std::vector<std::string> sendMessageBeforeStartRead = {}, std::optional<std::string> connectionName = {})
{
  using namespace boost::asio;
  using namespace boost::beast;

  auto connection = std::make_shared<Websocket> (Websocket{ ioContext });
  get_lowest_layer (*connection).expires_never ();
  connection->set_option (websocket::stream_base::timeout::suggested (role_type::client));
  connection->set_option (websocket::stream_base::decorator ([] (websocket::request_type &req) { req.set (http::field::user_agent, std::string (BOOST_BEAST_VERSION_STRING) + " websocket-client-async-ssl"); }));
  co_await get_lowest_layer (*connection).async_connect (endpoint, use_awaitable);
  co_await connection->async_handshake ("localhost:" + std::to_string (endpoint.port ()), "/", use_awaitable);
  static size_t id = 0;
  auto myWebsocket = std::make_shared<MyWebsocket<Websocket> > (MyWebsocket<Websocket>{ std::move (connection), connectionName ? connectionName.value () : std::string{ "connectWebsocket" }, fmt::fg (fmt::color::orange_red), std::to_string (id++) });
  for (auto message : sendMessageBeforeStartRead)
    {
      co_await myWebsocket->async_write_one_message (message);
    }
  using namespace boost::asio::experimental::awaitable_operators;
  co_await (myWebsocket->readLoop ([myWebsocket, handleMsgFromGame, &ioContext] (const std::string &msg) { handleMsgFromGame (ioContext, msg, myWebsocket); }) && myWebsocket->writeLoop ());
}

TEST_CASE ("send message to game", "[game]")
{
  using namespace boost::asio;
  auto ioContext = boost::asio::io_context{};
  signal_set signals (ioContext, SIGINT, SIGTERM);
  signals.async_wait ([&] (auto, auto) { ioContext.stop (); });
  auto server = Server{};
  using namespace boost::asio::experimental::awaitable_operators;
  auto userToGameViaMatchmaking = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  auto matchmakingToGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
  auto gameToMatchmaking = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_GAME_TO_MATCHMAKING };
  // clang-format off
  auto matchmakingGame = Mockserver{ gameToMatchmaking, { .requestStartsWithResponse = { { R"foo(GameOver)foo", R"foo(GameOverSuccess|{})foo" } } }, "MOCK_matchmaking_game", fmt::fg (fmt::color::violet), "0" };
  // clang-format on
  co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
  auto gameName = std::string{};
  auto handleMsgFromGame = [&gameName] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> >) {
    if (msg.starts_with ("StartGameSuccess"))
      {
        std::vector<std::string> splitMesssage{};
        boost::algorithm::split (splitMesssage, msg, boost::is_any_of ("|"));
        if (splitMesssage.size () == 2)
          {
            auto const &objectAsString = splitMesssage.at (1);
            gameName = stringToObject<matchmaking_game::StartGameSuccess> (objectAsString).gameName;
            ioContext.stop ();
          }
      }
  };
  auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
  // clang-format off
  auto sendMessageBeforeStartRead = std::vector<std::string>{{R"foo(StartGame|{"players":["81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"],"gameOption":{"gameOption":{"maxCardValue":9,"typeCount":4,"numberOfCardsPlayerShouldHave":6,"roundToStart":1,"customCardDeck":null}},"ratedGame":false})foo"}};
  // clang-format on
  co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
  ioContext.run_for (std::chrono::seconds{ 5 });
  ioContext.reset ();
  SECTION ("DurakLeaveGame")
  {
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto leaveGameSuccessCalledOnUser2 = false;
    auto user1Logic = [] (auto &&, auto &&, auto &&) {};
    auto user2Logic = [&leaveGameSuccessCalledOnUser2] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakLeaveGame{}));
        }
      if (msg.starts_with ("LeaveGameSuccess"))
        {
          leaveGameSuccessCalledOnUser2 = true;
          ioContext.stop ();
        }
    };
    co_spawn (ioContext, connectWebsocket (user1Logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}},"user1"), printException);
    co_spawn (ioContext, connectWebsocket (user2Logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (leaveGameSuccessCalledOnUser2);
  }
  SECTION ("DurakAttackPass")
  {
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto durakAttackPassError = false;
    auto someMsg = [&durakAttackPassError] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakAttackPass{}));
        }
      if (msg.starts_with ("DurakAttackPassError"))
        {
          durakAttackPassError = true;
          ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
      co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
      co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
      ioContext.run_for (std::chrono::seconds{ 5 });
      REQUIRE (durakAttackPassError);
  }
  SECTION ("DurakAssistPass")
  {
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto durakAssistPassError = false;
    auto someMsg = [&durakAssistPassError] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakAssistPass{}));
        }
      if (msg.starts_with ("DurakAssistPassError"))
        {
          durakAssistPassError = true;
          ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (durakAssistPassError);
  }
  SECTION ("DurakDefendPass")
  {
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto durakDefendPassError = false;
    auto someMsg = [&durakDefendPassError] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakDefendPass{}));
        }
      if (msg.starts_with ("DurakDefendPassError"))
        {
          durakDefendPassError = true;
          ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (durakDefendPassError);
  }
  SECTION ("DurakDefend")
  {
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto durakDefendError = false;
    auto someMsg = [&durakDefendError] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakDefend{}));
        }
      if (msg.starts_with ("DurakDefendError"))
        {
          durakDefendError = true;
          ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (durakDefendError);
  }
  SECTION ("DurakAttack")
  {
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto durakAttackError = false;
    auto someMsg = [&durakAttackError] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakAttack{}));
        }
      if (msg.starts_with ("DurakAttackError"))
        {
          durakAttackError = true;
          ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (durakAttackError);
  }
  SECTION ("DurakAskDefendWantToTakeCardsAnswer")
  {
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto unhandledEventError = false;
    auto someMsg = [&unhandledEventError] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswer{}));
        }
      if (msg.starts_with ("UnhandledEventError"))
        {
          unhandledEventError = true;
          ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
      co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
      co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
      ioContext.run_for (std::chrono::seconds{ 5 });
      REQUIRE (unhandledEventError);
  }
  ioContext.stop ();
  ioContext.reset ();
}

TEST_CASE ("game not found", "[game]")
{
  using namespace boost::asio;
  auto ioContext = boost::asio::io_context{};
  signal_set signals (ioContext, SIGINT, SIGTERM);
  signals.async_wait ([&] (auto, auto) { ioContext.stop (); });
  auto server = Server{};
  using namespace boost::asio::experimental::awaitable_operators;
  auto userToGameViaMatchmaking = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  auto matchmakingToGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
  auto gameToMatchmaking = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_GAME_TO_MATCHMAKING };
  SECTION ("DurakAskDefendWantToTakeCardsAnswer")
  {
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto unhandledEventError = false;
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext,
              connectWebsocket (
                  [&unhandledEventError] (auto &&ioContext, auto const &msg, auto &&) {
                    if (boost::starts_with (msg, "DurakAttackError"))
                      {
                        unhandledEventError = true;
                        ioContext.stop ();
                      }
                  },
                  ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{ { objectToStringWithObjectName (shared_class::DurakAttack{}) } }),
              printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (unhandledEventError);
  }
  ioContext.stop ();
  ioContext.reset ();
}