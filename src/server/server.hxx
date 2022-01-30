#ifndef AC7BAF85_A15E_49E8_AD93_D8065253C6DF
#define AC7BAF85_A15E_49E8_AD93_D8065253C6DF

// #include "src/game/logic/gameMachine.hxx"
#include "src/game/logic/game.hxx"
#include "user.hxx"
#include <list>

class Server
{
public:
  boost::asio::awaitable<void> listenerUserToGameViaMatchmaking (boost::asio::ip::tcp::endpoint const &endpoint);
  boost::asio::awaitable<void> listenerMatchmakingToGame (boost::asio::ip::tcp::endpoint const &endpoint);

private:
  std::list<Game> games{};
};

#endif /* AC7BAF85_A15E_49E8_AD93_D8065253C6DF */
