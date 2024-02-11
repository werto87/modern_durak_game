#include "example_of_a_game_server/util/util.hxx"
#include "example_of_a_game_server/game/logic/allowedMoves.hxx"
#include <durak_computer_controlled_opponent/database.hxx>
#ifdef LOG_CO_SPAWN_PRINT_EXCEPTIONS
void
printExceptionHelper (std::exception_ptr eptr)
{
  try
    {
      if (eptr)
        {
          std::rethrow_exception (eptr);
        }
    }
  catch (std::exception const &e)
    {
      std::cout << "co_spawn exception: '" << e.what () << "'" << std::endl;
    }
}
#else
void
printExceptionHelper (std::exception_ptr)
{
}
#endif

void
createCombinationDatabase (std::filesystem::path const &databasePath)
{
  std::filesystem::remove (databasePath);
  auto gameLookup = std::map<std::tuple<uint8_t, uint8_t>, std::array<std::map<std::tuple<std::vector<uint8_t>, std::vector<uint8_t> >, std::vector<std::tuple<uint8_t, durak_computer_controlled_opponent::Result> > >, 4> > {};
  std::cout << "create new game lookup table" << std::endl;
  std::cout << "solveDurak (36, 1, 1, gameLookup)" << std::endl;
  gameLookup.insert ({ { 1, 1 }, solveDurak (36, 1, 1, gameLookup) });
  std::cout << "solveDurak (36, 2, 2, gameLookup)" << std::endl;
  gameLookup.insert ({ { 2, 2 }, solveDurak (36, 2, 2, gameLookup) });
  std::cout << "solveDurak (36, 3, 1, gameLookup)" << std::endl;
  gameLookup.insert ({ { 3, 1 }, solveDurak (36, 3, 1, gameLookup) });
  std::cout << "solveDurak (36, 2, 4, gameLookup)" << std::endl;
  gameLookup.insert ({ { 2, 4 }, solveDurak (36, 2, 4, gameLookup) });
  std::cout << "solveDurak (36, 3, 3, gameLookup)" << std::endl;
  gameLookup.insert ({ { 3, 3 }, solveDurak (36, 3, 3, gameLookup) });
  std::cout << "insert lookup table into database" << std::endl;
  durak_computer_controlled_opponent::database::createDatabaseIfNotExist (databasePath);
  soci::session sql (soci::sqlite3, databasePath);
  durak_computer_controlled_opponent::database::createTables (databasePath);
  durak_computer_controlled_opponent::database::insertGameLookUp (databasePath, gameLookup);
  std::cout << "finished creating game lookup table " << std::endl;
}