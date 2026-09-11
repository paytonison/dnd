#include "srd51_internal.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>

namespace dnd::srd51 {
namespace {
void require(bool condition, const std::string& field, const std::string& text) {
    if (!condition) throw std::runtime_error(field + ": " + text);
}
void keys(const Json& object, const std::set<std::string>& allowed) {
    require(object.is_object(), "/", "Expected an object.");
    for (auto it = object.begin(); it != object.end(); ++it)
        require(allowed.contains(it.key()), "/" + it.key(), "Unsupported mechanical field.");
}
void number(const Json& value, int low, int high, const std::string& field) {
    require(value.is_number_integer() && value >= low && value <= high, field,
            "Expected a whole number from " + std::to_string(low) + " to " + std::to_string(high) + ".");
}
void text(const Json& value, const std::string& field) {
    require(value.is_string() && !value.get<std::string>().empty(), field, "Expected nonempty text.");
}
void list(const Json& value, const std::string& field, bool unique = true) {
    require(value.is_array() && value.size() <= 100, field, "Expected an array of at most 100 identifiers.");
    std::set<std::string> seen;
    for (const auto& v : value) {
        text(v, field);
        const bool fresh = seen.insert(v.get<std::string>()).second;
        require(!unique || fresh, field, "Duplicate identifier.");
    }
}
void oneOf(const Json& value, const std::set<std::string>& allowed, const std::string& field) {
    require(value.is_string() && allowed.contains(value.get<std::string>()), field, "Unsupported binding.");
}
void groups(const Json& value, bool martial) {
    require(value.is_array() && !value.empty() && value.size() <= 8, "/equipmentGroups", "Expected 1-8 groups.");
    std::set<std::string> seen;
    for (const auto& group : value) {
        keys(group, {"key", "name", "options"});
        text(group.at("key"), "/equipmentGroups/key");
        const auto key = group.at("key").get<std::string>();
        require(std::regex_match(key, std::regex("[a-z][a-z0-9-]*")) && seen.insert(key).second,
                "/equipmentGroups/key", "Expected a unique simple key.");
        text(group.at("name"), "/equipmentGroups/name");
        const auto& options = group.at("options");
        require(options.is_array() && !options.empty() && options.size() <= 10,
                "/equipmentGroups/options", "Expected 1-10 options.");
        std::set<std::string> optionIds;
        for (const auto& option : options) {
            keys(option, {"id", "name", "items", "martialWeapons"});
            text(option.at("id"), "/equipmentGroups/options/id");
            require(optionIds.insert(option.at("id").get<std::string>()).second,
                    "/equipmentGroups/options/id", "Duplicate option.");
            text(option.at("name"), "/equipmentGroups/options/name");
            list(option.at("items"), "/equipmentGroups/options/items", false);
            if (option.contains("martialWeapons"))
                number(option.at("martialWeapons"), 0, martial ? 2 : 0, "/equipmentGroups/options/martialWeapons");
        }
    }
}
void validateEntry(const Json& e) {
    const auto kind = e.at("kind").get<std::string>();
    std::set<std::string> allowed{"id", "kind", "name", "source", "replaces"};
    auto allow = [&](std::initializer_list<const char*> names) { for (auto name : names) allowed.insert(name); };
    if (kind == "class") {
        allow({"rulesProfile", "creationId", "hitDie", "fixedHp", "maxLevel", "savingThrows",
               "skills", "skillChoices", "styles", "equipmentGroups", "progression"});
        oneOf(e.at("rulesProfile"), {"fighter"}, "/rulesProfile");
        text(e.at("creationId"), "/creationId");
        number(e.at("hitDie"), 10, 10, "/hitDie");
        number(e.at("fixedHp"), 6, 6, "/fixedHp");
        number(e.at("maxLevel"), 3, 3, "/maxLevel");
        list(e.at("savingThrows"), "/savingThrows");
        for (const auto& ability : e.at("savingThrows"))
            oneOf(ability, {"strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"}, "/savingThrows");
        list(e.at("skills"), "/skills");
        number(e.at("skillChoices"), 1, static_cast<int>(e.at("skills").size()), "/skillChoices");
        list(e.at("styles"), "/styles");
        require(!e.at("styles").empty(), "/styles", "At least one fighting style is required.");
        groups(e.at("equipmentGroups"), true);
        const auto& rows = e.at("progression");
        require(rows.is_array() && rows.size() == 3, "/progression", "Exactly three supported level rows are required.");
        int previousXp = -1;
        for (int i = 0; i < 3; ++i) {
            const auto& row = rows.at(i);
            keys(row, {"level", "xp", "proficiency", "secondWind", "actionSurge"});
            number(row.at("level"), i + 1, i + 1, "/progression/level");
            number(row.at("xp"), previousXp + 1, 1000000, "/progression/xp");
            previousXp = row.at("xp").get<int>();
            number(row.at("proficiency"), 2, 2, "/progression/proficiency");
            number(row.at("secondWind"), 1, 10, "/progression/secondWind");
            number(row.at("actionSurge"), i == 0 ? 0 : 1, i == 0 ? 0 : 10, "/progression/actionSurge");
        }
    } else if (kind == "race") {
        allow({"rulesProfile", "abilityIncreases", "size", "speed", "languages", "languageChoices"});
        oneOf(e.at("rulesProfile"), {"human"}, "/rulesProfile");
        const auto& increases = e.at("abilityIncreases");
        keys(increases, {"strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"});
        for (auto ability : abilities) number(increases.at(ability), 0, 2, "/abilityIncreases/" + std::string(ability));
        oneOf(e.at("size"), {"Medium"}, "/size");
        number(e.at("speed"), 5, 60, "/speed");
        list(e.at("languages"), "/languages");
        number(e.at("languageChoices"), 0, 5, "/languageChoices");
    } else if (kind == "background") {
        allow({"rulesProfile", "skills", "languageChoices", "goldCp", "items", "equipmentGroups", "description"});
        oneOf(e.at("rulesProfile"), {"acolyte"}, "/rulesProfile");
        list(e.at("skills"), "/skills");
        require(e.at("skills").size() == 2, "/skills", "Exactly two background skills are supported.");
        number(e.at("languageChoices"), 0, 5, "/languageChoices");
        number(e.at("goldCp"), 0, 1000000, "/goldCp");
        list(e.at("items"), "/items", false);
        groups(e.at("equipmentGroups"), false);
        text(e.at("description"), "/description");
    } else if (kind == "subclass") {
        allow({"rulesProfile", "classId", "level", "criticalMinimum"});
        oneOf(e.at("rulesProfile"), {"champion"}, "/rulesProfile");
        text(e.at("classId"), "/classId");
        number(e.at("level"), 3, 3, "/level");
        number(e.at("criticalMinimum"), 19, 19, "/criticalMinimum");
    } else if (kind == "fighting-style") {
        allow({"rulesProfile", "description"});
        oneOf(e.at("rulesProfile"), {"archery", "defense", "dueling", "great-weapon-fighting", "protection", "two-weapon-fighting"}, "/rulesProfile");
        text(e.at("description"), "/description");
    } else if (kind == "table") {
        allow({"rulesProfile", "standardArray", "pointCosts", "pointBudget"});
        oneOf(e.at("rulesProfile"), {"creation"}, "/rulesProfile");
        require(e.at("standardArray").is_array() && e.at("standardArray").size() == 6,
                "/standardArray", "Expected six ability scores.");
        for (const auto& v : e.at("standardArray")) number(v, 3, 18, "/standardArray");
        require(e.at("pointCosts").is_array() && e.at("pointCosts").size() == 8,
                "/pointCosts", "Expected eight costs for scores 8-15.");
        for (const auto& v : e.at("pointCosts")) number(v, 0, 27, "/pointCosts");
        number(e.at("pointBudget"), 0, 162, "/pointBudget");
    } else if (kind == "skill") {
        allow({"ability"});
        oneOf(e.at("ability"), {"strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"}, "/ability");
    } else if (kind == "armor") {
        allow({"category", "baseAc", "costCp", "strength", "stealthDisadvantage"});
        oneOf(e.at("category"), {"light", "medium", "heavy"}, "/category");
        number(e.at("baseAc"), 10, 20, "/baseAc");
        number(e.at("strength"), 0, 20, "/strength");
        require(e.at("category") == "heavy" || e.at("strength") == 0, "/strength", "Only heavy armor has a Strength speed threshold.");
        require(e.at("stealthDisadvantage").is_boolean(), "/stealthDisadvantage", "Expected true or false.");
    } else if (kind == "shield") {
        allow({"bonusAc", "costCp"});
        number(e.at("bonusAc"), 1, 5, "/bonusAc");
    } else if (kind == "weapon") {
        allow({"category", "mode", "damage", "damageType", "costCp", "properties", "versatile", "range", "ammunitionId"});
        oneOf(e.at("category"), {"simple", "martial"}, "/category");
        oneOf(e.at("mode"), {"melee", "ranged"}, "/mode");
        oneOf(e.at("damageType"), {"bludgeoning", "piercing", "slashing"}, "/damageType");
        auto dice = [&](const Json& v, const std::string& field) {
            require(v.is_string() && std::regex_match(v.get<std::string>(), std::regex("1|[1-4]d(4|6|8|10|12)")), field, "Unsupported damage dice.");
        };
        dice(e.at("damage"), "/damage");
        list(e.at("properties"), "/properties");
        for (const auto& property : e.at("properties"))
            oneOf(property, {"light", "finesse", "thrown", "ammunition", "loading", "two-handed", "heavy", "reach"}, "/properties");
        auto has = [&](const char* property) { return std::find(e.at("properties").begin(), e.at("properties").end(), Json(property)) != e.at("properties").end(); };
        if (e.contains("versatile")) {
            dice(e.at("versatile"), "/versatile");
            require(e.at("mode") == "melee" && !has("two-handed"), "/versatile", "Requires a one-handed melee profile.");
        }
        require(e.contains("range") == (e.at("mode") == "ranged" || has("thrown")), "/range", "Range must match the ranged/thrown profile.");
        if (e.contains("range")) {
            require(e.at("range").is_array() && e.at("range").size() == 2, "/range", "Expected normal and long ranges.");
            number(e.at("range").at(0), 5, 600, "/range/0");
            number(e.at("range").at(1), e.at("range").at(0).get<int>(), 1200, "/range/1");
        }
        require(e.contains("ammunitionId") == has("ammunition"), "/ammunitionId", "Ammunition binding must match its property.");
        if (has("ammunition")) {
            text(e.at("ammunitionId"), "/ammunitionId");
            require(e.at("mode") == "ranged" && !has("thrown"), "/properties", "Unsupported ammunition profile.");
        }
    } else if (kind == "gear") {
        allow({"description", "costCp", "purchasable", "purchaseQuantity", "contentsSummary"});
        text(e.at("description"), "/description");
        require(e.at("purchasable").is_boolean(), "/purchasable", "Expected true or false.");
        if (e.contains("purchaseQuantity")) number(e.at("purchaseQuantity"), 1, 100, "/purchaseQuantity");
        if (e.contains("contentsSummary")) text(e.at("contentsSummary"), "/contentsSummary");
    } else require(kind == "language", "/kind", "Unsupported content kind.");
    if (kind == "weapon" || kind == "armor" || kind == "shield" || kind == "gear")
        number(e.at("costCp"), 0, 1000000, "/costCp");
    keys(e, allowed);
}
} // namespace

std::vector<Message> validateContent(const ContentPack& pack) {
    std::vector<Message> messages;
    for (const auto& e : pack.entries) {
        const auto id = e.is_object() && e.contains("id") && e.at("id").is_string() ? e.at("id").get<std::string>() : "entry";
        try { validateEntry(e); }
        catch (const std::exception& error) {
            messages.push_back({"error", "srd51.content.mechanics", "/packs/" + pack.manifest.value("id", "unknown") + "/" + id,
                                error.what(), {}});
        }
    }
    return messages;
}
std::vector<Message> validateRuleset(const ResolvedRuleset& rules) {
    std::vector<Message> messages;
    for (const auto& [id, e] : rules.content) {
        auto check = [&](const std::string& target, const std::string& kind, const std::string& field,
                         const std::string& profile = "") {
            const auto* entry = rules.find(target);
            const auto actual = entry ? entry->value("kind", "") : "";
            const bool equipment = actual == "weapon" || actual == "armor" || actual == "shield" || actual == "gear";
            if (!entry || (kind == "equipment" ? !equipment : actual != kind) ||
                (!profile.empty() && entry->value("rulesProfile", "") != profile))
                messages.push_back({"error", "srd51.content.reference", id + "/" + field,
                                    "Required " + kind + " reference '" + target + "' is missing or incompatible.", {}});
        };
        for (const auto& [field, kind] : std::vector<std::pair<std::string, std::string>>{
                 {"skills", "skill"}, {"styles", "fighting-style"}, {"languages", "language"}, {"items", "equipment"}})
            if (e.contains(field)) for (const auto& target : e.at(field)) check(target, kind, field);
        if (e.contains("classId")) check(e.at("classId"), "class", "classId", "fighter");
        if (e.contains("creationId")) check(e.at("creationId"), "table", "creationId", "creation");
        if (e.contains("ammunitionId")) check(e.at("ammunitionId"), "gear", "ammunitionId");
        if (e.contains("equipmentGroups")) for (const auto& group : e.at("equipmentGroups"))
            for (const auto& option : group.at("options")) for (const auto& target : option.at("items"))
                check(target, "equipment", "equipmentGroups/" + group.at("key").get<std::string>());
    }
    return messages;
}
} // namespace dnd::srd51
