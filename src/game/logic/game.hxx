#ifndef EFFCC19D_EF93_4BD9_B516_6E5932A5ECA0
#define EFFCC19D_EF93_4BD9_B516_6E5932A5ECA0

#include <boost/asio/any_io_executor.hpp>
#include <boost/optional/optional.hpp>
#include <deque>
#include <list>
#include <memory>
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
  Game (matchmaking_game::StartGame const &startGame, std::string const &gameName, std::list<User> &&users, boost::asio::io_context &ioContext_);

  void processEvent (std::string const &event, std::string const &accountName);

  std::string const &gameName () const;

  bool isUserInGame (std::string const &userName) const;

  bool removeUser (std::string const &userName);

  size_t usersInGame () const;
  boost::optional<User &> user (std::string const &userName);

private:
  struct StateMachineWrapper;
  struct StateMachineWrapperDeleter
  {
    void operator() (StateMachineWrapper *p);
  };

  std::unique_ptr<StateMachineWrapper, StateMachineWrapperDeleter> sm; // only use this member inside of ".cxx". reason because of incomplete type
};

#endif /* EFFCC19D_EF93_4BD9_B516_6E5932A5ECA0 */
