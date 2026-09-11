#pragma once
#include "dnd/content.hpp"
#include <array>
#include <stdexcept>

// Fixed source-derived inputs for the original 2014 rules. Expected outputs
// belong in tests; these fixtures do not read the engine to solve choices.
namespace srd51fixtures {
inline dnd::CharacterDocument fighter(int level = 1) {
    auto document = dnd::newCharacter("srd51", "1.0.0");
    document.name = "2014 Human Fighter acceptance";
    document.choices = {
        {"classId", "srd51:fighter"}, {"raceId", "srd51:human"},
        {"backgroundId", "srd51:acolyte"}, {"level", level},
        {"xp", std::array<int, 3>{0, 300, 900}.at(static_cast<std::size_t>(level - 1))},
        {"alignment", "Neutral Good"}, {"abilityMethod", "standard-array"},
        {"abilities", {{"strength", 15}, {"dexterity", 14}, {"constitution", 13},
                       {"intelligence", 8}, {"wisdom", 12}, {"charisma", 10}}},
        {"classSkills", {"srd51:athletics", "srd51:perception"}},
        {"replacementSkills", dnd::Json::array()},
        {"languages", {"srd51:dwarvish", "srd51:elvish", "srd51:orc"}},
        {"fightingStyle", "srd51:defense"}, {"hpMethod", "fixed"},
        {"startingEquipment", {{"armor", "chain"}, {"weapons", "shield"},
                               {"secondary", "axes"}, {"pack", "explorer"}}},
        {"backgroundEquipment", {{"devotion", "book"}}},
        {"startingWeapons", {{"weapon1", "srd51:longsword"}}},
        {"armorId", "srd51:chain-mail"}, {"shieldId", "srd51:shield"},
        {"weaponId", "srd51:longsword"}, {"offhandId", "none"}, {"twoHands", false},
        {"purchases", dnd::Json::array()}};
    if (level == 3) document.choices["subclassId"] = "srd51:champion";
    return document;
}
inline dnd::ContentPack pack() {
    const auto loaded = dnd::loadPack(std::filesystem::path(DND_DATA_DIR) / "srd51-core");
    if (!loaded.valid()) {
        std::string errors;
        for (const auto& message : loaded.messages) errors += message.path + ": " + message.text + "\n";
        throw std::runtime_error("5E pack is invalid:\n" + errors);
    }
    return loaded.pack;
}
inline dnd::ResolvedRuleset rules() {
    auto resolved = dnd::resolveRuleset(fighter(), {pack()});
    if (!resolved.valid()) throw std::runtime_error("5E ruleset cannot be resolved.");
    return resolved;
}
} // namespace srd51fixtures
