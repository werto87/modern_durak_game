#include "src/util.hxx"
#include "src/serialization.hxx"
#include <durak/game.hxx>
#include <durak/gameData.hxx>
#include <map>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/transform.hpp>
#include <range/v3/algorithm/unique.hpp>
#include <range/v3/algorithm/unique_copy.hpp>
#include <range/v3/all.hpp>
#include <range/v3/iterator/insert_iterators.hpp>
durak::GameData
filterGameDataByAccountName (durak::GameData const &gameData, std::string const &accountName)
{
  auto filteredGameData = gameData;
  for (auto &player : filteredGameData.players | ranges::views::filter ([&accountName] (auto const &player) { return player.name != accountName; }))
    {
      ranges::transform (player.cards, player.cards.begin (), [] (boost::optional<durak::Card> const &) { return boost::optional<durak::Card>{}; });
    }
  return filteredGameData;
}

auto const &moveMapping = std::map<durak::Move, shared_class::Move>{ { durak::Move::startAttack, shared_class::Move::AddCards }, { durak::Move::addCard, shared_class::Move::AddCards }, { durak::Move::pass, shared_class::Move::AttackAssistPass }, { durak::Move::defend, shared_class::Move::Defend }, { durak::Move::takeCards, shared_class::Move::TakeCards } };

std::vector<shared_class::Move>
calculateAllowedMoves (durak::Game const &game, durak::PlayerRole playerRole)
{
  auto result = std::vector<shared_class::Move>{};
  auto durakAllowedMoves = game.getAllowedMoves (playerRole);
  ranges::transform (durakAllowedMoves, ranges::back_inserter (result), [] (auto move) { return moveMapping.at (move); });
  return result;
}

shared_class::DurakAllowedMoves
allowedMoves (durak::Game const &game, durak::PlayerRole playerRole, std::optional<std::vector<shared_class::Move> > const &removeFromAllowedMoves, std::optional<std::vector<shared_class::Move> > const &addToAllowedMoves)
{
  auto allowedMoves = shared_class::DurakAllowedMoves{ removeFromAllowedMoves.value_or (calculateAllowedMoves (game, playerRole)) };
  if (addToAllowedMoves && not addToAllowedMoves->empty ())
    {
      allowedMoves.allowedMoves.insert (allowedMoves.allowedMoves.end (), addToAllowedMoves.value ().begin (), addToAllowedMoves.value ().end ());
      ranges::sort (allowedMoves.allowedMoves);
      auto result = shared_class::DurakAllowedMoves{};
      ranges::unique_copy (allowedMoves.allowedMoves, ranges::back_inserter (result.allowedMoves));
      return result;
    }
  else
    {
      return allowedMoves;
    }
}

void
sendAvailableMoves (durak::Game const &game, std::vector<User> const &users, AllowedMoves const &removeFromAllowedMoves, AllowedMoves const &addToAllowedMoves)
{
  if (auto attackingPlayer = game.getAttackingPlayer ())
    {
      if (auto attackingUser = ranges::find_if (users, [attackingPlayerName = attackingPlayer->id] (User const &user) { return user.accountName == attackingPlayerName; }); attackingUser != users.end ())
        {
          attackingUser->sendMsgToUser (objectToStringWithObjectName (allowedMoves (game, durak::PlayerRole::attack, removeFromAllowedMoves.attack, addToAllowedMoves.attack)));
        }
    }
  if (auto assistingPlayer = game.getAssistingPlayer ())
    {
      if (auto assistingUser = ranges::find_if (users, [assistingPlayerName = assistingPlayer->id] (User const &user) { return user.accountName == assistingPlayerName; }); assistingUser != users.end ())
        {
          assistingUser->sendMsgToUser (objectToStringWithObjectName (allowedMoves (game, durak::PlayerRole::assistAttacker, removeFromAllowedMoves.assist, addToAllowedMoves.assist)));
        }
    }
  if (auto defendingPlayer = game.getDefendingPlayer ())
    {
      if (auto defendingUser = ranges::find_if (users, [defendingPlayerName = defendingPlayer->id] (User const &user) { return user.accountName == defendingPlayerName; }); defendingUser != users.end ())
        {
          defendingUser->sendMsgToUser (objectToStringWithObjectName (allowedMoves (game, durak::PlayerRole::defend, removeFromAllowedMoves.defend, addToAllowedMoves.defend)));
        }
    }
}

void
sendGameDataToAccountsInGame (durak::Game const &game, std::vector<User> const &users)
{
  auto gameData = game.getGameData ();
  ranges::for_each (gameData.players, [] (auto &player) { ranges::sort (player.cards, [] (auto const &card1, auto const &card2) { return card1.value () < card2.value (); }); });
  ranges::for_each (users, [&gameData] (User const &user) { user.sendMsgToUser (objectToStringWithObjectName (filterGameDataByAccountName (gameData, user.accountName))); });
}

#ifdef LOGGING_CO_SPAWN_PRINT_EXCEPTIONS
void
printExceptionHelper (std::exception_ptr eptr)
{
  try
    {
      if (eptr)
        {
          std::rethrow_exception (eptr);
        }
    }
  catch (std::exception const &e)
    {
      std::cout << "unhandled exception: '" << e.what () << "'" << std::endl;
    }
}
#else
void printExceptionHelper (std::exception_ptr) {}
#endif