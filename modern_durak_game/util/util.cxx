#include "modern_durak_game/util/util.hxx"
#include "modern_durak_game/game/logic/allowedMoves.hxx"
#include <durak_computer_controlled_opponent/database.hxx>
#include <login_matchmaking_game_shared/matchmakingGameSerialization.hxx>
#include <modern_durak_game_option/userDefinedGameOption.hxx>
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
  using namespace durak_computer_controlled_opponent;
  std::filesystem::remove (databasePath);
  auto gameLookup = std::map<std::tuple<uint8_t, uint8_t>, std::array<std::map<std::tuple<std::vector<uint8_t>, std::vector<uint8_t> >, small_memory_tree::SmallMemoryTree<std::tuple<Action, Result> > >, 4> > {};
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

std::expected<shared_class::GameOption, std::string>
toGameOption (std::string const &gameOptionAsString)
{
  auto ec = boost::system::error_code {};
  auto result = confu_json::read_json (gameOptionAsString, ec);
  if (ec)
    {
      auto error = std::stringstream {};
      error << "error while parsing string: error code: " << ec << std::endl;
      error << "error while parsing string: stringToParse: " << gameOptionAsString << std::endl;
      return std::unexpected (error.str ());
    }
  else
    {
      return confu_json::to_object<shared_class::GameOption> (result);
    }
}
