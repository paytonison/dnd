#pragma once
#include "dnd/engine.hpp"
#include <array>

namespace dnd::srd51 {
inline constexpr std::array<const char*, 6> abilities{
    "strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"};
inline SourceRef ref(const std::string& page) {
    return {"System Reference Document 5.1", page,
            "https://media.dndbeyond.com/compendium-images/srd/5.1/SRD_CC_v5.1.pdf#page=" + page};
}
std::vector<Message> validateContent(const ContentPack& pack);
std::vector<Message> validateRuleset(const ResolvedRuleset& rules);
} // namespace dnd::srd51
