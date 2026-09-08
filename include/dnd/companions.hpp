#pragma once
#include "dnd/engine.hpp"

namespace dnd::srd55v2 {
// Find Familiar remains one persistent creature across casting sources.
void appendSrd55CompanionState(const CharacterDocument&, const ResolvedRuleset&, Evaluation&);
void appendSrd55CompanionActions(const CharacterDocument&, const ResolvedRuleset&, Evaluation&);
TransitionResult applySrd55CompanionCommand(const CharacterDocument&, const ResolvedRuleset&, const CharacterCommand&);
// Explicit completed-rest mutation, called by the owning Long Rest command.
void expireSrd55CompanionsOnLongRest(CharacterDocument&);
}
