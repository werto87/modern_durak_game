#include "modern_durak_game/server/server.hxx"
#include "modern_durak_game/util/util.hxx"
#include <Corrade/Utility/Arguments.h>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/json/src.hpp>
#include <boost/numeric/conversion/cast.hpp>
#include <confu_soci/convenienceFunctionForSoci.hxx>
#include <durak_computer_controlled_opponent/database.hxx>
#include <durak_computer_controlled_opponent/solve.hxx>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
auto const DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING = std::string { "3232" };
auto const DEFAULT_PORT_MATCHMAKING_TO_GAME = std::string { "4242" };
auto const DEFAULT_PORT_GAME_TO_MATCHMAKING = std::string { "12312" };
auto const DEFAULT_ADDRESS_OF_MATCHMAKING = std::string { "127.0.0.1" };
auto const DEFAULT_DATABASE_PATH = std::string { CURRENT_BINARY_DIR } + "/combination.db";

int
main (int argc, char **argv)
{
  Corrade::Utility::Arguments args {};
  // clang-format off
  args
    .addOption("port-user-to-game-via-matchmaking", DEFAULT_PORT_USER_TO_GAME_VIA_MATCHMAKING).setHelp("port-user-to-game-via-matchmaking", "port user to game via matchmaking")
    .addOption("port-matchmaking-to-game", DEFAULT_PORT_MATCHMAKING_TO_GAME).setHelp("port-matchmaking-to-game", "port matchmaking to game")
    .addOption("port-game-to-matchmaking", DEFAULT_PORT_GAME_TO_MATCHMAKING).setHelp("port-game-to-matchmaking", "port game to matchmaking")
    .addOption("address-of-matchmaking", DEFAULT_ADDRESS_OF_MATCHMAKING).setHelp("address-of-matchmaking", "address of matchmaking")
    .addOption("path-to-database", DEFAULT_DATABASE_PATH).setHelp("path-to-database", "example /my/path/database.db")
    .setGlobalHelp("durak game")
    .parse(argc, argv);
  // clang-format on
  using namespace durak_computer_controlled_opponent;
  std::string DATABASE_PATH = args.value ("path-to-database");
  if (not std::filesystem::exists (DATABASE_PATH))
    {
      std::cerr << "combination.db not found at: '" + DATABASE_PATH + "' please provide it or create it by running create_combination_database executable. Consider building create_combination_database in release mode it is around 15 times faster than debug." << std::endl;
      std::terminate ();
    }
  else
    {
      std::cout << "starting modern_durak_game" << std::endl;
      try
        {
          using namespace boost::asio;
          io_context ioContext {};
          signal_set signals (ioContext, SIGINT, SIGTERM);
          signals.async_wait ([&] (auto, auto) { ioContext.stop (); });
          auto server = Server {};
          using namespace boost::asio::experimental::awaitable_operators;
          auto const PORT_USER_TO_GAME_VIA_MATCHMAKING = boost::numeric_cast<u_int16_t> (std::stoul (args.value ("port-user-to-game-via-matchmaking")));
          auto const PORT_MATCHMAKING_TO_GAME = boost::numeric_cast<u_int16_t> (std::stoul (args.value ("port-matchmaking-to-game")));
          auto const PORT_GAME_TO_MATCHMAKING = args.value ("port-game-to-matchmaking");
          std::string ADDRESS_MATCHMAKING = args.value ("address-of-matchmaking");

          auto userToGameViaMatchmaking = boost::asio::ip::tcp::endpoint { ip::tcp::v4 (), PORT_USER_TO_GAME_VIA_MATCHMAKING };
          auto matchmakingToGame = boost::asio::ip::tcp::endpoint { ip::tcp::v4 (), PORT_MATCHMAKING_TO_GAME };
          co_spawn (ioContext, server.listenerUserToGameViaMatchmaking (userToGameViaMatchmaking, ioContext, ADDRESS_MATCHMAKING, PORT_GAME_TO_MATCHMAKING, DATABASE_PATH) && server.listenerMatchmakingToGame (matchmakingToGame), printException);
          ioContext.run ();
        }
      catch (std::exception &e)
        {
          std::printf ("Exception: %s\n", e.what ());
        }
    }
  return 0;
}