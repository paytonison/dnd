#pragma once
#include "dnd/engine.hpp"
#include <set>
#include <algorithm>

namespace dnd::srd55v2 {
struct Context {
    Context(const CharacterDocument& d, const ResolvedRuleset& r) : document(d), rules(r) {}
    const CharacterDocument& document;
    const ResolvedRuleset& rules;
    std::map<std::string, int> classLevels; // keys are selected content identities, not mechanics profiles
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
struct CalculationTrace {
    std::vector<std::string> steps;
    std::vector<SourceRef> sources;
};
struct FeatureResult {
    Evaluation evaluation;
    std::map<std::string, int> abilityBonuses;
    std::map<std::string, int> abilityCaps;
    std::set<std::string> skillProficiencies;
    std::map<std::string, int> skillAcquisitionLevels;
    std::map<std::string, std::vector<SourceRef>> skillProficiencySources;
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
    std::map<std::string, CalculationTrace> armorFormulaTraces;
    std::map<std::string, std::vector<std::string>> armorFormulaAbilities;
    CalculationTrace speedTrace;
    std::map<std::string, CalculationTrace> skillBonusTraces;
    std::map<std::string, std::vector<std::string>> skillBonusAbilities;
    std::map<std::string, std::vector<SourceRef>> expertiseSources;
    std::map<std::string, std::vector<SourceRef>> skillAbilitySources;
    std::vector<SourceRef> halfProficiencySources;
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
FeatureResult advancementAbilityGrant(const std::string& profile, int classLevel);

struct WizardBookState {
    bool tracked = false;
    bool hasAccessibleBook = false;
    bool destinationAccessible = false;
    std::string destinationId;
    std::set<std::string> accessibleSpells;
    std::set<std::string> destinationSpells;
    std::set<std::string> prepared;
    std::vector<Message> messages;
};
std::set<std::string> wizardAcquiredSpells(const CharacterDocument&, const ResolvedRuleset&);
std::set<std::string> wizardAcquiredSpells(const Context&);
int wizardPreparationLimit(const CharacterDocument&, const ResolvedRuleset&, int classLevel = -1);
std::string originalWizardBookId(const CharacterDocument&, const ResolvedRuleset&);
std::set<std::string> wizardPreparedSpells(const CharacterDocument&, const ResolvedRuleset&);
std::set<std::string> wizardRetainedSelection(const CharacterDocument&, const std::string& path);
WizardBookState resolveWizardSpellbooks(const CharacterDocument&, const ResolvedRuleset&);
void appendWizardSpellbookState(const CharacterDocument&, const ResolvedRuleset&, Evaluation&);
void appendWizardSpellbookActions(const CharacterDocument&, const ResolvedRuleset&, Evaluation&);
TransitionResult applyWizardSpellbookCommand(const CharacterDocument&, const ResolvedRuleset&, const CharacterCommand&);
void recordWizardBookExternalCopy(CharacterDocument&, const ResolvedRuleset&, const std::string& spell,
                                  int minutes, int paidCp, const std::string& source);
void recordWizardBookAdvancement(const CharacterDocument&, CharacterDocument&, const ResolvedRuleset&);

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

// Content identities remain in the document; executable behavior and its stable
// choice/resource paths are selected through the validated mechanics profile.
inline std::string classProfile(const ResolvedRuleset& rules, const std::string& id) {
    const auto* definition = rules.find(id);
    return definition && definition->value("kind", "") == "class"
        ? text(*definition, "/rulesProfile") : "";
}
inline std::string selectedClassId(const Context& context, const std::string& profile) {
    for (const auto& [id, level] : context.classLevels)
        if (level > 0 && classProfile(context.rules, id) == profile) return id;
    return "";
}
inline int profileLevel(const Context& context, const std::string& profile) {
    const auto id = selectedClassId(context, profile);
    const auto found = context.classLevels.find(id);
    return found == context.classLevels.end() ? 0 : found->second;
}
inline const Json* selectedSubclass(const Context& context, const std::string& profile) {
    auto id = text(context.document.choices, "/subclasses/" + profile);
    if (id.empty() && context.classLevels.size() == 1)
        id = text(context.document.choices, "/subclassId");
    const auto* definition = context.rules.find(id);
    return profileLevel(context, profile) >= 3 && definition &&
        definition->value("kind", "") == "subclass" &&
        classProfile(context.rules, text(*definition, "/classId")) == profile
        ? definition : nullptr;
}
inline int profileLevel(const Json& choices, const ResolvedRuleset& rules, const std::string& profile) {
    int count = 0;
    const int total = std::clamp(number(choices, "/level", 1), 1, 20);
    const auto* multiclass = at(choices, "/multiclass");
    for (int level = 1; level <= total; ++level) {
        const auto id = level == 1 || !multiclass || *multiclass != true
            ? text(choices, "/classId")
            : text(choices, "/advancement/" + std::to_string(level) + "/classId");
        if (classProfile(rules, id) == profile) ++count;
    }
    return count;
}
} // namespace dnd::srd55v2
