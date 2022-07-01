#include "src/database.hxx"
#include "src/server/server.hxx"
#include "src/util.hxx"
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/json/src.hpp>
#include <confu_soci/convenienceFunctionForSoci.hxx>
#include <durak_computer_controlled_opponent/solve.hxx>
#include <exception>
#include <iostream>
#include <stdexcept>

auto const DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING = u_int16_t{ 3232 };
auto const DEFAULT_PORT_MATCHMAKING_TO_GAME = u_int16_t{ 4242 };
auto const DEFAULT_PORT_GAME_TO_MATCHMAKING = u_int16_t{ 12312 };

int
main ()
{
  using namespace durak_computer_controlled_opponent;
  soci::session sql (soci::sqlite3, database::databaseName);
  database::createDatabaseIfNotExist ();
  if (not confu_soci::doesTableExist (sql, confu_soci::typeNameWithOutNamespace (database::Round{})))
    {
      database::createTables ();
      auto gameLookup = std::map<std::tuple<uint8_t, uint8_t>, std::array<std::map<std::tuple<std::vector<uint8_t>, std::vector<uint8_t> >, std::vector<std::tuple<uint8_t, Result> > >, 4> >{};
      gameLookup.insert ({ { 1, 1 }, solveDurak (36, 1, 1, gameLookup) });
      database::insertGameLookUp (gameLookup);
    }
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
      auto gameToMatchmaking = boost::asio::ip::tcp::endpoint{ ip::tcp::v4 (), DEFAULT_PORT_GAME_TO_MATCHMAKING };
      co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, gameToMatchmaking) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
      ioContext.run ();
    }
  catch (std::exception &e)
    {
      std::printf ("Exception: %s\n", e.what ());
    }
  return 0;
}