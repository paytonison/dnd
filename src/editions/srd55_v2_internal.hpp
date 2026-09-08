#pragma once
#include "dnd/engine.hpp"
#include <set>

namespace dnd::srd55v2 {
struct Context {
    Context(const CharacterDocument& d, const ResolvedRuleset& r) : document(d), rules(r) {}
    const CharacterDocument& document;
    const ResolvedRuleset& rules;
    std::map<std::string, int> classLevels; // keys are full srd55:class ids
    std::map<std::string, std::vector<int>> classLevelEvents; // class level index -> character level
    std::map<std::string, int> choiceAcquisitionLevels;
    std::string initialClass;
    int totalLevel = 1;
    int proficiency = 2;
    std::map<std::string, int> scores;
    std::map<std::string, int> modifiers;
    std::set<std::string> castingAbilities;
    std::set<std::string> skillProficiencies; // namespaced ids
    std::map<std::string, int> skillAcquisitionLevels;
    std::set<std::string> expertise;
    std::set<std::string> feats;
    std::set<std::string> preparedSpells; // prepared or always prepared from any existing source
    std::set<std::string> toolProficiencies;
    std::map<std::string, int> toolAcquisitionLevels;
    std::set<std::string> armorTraining;
    std::set<std::string> weaponTraining;
    std::string armorCategory;
    bool armored = false;
    bool shield = false;
};
struct FeatureResult {
    Evaluation evaluation;
    std::map<std::string, int> abilityBonuses;
    std::map<std::string, int> abilityCaps;
    std::set<std::string> skillProficiencies;
    std::map<std::string, int> skillAcquisitionLevels;
    std::set<std::string> expertise;
    std::set<std::string> armorTraining;
    std::set<std::string> weaponTraining;
    std::set<std::string> saveProficiencies;
    std::set<std::string> feats;
    Json featGrants = Json::array(); // {id,path,level,source}; distinct repeatable grant instances
    std::set<std::string> languages;
    std::set<std::string> toolProficiencies;
    std::map<std::string, int> cantripBonuses; // full class id
    std::map<std::string, std::string> skillAbilityOverrides;
    std::map<std::string, int> statMinimums;
    Json spellGrants = Json::array(); // {spellId,ability,reason,freeUses,source}
    Json attackModes = Json::array(); // explicit conditional attack profiles
    std::map<std::string, int> physicalAbilityOverrides;
    Json transformedForm = Json::object();
    std::map<std::string, int> skillBonuses;
    std::map<std::string, int> saveBonuses;
    std::map<std::string, int> spellSaveBonuses;
    std::map<std::string, int> spellAttackBonuses;
    std::map<std::string, int> armorFormulas; // named complete base AC, no stacking
    int hpBonus = 0;
    int speedBonus = 0;
    int attackCount = 1;
    int initiativeBonus = 0;
    int halfProficiency = 0;
};
// First pass: grants and choices that affect abilities, proficiencies, and training.
FeatureResult resolveClassChoices(const Context& context);
// Second pass: final ability modifiers are available; calculations and capacities.
FeatureResult evaluateClassFeatures(const Context& context);
FeatureResult advancementAbilityGrant(const std::string& classId, int classLevel);

SourceRef ref(const std::string& page);
const Json* at(const Json& value, const std::string& pointer);
int number(const Json& value, const std::string& pointer, int fallback = 0);
std::string text(const Json& value, const std::string& pointer, const std::string& fallback = "");
std::vector<std::string> strings(const Json& value, const std::string& pointer);
void issue(Evaluation& result, const std::string& code, const std::string& path, const std::string& message,
           const std::string& page = "19", const std::string& severity = "error");
Field integer(const std::string& path, const std::string& label, int minimum, int maximum, const std::string& help = "");
Field select(const std::string& path, const std::string& label, std::vector<Choice> options, bool multiple = false, const std::string& help = "");
std::vector<Choice> options(const ResolvedRuleset& rules, const std::string& kind);
std::string pick(Evaluation& result, const Json& choices, const Field& field, bool required = true);
std::vector<std::string> picks(Evaluation& result, const Json& choices, const Field& field, int count, bool exact = true);
void merge(Evaluation& target, const Evaluation& source);
} // namespace dnd::srd55v2
