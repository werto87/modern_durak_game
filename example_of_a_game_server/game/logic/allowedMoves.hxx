#ifndef FA9CDEF0_BEE5_4919_910F_EC780C1C3C4C
#define FA9CDEF0_BEE5_4919_910F_EC780C1C3C4C

#include <modern_durak_game_shared/modern_durak_game_shared.hxx>
#include <durak/game.hxx>

struct AllowedMoves
{
  std::optional<std::vector<shared_class::Move> > defend{};
  std::optional<std::vector<shared_class::Move> > attack{};
  std::optional<std::vector<shared_class::Move> > assist{};
};

#endif /* FA9CDEF0_BEE5_4919_910F_EC780C1C3C4C */
