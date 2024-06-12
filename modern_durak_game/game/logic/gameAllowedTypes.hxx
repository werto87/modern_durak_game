#pragma once
#include <modern_durak_game_shared/modern_durak_game_shared.hxx>
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
DurakNextMove,
DurakNextMoveSuccess
  >  const gameTypes{};
}
// clang-format on