#include "modern_durak_game/server/server.hxx"
#include "modern_durak_game/util/util.hxx"
#include "test/constant.hxx"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json/src.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <catch2/catch.hpp>
#include <chrono>
#include <confu_json/to_json.hxx>
#include <durak/card.hxx>
#include <durak_computer_controlled_opponent/database.hxx>
#include <exception>
#include <iostream>
#include <modern_durak_game_option/userDefinedGameOption.hxx>
#include <my_web_socket/mockServer.hxx>
#include <sstream>
#include <stdexcept>

boost::asio::awaitable<void>
connectWebsocket (auto handleMsgFromGame, boost::asio::io_context &ioContext, boost::asio::ip::tcp::endpoint const &endpoint, std::vector<std::string> sendMessageBeforeStartRead = {}, std::optional<std::string> connectionName = {})
{
  using namespace boost::asio;
  using namespace boost::beast;
  auto connection = my_web_socket::WebSocket { ioContext };
  get_lowest_layer (connection).expires_never ();
  connection.set_option (websocket::stream_base::timeout::suggested (role_type::client));
  connection.set_option (websocket::stream_base::decorator ([] (websocket::request_type &req) { req.set (http::field::user_agent, std::string (BOOST_BEAST_VERSION_STRING) + " websocket-client-async-ssl"); }));
  co_await get_lowest_layer (connection).async_connect (endpoint, use_awaitable);
  co_await connection.async_handshake ("localhost:" + std::to_string (endpoint.port ()), "/", use_awaitable);
  static size_t id = 0;
  auto myWebSocket = std::make_shared<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > (my_web_socket::MyWebSocket<my_web_socket::WebSocket> { std::move (connection), connectionName ? connectionName.value () : std::string { "connectWebsocket" }, fmt::fg (fmt::color::orange_red), std::to_string (id++) });
  for (auto const &message : sendMessageBeforeStartRead)
    {
      co_await myWebSocket->async_write_one_message (message);
    }
  using namespace boost::asio::experimental::awaitable_operators;
  co_await (myWebSocket->readLoop ([myWebSocket, handleMsgFromGame, &ioContext] (std::string const &msg) { handleMsgFromGame (ioContext, msg, myWebSocket); }) && myWebSocket->writeLoop ());
}

TEST_CASE ("send message to game", "[game]")
{
  using namespace boost::asio;
  auto ioContext = boost::asio::io_context {};
  signal_set signals (ioContext, SIGINT, SIGTERM);
  signals.async_wait ([&] (auto, auto) { ioContext.stop (); });
  auto server = Server {};
  using namespace boost::asio::experimental::awaitable_operators;
  auto userToGameViaMatchmaking = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
  auto matchmakingToGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
  auto gameToMatchmaking = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), boost::numeric_cast<unsigned short> (std::stoul (DEFAULT_PORT_GAME_TO_MATCHMAKING)) };
  // clang-format off
  auto matchmakingGame = my_web_socket::MockServer{ gameToMatchmaking, { .requestStartsWithResponse = { { R"foo(GameOver)foo", R"foo(GameOverSuccess|{})foo" } } }, "MOCK_matchmaking_game", fmt::fg (fmt::color::violet), "0" };
  // clang-format on
  co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
  auto gameName = std::string {};
  auto handleMsgFromGame = [&gameName] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> >) {
    if (msg.starts_with ("StartGameSuccess"))
      {
        std::vector<std::string> splitMessage {};
        boost::algorithm::split (splitMessage, msg, boost::is_any_of ("|"));
        if (splitMessage.size () == 2)
          {
            auto const &objectAsString = splitMessage.at (1);
            gameName = stringToObject<matchmaking_game::StartGameSuccess> (objectAsString).gameName;
            _ioContext.stop ();
          }
      }
  };

  SECTION ("DurakLeaveGame")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"DurakLeaveGame81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto leaveGameSuccessCalledOnUser2 = false;
    auto user1Logic = [] (auto &&, auto &&, auto &&) {};
    auto user2Logic = [&leaveGameSuccessCalledOnUser2] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > myWebSocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakLeaveGame {}));
        }
      if (msg.starts_with ("LeaveGameSuccess"))
        {
          leaveGameSuccessCalledOnUser2 = true;
          _ioContext.stop ();
        }
    };
    co_spawn (ioContext, connectWebsocket (user1Logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}},"user1"), my_web_socket::printException);
    co_spawn (ioContext, connectWebsocket (user2Logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakLeaveGame81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (leaveGameSuccessCalledOnUser2);
  }
  SECTION ("DurakAttackPass")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"DurakAttackPass81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto durakAttackPassError = false;
    auto someMsg = [&durakAttackPassError] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > myWebSocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakAttackPass {}));
        }
      if (msg.starts_with ("DurakAttackPassError"))
        {
          durakAttackPassError = true;
          _ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
      co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), my_web_socket::printException);
      co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakAttackPass81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), my_web_socket::printException);
      ioContext.run_for (std::chrono::seconds { 5 });
      REQUIRE (durakAttackPassError);
  }
  SECTION ("DurakAssistPass")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"DurakAssistPass81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto durakAssistPassError = false;
    auto someMsg = [&durakAssistPassError] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > myWebSocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakAssistPass {}));
        }
      if (msg.starts_with ("DurakAssistPassError"))
        {
          durakAssistPassError = true;
          _ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakAssistPass81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (durakAssistPassError);
  }
  SECTION ("DurakDefendPass")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"DurakDefendPass81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto durakDefendPassError = false;
    auto someMsg = [&durakDefendPassError] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > myWebSocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakDefendPass {}));
        }
      if (msg.starts_with ("DurakDefendPassError"))
        {
          durakDefendPassError = true;
          _ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakDefendPass81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (durakDefendPassError);
  }
  SECTION ("DurakDefend")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"DurakDefend81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto durakDefendError = false;
    auto someMsg = [&durakDefendError] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > myWebSocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakDefend {}));
        }
      if (msg.starts_with ("DurakDefendError"))
        {
          durakDefendError = true;
          _ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakDefend81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (durakDefendError);
  }
  SECTION ("DurakAttack")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"DurakAttack81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto durakAttackError = false;
    auto someMsg = [&durakAttackError] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > myWebSocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakAttack {}));
        }
      if (msg.starts_with ("DurakAttackError"))
        {
          durakAttackError = true;
          _ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakAttack81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (durakAttackError);
  }
  SECTION ("DurakAskDefendWantToTakeCardsAnswer")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"DurakAskDefendWantToTakeCardsAnswer81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto unhandledEventError = false;
    auto someMsg = [&unhandledEventError] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > myWebSocket) {
      if (msg.starts_with ("ConnectToGameSuccess"))
        {
          myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakAskDefendWantToTakeCardsAnswer {}));
        }
      if (msg.starts_with ("UnhandledEventError"))
        {
          unhandledEventError = true;
          _ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
      co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
      co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakAskDefendWantToTakeCardsAnswer81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
      ioContext.run_for (std::chrono::seconds { 5 });
      REQUIRE (unhandledEventError);
  }
  SECTION ("NextMove")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"NextMove81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto unhandledEventError = false;
    auto someMsg = [&unhandledEventError] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > myWebSocket) {
      if (msg.starts_with ("GameData"))
        {
          myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakNextMove {}));
        }
      if (msg.starts_with ("DurakNextMoveSuccess"))
        {
          unhandledEventError = true;
          _ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"NextMove81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (unhandledEventError);
  }
  SECTION ("ComputerControlledOpponent")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    gameOption.computerControlledPlayerCount = 1;
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    auto playerCount = size_t {};
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto logic = [&playerCount] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> >) {
      if (msg.starts_with ("GameData"))
        {
          std::vector<std::string> splitMessage {};
          boost::algorithm::split (splitMessage, msg, boost::is_any_of ("|"));
          if (splitMessage.size () == 2)
            {
              auto const &objectAsString = splitMessage.at (1);
              auto gameData = stringToObject<durak::GameData> (objectAsString);
              playerCount = gameData.players.size ();
              _ioContext.stop ();
            }
        }
    };
    co_spawn (ioContext, connectWebsocket (logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"),my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (playerCount == 2);
  }
  SECTION ("NextMove 1vs3 attack")
  {
    using namespace durak;
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    startGame.players = { "NextMove" };
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.cardsInHands = std::vector<std::vector<Card> > {};
    gameOption.gameOption.customCardDeck = std::vector<Card> {};
    auto playerOneCards = std::vector<Card> { { 1, Type::clubs } };
    auto playerTwoCards = std::vector<Card> { { 2, Type::clubs }, { 2, Type::hearts }, { 1, Type::hearts } };
    gameOption.gameOption.cardsInHands->push_back (playerOneCards);
    gameOption.gameOption.cardsInHands->push_back (playerTwoCards);
    gameOption.computerControlledPlayerCount = 1;
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto unhandledEventError = false;
    auto someMsg = [&unhandledEventError] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > myWebSocket) {
      if (msg.starts_with ("GameData"))
        {
          myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakNextMove {}));
        }
      if (msg.starts_with ("DurakNextMoveSuccess"))
        {
          unhandledEventError = true;
          _ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"NextMove","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}),my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (unhandledEventError);
  }
  SECTION ("ComputerControlledOpponent timer")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    gameOption.computerControlledPlayerCount = 1;
    gameOption.timerOption.timeAtStartInSeconds = 1;
    gameOption.timerOption.timerType = shared_class::TimerType::addTimeOnNewRound;
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    auto leaveCalled = false;
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto logic = [&leaveCalled] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> >) {
      if (msg == "LeaveGameSuccess|{}")
        {
          leaveCalled = true;
          _ioContext.stop ();
        }
    };
    co_spawn (ioContext, connectWebsocket (logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"),my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (leaveCalled);
  }
  SECTION ("ComputerControlledOpponent next move")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint { boost::asio::ip::make_address("127.0.0.1"), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    auto startGame = matchmaking_game::StartGame {};
    // clang-format off
    startGame.players={"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57"};
    // clang-format on
    auto gameOption = shared_class::GameOption {};
    gameOption.gameOption.numberOfCardsPlayerShouldHave = 2;
    gameOption.gameOption.customCardDeck = std::vector<durak::Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } };
    gameOption.computerControlledPlayerCount = 1;
    auto ss = std::stringstream {};
    ss << confu_json::to_json (gameOption);
    startGame.gameOptionAsString.gameOptionAsString = ss.str ();
    auto sendMessageBeforeStartRead = std::vector<std::string> { objectToStringWithObjectName (startGame) };
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    ioContext.reset ();
    auto cardBeaten = false;
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), my_web_socket::printException);
    auto logic = [&cardBeaten] (boost::asio::io_context &_ioContext, std::string const &msg, std::shared_ptr<my_web_socket::MyWebSocket<my_web_socket::WebSocket> > const &myWebSocket) {
      if (msg.starts_with ("GameData"))
        {
          std::vector<std::string> splitMessage {};
          boost::algorithm::split (splitMessage, msg, boost::is_any_of ("|"));
          if (splitMessage.size () == 2)
            {
              auto const &objectAsString = splitMessage.at (1);
              auto gameData = stringToObject<durak::GameData> (objectAsString);
              if (not gameData.table.empty () and gameData.table.at (0).second.has_value ())
                {
                  cardBeaten = true;
                  _ioContext.stop ();
                }
              else
                {
                  myWebSocket->queueMessage (objectToStringWithObjectName (shared_class::DurakAttack { { { 3, durak::Type::clubs } } }));
                }
            }
        }
    };
    co_spawn (ioContext, connectWebsocket (logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"),my_web_socket::printException);
    ioContext.run_for (std::chrono::seconds { 5 });
    REQUIRE (cardBeaten);
  }
  ioContext.stop ();
  ioContext.reset ();
}