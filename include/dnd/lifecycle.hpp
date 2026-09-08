#pragma once
#include "dnd/engine.hpp"

namespace dnd::srd55v2 {
void appendLifecycleActions(const CharacterDocument &, const ResolvedRuleset &, Evaluation &);
void linkSrd55Resources(Evaluation &);
TransitionResult applyLifecycleCommand(const CharacterDocument &, const ResolvedRuleset &,
                                       const CharacterCommand &);

// History has its own baseline, validated change events, and pending advancement.
std::vector<Message> validateSrd55History(const CharacterDocument &, const ResolvedRuleset &);
void appendSrd55HistoryActions(const CharacterDocument &, const ResolvedRuleset &, Evaluation &);
TransitionResult applySrd55HistoryCommand(const CharacterDocument &, const ResolvedRuleset &,
                                          const CharacterCommand &);
void recordSrd55HistoryTrigger(const CharacterDocument &before, CharacterDocument &after,
                               const std::string &kind);
void recordSrd55HistoryChange(const CharacterDocument &before, CharacterDocument &after,
                              const std::string &kind, const Json &inputs);
std::map<std::string, int> srd55HistoryAcquisitionLevels(const CharacterDocument &);

// All transitions are deterministic; rolls/time and other accepted inputs come from the caller.
void appendSrd55InventoryActions(const CharacterDocument &, const ResolvedRuleset &, Evaluation &);
TransitionResult applySrd55InventoryCommand(const CharacterDocument &, const ResolvedRuleset &,
                                            const CharacterCommand &);
} // namespace dnd::srd55v2
