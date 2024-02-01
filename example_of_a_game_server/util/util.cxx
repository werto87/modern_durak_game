#include "util.hxx"
#include "util/serialization.hxx"
#include <durak/game.hxx>
#include <durak/gameData.hxx>
#include <map>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/transform.hpp>
#include <range/v3/algorithm/unique.hpp>
#include <range/v3/algorithm/unique_copy.hpp>
#include <range/v3/all.hpp>
#include <range/v3/iterator/insert_iterators.hpp>

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