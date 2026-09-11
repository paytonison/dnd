#include "srd51_internal.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace dnd::srd51 {
namespace {
const Json* at(const Json& value, const std::string& path) {
    const Json::json_pointer pointer(path);
    return value.contains(pointer) ? &value.at(pointer) : nullptr;
}
std::string str(const Json& value, const std::string& path, const std::string& fallback = {}) {
    const auto* v = at(value, path);
    return v && v->is_string() ? v->get<std::string>() : fallback;
}
int num(const Json& value, const std::string& path, int fallback = 0) {
    const auto* v = at(value, path);
    return v && v->is_number_integer() && *v >= -1000000 && *v <= 1000000 ? v->get<int>() : fallback;
}
bool has(const Json& list, const std::string& value) {
    return list.is_array() && std::find(list.begin(), list.end(), Json(value)) != list.end();
}
std::string title(std::string value) {
    if (!value.empty()) value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    return value;
}
std::string sign(int n) { return (n >= 0 ? "+" : "") + std::to_string(n); }
void issue(Evaluation& e, const std::string& code, const std::string& path, const std::string& text,
           const std::string& severity = "error") {
    e.messages.push_back({severity, "srd51." + code, path, text, {}});
}
Field integer(const std::string& path, const std::string& label, int low, int high) {
    return {path, label, "integer", low, high, {}, false, {}};
}
Field select(const std::string& path, const std::string& label, std::vector<Choice> choices, bool multi = false) {
    return {path, label, multi ? "multiselect" : "select", 0, 100, std::move(choices), false, {}};
}
Choice literal(const std::string& id, const std::string& label) { return {id, label, true, {}, {}}; }
bool subclassFor(const ResolvedRuleset& rules, const Json& subclass, const Json* cls) {
    const auto* parent = rules.find(subclass.at("classId"));
    return cls && parent && parent->value("rulesProfile", "") == cls->value("rulesProfile", "");
}
std::vector<Choice> options(const ResolvedRuleset& rules, const std::string& kind,
                            const std::function<bool(const Json&)>& filter = {}) {
    std::vector<Choice> choices;
    for (const auto& [id, entry] : rules.content)
        if (entry.at("kind") == kind && (!filter || filter(entry)))
            choices.push_back({id, entry.at("name"), true, {}, {sourceFromJson(entry.at("source"))}});
    std::sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) { return a.label < b.label; });
    return choices;
}
bool number(Evaluation& e, const Json& input, const std::string& path, int low, int high, bool required = true) {
    const auto* v = at(input, path);
    if (!v && !required) return true;
    if (!v || !v->is_number_integer() || *v < low || *v > high) {
        issue(e, "number", path, "Enter a whole number from " + std::to_string(low) + " to " + std::to_string(high) + ".");
        return false;
    }
    return true;
}
std::string pick(Evaluation& e, const Json& input, const Field& field, const std::string& fallback = {}) {
    const auto* v = at(input, field.path);
    const auto value = str(input, field.path, fallback);
    if (v && !v->is_string()) issue(e, "choice.type", field.path, "This choice must be a text identifier; its value is preserved.");
    auto match = std::find_if(field.options.begin(), field.options.end(), [&](const auto& o) { return o.id == value; });
    if (match == field.options.end()) {
        issue(e, "choice.unavailable", field.path, value.empty() ? "Choose " + field.label + "." : "Saved choice '" + value + "' is unavailable and preserved for correction.");
        return {};
    }
    if (!match->available) issue(e, "choice.ineligible", field.path, match->reason);
    return value;
}
std::vector<std::string> picks(Evaluation& e, const Json& input, const Field& field, int count, bool exact = true) {
    std::vector<std::string> result;
    const auto* raw = at(input, field.path);
    if (raw && !raw->is_array()) issue(e, "choice.type", field.path, "Expected a list of choices; the saved value is preserved.");
    if (raw && raw->is_array()) for (std::size_t i = 0; i < raw->size(); ++i) {
        auto one = field;
        one.path += "/" + std::to_string(i);
        const auto id = pick(e, input, one);
        if (!id.empty()) result.push_back(id);
    }
    if ((exact && static_cast<int>(result.size()) != count) || (!exact && static_cast<int>(result.size()) > count))
        issue(e, "choice.count", field.path, "Choose " + std::string(exact ? "exactly " : "at most ") + std::to_string(count) + " for " + field.label + ".");
    if (std::set<std::string>(result.begin(), result.end()).size() != result.size())
        issue(e, "choice.duplicate", field.path, "Choose distinct entries.");
    return result;
}
void record(Evaluation& e, SheetSection& section, const std::string& id, const std::string& label,
            Json value, std::vector<std::string> steps, std::vector<SourceRef> refs) {
    addCalculation(e, id, label, std::move(value), std::move(steps), std::move(refs));
    section.calculationIds.push_back(id);
}

Evaluation run(const CharacterDocument& d, const ResolvedRuleset& rules) {
    Evaluation e;
    const auto& c = d.choices;
    issue(e, "experimental", "/edition", "5E (2014): Human Fighter 1-3, Champion, Acolyte, ordinary starting equipment. Other races/classes, level 4+, feats, spellcasting, multiclassing, custom backgrounds, exotic languages, inventory transactions and encumbrance remain unsupported.", "info");
    const std::set<std::string> allowed{"classId", "raceId", "backgroundId", "level", "xp", "alignment", "languages", "abilityMethod", "abilities", "classSkills", "replacementSkills", "fightingStyle", "subclassId", "hp", "hpMethod", "startingEquipment", "backgroundEquipment", "startingWeapons", "purchases", "armorId", "shieldId", "weaponId", "offhandId", "twoHands", "weaponAbility", "offhandAbility", "options", "deity", "personality", "ideal", "bond", "flaw"};
    if (!c.is_object()) { issue(e, "shape", "/choices", "Character choices must be an object."); return e; }
    for (auto it = c.begin(); it != c.end(); ++it)
        if (!allowed.contains(it.key())) issue(e, "unsupported", "/" + it.key(), "This input is not supported by the 5E module; it is preserved and cannot be treated as implemented.");
    auto objectKeys = [&](const std::string& path, const std::set<std::string>& known) {
        const auto* object = at(c, path);
        if (object && object->is_object()) for (auto it = object->begin(); it != object->end(); ++it)
            if (!known.contains(it.key())) issue(e, "unsupported", path + "/" + it.key(), "This saved field is unsupported and preserved for correction.");
    };
    objectKeys("/abilities", {"strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"});
    if (c.contains("options") && c.at("options").is_object())
        for (auto it = c.at("options").begin(); it != c.at("options").end(); ++it)
            if (it.key() != "allowPointBuy" && it.value() != Json(false))
                issue(e, "unsupported", "/options/" + it.key(), "This active or malformed option is unsupported. Disabled boolean options are preserved without an effect.");
    for (const auto* key : {"abilities", "hp", "startingEquipment", "backgroundEquipment", "startingWeapons", "options"})
        if (c.contains(key) && !c.at(key).is_object()) issue(e, "shape", "/" + std::string(key), "Expected an object; the saved value is preserved.");
    if (c.contains("twoHands") && !c.at("twoHands").is_boolean()) issue(e, "choice.type", "/twoHands", "Use true or false.");
    const auto campaign = effectiveCampaignOptions(d);
    const bool pointBuy = campaign.value("allowPointBuy", Json(false)) == Json(true);
    if (campaign.contains("allowPointBuy") && !campaign.at("allowPointBuy").is_boolean()) issue(e, "option.type", "/options/allowPointBuy", "Point-buy permission must be true or false.");
    Stage identity{"identity", "Class, race and background", {
        select("/classId", "Class", options(rules, "class")),
        select("/raceId", "Race", options(rules, "race")),
        select("/backgroundId", "Background", options(rules, "background")),
        integer("/level", "Level (1-3 supported)", 1, 3), integer("/xp", "Experience points", 0, 1000000)}};
    const auto classId = pick(e, c, identity.fields[0]);
    const auto raceId = pick(e, c, identity.fields[1]);
    const auto backgroundId = pick(e, c, identity.fields[2]);
    number(e, c, "/level", 1, 3, false);
    number(e, c, "/xp", 0, 1000000, false);
    const int level = std::clamp(num(c, "/level", 1), 1, 3);
    const auto* cls = rules.find(classId);
    const auto* race = rules.find(raceId);
    const auto* background = rules.find(backgroundId);
    for (const auto* key : {"subclassId", "weaponAbility", "offhandAbility"}) {
        const auto* value = at(c, "/" + std::string(key));
        if (value && !value->is_string()) issue(e, "choice.type", "/" + std::string(key), "This saved selection must be a text identifier.");
    }
    for (const auto* key : {"weaponAbility", "offhandAbility"}) if (c.contains(key) &&
        str(c, "/" + std::string(key)) != "strength" && str(c, "/" + std::string(key)) != "dexterity")
        issue(e, "choice.unavailable", "/" + std::string(key), "Choose Strength or Dexterity; the saved value is preserved.");
    std::vector<Choice> alignments;
    for (const auto* name : {"Lawful Good", "Neutral Good", "Chaotic Good", "Lawful Neutral", "Neutral", "Chaotic Neutral", "Lawful Evil", "Neutral Evil", "Chaotic Evil"}) alignments.push_back(literal(name, name));
    identity.fields.push_back(select("/alignment", "Alignment", alignments));
    const auto alignment = pick(e, c, identity.fields.back());
    if (cls && c.contains("xp") && num(c, "/xp") < cls->at("progression").at(level - 1).at("xp").get<int>())
        issue(e, "xp", "/xp", "The recorded XP is below this level's published threshold.");
    if (level == 3) {
        identity.fields.push_back(select("/subclassId", "Martial archetype", options(rules, "subclass", [&](const Json& v) { return subclassFor(rules, v, cls); })));
        pick(e, c, identity.fields.back());
    } else if (c.contains("subclassId") && !str(c, "/subclassId").empty())
        issue(e, "subclass.level", "/subclassId", "The martial archetype becomes available at Fighter level 3.");
    e.stages.push_back(identity);

    auto methods = std::vector<Choice>{literal("standard-array", "Standard array"), literal("rolled", "Accepted 4d6, drop lowest"), literal("point-buy", "27-point buy (DM option)")};
    methods.back().available = pointBuy;
    methods.back().reason = pointBuy ? "" : "Enable point buy with your DM's permission in Advanced mode.";
    Stage scores{"abilities", "Ability scores", {select("/abilityMethod", "Generation method", methods)}};
    Field permission{"/options/allowPointBuy", "DM permits point buy", "boolean", 0, 1, {}, true, "An absent character setting inherits the campaign setting; false overrides it."};
    scores.fields.push_back(permission);
    const auto method = pick(e, c, scores.fields[0], "standard-array");
    bool ready = cls && race && background;
    std::map<std::string, int> totals, mods;
    std::vector<int> bases;
    SheetSection abilitySheet{"Ability scores", {}, {}};
    const auto* creation = cls ? rules.find(cls->at("creationId")) : nullptr;
    int points = 0;
    for (const auto* ability : abilities) {
        const auto path = "/abilities/" + std::string(ability);
        const int low = method == "point-buy" ? 8 : 3;
        const int high = method == "point-buy" ? 15 : 18;
        scores.fields.push_back(integer(path, title(ability) + " before racial increase", low, high));
        const bool valid = number(e, c, path, low, high);
        ready = ready && valid;
        const int base = num(c, path);
        bases.push_back(base);
        const int boost = race ? race->at("abilityIncreases").at(ability).get<int>() : 0;
        totals[ability] = base + boost;
        mods[ability] = static_cast<int>(std::floor((totals[ability] - 10) / 2.0));
        if (valid && race) {
            record(e, abilitySheet, "ability." + std::string(ability), title(ability), totals[ability],
                   {"Accepted base " + std::to_string(base) + " + " + race->at("name").get<std::string>() + " " + std::to_string(boost) + " = " + std::to_string(totals[ability])}, {sourceFromJson(race->at("source"))});
            record(e, abilitySheet, "ability." + std::string(ability) + ".mod", title(ability) + " modifier", mods[ability],
                   {"floor((" + std::to_string(totals[ability]) + " - 10) / 2) = " + std::to_string(mods[ability])}, {sourceFromJson(race->at("source")), ref("76")});
        }
        if (valid && method == "point-buy" && creation) points += creation->at("pointCosts").at(base - 8).get<int>();
        if (method == "rolled") e.rollRequests.push_back({"ability." + std::string(ability), title(ability), path, "abilities", 6, 4, 1});
    }
    if (ready && creation) {
        if (method == "standard-array") {
            auto expected = creation->at("standardArray").get<std::vector<int>>();
            std::sort(bases.begin(), bases.end()); std::sort(expected.begin(), expected.end());
            if (bases != expected) issue(e, "ability.array", "/abilities", "Assign each number in the standard array exactly once.");
        }
        if (method == "point-buy" && points != creation->at("pointBudget").get<int>()) issue(e, "ability.points", "/abilities", "Ability scores cost " + std::to_string(points) + " points; the budget is " + creation->at("pointBudget").dump() + ".");
    }
    e.stages.push_back(scores);
    if (!abilitySheet.calculationIds.empty()) e.sections.push_back(abilitySheet);
    if (!cls || !race || !background) return e;
    const auto classRef = sourceFromJson(cls->at("source")), raceRef = sourceFromJson(race->at("source")), backgroundRef = sourceFromJson(background->at("source"));
    const auto& row = cls->at("progression").at(level - 1);
    const int proficiency = row.at("proficiency").get<int>();
    Stage training{"training", "Proficiencies and fighting style", {}};
    training.fields.push_back(select("/classSkills", "Class skills", options(rules, "skill", [&](const Json& v) { return has(cls->at("skills"), v.at("id")); }), true));
    auto skills = picks(e, c, training.fields.back(), cls->at("skillChoices"));
    std::set<std::string> proficient(skills.begin(), skills.end());
    int duplicates = 0;
    for (const auto& v : background->at("skills")) if (!proficient.insert(v.get<std::string>()).second) ++duplicates;
    training.fields.push_back(select("/replacementSkills", "Replace duplicated skill proficiencies", options(rules, "skill", [&](const Json& v) { return !proficient.contains(v.at("id")); }), true));
    for (const auto& skill : picks(e, c, training.fields.back(), duplicates)) proficient.insert(skill);
    training.fields.push_back(select("/fightingStyle", "Fighting style", options(rules, "fighting-style", [&](const Json& v) { return has(cls->at("styles"), v.at("id")); })));
    const auto* style = rules.find(pick(e, c, training.fields.back()));
    const auto profile = style ? style->at("rulesProfile").get<std::string>() : "";
    const auto styleRef = style ? sourceFromJson(style->at("source")) : classRef;
    training.fields.push_back(select("/languages", "Additional standard languages", options(rules, "language", [&](const Json& v) { return !has(race->at("languages"), v.at("id")); }), true));
    const auto languages = picks(e, c, training.fields.back(), race->at("languageChoices").get<int>() + background->at("languageChoices").get<int>());
    e.stages.push_back(training);

    Stage equipment{"equipment", "Starting equipment and loadout", {}};
    std::map<std::string, int> owned;
    auto grant = [&](const Json& items) { for (const auto& v : items) ++owned[v.get<std::string>()]; };
    grant(background->at("items"));
    int weaponCount = 0;
    auto equipmentGroups = [&](const Json& owner, const std::string& prefix) {
        std::set<std::string> known;
        for (const auto& group : owner.at("equipmentGroups")) {
            known.insert(group.at("key").get<std::string>());
            std::vector<Choice> choices;
            for (const auto& option : group.at("options")) choices.push_back({option.at("id"), option.at("name"), true, {}, {sourceFromJson(owner.at("source"))}});
            equipment.fields.push_back(select(prefix + "/" + group.at("key").get<std::string>(), group.at("name"), choices));
            const auto chosen = pick(e, c, equipment.fields.back());
            for (const auto& option : group.at("options")) if (option.at("id") == chosen) {
                grant(option.at("items")); weaponCount += option.value("martialWeapons", 0);
            }
        }
        objectKeys(prefix, known);
    };
    equipmentGroups(*cls, "/startingEquipment"); equipmentGroups(*background, "/backgroundEquipment");
    int possibleWeaponCount = 0;
    for (const auto& group : cls->at("equipmentGroups")) {
        int maximum = 0;
        for (const auto& option : group.at("options")) maximum = std::max(maximum, option.value("martialWeapons", 0));
        possibleWeaponCount += maximum;
    }
    std::set<std::string> weaponKeys;
    for (int i = 0; i < possibleWeaponCount; ++i) {
        weaponKeys.insert("weapon" + std::to_string(i + 1));
        if (i >= weaponCount && at(c, "/startingWeapons/weapon" + std::to_string(i + 1)))
            pick(e, c, select("/startingWeapons/weapon" + std::to_string(i + 1), "Preserved starting weapon", options(rules, "weapon", [](const Json& v) { return v.at("category") == "martial"; })));
    }
    objectKeys("/startingWeapons", weaponKeys);
    for (int i = 0; i < weaponCount; ++i) {
        equipment.fields.push_back(select("/startingWeapons/weapon" + std::to_string(i + 1), "Martial weapon " + std::to_string(i + 1), options(rules, "weapon", [](const Json& v) { return v.at("category") == "martial"; })));
        const auto id = pick(e, c, equipment.fields.back()); if (!id.empty()) ++owned[id];
    }
    auto purchasable = [](const Json& v) { return v.value("costCp", 0) > 0 && v.value("purchasable", true); };
    std::vector<Choice> shopping;
    for (const auto* kind : {"weapon", "armor", "shield", "gear"}) {
        const auto items = options(rules, kind, purchasable); shopping.insert(shopping.end(), items.begin(), items.end());
    }
    for (auto& option : shopping) {
        const auto* item = rules.find(option.id);
        option.label = std::to_string(item->value("purchaseQuantity", 1)) + " x " + option.label + " (" + item->at("costCp").dump() + " cp)";
    }
    equipment.fields.push_back(select("/purchases", "Extra purchases (one of each)", shopping, true));
    int cost = 0;
    for (const auto& item : picks(e, c, equipment.fields.back(), 100, false)) {
        cost += rules.find(item)->at("costCp").get<int>(); owned[item] += rules.find(item)->value("purchaseQuantity", 1);
    }
    const int coins = background->at("goldCp").get<int>() - cost;
    if (coins < 0) issue(e, "equipment.budget", "/purchases", "Purchases exceed the background's starting money by " + std::to_string(-coins) + " cp.");
    auto ownedOptions = [&](const std::string& kind, bool optional) {
        auto opts = options(rules, kind);
        for (auto& o : opts) if (owned[o.id] == 0) { o.available = false; o.reason = "This item is not in the selected starting equipment or purchases."; }
        if (optional) opts.insert(opts.begin(), literal("none", "None"));
        return opts;
    };
    equipment.fields.push_back(select("/armorId", "Worn armor", ownedOptions("armor", true)));
    const auto* armor = rules.find(pick(e, c, equipment.fields.back(), "none"));
    equipment.fields.push_back(select("/shieldId", "Wielded shield", ownedOptions("shield", true)));
    const auto* shield = rules.find(pick(e, c, equipment.fields.back(), "none"));
    equipment.fields.push_back(select("/weaponId", "Wielded weapon", ownedOptions("weapon", true)));
    const auto* weapon = rules.find(pick(e, c, equipment.fields.back(), "none"));
    equipment.fields.push_back(select("/offhandId", "Second wielded weapon", ownedOptions("weapon", true)));
    const auto* offhand = rules.find(pick(e, c, equipment.fields.back(), "none"));
    const bool twoHands = c.value("twoHands", Json(false)) == Json(true);
    equipment.fields.push_back({"/twoHands", "Use versatile weapon in two hands", "boolean", 0, 1, {}, false, {}});
    auto property = [](const Json* item, const std::string& p) { return item && has(item->at("properties"), p); };
    const bool bothHands = property(weapon, "two-handed") || twoHands;
    if (twoHands && (!weapon || !weapon->contains("versatile"))) issue(e, "hands", "/twoHands", "Choose a versatile melee weapon to use this option.");
    if ((bothHands && (shield || offhand)) || (shield && offhand)) issue(e, "hands", "/shieldId", "The selected loadout needs more than two hands.");
    if (offhand && (!weapon || !property(weapon, "light") || weapon->at("mode") != "melee" || !property(offhand, "light") || offhand->at("mode") != "melee")) issue(e, "two-weapon", "/offhandId", "Two-weapon fighting requires two light melee weapons in this slice.");
    if (weapon && offhand && weapon->at("id") == offhand->at("id") && owned[weapon->at("id")] < 2)
        issue(e, "ownership", "/offhandId", "Two separately owned weapons are required; one item cannot occupy both hands.");
    if (weapon && weapon->contains("ammunitionId")) {
        if (!owned[weapon->at("ammunitionId")]) issue(e, "ammunition", "/weaponId", "This ranged weapon needs its matching ammunition.");
        if (shield || offhand) issue(e, "hands", "/weaponId", "Ammunition weapons require a free loading hand.");
    }
    std::map<std::string, std::string> attackAbilities;
    for (const auto& [key, item] : std::vector<std::pair<std::string, const Json*>>{{"weapon", weapon}, {"offhand", offhand}}) if (item) {
        std::vector<Choice> choices;
        const std::string normal = item->at("mode") == "ranged" ? "dexterity" : "strength";
        choices.push_back(literal(normal, title(normal)));
        if (property(item, "finesse")) choices.push_back(literal(normal == "strength" ? "dexterity" : "strength", normal == "strength" ? "Dexterity" : "Strength"));
        equipment.fields.push_back(select("/" + key + "Ability", title(key) + " attack ability", choices));
        const auto better = property(item, "finesse") && mods["strength"] > mods["dexterity"] ? "strength" : property(item, "finesse") ? "dexterity" : normal;
        attackAbilities[key] = pick(e, c, equipment.fields.back(), better);
    }
    e.stages.push_back(equipment);
    Stage hpStage{"advancement", "Hit points and advancement", {select("/hpMethod", "Future Hit Points", {literal("fixed", "Fixed class amount"), literal("rolled", "Accepted Hit Die rolls")})}};
    const auto hpMethod = pick(e, c, hpStage.fields.front(), "fixed");
    // Stored accepted results take precedence even if the future method changes.
    int hp = cls->at("hitDie").get<int>() + mods["constitution"];
    std::vector<std::string> hpSteps{"Level 1: " + cls->at("hitDie").dump() + " " + sign(mods["constitution"]) + " Constitution = " + std::to_string(hp)};
    if (c.contains("hp") && c.at("hp").is_object()) for (auto it = c.at("hp").begin(); it != c.at("hp").end(); ++it) {
        if (it.key() != "level2" && it.key() != "level3") issue(e, "hp.level", "/hp/" + it.key(), "Only accepted level-2 and level-3 Hit Dice belong here.");
        else number(e, c, "/hp/" + it.key(), 1, cls->at("hitDie"));
    }
    for (int gained = 2; gained <= level; ++gained) {
        const auto path = "/hp/level" + std::to_string(gained);
        const bool accepted = at(c, path);
        int base = accepted ? num(c, path) : cls->at("fixedHp").get<int>();
        if (hpMethod == "rolled" || accepted) {
            hpStage.fields.push_back(integer(path, "Accepted Hit Die at level " + std::to_string(gained), 1, cls->at("hitDie")));
            if (!accepted) number(e, c, path, 1, cls->at("hitDie"));
            e.rollRequests.push_back({"hp." + std::to_string(gained), "Level " + std::to_string(gained) + " Hit Die", path, "hitPoints", cls->at("hitDie"), 1, 0});
        }
        const int gain = std::max(1, base + mods["constitution"]);
        hp += gain;
        hpSteps.push_back("Level " + std::to_string(gained) + ": max(1, " + std::to_string(base) + " " + sign(mods["constitution"]) + ") = " + std::to_string(gain) + (accepted ? " (accepted input)" : " (fixed class amount)"));
    }
    hpSteps.push_back("Total maximum HP = " + std::to_string(hp));
    e.stages.push_back(hpStage);
    Stage story{"story", "Personality and faith", {}};
    for (const auto* key : {"deity", "personality", "ideal", "bond", "flaw"}) {
        story.fields.push_back({"/" + std::string(key), title(key), "text", 0, 100, {}, false, {}});
        if (c.contains(key) && !c.at(key).is_string()) issue(e, "choice.type", "/" + std::string(key), "Enter text.");
    }
    e.stages.push_back(story);
    if (!ready) return e;

    SheetSection summary{"Character", {}, {}};
    record(e, summary, "identity", "Class, race and background", cls->at("name").get<std::string>() + " " + std::to_string(level) + " / " + race->at("name").get<std::string>() + " / " + background->at("name").get<std::string>(), {"Selected content identities are preserved in this 5E character."}, {classRef, raceRef, backgroundRef});
    const auto* selectedSubclass = level == 3 ? rules.find(str(c, "/subclassId")) : nullptr;
    if (selectedSubclass && selectedSubclass->at("kind") == "subclass" && subclassFor(rules, *selectedSubclass, cls))
        record(e, summary, "subclass", "Martial archetype", selectedSubclass->at("name"),
               {"Selected at Fighter level 3."}, {sourceFromJson(selectedSubclass->at("source"))});
    record(e, summary, "alignment", "Alignment", alignment, {"Chosen alignment."}, {ref("59")});
    Json languageNames = Json::array();
    for (const auto& id : race->at("languages")) languageNames.push_back(rules.find(id)->at("name"));
    for (const auto& id : languages) languageNames.push_back(rules.find(id)->at("name"));
    record(e, summary, "languages", "Languages", languageNames, {race->at("name").get<std::string>() + " grants its default languages and " + race->at("languageChoices").dump() + " choice; " + background->at("name").get<std::string>() + " grants " + background->at("languageChoices").dump() + " choices."}, {raceRef, backgroundRef, ref("59")});
    e.sections.insert(e.sections.begin(), summary);
    SheetSection combat{"Defense and attacks", {}, {}};
    record(e, combat, "proficiency", "Proficiency bonus", proficiency, {"Fighter progression at level " + std::to_string(level) + " = " + std::to_string(proficiency)}, {classRef});
    record(e, combat, "hp.maximum", "Maximum hit points", hp, hpSteps, {classRef, raceRef, ref("56"), {"D&D Basic Rules (2018), 2014 rules", "12", "https://media.wizards.com/2018/dnd/downloads/DnD_BasicRules_2018.pdf#page=12"}});
    int acBase = armor ? armor->at("baseAc").get<int>() : 10;
    const std::string armorType = armor ? armor->at("category").get<std::string>() : "none";
    const int dex = armorType == "heavy" ? 0 : armorType == "medium" ? std::min(2, mods["dexterity"]) : mods["dexterity"];
    const int shieldBonus = shield ? shield->at("bonusAc").get<int>() : 0;
    const int defense = armor && profile == "defense" ? 1 : 0;
    std::vector<SourceRef> acRefs{raceRef, ref("63")};
    if (armor) acRefs.push_back(sourceFromJson(armor->at("source")));
    if (shield) acRefs.push_back(sourceFromJson(shield->at("source")));
    if (defense) acRefs.push_back(styleRef);
    record(e, combat, "armorClass", "Armor Class", acBase + dex + shieldBonus + defense,
           {(armor ? armor->at("name").get<std::string>() : "Unarmored") + " base " + std::to_string(acBase),
            "Dexterity modifier " + std::to_string(mods["dexterity"]) + "; " + armorType + " armor contribution = " + std::to_string(dex),
            "Shield " + std::to_string(shieldBonus) + "; Defense style " + std::to_string(defense),
            std::to_string(acBase) + " + (" + std::to_string(dex) + ") + " + std::to_string(shieldBonus) + " + " + std::to_string(defense) + " = " + std::to_string(acBase + dex + shieldBonus + defense)}, acRefs);
    const int penalty = armor && totals["strength"] < armor->at("strength").get<int>() ? 10 : 0;
    auto speedRefs = std::vector<SourceRef>{raceRef};
    if (penalty) { speedRefs.push_back(sourceFromJson(armor->at("source"))); speedRefs.push_back(ref("63")); }
    record(e, combat, "speed", "Walking speed (feet)", race->at("speed").get<int>() - penalty,
           {race->at("name").get<std::string>() + " speed " + race->at("speed").dump() + " - heavy armor penalty " + std::to_string(penalty) + " = " + std::to_string(race->at("speed").get<int>() - penalty), "Strength " + std::to_string(totals["strength"]) + "; armor threshold " + (armor ? armor->at("strength").dump() : "0")}, speedRefs);
    record(e, combat, "initiative", "Initiative", mods["dexterity"], {"Dexterity " + std::to_string(totals["dexterity"]) + ": floor((" + std::to_string(totals["dexterity"]) + " - 10) / 2) = " + std::to_string(mods["dexterity"])}, {raceRef, ref("80")});
    if (armor && armor->at("stealthDisadvantage") == true) combat.notes.push_back(armor->at("name").get<std::string>() + ": disadvantage on Dexterity (Stealth) checks.");
    for (const auto& [key, item] : std::vector<std::pair<std::string, const Json*>>{{"weapon", weapon}, {"offhand", offhand}}) if (item) {
        const int ability = mods[attackAbilities[key]];
        const int archery = profile == "archery" && item->at("mode") == "ranged" ? 2 : 0;
        const int dueling = key == "weapon" && profile == "dueling" && !offhand && !bothHands && item->at("mode") == "melee" ? 2 : 0;
        const int damageMod = key == "offhand" && profile != "two-weapon-fighting" ? std::min(0, ability) : ability;
        const std::string dice = key == "weapon" && twoHands && item->contains("versatile") ? item->at("versatile").get<std::string>() : item->at("damage").get<std::string>();
        auto refs = std::vector<SourceRef>{sourceFromJson(item->at("source")), classRef, raceRef, ref("94")};
        if (archery || dueling || (key == "offhand" && profile == "two-weapon-fighting")) refs.push_back(styleRef);
        record(e, combat, "attack." + key, item->at("name").get<std::string>() + " attack", ability + proficiency + archery,
               {title(attackAbilities[key]) + " " + std::to_string(totals[attackAbilities[key]]) + " gives " + sign(ability), "Ability " + std::to_string(ability) + " + proficiency " + std::to_string(proficiency) + " + Archery " + std::to_string(archery) + " = " + std::to_string(ability + proficiency + archery)}, refs);
        refs.push_back(ref("96"));
        if (key == "offhand") refs.push_back(ref("95"));
        record(e, combat, "damage." + key, item->at("name").get<std::string>() + " damage", dice + sign(damageMod + dueling) + " " + item->at("damageType").get<std::string>(),
               {"Weapon dice " + dice + "; ability contribution " + std::to_string(damageMod) + "; Dueling " + std::to_string(dueling) + " = " + dice + sign(damageMod + dueling)}, refs);
        std::string properties = item->at("name").get<std::string>() + ": ";
        for (const auto& propertyName : item->at("properties")) properties += propertyName.get<std::string>() + "; ";
        if (item->contains("versatile")) properties += "versatile (" + item->at("versatile").get<std::string>() + "); ";
        if (item->contains("range")) properties += "range " + item->at("range").at(0).dump() + "/" + item->at("range").at(1).dump() + " feet (disadvantage beyond normal range); ";
        if (property(item, "thrown") && item->at("mode") == "melee") properties += "thrown attacks use the same ability; Archery does not apply; ";
        if (!item->at("properties").empty() || item->contains("versatile")) combat.notes.push_back(properties);
    }
    const auto* subclass = level == 3 ? rules.find(str(c, "/subclassId")) : nullptr;
    if (subclass && (subclass->at("kind") != "subclass" || !subclassFor(rules, *subclass, cls))) subclass = nullptr;
    record(e, combat, "critical.minimum", "Weapon critical threshold", subclass ? subclass->at("criticalMinimum").get<int>() : 20,
           {subclass ? subclass->at("name").get<std::string>() + " Improved Critical: natural 19-20 on weapon attacks." : "A natural 20 is a critical hit."}, {subclass ? sourceFromJson(subclass->at("source")) : ref("94")});
    e.sections.push_back(combat);
    SheetSection skillSheet{"Saving throws and skills", {}, {}};
    for (const auto* ability : abilities) {
        const int trained = has(cls->at("savingThrows"), ability) ? proficiency : 0;
        record(e, skillSheet, "save." + std::string(ability), title(ability) + " save", mods[ability] + trained,
               {"Ability modifier " + std::to_string(mods[ability]) + " + saving throw proficiency " + std::to_string(trained) + " = " + std::to_string(mods[ability] + trained)}, {classRef, raceRef, ref("83")});
    }
    for (const auto& [id, skill] : rules.content) if (skill.at("kind") == "skill") {
        const auto ability = skill.at("ability").get<std::string>();
        const int trained = proficient.contains(id) ? proficiency : 0;
        record(e, skillSheet, "skill." + id, skill.at("name"), mods[ability] + trained,
               {title(ability) + " modifier " + std::to_string(mods[ability]) + " + skill proficiency " + std::to_string(trained) + " = " + std::to_string(mods[ability] + trained)}, {sourceFromJson(skill.at("source")), raceRef, classRef, backgroundRef, ref("60")});
    }
    e.sections.push_back(skillSheet);
    SheetSection features{"Features and resources", {}, {}};
    if (style) record(e, features, "fightingStyle", "Fighting style", style->at("name"), {style->at("description").get<std::string>()}, {styleRef});
    if (style) features.notes.push_back(style->at("description"));
    features.notes.push_back("Armor and weapons: all armor, shields, simple and martial weapons. No class tool proficiency.");
    features.notes.push_back(background->at("description"));
    record(e, features, "secondWind.uses", "Second Wind uses", row.at("secondWind"), {"Selected class level " + std::to_string(level) + " capacity = " + row.at("secondWind").dump() + "; short or long rest restores uses."}, {classRef});
    record(e, features, "actionSurge.uses", "Action Surge uses", row.at("actionSurge"), {"Selected class level " + std::to_string(level) + " capacity = " + row.at("actionSurge").dump() + "; short or long rest restores uses."}, {classRef, ref("25")});
    record(e, features, "hitDice.maximum", "Hit Dice (d10)", level, {std::to_string(level) + " Fighter levels = " + std::to_string(level) + "d10."}, {classRef, ref("56")});
    features.notes.push_back("Second Wind: bonus action, regain 1d10 + " + std::to_string(level) + " HP. Action Surge from level 2 grants one additional action. Resolve timing at the table.");
    e.sections.push_back(features);
    e.resources = {{"hp", "Current hit points", hp, "Long rest; explicit healing", {ref("87")}, "hp.maximum"},
                   {"hitDice", "Unspent Hit Dice (d10)", level, "Long rest: half total, rounded down, minimum 1", {ref("87")}, "hitDice.maximum"},
                   {"secondWind", "Second Wind uses remaining", row.at("secondWind"), "Short or long rest", {classRef}, "secondWind.uses"},
                   {"actionSurge", "Action Surge uses remaining", row.at("actionSurge"), "Short or long rest", {classRef, ref("25")}, "actionSurge.uses"}};
    SheetSection inventory{"Starting inventory", {}, {"Starting ownership only; purchases spend the background's initial coins. Ongoing acquisitions, consumption, containers and encumbrance are not implemented."}};
    Json inventoryRows = Json::array();
    for (const auto& [id, quantity] : owned) if (quantity > 0) {
        const auto* item = rules.find(id);
        inventoryRows.push_back({{"Item", item->at("name")}, {"Quantity", quantity}});
        if (item->contains("contentsSummary")) inventory.notes.push_back(item->at("name").get<std::string>() + ": " + item->at("contentsSummary").get<std::string>());
    }
    record(e, inventory, "inventory", "Starting possessions", inventoryRows, {"Class and background packages plus recorded purchases; equipment held in both hands requires separate quantities."}, {classRef, backgroundRef});
    record(e, inventory, "money.remainingCp", "Starting money remaining (cp)", coins, {background->at("goldCp").dump() + " starting cp - " + std::to_string(cost) + " purchase cp = " + std::to_string(coins)}, {backgroundRef, ref("62")});
    e.sections.push_back(inventory);
    SheetSection personality{"Personality and faith", {}, {}};
    for (const auto* key : {"deity", "personality", "ideal", "bond", "flaw"}) if (!str(c, "/" + std::string(key)).empty()) personality.notes.push_back(title(key) + ": " + str(c, "/" + std::string(key)));
    if (!personality.notes.empty()) e.sections.push_back(personality);
    // Accepted level gains lock their level and dice in the shared editor and remain auditable after reopening.
    int lastGain = 0;
    if (!d.advancement.is_array()) issue(e, "history", "/advancement", "Advancement history must be an array.");
    else for (std::size_t i = 0; i < d.advancement.size(); ++i) {
        const auto& entry = d.advancement.at(i);
        const auto path = "/advancement/" + std::to_string(i);
        const auto kind = stringChoice(entry, "kind");
        if (kind == "command") {
            if (!stringChoice(entry, "action").starts_with("srd51.") || stringChoice(entry, "moduleVersion") != d.moduleVersion)
                issue(e, "history", path, "This action record belongs to an unavailable rules module.");
        } else if (kind == "srd51.level") {
            const int gained = integerChoice(entry, "to"), from = integerChoice(entry, "from");
            const int base = integerChoice(entry, "hpBase", -1);
            const auto method = stringChoice(entry, "method");
            if (gained < 2 || gained > 3 || from != gained - 1 || (lastGain && from != lastGain) ||
                level < gained || num(c, "/hp/level" + std::to_string(gained)) != base ||
                stringChoice(entry, "classId") != classId || (method != "fixed" && method != "rolled") ||
                (method == "fixed" && base != cls->at("fixedHp").get<int>()))
                issue(e, "history", path, "The saved class, level sequence, HP method or accepted Hit Die conflicts with this advancement record.");
            lastGain = gained;
            for (auto& stage : e.stages) for (auto& field : stage.fields)
                if (field.path == "/level" || field.path == "/classId" ||
                    (gained == 3 && field.path == "/subclassId") || field.path == "/hp/level" + std::to_string(gained)) {
                    field.editable = false; field.readOnlyReason = "This accepted class level is recorded in history; use Character actions to advance.";
                }
        } else issue(e, "history", path, "This saved history entry is malformed or unsupported; it is preserved.");
    }
    if (lastGain && lastGain != level) issue(e, "history", "/level", "The level must match the last accepted advancement.");
    return e;
}

int stat(const Evaluation& e, const std::string& id) {
    const auto* calculation = e.find(id);
    if (!calculation || !calculation->effective.is_number_integer() || calculation->effective < -1000000 || calculation->effective > 1000000)
        throw std::runtime_error("A required statistic is unavailable: " + id);
    return calculation->effective.get<int>();
}
int current(const CharacterDocument& d, const Evaluation& e, const std::string& id) {
    const auto resource = std::find_if(e.resources.begin(), e.resources.end(), [&](const auto& r) { return r.id == id; });
    if (resource == e.resources.end()) throw std::runtime_error("Resource unavailable: " + id);
    const auto v = d.resources.value(id, Json(resource->maximum));
    if (!v.is_number_integer() || v < 0 || v > resource->maximum) throw std::runtime_error("Correct the saved current " + resource->label + ".");
    return v.get<int>();
}
void prepareActions(const CharacterDocument& d, const ResolvedRuleset& rules, Evaluation& e) {
    if (!e.find("hp.maximum")) return;
    SheetSection resources{"Current resources", {}, {}};
    Json hidden = Json::array();
    for (const auto& resource : e.resources) {
        hidden.push_back(resource.id);
        if (d.resources.contains(resource.id))
            resources.notes.push_back(resource.label + ": " + d.resources.at(resource.id).dump() + " / " + std::to_string(resource.maximum));
    }
    e.moduleData["sheet"]["excludedResourceKeys"] = hidden;
    if (!resources.notes.empty()) e.sections.push_back(resources);
    bool usable = e.complete();
    try { for (const auto& resource : e.resources) (void)current(d, e, resource.id); }
    catch (const std::exception& error) { issue(e, "resource.invalid", "/resources", error.what()); usable = false; }
    auto action = [&](const std::string& id, const std::string& label, const std::string& description, std::vector<Field> fields, bool available, std::vector<SourceRef> refs) {
        e.actions.push_back({"srd51." + id, label, description, std::move(fields), usable && available,
                             !usable ? "Complete the character and correct its saved resources first." : !available ? "The required level, XP or resource is unavailable." : "", std::move(refs)});
    };
    const auto* cls = rules.find(str(d.choices, "/classId"));
    if (!cls) return;
    const int level = num(d.choices, "/level", 1);
    for (const auto* mode : {"fixed", "rolled"}) {
        std::vector<Field> fields;
        if (std::string(mode) == "rolled") fields.push_back(integer("/hpRoll", "Accepted d10 result", 1, cls->at("hitDie")));
        if (level == 2) fields.push_back(select("/subclassId", "Martial archetype", options(rules, "subclass", [&](const Json& v) { return subclassFor(rules, v, cls); })));
        action("advance-" + std::string(mode), "Advance one Fighter level (" + std::string(mode) + " HP)", "Accept the next level and its HP input. Existing wounds and spent resources are preserved. Record enough XP before advancing; support stops at level 3.", fields,
               level < 3 && num(d.choices, "/xp") >= (level < 3 ? cls->at("progression").at(level).at("xp").get<int>() : 0), {sourceFromJson(cls->at("source")), ref("56")});
    }
    action("second-wind", "Use Second Wind", "Record an accepted d10 roll, spend one Second Wind use and restore roll + Fighter level HP, up to your maximum.", {integer("/roll", "Accepted d10 result", 1, 10)}, usable && current(d, e, "secondWind") > 0 && current(d, e, "hp") > 0, {sourceFromJson(cls->at("source")), ref("24")});
    action("action-surge", "Use Action Surge", "Spend one use. Resolve the additional action and timing at the table.", {}, usable && current(d, e, "actionSurge") > 0, {sourceFromJson(cls->at("source")), ref("25")});
    Field eligible{"/eligible", "I completed a qualifying rest", "boolean", 0, 1, {}, false, "Confirm the published duration and interruption requirements. A long rest includes at least six hours of sleep and no more than two hours of light activity, at least 1 HP at its start, and no other long-rest benefit in the preceding 24 hours."};
    std::vector<Field> restFields{eligible};
    const int dice = usable ? current(d, e, "hitDice") : 0;
    for (int i = 1; i <= std::min(3, dice); ++i) restFields.push_back(integer("/die" + std::to_string(i), "Accepted d10 #" + std::to_string(i) + " (0 = not spent)", 0, 10));
    const SourceRef resting{"D&D Basic Rules (2018), 2014 rules", "70", "https://media.wizards.com/2018/dnd/downloads/DnD_BasicRules_2018.pdf#page=70"};
    action("short-rest", "Finish a short rest", "At least one hour of qualifying rest. Restore Second Wind and Action Surge. Optionally spend Hit Dice, adding Constitution to each accepted roll (minimum zero healing); unspent dice are retained.", restFields, true, {ref("24"), ref("25"), ref("87"), resting});
    action("long-rest", "Finish a long rest", "At least eight hours of qualifying rest, no more than once per 24 hours. Restore HP and class uses; recover half your total Hit Dice, rounded down, minimum one.", {eligible}, usable && current(d, e, "hp") > 0, {ref("24"), ref("25"), ref("87"), resting});
}
TransitionResult applyCommand(const CharacterDocument& d, const ResolvedRuleset& rules, const CharacterCommand& command) {
    TransitionResult result{d, {}};
    auto& candidate = result.document;
    const auto e = evaluate(d, rules);
    if (!e.complete()) throw std::runtime_error("Complete the character before applying actions.");
    const auto* cls = rules.find(str(d.choices, "/classId"));
    if (!cls) throw std::runtime_error("The selected class is unavailable.");
    const int level = num(d.choices, "/level", 1);
    auto input = [&](const std::string& key, int low, int high, int fallback = -1) {
        const auto value = command.inputs.value(key, Json(fallback));
        if (!value.is_number_integer() || value < low || value > high) throw std::runtime_error("Enter a valid " + key + ".");
        return value.get<int>();
    };
    if (command.id == "srd51.advance-fixed" || command.id == "srd51.advance-rolled") {
        if (level >= 3 || num(d.choices, "/xp") < cls->at("progression").at(level).at("xp").get<int>()) throw std::runtime_error("This level gain is unavailable.");
        const int base = command.id == "srd51.advance-fixed" ? cls->at("fixedHp").get<int>() : input("hpRoll", 1, cls->at("hitDie"));
        if (at(d.choices, "/hp/level" + std::to_string(level + 1))) throw std::runtime_error("The next level already contains an accepted HP input; resolve it before advancing.");
        candidate.choices["level"] = level + 1;
        candidate.choices["hp"]["level" + std::to_string(level + 1)] = base;
        if (level == 2) candidate.choices["subclassId"] = command.inputs.at("subclassId");
        candidate.advancement.push_back({{"kind", "srd51.level"}, {"from", level}, {"to", level + 1}, {"classId", cls->at("id")}, {"hpBase", base}, {"method", command.id == "srd51.advance-fixed" ? "fixed" : "rolled"}});
        const auto after = evaluate(candidate, rules);
        if (!after.find("hp.maximum")) throw std::runtime_error("The proposed level cannot be evaluated.");
        candidate.resources["hp"] = current(d, e, "hp") + stat(after, "hp.maximum") - stat(e, "hp.maximum");
        candidate.resources["hitDice"] = current(d, e, "hitDice") + 1;
        candidate.resources["secondWind"] = current(d, e, "secondWind");
        candidate.resources["actionSurge"] = current(d, e, "actionSurge") + stat(after, "actionSurge.uses") - stat(e, "actionSurge.uses");
    } else if (command.id == "srd51.second-wind") {
        if (current(d, e, "secondWind") < 1 || current(d, e, "hp") < 1) throw std::runtime_error("Second Wind is unavailable.");
        candidate.resources["secondWind"] = current(d, e, "secondWind") - 1;
        candidate.resources["hp"] = std::min(stat(e, "hp.maximum"), current(d, e, "hp") + input("roll", 1, 10) + level);
    } else if (command.id == "srd51.action-surge") {
        if (current(d, e, "actionSurge") < 1) throw std::runtime_error("No Action Surge uses remain.");
        candidate.resources["actionSurge"] = current(d, e, "actionSurge") - 1;
    } else if (command.id == "srd51.short-rest" || command.id == "srd51.long-rest") {
        if (command.inputs.value("eligible", Json(false)) != Json(true)) throw std::runtime_error("Confirm that this rest meets the published requirements.");
        candidate.resources["secondWind"] = stat(e, "secondWind.uses");
        candidate.resources["actionSurge"] = stat(e, "actionSurge.uses");
        if (command.id == "srd51.long-rest") {
            if (current(d, e, "hp") < 1) throw std::runtime_error("A qualifying long rest requires at least 1 HP at its start.");
            candidate.resources["hp"] = stat(e, "hp.maximum");
            candidate.resources["hitDice"] = std::min(stat(e, "hitDice.maximum"), current(d, e, "hitDice") + std::max(1, level / 2));
        } else {
            int spent = 0, healing = 0;
            for (int i = 1; i <= std::min(3, current(d, e, "hitDice")); ++i) {
                const int roll = input("die" + std::to_string(i), 0, 10, 0);
                if (roll) { ++spent; healing += std::max(0, roll + stat(e, "ability.constitution.mod")); }
            }
            candidate.resources["hitDice"] = current(d, e, "hitDice") - spent;
            candidate.resources["hp"] = std::min(stat(e, "hp.maximum"), current(d, e, "hp") + healing);
        }
    } else throw std::runtime_error("Unsupported 5E action.");
    const auto after = evaluate(candidate, rules);
    for (const auto& message : after.messages) if (message.severity == "error") result.messages.push_back(message);
    return result;
}
} // namespace
} // namespace dnd::srd51

namespace dnd {
EditionModule srd51Module() {
    return {"srd51", "5E (2014) / SRD 5.1 - Human Fighter 1-3 (experimental)", "1.0.0", true,
            srd51::run, srd51::validateContent, srd51::validateRuleset, {{"srd51-core", "1.0.0"}},
            srd51::applyCommand, srd51::prepareActions};
}
} // namespace dnd
