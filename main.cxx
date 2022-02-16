#include "src/server/server.hxx"
#include "src/util.hxx"
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/json/src.hpp>
#include <exception>
#include <iostream>
#include <stdexcept>

auto const DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING = u_int16_t{ 33333 };
auto const DEFAULT_PORT_MATCHMAKING_TO_GAME = u_int16_t{ 44444 };

int
main ()
{
  try
    {
      using namespace boost::asio;
      io_context ioContext{};
      signal_set signals (ioContext, SIGINT, SIGTERM);
      signals.async_wait ([&] (auto, auto) { ioContext.stop (); });
      auto server = Server{};
      using namespace boost::asio::experimental::awaitable_operators;
      auto userToGameViaMatchmaking = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
      auto matchmakingToGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
      co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
      ioContext.run ();
    }
  catch (std::exception &e)
    {
      std::printf ("Exception: %s\n", e.what ());
    }
  return 0;
}