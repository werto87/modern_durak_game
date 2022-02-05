#include "gameToCreate.hxx"
#include <range/v3/algorithm/find.hpp>
std::optional<std::string>
GameToCreate::tryToAddUser (User &&user)
{
  if (ranges::find (startGame.players, user.accountName, [] (std::string const &accountName) { return accountName; }) != startGame.players.end ())
    {
      if (ranges::find (users, user.accountName, [] (User const &userToCheck) { return userToCheck.accountName; }) == users.end ())
        {
          users.emplace_back (user);
          return {};
        }
      else
        {
          return { "Already in game" };
        }
    }
  else
    {
      return { "Not part of the game" };
    }
}

bool
GameToCreate::allUsersConnected ()
{
  return startGame.players.size () == users.size ();
}
