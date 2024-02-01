#ifndef C8BDF7DD_992C_419F_968A_70034261F4D4
#define C8BDF7DD_992C_419F_968A_70034261F4D4

#include "myWebsocket.hxx"
#include "util/serialization.hxx"
#include "user.hxx"
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

struct GameToCreate
{
  GameToCreate (matchmaking_game::StartGame _startGame) : startGame{ std::move (_startGame) } {}

  // optional with error message
  std::optional<std::string> tryToAddUser (User &&user);
  bool allUsersConnected () const;

  std::string gameName{ boost::uuids::to_string (boost::uuids::random_generator () ()) };
  matchmaking_game::StartGame startGame{};
  std::list<User> users{};
};

#endif /* C8BDF7DD_992C_419F_968A_70034261F4D4 */
