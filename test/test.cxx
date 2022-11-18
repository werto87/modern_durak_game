#include "example_of_a_game_server/serialization.hxx"
#include "example_of_a_game_server/server/server.hxx"
#include "example_of_a_game_server/util.hxx"
#include "mockserver.hxx"
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
#include <sstream>
#include <stdexcept>

auto const DEFAULT_DATABASE_PATH = std::string{ CURRENT_BINARY_DIR } + "/test_database/combination.db";

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
  auto gameToMatchmaking = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), boost::numeric_cast<unsigned short> (std::stoul (DEFAULT_PORT_GAME_TO_MATCHMAKING)) };
  // clang-format off
  auto matchmakingGame = Mockserver{ gameToMatchmaking, { .requestStartsWithResponse = { { R"foo(GameOver)foo", R"foo(GameOverSuccess|{})foo" } } }, "MOCK_matchmaking_game", fmt::fg (fmt::color::violet), "0" };
  // clang-format on
  co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
  auto gameName = std::string{};
  auto handleMsgFromGame = [&gameName] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> >) {
    if (msg.starts_with ("StartGameSuccess"))
      {
        std::vector<std::string> splitMessage{};
        boost::algorithm::split (splitMessage, msg, boost::is_any_of ("|"));
        if (splitMessage.size () == 2)
          {
            auto const &objectAsString = splitMessage.at (1);
            gameName = stringToObject<matchmaking_game::StartGameSuccess> (objectAsString).gameName;
            ioContext.stop ();
          }
      }
  };

  SECTION ("DurakLeaveGame")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"DurakLeaveGame81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
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
    co_spawn (ioContext, connectWebsocket (user2Logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakLeaveGame81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (leaveGameSuccessCalledOnUser2);
  }
  SECTION ("DurakAttackPass")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"DurakAttackPass81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
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
      co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakAttackPass81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
      ioContext.run_for (std::chrono::seconds{ 5 });
      REQUIRE (durakAttackPassError);
  }
  SECTION ("DurakAssistPass")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"DurakAssistPass81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
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
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakAssistPass81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (durakAssistPassError);
  }
  SECTION ("DurakDefendPass")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"DurakDefendPass81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
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
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakDefendPass81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (durakDefendPassError);
  }
  SECTION ("DurakDefend")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"DurakDefend81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
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
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakDefend81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (durakDefendError);
  }
  SECTION ("DurakAttack")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"DurakAttack81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
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
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakAttack81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (durakAttackError);
  }
  SECTION ("DurakAskDefendWantToTakeCardsAnswer")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"DurakAskDefendWantToTakeCardsAnswer81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
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
      co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"DurakAskDefendWantToTakeCardsAnswer81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
      ioContext.run_for (std::chrono::seconds{ 5 });
      REQUIRE (unhandledEventError);
  }
  SECTION ("NextMove")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"NextMove81b0117d-973b-469b-ac39-3bd49c23ef57","669454d5-b39b-44d6-b417-4740d6566ca8"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    durak_computer_controlled_opponent::database::deleteDatabaseAndCreateNewDatabase (DEFAULT_DATABASE_PATH);
    durak_computer_controlled_opponent::database::createTables (DEFAULT_DATABASE_PATH);
    auto gameLookup = std::map<std::tuple<uint8_t, uint8_t>, std::array<std::map<std::tuple<std::vector<uint8_t>, std::vector<uint8_t> >, std::vector<std::tuple<uint8_t, durak_computer_controlled_opponent::Result> > >, 4> >{};
    gameLookup.insert ({ { 1, 1 }, solveDurak (36, 1, 1, gameLookup) });
    gameLookup.insert ({ { 2, 2 }, solveDurak (36, 2, 2, gameLookup) });
    durak_computer_controlled_opponent::database::insertGameLookUp (DEFAULT_DATABASE_PATH, gameLookup);
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto unhandledEventError = false;
    auto someMsg = [&unhandledEventError] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg.starts_with ("GameData"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakNextMove{}));
        }
      if (msg.starts_with ("DurakNextMoveSuccess"))
        {
          unhandledEventError = true;
          ioContext.stop ();
        }
    };
    auto endpointUserViaMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
    co_spawn (ioContext, connectWebsocket (someMsg, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"NextMove81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    co_spawn (ioContext, connectWebsocket ([] (auto&&,auto&&,auto&&) {}, ioContext, endpointUserViaMatchmakingGame, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"669454d5-b39b-44d6-b417-4740d6566ca8","gameName":")foo" +gameName +R"foo("})foo"}}), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (unhandledEventError);
  }
  SECTION ("ComputerControlledOpponent")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    startGame.gameOption.computerControlledPlayerCount=1;
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    auto playerCount = size_t{};
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto logic = [&playerCount] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> >) {
      if (msg.starts_with ("GameData"))
        {
          std::vector<std::string> splitMessage{};
          boost::algorithm::split (splitMessage, msg, boost::is_any_of ("|"));
          if (splitMessage.size () == 2)
            {
              auto const &objectAsString = splitMessage.at (1);
              auto gameData = stringToObject<durak::GameData> (objectAsString);
              playerCount = gameData.players.size ();
              ioContext.stop ();
            }
        }
    };
    co_spawn (ioContext, connectWebsocket (logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (playerCount == 2);
  }
  SECTION ("ComputerControlledOpponent timer")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    startGame.gameOption.computerControlledPlayerCount=1;
    startGame.gameOption.timerOption.timeAtStartInSeconds=1;
    startGame.gameOption.timerOption.timerType=shared_class::TimerType::addTimeOnNewRound;
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    auto leaveCalled = false;
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto logic = [&leaveCalled] (boost::asio::io_context &ioContext, std::string const &msg, std::shared_ptr<MyWebsocket<Websocket> > myWebsocket) {
      if (msg == "LeaveGameSuccess|{}")
        {
          leaveCalled = true;
          ioContext.stop ();
        }
    };
    co_spawn (ioContext, connectWebsocket (logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    REQUIRE (leaveCalled);
  }
  SECTION ("ComputerControlledOpponent next move")
  {
    auto endpointMatchmakingGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
    // clang-format off
    auto startGame=matchmaking_game::StartGame{};
    startGame.players={"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57"};
    startGame.gameOption.gameOption.numberOfCardsPlayerShouldHave=2;
    startGame.gameOption.gameOption.customCardDeck=std::vector<durak::Card>{{7,durak::Type::clubs},{8,durak::Type::clubs},{3,durak::Type::hearts},{3,durak::Type::clubs}};
    startGame.gameOption.computerControlledPlayerCount=1;
    auto sendMessageBeforeStartRead = std::vector<std::string>{objectToStringWithObjectName(startGame)};
    // clang-format on
    co_spawn (ioContext, connectWebsocket (handleMsgFromGame, ioContext, endpointMatchmakingGame, sendMessageBeforeStartRead, "start_game"), printException);
    ioContext.run_for (std::chrono::seconds{ 5 });
    ioContext.reset ();
    durak_computer_controlled_opponent::database::deleteDatabaseAndCreateNewDatabase (DEFAULT_DATABASE_PATH);
    durak_computer_controlled_opponent::database::createTables (DEFAULT_DATABASE_PATH);
    auto gameLookup = std::map<std::tuple<uint8_t, uint8_t>, std::array<std::map<std::tuple<std::vector<uint8_t>, std::vector<uint8_t> >, std::vector<std::tuple<uint8_t, durak_computer_controlled_opponent::Result> > >, 4> >{};
    gameLookup.insert ({ { 1, 1 }, solveDurak (36, 1, 1, gameLookup) });
    gameLookup.insert ({ { 2, 2 }, solveDurak (36, 2, 2, gameLookup) });
    durak_computer_controlled_opponent::database::insertGameLookUp (DEFAULT_DATABASE_PATH, gameLookup);
    auto playerCount = size_t{};
    co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, DEFAULT_ADDRESS_OF_MATCHMAKING, DEFAULT_PORT_GAME_TO_MATCHMAKING, DEFAULT_DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
    auto logic = [&playerCount] (boost::asio::io_context &ioContext, std::string const &msg, const std::shared_ptr<MyWebsocket<Websocket> > &myWebsocket) {
      if (msg.starts_with ("GameData"))
        {
          myWebsocket->sendMessage (objectToStringWithObjectName (shared_class::DurakAttack{ { { 3, durak::Type::clubs } } }));
        }
    };
    co_spawn (ioContext, connectWebsocket (logic, ioContext, userToGameViaMatchmaking, std::vector<std::string>{{R"foo(ConnectToGame|{"accountName":"ComputerControlledOpponent81b0117d-973b-469b-ac39-3bd49c23ef57","gameName":")foo" +gameName +R"foo("})foo"}},"user2"), printException);
    ioContext.run_for (std::chrono::seconds{ 9999 });
    REQUIRE (playerCount == 2);
  }
  ioContext.stop ();
  ioContext.reset ();
}