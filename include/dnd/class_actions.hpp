#pragma once
#include "dnd/engine.hpp"

namespace dnd::srd55v2 {
void appendSrd55ClassActions(const CharacterDocument &, const ResolvedRuleset &, Evaluation &);
TransitionResult applySrd55ClassCommand(const CharacterDocument &, const ResolvedRuleset &,
                                        const CharacterCommand &);
} // namespace dnd::srd55v2
