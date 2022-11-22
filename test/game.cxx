#include "example_of_a_game_server/game/logic/game.hxx"
#include "constant.hxx"
#include "durak_computer_controlled_opponent/database.hxx"
#include "example_of_a_game_server/server/server.hxx"
#include <catch2/catch.hpp>
#include <durak/card.hxx>
#include <durak/game.hxx>
#include <durak/gameOption.hxx>
#include <durak_computer_controlled_opponent/solve.hxx>
#include <durak_computer_controlled_opponent/util.hxx>
std::optional<shared_class::DurakNextMoveSuccess> calcNextMove (std::optional<durak_computer_controlled_opponent::Action> const &action, std::vector<shared_class::Move> const &moves, durak::PlayerRole const &playerRole, std::vector<std::tuple<uint8_t, durak::Card> > const &defendIdCardMapping, std::vector<std::tuple<uint8_t, durak::Card> > const &attackIdCardMapping);
struct PassAttackAndAssist
{
  bool attack{};
  bool assist{};
};
std::vector<shared_class::Move> calculateAllowedMovesWithPassState (durak::Game const &game, durak::PlayerRole playerRole, PassAttackAndAssist passAttackAndAssist);
bool hasToMove (durak::Game const &game, durak::PlayerRole playerRole, PassAttackAndAssist passAttackAndAssist);

TEST_CASE ("hasToMove no card played", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game{ { "a", "b" }, GameOption{ .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card>{ { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  SECTION ("attack moves") { REQUIRE (hasToMove (game, PlayerRole::attack, {})); }
  SECTION ("defend moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::defend, {})); }
}

TEST_CASE ("hasToMove card played", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game{ { "a", "b" }, GameOption{ .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card>{ { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  SECTION ("attack moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::attack, {})); }
  SECTION ("defend moves") { REQUIRE (hasToMove (game, PlayerRole::defend, {})); }
}

TEST_CASE ("hasToMove attack,defend", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game{ { "a", "b" }, GameOption{ .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card>{ { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  game.playerDefends ({ 3, durak::Type::clubs }, { 7, durak::Type::clubs });
  SECTION ("attack moves") { REQUIRE (hasToMove (game, PlayerRole::attack, {})); }
  SECTION ("defend moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::defend, {})); }
}

TEST_CASE ("hasToMove attack,defend,pass", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  auto game = durak::Game{ { "a", "b" }, GameOption{ .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card>{ { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  game.playerDefends ({ 3, durak::Type::clubs }, { 7, durak::Type::clubs });
  SECTION ("attack moves") { REQUIRE_FALSE (hasToMove (game, PlayerRole::attack, { true, true })); }
  SECTION ("defend moves") { REQUIRE (hasToMove (game, PlayerRole::defend, { true, true })); }
}

TEST_CASE ("calcNextMove fresh round", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game{ { "a", "b" }, GameOption{ .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card>{ { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForAttack >>= pipes::unzip (pipes::push_back (attackCardsCompressed), pipes::dev_null ());
  auto defendCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForDefend >>= pipes::unzip (pipes::push_back (defendCardsCompressed), pipes::dev_null ());
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, binaryToMoveResult (someRound.value ().combination));
  SECTION ("attack moves")
  {
    auto playerRole = durak::PlayerRole::attack;
    auto passAttackAndAssist = PassAttackAndAssist{};
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        REQUIRE (calculatedNextMove.has_value ());
      }
  }
  SECTION ("defend moves")
  {
    auto playerRole = durak::PlayerRole::defend;
    auto passAttackAndAssist = PassAttackAndAssist{};
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {

        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        REQUIRE_FALSE (calculatedNextMove.has_value ());
      }
  }
}

TEST_CASE ("calcNextMove first card played", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game{ { "a", "b" }, GameOption{ .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card>{ { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForAttack >>= pipes::unzip (pipes::push_back (attackCardsCompressed), pipes::dev_null ());
  auto defendCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForDefend >>= pipes::unzip (pipes::push_back (defendCardsCompressed), pipes::dev_null ());
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, binaryToMoveResult (someRound.value ().combination));
  SECTION ("attack moves")
  {
    auto playerRole = durak::PlayerRole::attack;
    auto passAttackAndAssist = PassAttackAndAssist{};
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {

        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        REQUIRE_FALSE (calculatedNextMove.has_value ());
      }
  }
  SECTION ("defend moves")
  {
    auto playerRole = durak::PlayerRole::defend;
    auto passAttackAndAssist = PassAttackAndAssist{};
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        REQUIRE (calculatedNextMove.has_value ());
      }
  }
}

TEST_CASE ("calcNextMove first card played and defended", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game{ { "a", "b" }, GameOption{ .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card>{ { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 4, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  game.playerDefends ({ 3, durak::Type::clubs }, { 7, durak::Type::clubs });
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForAttack >>= pipes::unzip (pipes::push_back (attackCardsCompressed), pipes::dev_null ());
  auto defendCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForDefend >>= pipes::unzip (pipes::push_back (defendCardsCompressed), pipes::dev_null ());
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, binaryToMoveResult (someRound.value ().combination));
  SECTION ("attack moves pass")
  {
    auto playerRole = durak::PlayerRole::attack;
    auto passAttackAndAssist = PassAttackAndAssist{};
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {

        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        REQUIRE (calculatedNextMove.has_value ());
      }
  }
  SECTION ("defend moves")
  {
    auto playerRole = durak::PlayerRole::defend;
    auto passAttackAndAssist = PassAttackAndAssist{};
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {

        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        REQUIRE_FALSE (calculatedNextMove.has_value ());
      }
  }
}

TEST_CASE ("calcNextMove first card, defended, attack pass", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game{ { "a", "b" }, GameOption{ .numberOfCardsPlayerShouldHave = 2, .customCardDeck = std::vector<Card>{ { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::clubs } });
  game.playerDefends ({ 3, durak::Type::clubs }, { 7, durak::Type::clubs });

  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForAttack >>= pipes::unzip (pipes::push_back (attackCardsCompressed), pipes::dev_null ());
  auto defendCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForDefend >>= pipes::unzip (pipes::push_back (defendCardsCompressed), pipes::dev_null ());
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, binaryToMoveResult (someRound.value ().combination));
  SECTION ("attack moves")
  {
    auto playerRole = durak::PlayerRole::attack;
    auto passAttackAndAssist = PassAttackAndAssist{ true, true };
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        REQUIRE_FALSE (calculatedNextMove.has_value ());
      }
  }
  SECTION ("defend moves")
  {
    auto playerRole = durak::PlayerRole::defend;
    auto passAttackAndAssist = PassAttackAndAssist{ true, true };
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        //    TODO should be true. move should be AnswerDefendWantsToTakeCardsYes or AnswerDefendWantsToTakeCardsNo
        REQUIRE (calculatedNextMove.has_value ());
      }
  }
}

TEST_CASE ("calcNextMove first card, defended has to take cards", "[game]")
{
  using namespace durak;
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, DEFAULT_DATABASE_PATH);
  auto game = durak::Game{ { "a", "b" }, GameOption{ .numberOfCardsPlayerShouldHave = 2, .trump = Type::hearts, .customCardDeck = std::vector<Card>{ { 7, durak::Type::clubs }, { 8, durak::Type::clubs }, { 3, durak::Type::hearts }, { 3, durak::Type::clubs } } } };
  game.playerStartsAttack ({ { 3, durak::Type::hearts } });
  auto const [compressedCardsForAttack, compressedCardsForDefend, compressedCardsForAssist] = calcIdAndCompressedCardsForAttackAndDefend (game);
  auto attackCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForAttack >>= pipes::unzip (pipes::push_back (attackCardsCompressed), pipes::dev_null ());
  auto defendCardsCompressed = std::vector<uint8_t>{};
  compressedCardsForDefend >>= pipes::unzip (pipes::push_back (defendCardsCompressed), pipes::dev_null ());
  auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", database::gameStateAsString ({ attackCardsCompressed, defendCardsCompressed }, game.getTrump ()));
  REQUIRE (someRound.has_value ());
  auto actions = durak_computer_controlled_opponent::historyEventsToActionsCompressedCards (game.getHistory (), calcCardsAndCompressedCardsForAttackAndDefend (game));
  auto result = nextActionsAndResults (actions, binaryToMoveResult (someRound.value ().combination));
  SECTION ("attack moves")
  {
    auto playerRole = durak::PlayerRole::attack;
    auto passAttackAndAssist = PassAttackAndAssist{};
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        REQUIRE_FALSE (calculatedNextMove.has_value ());
      }
  }
  SECTION ("defend moves")
  {
    auto playerRole = durak::PlayerRole::defend;
    auto passAttackAndAssist = PassAttackAndAssist{};
    if (hasToMove (game, playerRole, passAttackAndAssist))
      {
        auto actionForRole = nextActionForRole (result, playerRole);
        auto allowedMoves = calculateAllowedMovesWithPassState (game, playerRole, passAttackAndAssist);
        auto calculatedNextMove = calcNextMove (actionForRole, allowedMoves, playerRole, compressedCardsForDefend, compressedCardsForAttack);
        //    TODO should be true. TAKE CARDS
        REQUIRE (calculatedNextMove.has_value ());
      }
  }
}