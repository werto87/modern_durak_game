#ifndef EFFCC19D_EF93_4BD9_B516_6E5932A5ECA0
#define EFFCC19D_EF93_4BD9_B516_6E5932A5ECA0

#include <boost/asio/any_io_executor.hpp>
#include <filesystem>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#include <boost/asio/ip/tcp.hpp>
#pragma GCC diagnostic pop
#include <boost/optional/optional.hpp>
#include <deque>
#include <durak/game.hxx>
#include <durak/gameData.hxx>
#include <list>
#include <memory>
#include <optional>
namespace boost::asio
{
class io_context;
}

namespace matchmaking_game
{
struct StartGame;
}
struct User;

class Game
{

public:
  Game (matchmaking_game::StartGame const &startGame, std::string const &gameName, std::list<User> &&users, boost::asio::io_context &ioContext_, boost::asio::ip::tcp::endpoint const &gameToMatchmakingEndpoint_, std::filesystem::path const &databasePath);

  std::optional<std::string> processEvent (std::string const &event, std::string const &accountName);

  void startGame ();

  std::string const &gameName () const;

  bool isUserInGame (std::string const &userName) const;

  bool removeUser (std::string const &userName);

  size_t usersInGame () const;
  boost::optional<User &> user (std::string const &userName);

  durak::Game const &durakGame () const;

private:
  struct StateMachineWrapper;
  struct StateMachineWrapperDeleter
  {
    void operator() (StateMachineWrapper *p) const;
  };

  std::unique_ptr<StateMachineWrapper, StateMachineWrapperDeleter> sm; // only use this member inside of ".cxx". reason because of incomplete type
};

#endif /* EFFCC19D_EF93_4BD9_B516_6E5932A5ECA0 */
