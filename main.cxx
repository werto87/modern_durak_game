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
      io_context io_context (1);
      signal_set signals (io_context, SIGINT, SIGTERM);
      signals.async_wait ([&] (auto, auto) { io_context.stop (); });
      auto server = Server{};
      using namespace boost::asio::experimental::awaitable_operators;
      auto userToGameViaMatchmaking = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING };
      auto matchmakingToGame = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_MATCHMAKING_TO_GAME };
      co_spawn (io_context, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
      io_context.run ();
    }
  catch (std::exception &e)
    {
      std::printf ("Exception: %s\n", e.what ());
    }
  return 0;
}