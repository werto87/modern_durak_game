#ifndef C02CDB99_AA83_45B0_83E7_8C8BC254A8A2
#define C02CDB99_AA83_45B0_83E7_8C8BC254A8A2

#include <boost/algorithm/string.hpp>
#include <boost/fusion/adapted/struct/adapt_struct.hpp>
#include <boost/fusion/adapted/struct/define_struct.hpp>
#include <boost/fusion/algorithm/query/count.hpp>
#include <boost/fusion/functional.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include <boost/fusion/include/algorithm.hpp>
#include <boost/fusion/include/at.hpp>
#include <boost/fusion/include/count.hpp>
#include <boost/fusion/include/define_struct.hpp>
#include <boost/fusion/sequence/intrinsic/at.hpp>
#include <boost/fusion/sequence/intrinsic_fwd.hpp>
#include <boost/hana/assert.hpp>
#include <boost/hana/at_key.hpp>
#include <boost/hana/equal.hpp>
#include <boost/hana/find.hpp>
#include <boost/hana/for_each.hpp>
#include <boost/hana/integral_constant.hpp>
#include <boost/hana/map.hpp>
#include <boost/hana/optional.hpp>
#include <boost/hana/pair.hpp>
#include <boost/hana/tuple.hpp>
#include <boost/hana/type.hpp>
#include <boost/json.hpp>
#include <boost/mpl/for_each.hpp>
#include <boost/mpl/if.hpp>
#include <boost/mpl/range_c.hpp>
#include <cstddef>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <variant>

BOOST_FUSION_DEFINE_STRUCT ((shared_class), UnhandledMessageError, (std::string, msg) (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), StartGame, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), GameStarted, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), LeaveGame, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), GameOver, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), ChangeRating, (std::vector<std::string>, winners) (std::vector<std::string>, losers) (std::vector<std::string>, draws))

// clang-format off
namespace shared_class{
    // TODO-TEMPLATE add new type to handle in server
static boost::hana::tuple<
LeaveGame,
StartGame
  >  const sharedClasses{};
}
// clang-format on

#endif /* C02CDB99_AA83_45B0_83E7_8C8BC254A8A2 */
