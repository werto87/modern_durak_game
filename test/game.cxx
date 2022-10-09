#include "example_of_a_game_server/game/logic/game.hxx"
#include "example_of_a_game_server/server/server.hxx"
#include <catch2/catch.hpp>
#include <durak/card.hxx>
#include <durak/game.hxx>
#include <durak/gameOption.hxx>
#include <durak_computer_controlled_opponent/solve.hxx>

std::tuple<std::vector<std::tuple<uint8_t, durak::Card> >, std::vector<std::tuple<uint8_t, durak::Card> > > calcCompressedCardsForAttackAndDefend (durak::Game const &game);
shared_class::DurakNextMoveSuccess calcNextMove (std::optional<durak_computer_controlled_opponent::Action> const &action, std::vector<shared_class::Move> const &moves, durak::PlayerRole const &playerRole, std::vector<std::tuple<uint8_t, durak::Card> > const &defendIdCardMapping, std::vector<std::tuple<uint8_t, durak::Card> > const &attackIdCardMapping);
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
    // TODO TEST THIS
    //    calcNextMove (std::optional<durak_computer_controlled_opponent::Action> const &action, std::vector<shared_class::Move> const &moves, durak::PlayerRole const &playerRole, std::vector<std::tuple<uint8_t, durak::Card> > const &defendIdCardMapping, std::vector<std::tuple<uint8_t, durak::Card> > const &attackIdCardMapping)calcNextMove (std::optional<durak_computer_controlled_opponent::Action> const &action, std::vector<shared_class::Move> const &moves, durak::PlayerRole const &playerRole, std::vector<std::tuple<uint8_t, durak::Card> > const &defendIdCardMapping, std::vector<std::tuple<uint8_t, durak::Card> > const &attackIdCardMapping)calcNextMove (std::optional<durak_computer_controlled_opponent::Action> const &action, std::vector<shared_class::Move> const &moves, durak::PlayerRole const &playerRole, std::vector<std::tuple<uint8_t, durak::Card> > const &defendIdCardMapping, std::vector<std::tuple<uint8_t, durak::Card> > const &attackIdCardMapping);
  }
}