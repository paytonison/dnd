#pragma once
#include "dnd/content.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <regex>
#include <set>
#include <string>

// Public-interface acceptance fixtures: no internal Context, mock calculations,
// relaxed validation, or DM overrides. The solver makes ordinary builder choices
// from current Fields and reports unresolved validation rather than suppressing it.
namespace srd55fixtures {
using dnd::Json;
inline const std::array<std::string,12> classes = {"barbarian","bard","cleric","druid","fighter","monk","paladin","ranger","rogue","sorcerer","warlock","wizard"};
inline const std::map<std::string,std::string> subclasses = {
    {"barbarian","path-of-the-berserker"},{"bard","college-of-lore"},{"cleric","life-domain"},{"druid","circle-of-the-land"},
    {"fighter","champion"},{"monk","warrior-of-the-open-hand"},{"paladin","oath-of-devotion"},{"ranger","hunter"},
    {"rogue","thief"},{"sorcerer","draconic-sorcery"},{"warlock","fiend-patron"},{"wizard","evoker"}};
inline Json at(const Json& object, const std::string& path) {
    try { return object.at(Json::json_pointer(path)); } catch (...) { return nullptr; }
}
inline std::string normalize(std::string path) {
    if (path.starts_with("/choices/")) path.erase(0, 8);
    return path;
}
inline void put(Json& object, const std::string& path, const Json& value) { object[Json::json_pointer(path)] = value; }
inline std::string errors(const dnd::Evaluation& result) {
    std::string text;
    for (const auto& message : result.messages) if (message.severity == "error") text += message.path + " [" + message.code + "]: " + message.text + "\n";
    return text;
}
inline dnd::ResolvedRuleset rules() {
    const auto loaded = dnd::loadPack(std::filesystem::path(DND_DATA_DIR) / "srd55-core-v2");
    if (!loaded.valid()) {
        std::string message; for (const auto& issue : loaded.messages) message += issue.path + ": " + issue.text + "\n";
        throw std::runtime_error("Full SRD pack is not valid:\n" + message);
    }
    auto ruleset = dnd::resolveRuleset(dnd::newCharacter("srd55", "2.0.0"), {loaded.pack});
    if (!ruleset.valid()) throw std::runtime_error("Full SRD ruleset cannot be resolved.");
    return ruleset;
}

inline void seedAdvancement(dnd::CharacterDocument& document, const dnd::ResolvedRuleset& ruleset, const std::string& cls, int level) {
    auto& c = document.choices; c["level"] = level;
    if (level >= 3) c["subclasses"][cls] = "srd55:" + subclasses.at(cls);
    const auto* definition = ruleset.find("srd55:" + cls);
    if (!definition) throw std::runtime_error("Missing class " + cls);
    // Ordinary ASIs improve Dexterity/Wisdom/Intelligence in that order and never
    // Constitution. At class level 19 choose Boon of Fate, increasing Charisma.
    // This leaves Constitution's HP contribution independently predictable.
    int asi = 0;
    for (const auto& acquired : definition->at("featLevels")) {
        const int atLevel = acquired.get<int>(); if (atLevel > level) continue;
        const std::string path = "/feats/" + std::to_string(atLevel);
        if (atLevel == 19) { put(c, path, {{"id","srd55:boon-of-fate"},{"boosts",{{"charisma",1}}}}); }
        else {
            const std::string ability = asi < 2 ? "dexterity" : asi < 4 ? "wisdom" : "intelligence";
            put(c, path, {{"id","srd55:ability-score-improvement"},{"boosts",{{ability,2}}}}); ++asi;
        }
    }
    const auto feature = [&](const std::string& path, const Json& value) { put(c, "/features/" + cls + "/" + path, value); };
    if (cls == "barbarian" && level >= 3) feature("primalKnowledge", Json::array({"srd55:nature"}));
    if (cls == "bard") {
        if (level >= 2) feature("expertise2", Json::array({"srd55:arcana","srd55:perception"}));
        if (level >= 3) feature("loreSkills", Json::array({"srd55:history","srd55:insight","srd55:investigation"}));
        if (level >= 9) feature("expertise9", Json::array({"srd55:deception","srd55:history"}));
    }
    if (cls == "cleric") { feature("divineOrder", "protector"); if (level >= 7) feature("blessedStrikes", "divine-strike"); }
    if (cls == "druid") { feature("primalOrder", "warden"); if (level >= 3) feature("land", "arid"); if (level >= 7) feature("elementalFury", "primal-strike"); }
    if (cls == "fighter") {
        feature("fightingStyle/0", "srd55:great-weapon-fighting");
        if (level >= 7) feature("fightingStyle/1", "srd55:defense");
    }
    if (cls == "paladin" && level >= 2) feature("fightingStyle/0", "srd55:defense");
    if (cls == "ranger") {
        if (level >= 2) { feature("fightingStyle/0", "srd55:archery"); feature("expertise2", Json::array({"srd55:perception"})); feature("languages", Json::array({"srd55:elvish","srd55:gnomish"})); }
        if (level >= 3) feature("huntersPrey", "colossus-slayer");
        if (level >= 7) feature("defensiveTactics", "escape-the-horde");
        if (level >= 9) feature("expertise9", Json::array({"srd55:survival","srd55:nature"}));
    }
    if (cls == "rogue") {
        feature("expertise1", Json::array({"srd55:acrobatics","srd55:stealth"})); feature("language", "srd55:elvish");
        if (level >= 6) feature("expertise6", Json::array({"srd55:deception","srd55:insight"}));
    }
    if (cls == "sorcerer" && level >= 6) feature("elementalAffinity", "fire");
    if (cls == "warlock") {
        const std::array<std::string,10> invocations = {"eldritch-mind","armor-of-shadows","devils-sight","fiendish-vigor","mask-of-many-faces","misty-visions","whispers-of-the-grave","ascendant-step","master-of-myriad-forms","witch-sight"};
        const int count = level >= 18 ? 10 : level >= 15 ? 9 : level >= 12 ? 8 : level >= 9 ? 7 : level >= 7 ? 6 : level >= 5 ? 5 : level >= 2 ? 3 : 1;
        Json selected = Json::array(); for (int i = 0; i < count; ++i) selected.push_back("srd55:" + invocations[static_cast<std::size_t>(i)]);
        feature("invocations", selected); if (level >= 10) feature("fiendishResilience", "cold");
    }
    if (cls == "wizard" && level >= 2) feature("scholar", Json::array({"srd55:arcana"}));
}

inline dnd::CharacterDocument base(const std::string& cls, const dnd::ResolvedRuleset& ruleset, int level = 1) {
    auto document = dnd::newCharacter("srd55", "2.0.0"); document.name = "Complete " + cls + " acceptance";
    document.choices = {{"classId","srd55:"+cls},{"speciesId","srd55:dwarf"},{"backgroundId","srd55:soldier"},
        {"abilityMethod","rolled"},{"abilities",{{"strength",15},{"dexterity",15},{"constitution",15},{"intelligence",15},{"wisdom",15},{"charisma",15}}},
        {"backgroundBoosts",{{"strength",2},{"constitution",1}}},{"alignment","Neutral Good"},{"languages",{"srd55:dwarvish","srd55:draconic"}},
        {"gamingSet","srd55:dice"},{"hpMethod","fixed"},{"classEquipment","B"},{"backgroundEquipment","B"},{"armorId","none"},{"weaponId","none"},{"shield",false}};
    const std::map<std::string,Json> skills = {
        {"barbarian",{"srd55:perception","srd55:survival"}},{"bard",{"srd55:arcana","srd55:deception","srd55:perception"}},
        {"cleric",{"srd55:insight","srd55:religion"}},{"druid",{"srd55:nature","srd55:perception"}},
        {"fighter",{"srd55:perception","srd55:survival"}},{"monk",{"srd55:acrobatics","srd55:stealth"}},
        {"paladin",{"srd55:insight","srd55:persuasion"}},{"ranger",{"srd55:perception","srd55:survival","srd55:nature"}},
        {"rogue",{"srd55:acrobatics","srd55:deception","srd55:insight","srd55:stealth"}},
        {"sorcerer",{"srd55:arcana","srd55:deception"}},{"warlock",{"srd55:arcana","srd55:deception"}},{"wizard",{"srd55:arcana","srd55:history"}}};
    document.choices["classSkills"] = skills.at(cls);
    for (const auto& ability : {"strength","dexterity","constitution","intelligence","wisdom","charisma"})
        document.rolls["abilities"][ability] = {{"dice",{6,5,4,1}},{"dropLowest",1},{"total",15}};
    seedAdvancement(document, ruleset, cls, level); return document;
}

struct Result { dnd::CharacterDocument document; dnd::Evaluation evaluation; int edits = 0; std::string stopped; };
inline Result finish(dnd::CharacterDocument document, const dnd::ResolvedRuleset& ruleset) {
    Result result; result.document = std::move(document);
    const std::regex countPattern("Choose (?:exactly |at most )?([0-9]+)");
    std::set<std::string> seen;
    for (int iteration = 0; iteration < 400; ++iteration) {
        result.evaluation = dnd::evaluate(result.document, ruleset);
        if (result.evaluation.complete()) return result;
        const auto signature = result.document.choices.dump();
        if (!seen.insert(signature).second) { result.stopped = "The builder repeated a prior choice state."; return result; }
        std::set<std::string> invalid;
        std::map<std::string,int> counts;
        for (const auto& message : result.evaluation.messages) if (message.severity == "error") {
            const auto path = normalize(message.path); invalid.insert(path);
            std::smatch match; if (std::regex_search(message.text, match, countPattern)) counts[path] = std::stoi(match[1]);
        }
        bool changed = false;
        for (const auto& stage : result.evaluation.stages) {
            for (const auto& field : stage.fields) {
                if (field.scope != "choices" || !invalid.contains(field.path)) continue;
                const auto previous = at(result.document.choices, field.path);
                std::vector<dnd::Choice> available;
                for (const auto& choice : field.options) if (choice.available && !choice.id.empty()) available.push_back(choice);
                // Learn spells of the highest currently legal level, ensuring a
                // real spread of spell levels for Mastery, Signature, and Savant.
                if (field.path.find("/spellbook/") != std::string::npos || field.path.find("/savant/") != std::string::npos)
                    std::stable_sort(available.begin(), available.end(), [&](const auto& a, const auto& b) {
                        const auto* aa = ruleset.find(a.id); const auto* bb = ruleset.find(b.id);
                        return (aa ? aa->value("level",0) : 0) > (bb ? bb->value("level",0) : 0);
                    });
                Json proposed = previous;
                if (field.kind == "select" && !available.empty()) proposed = available.front().id;
                else if (field.kind == "multiselect") {
                    const int desired = counts.contains(field.path) ? counts.at(field.path) : previous.is_array() ? static_cast<int>(previous.size()) : 0;
                    if (desired < 0 || desired > 100) continue;
                    proposed = Json::array();
                    if (previous.is_array()) for (const auto& id : previous)
                        if (id.is_string() && std::any_of(available.begin(), available.end(), [&id](const auto& choice) { return choice.id == id.get<std::string>(); }) && std::find(proposed.begin(), proposed.end(), id) == proposed.end() && static_cast<int>(proposed.size()) < desired) proposed.push_back(id);
                    for (const auto& choice : available) if (static_cast<int>(proposed.size()) < desired && std::find(proposed.begin(), proposed.end(), Json(choice.id)) == proposed.end()) proposed.push_back(choice.id);
                }
                if (proposed != previous) { put(result.document.choices, field.path, proposed); ++result.edits; changed = true; break; }
            }
            if (changed) break;
        }
        if (!changed) { result.stopped = "No supported legal field edit resolves the remaining validation messages."; return result; }
    }
    result.evaluation = dnd::evaluate(result.document, ruleset); result.stopped = "Completion exceeded the bounded edit limit."; return result;
}
inline Result complete(const std::string& cls, int level, const dnd::ResolvedRuleset& ruleset) { return finish(base(cls,ruleset,level),ruleset); }
}
