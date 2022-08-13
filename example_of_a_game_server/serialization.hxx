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
#include <durak/card.hxx>
#include <durak/gameData.hxx>
#include <durak/gameOption.hxx>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <variant>

// GENERIC GAME MESSAGES
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), UnhandledMessageError, (std::string, msg) (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), LeaveGameServer, (std::string, accountName))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), LeaveGameSuccess, )
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), LeaveGameError, )
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), GameOver, (std::string, gameName) (bool, ratedGame) (std::vector<std::string>, winners) (std::vector<std::string>, losers) (std::vector<std::string>, draws))
typedef std::vector<std::pair<std::string, long long int> > UserTimeMilliseconds;
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakTimers, (UserTimeMilliseconds, runningTimeUserTimePointMilliseconds) (UserTimeMilliseconds, pausedTimeUserDurationMilliseconds))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), GameOption, (durak::GameOption, gameOption) (shared_class::DurakTimers, durakTimers)) // TODO-TEMPLATE add game options
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), StartGame, (std::vector<std::string>, players) (shared_class::GameOption, gameOption) (bool, ratedGame))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), StartGameError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), StartGameSuccess, (std::string, gameName))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), GameOverSuccess, )
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), GameOverError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), UserLeftGame, (std::string, accountName))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), UserLeftGameSuccess, (std::string, accountName))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), UserLeftGameError, (std::string, accountName) (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), ConnectToGame, (std::string, accountName) (std::string, gameName))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), ConnectToGameError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((matchmaking_game), ConnectToGameSuccess, )
// GENERIC GAME MESSAGES //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// MODERN DURAK TYPES
// TODO this events come from the user and the game but the problem is for example DurakAttack has no name but the state machine uses attack with user name ???
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAttack, (std::vector<durak::Card>, cards))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAttackSuccess, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAttackError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefend, (durak::Card, cardToBeat) (durak::Card, card))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendSuccess, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAttackPass, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAttackPassSuccess, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAttackPassError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAssistPass, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAssistPassSuccess, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAssistPassError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendPass, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendPassSuccess, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendPassError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAskDefendWantToTakeCards, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAskDefendWantToTakeCardsAnswer, (bool, answer))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAskDefendWantToTakeCardsAnswerSuccess, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAskDefendWantToTakeCardsAnswerError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendWantsToTakeCardsFromTableDoYouWantToAddCards, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendWantsToTakeCardsFromTableDoYouWantToAddCardsAnswer, (bool, answer))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendWantsToTakeCardsFromTableDoneAddingCards, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendWantsToTakeCardsFromTableDoneAddingCardsSuccess, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakGameOverWon, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakGameOverLose, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakGameOverDraw, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakLeaveGame, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakLeaveGameError, (std::string, error))

namespace shared_class
{
enum struct Move
{
  AttackAssistPass,
  AttackAssistDoneAddingCards,
  AddCards,
  Defend,
  TakeCards,
  AnswerDefendWantsToTakeCardsYes,
  AnswerDefendWantsToTakeCardsNo
};
}
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakAllowedMoves, (std::vector<shared_class::Move>, allowedMoves))

namespace shared_class
{
enum struct TimerType
{
  noTimer,
  resetTimeOnNewRound,
  addTimeOnNewRound
};
}
// TODO there is no support for std::chrono::seconds in confu_json
BOOST_FUSION_DEFINE_STRUCT ((shared_class), SetTimerOption, (shared_class::TimerType, timerType) (int, timeAtStartInSeconds) (int, timeForEachRoundInSeconds))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), SetTimerOptionError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), UnhandledEventError, (std::string, unhandledEvent) (std::string, reason))

// MODERN DURAK TYPES //////////////////////////////////////////////////////////////////////////////////////////////

BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakNextMove, )
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakNextMoveError, (std::string, error))
BOOST_FUSION_DEFINE_STRUCT ((shared_class), DurakNextMoveSuccess, (shared_class::Move, nextMove) (std::optional<durak::Card>, card))

// clang-format off
namespace shared_class{
    // TODO-TEMPLATE add new type to handle in server
static boost::hana::tuple<
DurakAttack,
DurakAttackSuccess,
DurakAttackError,
DurakDefend,
DurakDefendSuccess,
DurakDefendError,
DurakAttackPass,
DurakAttackPassSuccess,
DurakAttackPassError,
DurakAssistPass,
DurakAssistPassSuccess,
DurakAssistPassError,
DurakDefendPass,
DurakDefendPassSuccess,
DurakDefendPassError,
DurakAskDefendWantToTakeCards,
DurakAskDefendWantToTakeCardsAnswer,
DurakAskDefendWantToTakeCardsAnswerSuccess,
DurakAskDefendWantToTakeCardsAnswerError,
DurakDefendWantsToTakeCardsFromTableDoYouWantToAddCards,
DurakDefendWantsToTakeCardsFromTableDoYouWantToAddCardsAnswer,
DurakDefendWantsToTakeCardsFromTableDoneAddingCards,
DurakDefendWantsToTakeCardsFromTableDoneAddingCardsSuccess,
DurakDefendWantsToTakeCardsFromTableDoneAddingCardsError,
DurakGameOverWon,
DurakGameOverLose,
DurakGameOverDraw,
DurakLeaveGame,
DurakLeaveGameError,
DurakTimers,
DurakAllowedMoves,
DurakNextMove
  >  const gameTypes{};
}
// clang-format on

#endif /* C02CDB99_AA83_45B0_83E7_8C8BC254A8A2 */
