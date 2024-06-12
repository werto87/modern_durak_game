#include "gameToCreate.hxx"
std::optional<std::string>
GameToCreate::tryToAddUser (User &&user)
{
  if (std::ranges::find (startGame.players, user.accountName, [] (std::string const &accountName) { return accountName; }) != startGame.players.end ())
    {
      if (std::ranges::find (users, user.accountName, [] (User const &userToCheck) { return userToCheck.accountName; }) == users.end ())
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
GameToCreate::allUsersConnected () const
{
  return startGame.players.size () == users.size ();
}
