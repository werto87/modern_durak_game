#include "example_of_a_game_server/game/logic/game.hxx"
#include "example_of_a_game_server/database.hxx"
#include "example_of_a_game_server/server/server.hxx"
#include <catch2/catch.hpp>
#include <durak/card.hxx>
#include <durak/game.hxx>
#include <durak/gameOption.hxx>
#include <durak_computer_controlled_opponent/solve.hxx>

std::tuple<std::vector<std::tuple<uint8_t, durak::Card> >, std::vector<std::tuple<uint8_t, durak::Card> > > calcCompressedCardsForAttackAndDefend (durak::Game const &game);
shared_class::DurakNextMoveSuccess calcNextMove (std::optional<durak_computer_controlled_opponent::Action> const &action, std::vector<shared_class::Move> const &moves, durak::PlayerRole const &playerRole, std::vector<std::tuple<uint8_t, durak::Card> > const &defendIdCardMapping, std::vector<std::tuple<uint8_t, durak::Card> > const &attackIdCardMapping);
std::optional<durak_computer_controlled_opponent::Action> nextActionForRole (std::vector<std::tuple<uint8_t, durak_computer_controlled_opponent::Result> > const &nextActions, durak::PlayerRole const &playerRole);

TEST_CASE ("nextActionForRole", "[game]")
{
  SECTION ("nextActionForRole")
  {
    auto playerRole = durak::PlayerRole::attack;
    soci::session sql (soci::sqlite3, database::databaseName);
    auto someRound = confu_soci::findStruct<database::Round> (sql, "gameState", "0,1;5,9;1");
    auto test = someRound.value ();
    auto moveResult = durak_computer_controlled_opponent::binaryToMoveResult (someRound.value ().combination);
    auto test123 = moveResult.size ();
    std::for_each (moveResult.begin (), moveResult.end (), [] (auto test) {
      //
      auto test42 = test;
    });
    auto nextActions = durak_computer_controlled_opponent::nextActions ({ 0 }, moveResult);

    auto nextAction = nextActionForRole (nextActions, playerRole);
    //    REQUIRE (durakNextMoveSuccess.nextMove == shared_class::DurakNextMoveSuccess{}.nextMove);
  }
}

TEST_CASE ("calcCompressedCardsForAttackAndDefend", "[game]")
{
  SECTION ("calcCompressedCardsForAttackAndDefend 2 v 2 cards")
  {

    auto gameOption = durak::GameOption{};
    gameOption.cardsInHands = std::vector<std::vector<durak::Card> >{ std::vector<durak::Card>{ durak::Card{ 1, durak::Type::clubs }, durak::Card{ 2, durak::Type::clubs } }, std::vector<durak::Card>{ durak::Card{ 3, durak::Type::clubs }, durak::Card{ 4, durak::Type::clubs } } };
    auto result = calcCompressedCardsForAttackAndDefend (durak::Game{ std::vector<std::string>{ "player1", "player2" }, gameOption });
    auto playerOne = std::get<0> (result);
    auto playerTwo = std::get<1> (result);
    REQUIRE (playerOne.size () == 2);
    REQUIRE (playerTwo.size () == 2);
  }
}

TEST_CASE ("calcNextMove", "[game]")
{
  SECTION ("calcNextMove")
  {
    auto action = durak_computer_controlled_opponent::Action{ 0 };
    auto moves = std::vector<shared_class::Move>{ shared_class::Move{ shared_class::Move::AddCards } };
    auto playerRole = durak::PlayerRole::attack;
    auto defendIdCardMapping = std::vector<std::tuple<uint8_t, durak::Card> >{ { 5, { 7, durak::Type::clubs } }, { 9, { 8, durak::Type::clubs } } };
    auto attackIdCardMapping = std::vector<std::tuple<uint8_t, durak::Card> >{ { 0, { 3, durak::Type::hearts } }, { 1, { 3, durak::Type::clubs } } };
    auto durakNextMoveSuccess = calcNextMove (action, moves, playerRole, defendIdCardMapping, attackIdCardMapping);
    REQUIRE (durakNextMoveSuccess.nextMove == shared_class::DurakNextMoveSuccess{}.nextMove);
  }
}