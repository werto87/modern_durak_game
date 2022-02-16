#include "mockserver.hxx"
#include "src/serialization.hxx"
#include "src/server/server.hxx"
#include "src/util.hxx"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json/src.hpp>
#include <catch2/catch.hpp>
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
  auto const DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING = u_int16_t{ 33333 };
  auto const DEFAULT_PORT_MATCHMAKING_TO_GAME = u_int16_t{ 44444 };
  using namespace boost::asio;
  auto ioContext = boost::asio::io_context{};
  signal_set signals (ioContext, SIGINT, SIGTERM);
  signals.async_wait ([&] (auto, auto) { ioContext.stop (); });
  auto server = Server{};
  using namespace boost::asio::experimental::awaitable_operators;
  auto userToGameViaMatchmaking = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  auto matchmakingToGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
  // clang-format off
  auto matchmakingGame = Mockserver{ { ip::tcp::v4 (), 22222 }, { .requestStartsWithResponse = { { R"foo(GameOver)foo", R"foo(GameOverSuccess|{})foo" } } }, "MOCK_matchmaking_game", fmt::fg (fmt::color::violet), "0" };
  // clang-format on
  co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
  auto messagesFromGamePlayer1 = std::vector<std::string>{};
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
  ioContext.run ();
  ioContext.reset ();
  // ioContext.stop ();
  SECTION ("DurakLeaveGame")
  {
    // TODO find a way to stop this test :)
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto user1Logic = [] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("LeaveGameSuccess"))
        {
          co_spawn (ioContext, myWebsocket->async_close (), printException);
        }
    };
    auto user2Logic = [] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakLeaveGame{}));
        }
      if (msg.starts_with ("LeaveGameSuccess"))
        {
          co_spawn (ioContext, myWebsocket->async_close (), printException);
        }
    };
    co_spawn (ioContext, connectWebsocket (user1Logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}},"user1"), printException);
    co_spawn (ioContext, connectWebsocket (user2Logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"), printException);
    ioContext.run ();
  }
  // SECTION ("DurakAttackPass")
  // {
  //   co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
  //   auto someMsg = [] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
  //     if (msg.starts_with ("ConnectToGameSuccess"))
  //       {
  //         myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakAttackPass{}));
  //       }
  //     if (msg.starts_with ("DurakAttackPassError"))
  //       {
  //         ioContext.stop ();
  //       }
  //   };
  //   auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  //   co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   ioContext.run ();
  // }
  // SECTION ("DurakAssistPass")
  // {
  //   co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
  //   auto someMsg = [] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
  //     if (msg.starts_with ("ConnectToGameSuccess"))
  //       {
  //         myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakAssistPass{}));
  //       }
  //     if (msg.starts_with ("DurakAssistPassError"))
  //       {
  //         ioContext.stop ();
  //       }
  //   };
  //   auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  //   co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   ioContext.run ();
  // }
  // SECTION ("DurakDefendPass")
  // {
  //   co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
  //   auto someMsg = [] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
  //     if (msg.starts_with ("ConnectToGameSuccess"))
  //       {
  //         myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakDefendPass{}));
  //       }
  //     if (msg.starts_with ("DurakDefendPassError"))
  //       {
  //         ioContext.stop ();
  //       }
  //   };
  //   auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  //   co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   ioContext.run ();
  // }
  // SECTION ("DurakDefend")
  // {
  //   co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
  //   auto someMsg = [] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
  //     if (msg.starts_with ("ConnectToGameSuccess"))
  //       {
  //         myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakDefend{}));
  //       }
  //     if (msg.starts_with ("DurakDefendError"))
  //       {
  //         ioContext.stop ();
  //       }
  //   };
  //   auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  //   co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   ioContext.run ();
  // }
  // SECTION ("DurakAttack")
  // {
  //   co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
  //   auto someMsg = [] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
  //     if (msg.starts_with ("ConnectToGameSuccess"))
  //       {
  //         myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakAttack{}));
  //       }
  //     if (msg.starts_with ("DurakAttackError"))
  //       {
  //         ioContext.stop ();
  //       }
  //   };
  //   auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  //   co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //   ioContext.run ();
  // }
  // SECTION ("DurakAskDefendWantToTakeCardsAnswer")
  // {
  //   co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
  //   auto someMsg = [] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
  //     if (msg.starts_with ("ConnectToGameSuccess"))
  //       {
  //         myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswer{}));
  //       }
  //     if (msg.starts_with ("UnhandledEventError"))
  //       {
  //         ioContext.stop ();
  //       }
  //   };
  //   auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  //     co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //     co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
  //     ioContext.run ();
  // }
  ioContext.stop ();
  ioContext.reset ();
}