#ifndef AC7BAF85_A15E_49E8_AD93_D8065253C6DF
#define AC7BAF85_A15E_49E8_AD93_D8065253C6DF

#include "gameToCreate.hxx"
#include "src/game/logic/game.hxx"
#include "user.hxx"
#include <boost/asio/awaitable.hpp>
#include <list>

class Server
{
public:
  boost::asio::awaitable<void> listenerUserToGameViaMatchmaking (boost::asio::ip::tcp::endpoint const &endpoint, boost::asio::io_context &ioContext);
  boost::asio::awaitable<void> listenerMatchmakingToGame (boost::asio::ip::tcp::endpoint const &endpoint);

private:
  std::list<Game> games{};
  std::list<GameToCreate> gamesToCreate{};
};

#endif /* AC7BAF85_A15E_49E8_AD93_D8065253C6DF */
