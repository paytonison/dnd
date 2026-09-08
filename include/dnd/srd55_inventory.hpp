#pragma once
#include "dnd/engine.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dnd::srd55v2 {
struct Context;
struct InventoryResult {
    Evaluation evaluation;
    bool initialized = false;
    std::vector<std::string> ownedProfiles;
    std::string armorProfile;
    std::string weaponProfile;
    std::string weaponInstanceId;
    bool shield = false;
    int attunementMaximum = 3;
    std::map<std::string, int>
        abilityMinimums; // Current equipment; never rewrite natural scores/history.
    std::map<std::string, int>
        abilityBonuses; // Current additive item increases, applied before minimum-setting items.
    std::map<std::string, int> abilityBonusCaps;
    std::set<std::string> languages;
    std::set<std::string> weaponTraining;
    std::map<std::string, int> saveBonuses;
    std::map<std::string, int> skillBonuses;
    std::map<std::string, int> speedMinimums;
    int armorBonus = 0;
    int weaponAttackBonus = 0;
    int weaponDamageBonus = 0;
    int spellAttackBonus = 0;
    int spellSaveBonus = 0;
    int speedBonus = 0;
    int initiativeBonus = 0;
    int darkvisionBonus = 0;
    int darkvisionMinimum = 0;
    int proficiencyBonus = 0;
    bool ignoreArmorStealthDisadvantage = false;
    int hpBonusPerLevel = 0;
    bool ignoreArmorSpeedPenalty = false;
    Json armorFormulas =
        Json::array(); // {label,base,abilities:[...]} after eligibility has been checked.
    Json effects = Json::array();     // Applied sourced conditional/resistance/action effects.
    Json spellGrants = Json::array(); // Item source profiles; fixed DCs are not character DCs.
    Json permanentEffects =
        Json::array(); // {op,ability,value,maxScore,acquiredCharacterLevel,eventId,source}.
};
InventoryResult resolveInventory(const Context &);
void appendSrd55InventoryActions(const CharacterDocument &, const ResolvedRuleset &, Evaluation &);
TransitionResult applySrd55InventoryCommand(const CharacterDocument &, const ResolvedRuleset &,
                                            const CharacterCommand &);
TransitionResult consumeSrd55InventoryScroll(const CharacterDocument &, const ResolvedRuleset &,
                                             const std::string &instanceId,
                                             const std::string &reason);
std::vector<Message> validateSrd55MagicItems(const ContentPack &);
std::vector<Message> validateSrd55InventoryReferences(const ResolvedRuleset &);
} // namespace dnd::srd55v2
