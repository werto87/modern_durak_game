#include "modern_durak_game/game/logic/game.hxx"
#include "constant.hxx"
#include "durak_computer_controlled_opponent/database.hxx"
#include "modern_durak_game/server/server.hxx"
#include "modern_durak_game/util/util.hxx"
#include <catch2/catch.hpp>
#include <confu_json/to_json.hxx>
#include <durak/card.hxx>
#include <durak/game.hxx>
#include <durak/gameOption.hxx>
#include <durak_computer_controlled_opponent/compressCard.hxx>
#include <durak_computer_controlled_opponent/permutation.hxx>
#include <durak_computer_controlled_opponent/solve.hxx>
#include <durak_computer_controlled_opponent/util.hxx>
#include <modern_durak_game_option/userDefinedGameOption.hxx>

struct PassAttackAndAssist
{
  bool attack {};
  bool assist {};
};
std::vector<shared_class::Move> calculateAllowedMovesWithPassState (durak::Game const &game, durak::PlayerRole playerRole, PassAttackAndAssist passAttackAndAssist);

struct Chill
{
};
struct AskDef
{
};
struct AskAttackAndAssist
{
};
std::optional<shared_class::DurakNextMoveSuccess> calcNextMove (std::optional<durak_computer_controlled_opponent::Action> const &action, std::vector<shared_class::Move> const &moves, durak::PlayerRole const &playerRole, std::vector<std::tuple<uint8_t, durak::Card> > const &defendIdCardMapping, std::vector<std::tuple<uint8_t, durak::Card> > const &attackIdCardMapping, std::variant<AskAttackAndAssist, AskDef, Chill> const &currentState);
bool hasToMove (durak::Game const &game, durak::PlayerRole playerRole, PassAttackAndAssist passAttackAndAssist, std::variant<AskAttackAndAssist, AskDef, Chill> const &currentState);
void playNextMove (std::string const &id, std::string const &gameName, std::list<Game> &games, boost::asio::io_context &ioContext, std::string const &msg);
TEST_CASE ("hasToMove Chill no card played", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  SECTION ("attack moves") { REQUIRE (hasToMove (game, PlayerRole::attack, {}, Chill {})); }
  SECTION ("defend moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::defend, {}, Chill {})); }
}

TEST_CASE ("hasToMove Chill card played", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  SECTION ("attack moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::attack, {}, Chill {})); }
  SECTION ("defend moves") { REQUIRE (hasToMove (game, PlayerRole::defend, {}, Chill {})); }
}

TEST_CASE ("hasToMove Chill attack,defend", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  game.playerDefends ({ 3, durak::Type::clubs }, { 7, durak::Type::clubs });
  SECTION ("attack moves") { REQUIRE (hasToMove (game, PlayerRole::attack, {}, Chill {})); }
  SECTION ("defend moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::defend, {}, Chill {})); }
}

TEST_CASE ("hasToMove Chill attack,defend,pass", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  game.playerDefends ({ 3, durak::Type::clubs }, { 7, durak::Type::clubs });
  SECTION ("attack moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::attack, { true, true }, Chill {})); }
  SECTION ("defend moves") { REQUIRE (hasToMove (game, PlayerRole::defend, { true, true }, Chill {})); }
}

TEST_CASE ("hasToMove AskAttackAndAssist attack,defend", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  SECTION ("attack moves") { REQUIRE (hasToMove (game, PlayerRole::attack, {}, AskAttackAndAssist {})); }
  SECTION ("defend moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::defend, {}, AskAttackAndAssist {})); }
}

TEST_CASE ("hasToMove AskDef attack,defend", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  game.playerDefends ({ 3, durak::Type::clubs }, { 7, durak::Type::clubs });
  SECTION ("attack moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::attack, { true, true }, AskDef {})); }
  SECTION ("defend moves") { REQUIRE (hasToMove (game, PlayerRole::defend, { true, true }, AskDef {})); }
}

TEST_CASE ("calcNextMove fresh round", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForAttack, std::back_inserter (attackCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto defendCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForDefend, std::back_inserter (defendCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, small_memory_tree::SmallMemoryTree (binaryToSmallMemoryTreeData (someRound.value ())));
  SECTION ("attack moves")
  {
    auto playerRole = durak::PlayerRole::attack;
    auto passAttackAndAssist = PassAttackAndAssist {};
    if (hasToMove (game, playerRole, passAttackAndAssist, Chill {}))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack, Chill {});
        REQUIRE (calculatedNextMove.has_value ());
      }
    else
      {
        REQUIRE (false);
      }
  }
}

TEST_CASE ("calcNextMove first card played", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForAttack, std::back_inserter (attackCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto defendCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForDefend, std::back_inserter (defendCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, small_memory_tree::SmallMemoryTree (binaryToSmallMemoryTreeData (someRound.value ())));
  SECTION ("defend moves")
  {
    auto playerRole = durak::PlayerRole::defend;
    auto passAttackAndAssist = PassAttackAndAssist {};
    if (hasToMove (game, playerRole, passAttackAndAssist, Chill {}))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack, Chill {});
        REQUIRE (calculatedNextMove.has_value ());
      }
    else
      {
        REQUIRE (false);
      }
  }
}

TEST_CASE ("calcNextMove first card played and defended", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 4, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  game.playerDefends ({ 3, durak::Type::clubs }, { 7, durak::Type::clubs });
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForAttack, std::back_inserter (attackCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto defendCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForDefend, std::back_inserter (defendCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, small_memory_tree::SmallMemoryTree (binaryToSmallMemoryTreeData (someRound.value ())));
  SECTION ("attack moves pass")
  {
    auto playerRole = durak::PlayerRole::attack;
    auto passAttackAndAssist = PassAttackAndAssist {};
    if (hasToMove (game, playerRole, passAttackAndAssist, Chill {}))
      {

        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack, Chill {});
        REQUIRE (calculatedNextMove.has_value ());
      }
    else
      {
        REQUIRE (false);
      }
  }
}

TEST_CASE ("calcNextMove first card, defended, attack pass", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  game.playerDefends ({ 3, durak::Type::clubs }, { 7, durak::Type::clubs });
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForAttack, std::back_inserter (attackCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto defendCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForDefend, std::back_inserter (defendCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, small_memory_tree::SmallMemoryTree (binaryToSmallMemoryTreeData (someRound.value ())));
  SECTION ("defend moves")
  {
    auto playerRole = durak::PlayerRole::defend;
    auto passAttackAndAssist = PassAttackAndAssist { true, true };
    if (hasToMove (game, playerRole, passAttackAndAssist, Chill {}))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack, Chill {});
        REQUIRE (calculatedNextMove.has_value ());
      }
    else
      {
        REQUIRE (false);
      }
  }
}

TEST_CASE ("calcNextMove first card, defended has to take cards", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 2, .trump = Type::hearts, .customCardDeck = std::vector<Card> { { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::hearts } });
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForAttack, std::back_inserter (attackCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto defendCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForDefend, std::back_inserter (defendCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, small_memory_tree::SmallMemoryTree (binaryToSmallMemoryTreeData (someRound.value ())));
  SECTION ("defend moves")
  {
    auto playerRole = durak::PlayerRole::defend;
    auto passAttackAndAssist = PassAttackAndAssist {};
    if (hasToMove (game, playerRole, passAttackAndAssist, Chill {}))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack, Chill {});
        REQUIRE (calculatedNextMove.has_value ());
      }
    else
      {
        REQUIRE (false);
      }
  }
}

TEST_CASE ("calcNextMove first card, defended, second card", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game { { "a", "b" }, GameOption { .numberOfCardsPlayerShouldHave = 3, .trump = Type::hearts, .customCardDeck = std::vector<Card> { { 7, durak::Type::spades }, { 2, durak::Type::diamonds }, { 5, durak::Type::hearts }, { 5, durak::Type::clubs }, { 9, durak::Type::hearts }, { 4, durak::Type::hearts } } } };
  game.playerStartsAttack ({ { 4, durak::Type::hearts } });
  game.playerDefends ({ 4, durak::Type::hearts }, { 5, durak::Type::hearts });
  game.playerStartsAttack ({ { 5, durak::Type::clubs } });
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForAttack, std::back_inserter (attackCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto defendCardsCompressed = std::vector<uint8_t> {};
  std::ranges::transform (compressedCardsForDefend, std::back_inserter (defendCardsCompressed), [] (auto const &idAndCard) { return std::get<0> (idAndCard); });
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, small_memory_tree::SmallMemoryTree (binaryToSmallMemoryTreeData (someRound.value ())));
  SECTION ("defend moves")
  {
    auto playerRole = durak::PlayerRole::defend;
    auto passAttackAndAssist = PassAttackAndAssist {};
    REQUIRE (hasToMove (game, playerRole, passAttackAndAssist, Chill {}));
    auto actionForRole = nextActionForRole (result, playerRole);
    auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
    auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack, Chill {});
    REQUIRE (calculatedNextMove.has_value ());
  }
}

std::vector<std::vector<std::vector<durak::Card> > >
attackDefendCards (uint64_t attackCardCount, uint64_t defendCardCount)
{
  auto result = std::vector<std::vector<std::vector<durak::Card> > > {};
  using namespace durak;
  auto combinations = durak_computer_controlled_opponent::compressed_permutations ({ attackCardCount, defendCardCount }, 20);
  std::cout << "combinations.size (): " << combinations.size () << std::endl;
  for (auto const &combi : combinations)
    {
      auto cards = durak_computer_controlled_opponent::idsToCards (combi);
      auto attackCards = std::vector<Card> (cards.begin (), cards.begin () + static_cast<long> (attackCardCount));
      auto defendCards = std::vector<Card> (cards.begin () + static_cast<long> (attackCardCount), cards.end ());
      result.push_back (std::vector<std::vector<Card> > { attackCards, defendCards });
    }
  return result;
}

TEST_CASE ("play the game", "[game]")
{
  using namespace durak_computer_controlled_opponent;
  auto cardsInHands = std::vector<std::vector<durak::Card> > {};
  auto playerOneCards = std::vector<durak::Card> { { 1, durak::Type::clubs }, { 4, durak::Type::clubs }, { 5, durak::Type::clubs } };
  auto playerTwoCards = std::vector<durak::Card> { { 2, durak::Type::clubs }, { 2, durak::Type::hearts }, { 1, durak::Type::hearts } };
  cardsInHands.push_back (playerOneCards);
  cardsInHands.push_back (playerTwoCards);
  auto trumpType = durak::Type::spades;
  matchmaking_game::StartGame startGame {};
  auto durakGameOption = durak::GameOption { .numberOfCardsPlayerShouldHave = 3, .trump = trumpType, .customCardDeck = std::vector<durak::Card> {}, .cardsInHands = cardsInHands };
  auto gameOption = shared_class::GameOption {};
  gameOption.gameOption = durakGameOption;
  gameOption.opponentCards = shared_class::OpponentCards::showOpponentCards;
  auto ss = std::stringstream {};
  ss << confu_json::to_json (gameOption);
  startGame.gameOptionAsString.gameOptionAsString = ss.str ();
  std::string gameName { "gameName" };
  std::list<User> users {};
  boost::asio::io_context ioContext {};
  boost::asio::ip::tcp::endpoint gameToMatchmakingEndpoint_ {};
  auto computerControlledPlayerNames = std::vector<std::string> { "a", "b" };
  std::list<Game> games {};
  auto gameOver = false;
  std::ranges::for_each (computerControlledPlayerNames, [&gameOver, gameName, &games = games, &users, &ioContext] (auto const &id) {
    users.push_back ({ id,
                       [&gameOver, id, gameName, &games = games, &ioContext] (auto const &msg) {
                         if (boost::starts_with (msg, "DurakGameOverWon") or boost::starts_with (msg, "DurakGameOverDraw"))
                           {
                             gameOver = true;
                             ioContext.stop ();
                           }
                         // clang-format off
                         else if (boost::starts_with (msg, R"(DurakNextMoveError|{"error":"Unsupported card combination."})"))
                           // clang-format on
                           {
                             gameOver = true;
                             ioContext.stop ();
                           }
                         else
                           {
                             playNextMove (id, gameName, games, ioContext, msg);
                           }
                       },
                       std::make_shared<boost::asio::system_timer> (ioContext) });
  });
  auto &game = games.emplace_back (Game { startGame, gameName, std::move (users), ioContext, gameToMatchmakingEndpoint_, DEFAULT_DATABASE_PATH });
  game.startGame ();
  // ioContext.run_for (std::chrono::seconds { 5 });
  ioContext.run ();
  REQUIRE (gameOver);
}