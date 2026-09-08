#include "dnd/srd55_inventory.hpp"
#include "dnd/lifecycle.hpp"
#include "srd55_v2_internal.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace dnd::srd55v2 {
namespace {
constexpr const char *prefix = "srd55.inventory.";
const std::array<std::string, 6> abilityNames{"strength",     "dexterity", "constitution",
                                              "intelligence", "wisdom",    "charisma"};
const Json emptyObject = Json::object();
const Json emptyArray = Json::array();
struct Invalid : std::runtime_error {
    using std::runtime_error::runtime_error;
};
std::string s(const Json &j, const std::string &key, const std::string &fallback = "") {
    return j.is_object() && j.contains(key) && j.at(key).is_string() ? j.at(key).get<std::string>()
                                                                     : fallback;
}
bool b(const Json &j, const std::string &key, bool fallback = false) {
    return j.is_object() && j.contains(key) && j.at(key).is_boolean() ? j.at(key).get<bool>()
                                                                      : fallback;
}
long long n(const Json &j, const std::string &key, long long fallback = 0) {
    if (!j.is_object() || !j.contains(key) || !j.at(key).is_number_integer())
        return fallback;
    if (j.at(key).is_number_unsigned() &&
        j.at(key).get<unsigned long long>() >
            static_cast<unsigned long long>(std::numeric_limits<long long>::max()))
        return fallback;
    return j.at(key).get<long long>();
}
const Json &object(const Json &j, const std::string &key) {
    return j.is_object() && j.contains(key) && j.at(key).is_object() ? j.at(key) : emptyObject;
}
const Json &array(const Json &j, const std::string &key) {
    return j.is_object() && j.contains(key) && j.at(key).is_array() ? j.at(key) : emptyArray;
}
bool has(const Json &values, const std::string &id) {
    return values.is_array() && std::find(values.begin(), values.end(), Json(id)) != values.end();
}
bool owned(const Json &instance) {
    return s(instance, "status", "owned") == "owned" && n(instance, "quantity", 1) > 0;
}
bool initialized(const CharacterDocument &d) {
    return b(object(d.resources, "inventory"), "initialized");
}
SourceRef src(const Json &item) {
    return item.contains("source") ? sourceFromJson(item.at("source")) : ref("102");
}
std::string label(const Json &instance, const Json *item) {
    return (item ? s(*item, "name", s(instance, "itemId")) : s(instance, "itemId")) + " [" +
           s(instance, "id") + "]";
}
std::string need(const Json &inputs, const std::string &key) {
    const auto value = s(inputs, key);
    if (value.find_first_not_of(" \t\r\n") == std::string::npos)
        throw Invalid("Choose " + key + ".");
    return value;
}
long long amount(const Json &inputs, const std::string &key, long long low, long long high,
                 long long fallback = -1) {
    if (!inputs.contains(key)) {
        if (fallback >= low && fallback <= high)
            return fallback;
        throw Invalid("Enter " + key + ".");
    }
    if (!inputs.at(key).is_number_integer())
        throw Invalid(key + " must be a whole number.");
    const auto value = n(inputs, key, low - 1);
    if (value < low || value > high)
        throw Invalid(key + " must be between " + std::to_string(low) + " and " +
                      std::to_string(high) + ".");
    return value;
}
void requireTrue(const Json &inputs, const std::string &key, const std::string &explanation) {
    if (!b(inputs, key))
        throw Invalid(explanation);
}
const Json *catalog(const ResolvedRuleset &r, const Json &instance) {
    return r.find(s(instance, "itemId"));
}
const Json *variant(const Json &item, const Json &instance) {
    for (const auto &v : array(item, "variants"))
        if (s(v, "id") == s(instance, "variantId"))
            return &v;
    return nullptr;
}
std::string baseProfile(const Json &item, const Json &instance) {
    return s(instance, "baseProfile",
             s(item, "baseProfile", s(item, "weaponProfile", s(item, "id"))));
}
std::string rarity(const Json &item, const Json &instance) {
    const auto *v = variant(item, instance);
    return v ? s(*v, "rarity", s(item, "rarity")) : s(item, "rarity");
}
std::string duplicateKey(const Json &item, const Json &instance) {
    return s(item, "id") + "#" + s(instance, "variantId");
}
std::string defaultSlot(const Json &item, const ResolvedRuleset &rules, const Json &instance) {
    if (item.contains("wearSlot"))
        return s(item, "wearSlot");
    auto kind = s(item, "kind");
    if (kind == "armor")
        return "armor";
    if (kind == "shield")
        return "off-hand";
    if (kind == "weapon")
        return "main-hand";
    if (item.contains("weaponProfile"))
        return "main-hand";
    const auto *profile = rules.find(baseProfile(item, instance));
    if (profile && s(*profile, "kind") == "weapon")
        return "main-hand";
    return "carried";
}
bool equipped(const Json &item, const Json &instance) {
    return owned(instance) && s(instance, "location", "carried") == "carried" &&
           (s(item, "wearSlot", "carried") == "carried" || b(instance, "equipped"));
}
int levelOf(const Context &ctx, const std::string &name) {
    return profileLevel(ctx, name);
}
bool shieldTraining(const Context &ctx) {
    if (ctx.armorTraining.contains("shield"))
        return true;
    for (const auto &[id, level] : ctx.classLevels) {
        (void)level;
        const auto *cls = ctx.rules.find(id);
        if (!cls)
            continue;
        const auto &training = id == ctx.initialClass
                                   ? array(*cls, "armorTraining")
                                   : array(object(*cls, "multiclassTraining"), "armor");
        if (has(training, "shield"))
            return true;
    }
    return false;
}
int attunementLimit(const Context &ctx) {
    const auto* subclass = selectedSubclass(ctx, "rogue");
    return levelOf(ctx, "rogue") >= 13 && subclass && text(*subclass, "/rulesProfile") == "thief" ? 4 : 3;
}
std::set<std::string> castingAbilities(const Context &ctx) {
    std::set<std::string> result = ctx.castingAbilities;
    for (const auto &[id, level] : ctx.classLevels) {
        (void)level;
        const auto *item = ctx.rules.find(id);
        if (!item)
            continue;
        const auto &casting = object(*item, "casting");
        const auto ability = s(casting, "ability");
        if (s(casting, "kind", "none") != "none" && !ability.empty())
            result.insert(ability);
    }
    const auto species = text(ctx.document.choices, "/speciesId");
    if (species == "srd55:elf" || species == "srd55:gnome" || species == "srd55:tiefling") {
        const auto ability = text(ctx.document.choices, "/speciesCastingAbility");
        if (!ability.empty())
            result.insert(ability);
    }
    for (const auto &origin : {"background", "human"}) {
        if (origin == std::string("human") &&
            text(ctx.document.choices, "/speciesId") != "srd55:human")
            continue;
        const auto *feat = ctx.rules.find(
            origin == std::string("background")
                ? (ctx.rules.find(text(ctx.document.choices, "/backgroundId"))
                       ? s(*ctx.rules.find(text(ctx.document.choices, "/backgroundId")), "feat")
                       : "")
                : text(ctx.document.choices, "/humanFeat"));
        if (feat && feat->contains("spellList")) {
            const auto ability =
                text(ctx.document.choices, "/magicInitiate/" + std::string(origin) + "/ability");
            if (!ability.empty())
                result.insert(ability);
        }
    }
    for (const auto &feat : object(ctx.document.choices, "feats")) {
        if (!feat.is_object() || !ctx.feats.contains(s(feat, "id")))
            continue;
        const auto *def = ctx.rules.find(s(feat, "id"));
        if (def && def->contains("spellList") && !s(feat, "ability").empty())
            result.insert(s(feat, "ability"));
    }
    return result;
}
std::string attunementFailure(const Context &ctx, const Json &item, const Json &inv) {
    const auto &pre = object(item, "attunementPrerequisites");
    if (pre.contains("anyClass")) {
        bool matches = false;
        for (const auto &cls : array(pre, "anyClass"))
            if (cls.is_string() && levelOf(ctx, cls.get<std::string>()) > 0)
                matches = true;
        if (!matches)
            return "Attunement requires one of the item's listed classes.";
    }
    if (b(pre, "spellcaster") && castingAbilities(ctx).empty())
        return "Attunement requires the ability to cast a spell through a trait or feature; "
               "item-granted spells do not qualify.";
    if (b(pre, "dwarfOrBeltOfDwarvenkind") &&
        text(ctx.document.choices, "/speciesId") != "srd55:dwarf") {
        bool belt = false;
        for (const auto &instance : array(inv, "instances"))
            if (s(instance, "itemId") == "srd55:magic-belt-of-dwarvenkind" &&
                b(instance, "attuned"))
                belt = true;
        if (!belt)
            return "Dwarven Thrower requires a Dwarf or a creature attuned to a Belt of "
                   "Dwarvenkind.";
    }
    return {};
}
Context contextFor(const CharacterDocument &d, const ResolvedRuleset &rules,
                   const Evaluation *evaluation = nullptr) {
    Context ctx(d, rules);
    ctx.totalLevel = std::clamp(integerChoice(d.choices, "level", 1), 1, 20);
    ctx.proficiency = 2 + (ctx.totalLevel - 1) / 4;
    ctx.initialClass = stringChoice(d.choices, "classId");
    for (int level = 1; level <= ctx.totalLevel; ++level) {
        auto cls = ctx.initialClass;
        if (level > 1 && b(d.choices, "multiclass"))
            cls = text(d.choices, "/advancement/" + std::to_string(level) + "/classId");
        if (rules.find(cls))
            ctx.classLevels[cls]++;
    }
    if (const auto *cls = rules.find(ctx.initialClass)) {
        for (const auto &value : array(*cls, "armorTraining"))
            if (value.is_string())
                ctx.armorTraining.insert(value.get<std::string>());
    }
    for (const auto &[id, level] : ctx.classLevels) {
        (void)level;
        const auto *cls = rules.find(id);
        if (id != ctx.initialClass && cls)
            for (const auto &v : array(object(*cls, "multiclassTraining"), "armor"))
                if (v.is_string())
                    ctx.armorTraining.insert(v.get<std::string>());
    }
    if (text(d.choices, "/features/cleric/divineOrder") == "protector")
        ctx.armorTraining.insert("heavy");
    if (text(d.choices, "/features/druid/primalOrder") == "warden")
        ctx.armorTraining.insert("medium");
    for (const auto &ability : abilityNames) {
        ctx.scores[ability] = number(d.choices, "/abilities/" + ability, 10) +
                              number(d.choices, "/backgroundBoosts/" + ability, 0);
        if (evaluation)
            if (const auto *value = evaluation->find("ability." + ability);
                value && value->effective.is_number_integer())
                ctx.scores[ability] = value->effective.get<int>();
        ctx.modifiers[ability] = static_cast<int>(std::floor((ctx.scores[ability] - 10) / 2.0));
    }
    if (evaluation)
        if (const auto *prof = evaluation->find("proficiency");
            prof && prof->effective.is_number_integer())
            ctx.proficiency = prof->effective.get<int>();
    if (evaluation)
        for (const auto &ability : array(evaluation->moduleData, "castingAbilities"))
            if (ability.is_string())
                ctx.castingAbilities.insert(ability.get<std::string>());
    return ctx;
}
void message(Evaluation &e, const Json &item, const std::string &code, const std::string &path,
             const std::string &detail, const std::string &severity = "error") {
    e.messages.push_back({severity, "srd55.inventory." + code, path, detail, {src(item)}});
}
void calculation(Evaluation &e, SheetSection &section, const std::string &id,
                 const std::string &label, Json value, const std::vector<std::string> &steps,
                 const std::vector<SourceRef> &sources) {
    addCalculation(e, id, label, std::move(value), steps, sources);
    section.calculationIds.push_back(id);
}
Json allEffects(const Json &item, const Json &instance, bool curse = false) {
    Json effects = curse ? array(object(item, "curse"), "effects") : array(item, "effects");
    if (const auto *v = variant(item, instance))
        for (const auto &effect : array(*v, curse ? "curseEffects" : "effects"))
            effects.push_back(effect);
    return effects;
}
Json itemActions(const Json &item, const Json &instance) {
    Json actions = array(item, "actions");
    if (const auto *v = variant(item, instance))
        for (const auto &a : array(*v, "actions"))
            actions.push_back(a);
    return actions;
}
const Json *actionOf(const Json &item, const Json &instance, const std::string &id) {
    if (const auto *v = variant(item, instance))
        for (const auto &a : array(*v, "actions"))
            if (s(a, "id") == id)
                return &a;
    for (const auto &a : array(item, "actions"))
        if (s(a, "id") == id)
            return &a;
    return nullptr;
}
std::vector<Choice> instanceOptions(const CharacterDocument &d, const ResolvedRuleset &r,
                                    bool onlyOwned = true) {
    std::vector<Choice> choices;
    for (const auto &instance : array(object(d.resources, "inventory"), "instances")) {
        if (onlyOwned && !owned(instance))
            continue;
        const auto *item = catalog(r, instance);
        choices.push_back(
            {s(instance, "id"),
             label(instance, item) + " ×" + std::to_string(n(instance, "quantity", 1)),
             item != nullptr, item ? "" : "The exact item source is unavailable.",
             item ? std::vector<SourceRef>{src(*item)} : std::vector<SourceRef>{}});
    }
    return choices;
}
Field input(const std::string &name, const std::string &label, const std::string &kind, int low = 0,
            int high = 1000000000, const std::string &help = "") {
    return {"/" + name, label, kind, low, high, {}, false, help};
}
Field choice(const std::string &name, const std::string &label, std::vector<Choice> options,
             const std::string &help = "") {
    auto f = input(name, label, "select", 0, 0, help);
    f.options = std::move(options);
    return f;
}
std::vector<Choice> choicesOf(std::initializer_list<const char *> values) {
    std::vector<Choice> result;
    for (const auto *value : values)
        result.push_back({value, value, true, {}, {ref("102")}});
    return result;
}
Json &findInstance(Json &inv, const std::string &id) {
    for (auto &instance : inv["instances"])
        if (s(instance, "id") == id)
            return instance;
    throw Invalid("The selected item instance is unavailable.");
}
std::string nextInstanceId(Json &inv) {
    auto number = n(inv, "nextId", 1);
    if (number < 1 || number > 1000000000)
        throw Invalid("The saved inventory instance counter is invalid.");
    auto exists = [&](const std::string &id) {
        for (const auto &item : array(inv, "instances"))
            if (s(item, "id") == id)
                return true;
        return false;
    };
    while (exists("item-" + std::to_string(number))) {
        if (++number > 1000000000)
            throw Invalid("The inventory instance counter is exhausted.");
    }
    inv["nextId"] = number + 1;
    return "item-" + std::to_string(number);
}
Json makeInstance(Json &inv, const ResolvedRuleset &rules, const std::string &itemId, int quantity,
                  const Json &inputs) {
    const auto *item = rules.find(itemId);
    if (!item || !item->contains("kind"))
        throw Invalid("The selected item source is unavailable.");
    const auto kind = s(*item, "kind");
    if (kind != "magic-item" && kind != "weapon" && kind != "armor" && kind != "shield" &&
        kind != "tool" && kind != "gear")
        throw Invalid("This content entry is not equipment.");
    Json instance = {{"id", nextInstanceId(inv)}, {"itemId", itemId},
                     {"quantity", quantity},      {"status", "owned"},
                     {"location", "carried"},     {"equipped", false},
                     {"attuned", false},          {"identified", b(inputs, "identified", true)},
                     {"curseActive", false},      {"dormant", false}};
    if (!array(*item, "baseOptions").empty()) {
        const auto profile = need(inputs, "baseProfile");
        if (!has(item->at("baseOptions"), profile))
            throw Invalid("The base equipment profile is not eligible for this magic item.");
        instance["baseProfile"] = profile;
    } else if (item->contains("baseProfile")) {
        if (!s(inputs, "baseProfile").empty() &&
            s(inputs, "baseProfile") != s(*item, "baseProfile"))
            throw Invalid("This item has a fixed base equipment profile.");
        instance["baseProfile"] = item->at("baseProfile");
    } else if (!s(inputs, "baseProfile").empty())
        throw Invalid("This item does not require a base equipment selection.");
    if (!array(*item, "variants").empty()) {
        const auto selected = need(inputs, "variantId");
        instance["variantId"] = selected;
        if (!variant(*item, instance))
            throw Invalid("Choose one of this item's published variants.");
    } else if (!s(inputs, "variantId").empty())
        throw Invalid("This item does not have a selectable published variant.");
    if (b(*item, "spellSelection")) {
        const auto spellId = need(inputs, "spellId");
        const auto *spell = rules.find(spellId);
        const auto *selected = variant(*item, instance);
        if (!spell || s(*spell, "kind") != "spell" || !selected ||
            n(*spell, "level", -1) != n(*selected, "scrollLevel", -2))
            throw Invalid("The selected spell must match this scroll's published spell level.");
        instance["spellId"] = spellId;
        instance["scrollLevel"] = selected->at("scrollLevel");
        instance["scrollSaveDc"] = selected->at("saveDc");
        instance["scrollAttackBonus"] = selected->at("attackBonus");
    } else if (!s(inputs, "spellId").empty())
        throw Invalid("This item does not contain a selectable scroll spell.");
    if (item->contains("charges"))
        instance["charges"] =
            n(object(*item, "charges"), "initial", n(object(*item, "charges"), "maximum"));
    return instance;
}
void event(Json &inv, const CharacterCommand &command, const Json &details) {
    Json e = details;
    e["id"] = "inventory-event-" + std::to_string(inv["events"].size() + 1);
    e["action"] = command.id;
    e["inputs"] = command.inputs;
    inv["events"].push_back(std::move(e));
}
} // namespace

InventoryResult resolveInventory(const Context &ctx) {
    InventoryResult out;
    const auto &inv = object(ctx.document.resources, "inventory");
    out.initialized = b(inv, "initialized");
    if (!out.initialized)
        return out;
    out.attunementMaximum = attunementLimit(ctx);
    out.permanentEffects = array(inv, "permanentEffects");
    auto &e = out.evaluation;
    SheetSection section{"Current owned inventory", {}, {}};
    Json rows = Json::array();
    std::set<std::string> ids, attunementKeys;
    int attunedCount = 0;
    if (!inv.contains("instances") || !inv.at("instances").is_array()) {
        message(e, emptyObject, "shape", "/resources/inventory",
                "Owned item instances must be an array.");
        return out;
    }
    const bool merged = !text(ctx.document.resources, "/wildShapeForm").empty() &&
                        text(ctx.document.resources, "/formEquipment", "merged") != "worn";
    // Determine actual equipment before conditional item effects are evaluated.
    bool actualArmor = false, actualShield = false;
    for (const auto &instance : inv.at("instances"))
        if (owned(instance) && s(instance, "location", "carried") == "carried" &&
            b(instance, "equipped") && !merged) {
            const auto *item = catalog(ctx.rules, instance);
            if (!item)
                continue;
            const auto profile = baseProfile(*item, instance);
            const auto *base = ctx.rules.find(profile);
            const auto slot = s(instance, "slot");
            if (base && s(*base, "kind") == "armor") {
                out.armorProfile = profile;
                actualArmor = true;
            }
            if (base && s(*base, "kind") == "shield") {
                out.shield = true;
                actualShield = true;
            }
            if (slot == "main-hand" && base && s(*base, "kind") == "weapon") {
                out.weaponProfile = profile;
                out.weaponInstanceId = s(instance, "id");
            }
        }
    for (const auto &instance : inv.at("instances")) {
        const auto id = s(instance, "id"), path = "/resources/inventory/instances/" + id;
        const auto *item = catalog(ctx.rules, instance);
        if (id.empty() || !ids.insert(id).second) {
            message(e, item ? *item : emptyObject, "identity", path,
                    "Each owned item needs a unique nonempty instance ID.");
            continue;
        }
        if (!item) {
            message(e, emptyObject, "source", path,
                    "The exact source for " + s(instance, "itemId") +
                        " is unavailable. The instance and its state are preserved.");
            continue;
        }
        if (n(instance, "quantity", -1) < 0 || n(instance, "quantity", -1) > 100000) {
            message(e, *item, "quantity", path, "Item quantity is invalid.");
            continue;
        }
        if (!array(*item, "baseOptions").empty() &&
            !has(item->at("baseOptions"), s(instance, "baseProfile"))) {
            message(e, *item, "base", path, "This instance lacks a valid base equipment profile.");
            continue;
        }
        if (!array(*item, "variants").empty() && !variant(*item, instance)) {
            message(e, *item, "variant", path, "This instance lacks a valid item variant.");
            continue;
        }
        if (s(*item, "kind") == "magic-item" && n(instance, "quantity", 1) > 1) {
            message(e, *item, "stack", path,
                    "Magic items require individual physical instances for independent charges and "
                    "attunement.");
            continue;
        }
        if (b(*item, "spellSelection")) {
            const auto *spell = ctx.rules.find(s(instance, "spellId"));
            const auto *rank = variant(*item, instance);
            if (!spell || s(*spell, "kind") != "spell" || !rank ||
                n(*spell, "level", -1) != n(*rank, "scrollLevel", -2) ||
                n(instance, "scrollLevel", -1) != n(*rank, "scrollLevel", -2) ||
                n(instance, "scrollSaveDc", -1) != n(*rank, "saveDc", -2) ||
                n(instance, "scrollAttackBonus", -1) != n(*rank, "attackBonus", -2)) {
                message(e, *item, "scroll", path,
                        "The scroll spell, rank, and printed casting profile must match its source "
                        "variant.");
                continue;
            }
        }
        const auto failure = attunementFailure(ctx, *item, inv);
        const bool attuned = b(instance, "attuned") && failure.empty();
        if (b(instance, "attuned") && !failure.empty())
            message(e, *item, "attunement.prerequisite", path,
                    failure + " Stored attunement is preserved for reconciliation.");
        if (attuned) {
            ++attunedCount;
            if (!attunementKeys.insert(duplicateKey(*item, instance)).second)
                message(e, *item, "attunement.duplicate", path,
                        "You cannot attune to another copy of this item variant.");
        }
        if (owned(instance)) {
            out.ownedProfiles.push_back(s(*item, "id"));
            const auto profile = baseProfile(*item, instance);
            if (profile != s(*item, "id"))
                out.ownedProfiles.push_back(profile);
        }
        const auto maximum = n(object(*item, "charges"), "maximum");
        if (item->contains("charges") &&
            (n(instance, "charges", -1) < 0 || n(instance, "charges", -1) > maximum))
            message(
                e, *item, "charges", path,
                "The saved charge count is outside this item's capacity; its value is preserved.");
        const bool magicAllowed =
            !b(instance, "dormant") && (!b(*item, "requiresAttunement") || attuned);
        const bool active = equipped(*item, instance) && magicAllowed && !merged;
        const auto *selectedVariant = variant(*item, instance);
        const auto &coverageData = selectedVariant && selectedVariant->contains("coverage")
                                       ? object(*selectedVariant, "coverage")
                                       : object(*item, "coverage");
        const auto coverage = s(coverageData, "status", "implemented");
        const auto *physicalProfile = ctx.rules.find(baseProfile(*item, instance));
        rows.push_back(
            {{"instance", id},
             {"item", s(*item, "name")},
             {"variant", selectedVariant ? s(*selectedVariant, "name") : ""},
             {"base", physicalProfile && s(*physicalProfile, "id") != s(*item, "id")
                          ? s(*physicalProfile, "name")
                          : ""},
             {"identified", b(instance, "identified")},
             {"spell", instance.contains("spellId")
                           ? (ctx.rules.find(s(instance, "spellId"))
                                  ? s(*ctx.rules.find(s(instance, "spellId")), "name")
                                  : s(instance, "spellId"))
                           : ""},
             {"quantity", n(instance, "quantity", 1)},
             {"status", s(instance, "status", "owned")},
             {"location", s(instance, "location", "carried")},
             {"slot", b(instance, "equipped") ? s(instance, "slot") : "carried"},
             {"attuned", attuned},
             {"charges", item->contains("charges") ? Json(n(instance, "charges")) : Json(nullptr)},
             {"capacity", item->contains("charges") ? Json(maximum) : Json(nullptr)},
             {"cursed", b(instance, "curseActive")},
             {"coverage", coverage}});
        if (owned(instance) && coverage == "cataloged")
            message(e, *item, "coverage", path,
                    "Magic properties for " + s(*item, "name") +
                        " are cataloged only. They are not included in derived statistics or "
                        "executable item actions.",
                    active && s(coverageData, "passive") != "none" ? "error" : "warning");
        else if (owned(instance) && coverage == "partial")
            section.notes.push_back(
                s(*item, "name") +
                ": some mechanics remain source-reference only; inspect its coverage details.");
        for (bool curse : {false, true}) {
            if (curse && !b(instance, "curseActive"))
                continue;
            for (auto effect : allEffects(*item, instance, curse)) {
                const auto op = s(effect, "op"), condition = s(effect, "condition");
                bool applies =
                    curse || (condition == "attuned" ? attuned && !merged && magicAllowed : active);
                if (condition == "no-armor-or-shield")
                    applies = applies && !actualArmor && !actualShield;
                if (condition == "no-armor")
                    applies = applies && !actualArmor;
                if (condition == "not-dwarf")
                    applies = applies && text(ctx.document.choices, "/speciesId") != "srd55:dwarf";
                if (!applies)
                    continue;
                effect["itemId"] = s(*item, "id");
                effect["instanceId"] = id;
                effect["source"] = item->at("source");
                effect["curse"] = curse;
                out.effects.push_back(effect);
                const int value = static_cast<int>(n(effect, "value"));
                if (op == "ability-minimum")
                    out.abilityMinimums[s(effect, "ability")] =
                        std::max(out.abilityMinimums[s(effect, "ability")], value);
                else if (op == "ability-bonus") {
                    out.abilityBonuses[s(effect, "ability")] += value;
                    out.abilityBonusCaps[s(effect, "ability")] =
                        static_cast<int>(n(effect, "maxScore", 20));
                } else if (op == "language-grant")
                    out.languages.insert(s(effect, "languageId"));
                else if (op == "proficiency-bonus")
                    out.proficiencyBonus += value;
                else if (op == "darkvision-minimum")
                    out.darkvisionMinimum = std::max(out.darkvisionMinimum, value);
                else if (op == "ac-bonus") {
                    const auto *profile = ctx.rules.find(baseProfile(*item, instance));
                    const bool contributes = !profile || s(*profile, "kind") != "shield" || shieldTraining(ctx);
                    out.effects.back()["contributionApplied"] = contributes;
                    if (contributes)
                        out.armorBonus += value;
                } else if (op == "save-bonus")
                    for (const auto &ability : abilityNames)
                        out.saveBonuses[ability] += value;
                else if (op == "ability-check-bonus") {
                    for (const auto &[skillId, skill] : ctx.rules.content)
                        if (s(skill, "kind") == "skill")
                            out.skillBonuses[skillId] += value;
                    out.initiativeBonus += value;
                } else if (op == "weapon-attack-bonus" && id == out.weaponInstanceId)
                    out.weaponAttackBonus += value;
                else if (op == "weapon-training") {
                    for (const auto &profile : array(effect, "weaponProfiles"))
                        if (profile.is_string())
                            out.weaponTraining.insert(profile.get<std::string>());
                } else if (op == "weapon-damage-bonus" &&
                           (id == out.weaponInstanceId ||
                            has(array(effect, "weaponProfiles"), out.weaponProfile)))
                    out.weaponDamageBonus += value;
                else if (op == "spell-attack-bonus")
                    out.spellAttackBonus += value;
                else if (op == "spell-save-bonus")
                    out.spellSaveBonus += value;
                else if (op == "darkvision-bonus")
                    out.darkvisionBonus += value;
                else if (op == "speed-minimum")
                    out.speedMinimums[s(effect, "mode", "walk")] =
                        std::max(out.speedMinimums[s(effect, "mode", "walk")], value);
                else if (op == "armor-speed-penalty-ignored" ||
                         op == "armor-strength-requirement-ignored")
                    out.ignoreArmorSpeedPenalty = true;
                else if (op == "armor-stealth-disadvantage-ignored")
                    out.ignoreArmorStealthDisadvantage = true;
                else if (op == "hp-per-level")
                    out.hpBonusPerLevel += value;
                else if (op == "armor-formula")
                    out.armorFormulas.push_back({{"label", s(*item, "name")},
                                                 {"base", value ? value : n(effect, "base")},
                                                 {"abilities", array(effect, "abilities")},
                                                 {"source", item->at("source")}});
            }
        }
        if (active)
            for (const auto &action : itemActions(*item, instance))
                if (s(action, "kind") == "cast-spell") {
                    auto abilities = castingAbilities(ctx);
                    if (!b(action, "usesActorCastingAbility"))
                        abilities = {action.contains("saveDc") ? "" : "none"};
                    else if (abilities.empty())
                        abilities = {"none"};
                    for (const auto &ability : abilities) {
                        Json grant = {
                            {"spellId", s(action, "spellId")},
                            {"sourceType", "item"},
                            {"itemId", s(*item, "id")},
                            {"instanceId", id},
                            {"reason", s(*item, "name") + " [" + id + "]"},
                            {"source", item->at("source")},
                            {"ability", ability},
                            {"castLevel", n(action, "castLevel")},
                            {"chargeCost", n(action, "chargeCost")},
                            {"activationActionId", s(action, "id")},
                            {"usesActorCastingAbility", b(action, "usesActorCastingAbility")},
                            {"available",
                             n(instance, "charges", 100000) >= n(action, "chargeCost")}};
                        if (action.contains("saveDc"))
                            grant["fixedDC"] = action.at("saveDc");
                        if (action.contains("attackBonus"))
                            grant["fixedAttack"] = action.at("attackBonus");
                        out.spellGrants.push_back(std::move(grant));
                    }
                }
    }
    if (attunedCount > out.attunementMaximum)
        message(e, emptyObject, "attunement.limit", "/resources/inventory",
                "Attuned items exceed the character's limit of " +
                    std::to_string(out.attunementMaximum) + ".");
    calculation(e, section, "inventory.instances", "Owned item instances", rows,
                {"Physical instances are frozen from creation only by an explicit initialization "
                 "command; acquisitions and dispositions are recorded separately."},
                {ref("102"), ref("103")});
    calculation(e, section, "inventory.attunement", "Attunement",
                {{"used", attunedCount}, {"maximum", out.attunementMaximum}},
                {"Normally three items; Thief level 13 permits four. Each item variant can be "
                 "attuned only once."},
                {ref("102"), ref("64")});
    if (!out.effects.empty())
        calculation(e, section, "inventory.effects", "Applied item effects", out.effects,
                    {"Only eligible current equipment/attunement contributes; persistent curses "
                     "survive removal where the item specifies."},
                    {ref("102"), ref("206")});
    if (!out.permanentEffects.empty())
        calculation(e, section, "inventory.permanentEffects", "Recorded permanent item effects",
                    out.permanentEffects,
                    {"These sourced events persist independently of the consumed item's possession "
                     "and are applied at their acquisition level."},
                    {ref("249"), ref("250")});
    e.sections.push_back(std::move(section));
    return out;
}

void appendSrd55InventoryActions(const CharacterDocument &d, const ResolvedRuleset &rules,
                                 Evaluation &e) {
    auto add = [&](const std::string &id, const std::string &label, const std::string &description,
                   std::vector<Field> fields, bool available = true, const std::string &reason = "",
                   const std::string &page = "102") {
        e.actions.push_back({std::string(prefix) + id,
                             label,
                             description,
                             std::move(fields),
                             available,
                             reason,
                             {ref(page)}});
        if (id == "acquire")
            e.actions.back().initialInputs = {
                {"quantity", 1},   {"source", "loot"}, {"paidCp", 0},       {"baseProfile", ""},
                {"variantId", ""}, {"spellId", ""},    {"identified", true}};
        else if (id == "dispose")
            e.actions.back().initialInputs = {
                {"quantity", 1}, {"disposition", "lost"}, {"proceedsCp", 0}};
        else if (id == "recover")
            e.actions.back().initialInputs = {{"quantity", 1}, {"paidCp", 0}};
        else if (id == "equip")
            e.actions.back().initialInputs = {{"slot", "auto"}};
        else if (id == "use") {
            e.actions.back().initialInputs = {{"charges", 0}, {"castingAbility", "none"}};
            const auto &abilities = array(e.moduleData, "castingAbilities");
            if (abilities.size() == 1)
                e.actions.back().initialInputs["castingAbility"] = abilities[0];
        } else if (id == "identify")
            e.actions.back().initialInputs = {{"method", "short-rest"}};
        else if (id == "recharge")
            e.actions.back().initialInputs = {{"event", "dawn"}};
    };
    if (!initialized(d)) {
        const auto *seed = at(e.moduleData, "/inventory.creationSeed");
        const auto *money = e.find("money.remainingCp");
        bool good = seed && seed->is_array() && money && money->effective.is_number_integer() &&
                    money->effective.get<long long>() >= 0;
        for (const auto &m : e.messages)
            if (m.severity == "error" &&
                (m.path.starts_with("/classEquipment") ||
                 m.path.starts_with("/backgroundEquipment") || m.path.starts_with("/purchases") ||
                 m.path.starts_with("/purchaseQuantities")))
                good = false;
        add("initialize", "Begin owned inventory",
            "Freeze the selected starting items and purchases as physical item instances. Later "
            "acquisitions, sales, equipment, and attunement use the owned ledger; creation "
            "selections are retained.",
            {}, good, "Finish valid starting equipment selections and purchases first.", "20");
        return;
    }
    add("adjust-currency", "Record income or expense",
        "Record a coin award, shared loot, fee, or other agreed currency adjustment without "
        "inventing an item transaction. A nonblank explanation is required; expenses cannot exceed "
        "current funds.",
        {input("deltaCp", "Change in copper pieces (negative for an expense)", "integer",
               -1000000000, 1000000000),
         input("reason", "Income or expense explanation", "text")},
        true, "", "89");
    auto instances = instanceOptions(d, rules), allInstances = instanceOptions(d, rules, false);
    const auto &inv = object(d.resources, "inventory");
    std::vector<Choice> items, profiles, variants = {{"", "No variant", true, {}, {ref("209")}}};
    std::set<std::string> variantIds;
    for (const auto &[id, item] : rules.content) {
        const auto kind = s(item, "kind");
        if (kind == "weapon" || kind == "armor" || kind == "shield" || kind == "tool" ||
            kind == "gear" || kind == "magic-item")
            items.push_back({id, s(item, "name"), true, {}, {src(item)}});
        if (kind == "weapon" || kind == "armor" || kind == "shield" || kind == "gear")
            profiles.push_back({id, s(item, "name"), true, {}, {src(item)}});
        for (const auto &v : array(item, "variants"))
            if (variantIds.insert(s(v, "id")).second)
                variants.push_back({s(v, "id"), s(v, "name", s(v, "id")), true, {}, {src(item)}});
    }
    profiles.insert(profiles.begin(), {"", "No base selection", true, {}, {ref("204")}});
    std::sort(items.begin(), items.end(),
              [](const auto &a, const auto &b) { return a.label < b.label; });
    add("acquire", "Acquire an item",
        "Record actual loot, a gift, an agreed purchase, or a GM-approved higher-level starting "
        "allowance. Variable magic items require the matching published base and variant.",
        {choice("itemId", "Item", items), input("quantity", "Quantity", "integer", 1, 1000),
         choice("source", "Acquisition source",
                choicesOf({"loot", "gift", "purchase", "starting-guide"})),
         input("paidCp", "Total price paid (copper pieces)", "integer", 0, 1000000000),
         choice("baseProfile", "Base equipment, when required", profiles),
         choice("variantId", "Published variant, when required", variants),
         choice("spellId", "Spell on a scroll, when required",
                [&]() {
                    auto opts = options(rules, "spell");
                    opts.insert(opts.begin(), {"", "Not a scroll", true, {}, {ref("244")}});
                    return opts;
                }()),
         input("identified", "Properties identified", "boolean", 0, 1),
         input("reason", "Source or acquisition note", "text")},
        true, "", "209");
    add("dispose", "Dispose of owned items",
        "Record a sale, gift, loss, or destruction. Curses that prevent parting block disposition. "
        "Removing an item does not automatically end a persistent curse or an attunement bond.",
        {choice("instanceId", "Owned item", instances),
         input("quantity", "Quantity leaving inventory", "integer", 1, 1000),
         choice("disposition", "Disposition", choicesOf({"sold", "given", "lost", "destroyed"})),
         input("proceedsCp", "Total proceeds (copper pieces)", "integer", 0, 1000000000),
         input("reason", "Disposition note", "text")},
        !instances.empty(), "No owned items are available.", "89");
    add("recover", "Recover a disposed item",
        "Return the same recorded physical item after a loss, gift, or sale. Its charges and "
        "magical state are preserved; destroyed items cannot be restored this way.",
        {choice("instanceId", "Recorded item", allInstances),
         input("quantity", "Quantity returned", "integer", 1, 1000),
         input("paidCp", "Recovery or repurchase cost (cp)", "integer", 0, 1000000000),
         input("reason", "Recovery note", "text")},
        !allInstances.empty(), "No historical item instances are available.", "89");
    add("equip", "Equip an owned item",
        "Equip one physical item. A stack is split when necessary. Equipping a replacement removes "
        "the previous item from that exclusive location; it does not remove any persistent curse.",
        {choice("instanceId", "Owned item", instances),
         choice("slot", "Location",
                choicesOf({"auto", "main-hand", "off-hand", "armor", "head", "cloak", "boots",
                           "gloves", "bracers", "worn", "neck", "belt", "eyes", "ring", "orbit",
                           "carried"}))},
        !instances.empty(), "No owned items are available.", "103");
    add("move", "Carry or store an owned item",
        "Keep ownership while moving an item into storage or retrieving it. Stored equipment "
        "grants no worn/held/on-person effects; an existing attunement bond and persistent curse "
        "remain until a rule ends them.",
        {choice("instanceId", "Owned item", instances),
         choice("location", "Location", choicesOf({"carried", "stored"})),
         input("reason", "Storage or retrieval note", "text")},
        !instances.empty(), "No owned items are available.", "102");
    add("unequip", "Unequip an item",
        "Stop wearing or wielding this item. It remains owned; attunement and persistent curses "
        "remain unless a separate rule ends them.",
        {choice("instanceId", "Item", instances)}, !instances.empty(),
        "No owned items are available.", "103");
    add("identify", "Identify an item",
        "Record completed identification by a focused Short Rest or the Identify spell. A Short "
        "Rest used to identify an item cannot also attune to it. Most curses remain undiscovered.",
        {choice("instanceId", "Item", instances),
         choice("method", "Completed identification", choicesOf({"short-rest", "identify-spell"})),
         input("completed", "The identification has been completed", "boolean", 0, 1)},
        !instances.empty(), "No owned items are available.", "102");
    add("attune", "Attune to an item",
        "Record a completed focused Short Rest. Check the item's prerequisites, maximum "
        "attunements, and duplicate-copy rule. The same Short Rest cannot also identify the item.",
        {choice("instanceId", "Item", instances),
         input("completedShortRest", "A separate attunement Short Rest was completed", "boolean", 0,
               1)},
        !instances.empty(), "No owned items are available.", "102");
    add("unattune", "End attunement",
        "Voluntary attunement ending requires its own focused Short Rest and cannot end a cursed "
        "bond. Other published endings must be recorded as already resolved external events.",
        {choice("instanceId", "Item", allInstances),
         choice("cause", "Resolved ending",
                choicesOf({"voluntary-short-rest", "prerequisites-lost", "distance-24-hours",
                           "death", "another-owner"})),
         input("completed", "The stated ending has occurred", "boolean", 0, 1)},
        !allInstances.empty(), "No inventory records are available.", "102");
    add("release-curse", "Record a curse release",
        "Record an already resolved Remove Curse or similar effect. This clears the current "
        "owner's curse and attunement; it does not make the item permanently uncursed or cast a "
        "spell automatically.",
        {choice("instanceId", "Cursed item", allInstances),
         input("resolved", "Remove Curse or a similar effect has resolved", "boolean", 0, 1),
         input("reason", "Resolved effect or GM ruling", "text")},
        !allInstances.empty(), "No inventory records are available.", "159");
    std::vector<Choice> usable, study, rechargeable;
    std::map<std::string, Choice> actionChoices;
    for (const auto &instance : array(inv, "instances")) {
        if (!owned(instance))
            continue;
        const auto *item = catalog(rules, instance);
        if (!item)
            continue;
        bool use = false, book = false;
        for (const auto &action : itemActions(*item, instance)) {
            if (s(action, "kind") == "permanent-ability")
                book = true;
            else {
                use = true;
                actionChoices.emplace(
                    s(action, "id"),
                    Choice{s(action, "id"), s(action, "name"), true, {}, {src(*item)}});
            }
        }
        if (use)
            usable.push_back({s(instance, "id"), label(instance, item), true, {}, {src(*item)}});
        if (book)
            study.push_back({s(instance, "id"), label(instance, item), true, {}, {src(*item)}});
        if (object(*item, "charges").contains("recharge"))
            rechargeable.push_back(
                {s(instance, "id"), label(instance, item), true, {}, {src(*item)}});
    }
    std::vector<Choice> useOptions;
    for (const auto &[id, c] : actionChoices) {
        (void)id;
        useOptions.push_back(c);
    }
    add("use", "Use an item property",
        "Use a supported spell/healing property and record its charge cost atomically. Roll "
        "outcomes are accepted inputs. A Thief 13+ rolls d6 for charge conservation; last-charge "
        "destruction rolls are separate. Spell targets and encounter outcomes remain governed by "
        "the spell description.",
        {choice("instanceId", "Item", usable), choice("itemActionId", "Property", useOptions),
         input("charges", "Charges to spend (0 means normal cost)", "integer", 0, 50),
         choice("castingAbility", "Own casting ability, if required",
                choicesOf({"none", "intelligence", "wisdom", "charisma"})),
         input("acceptedRoll", "Accepted healing total, if required", "integer", 0, 1000),
         choice("healingTarget", "Healing recipient", choicesOf({"self", "other"})),
         input("administeredByOther", "Another creature administered this potion", "boolean", 0, 1),
         input("chargeRefundRoll", "Thief charge-conservation d6 (0 when not required)", "integer",
               0, 6),
         input("depletionRoll", "Last-charge d20 (0 when not required)", "integer", 0, 20)},
        !usable.empty(), "No owned item has an implemented property action.", "206");
    add("recharge", "Record an item's recharge",
        "Record one actual recharge event and its accepted dice total. Recalculation never "
        "restores charges. Event references prevent recording the same recharge twice for an item.",
        {choice("instanceId", "Item", rechargeable),
         choice("event", "Recharge event", choicesOf({"dawn", "long-rest"})),
         input("eventReference", "Game date or distinct recharge reference", "text"),
         input("acceptedRoll", "Accepted recharge total (0 for a fixed refill)", "integer", 0,
               1000),
         input("completed", "The recharge event has occurred", "boolean", 0, 1)},
        !rechargeable.empty(), "No owned item has a supported recharge rule.", "206");
    add("study", "Complete a magic manual or tome",
        "Record at least 48 hours of study within six days. The permanent ability increase is "
        "recorded at the current character level, independently of future possession. The book "
        "becomes dormant for a century.",
        {choice("instanceId", "Manual or tome", study),
         input("hours", "Accepted completed study hours", "integer", 48, 144),
         input("days", "Days containing that study", "integer", 2, 6)},
        !study.empty(), "No owned manual or tome has a supported study effect.", "249");
    add("awaken-tome", "Record a tome's century of recovery",
        "Restore a manual or tome after at least a century has actually passed since its use. Its "
        "earlier permanent effect remains; no real-world timer is scheduled.",
        {choice("instanceId", "Manual or tome", study),
         input("yearsElapsed", "Accepted game years since study", "integer", 100, 1000000),
         input("completed", "That time has elapsed", "boolean", 0, 1)},
        !study.empty(), "No supported manual or tome is owned.", "250");
    const int creationLevel = static_cast<int>(n(inv, "creationLevel", 1));
    add("higher-level-guide", "Apply the higher-level starting guide",
        "Apply the optional, GM-approved equipment guide once for the character's recorded "
        "starting level. Money is credited once; rarity allowances are spent through Acquire an "
        "item → starting-guide.",
        {input("approved", "The GM approved this starting-equipment guide", "boolean", 0, 1),
         input("acceptedD10", "Accepted d10 (unused for levels 2–4)", "integer", 1, 10)},
        creationLevel >= 2 && !b(inv, "guideApplied"),
        creationLevel < 2 ? "The recorded starting level is 1."
                          : "The starting-equipment guide was already applied.",
        "24");
}

TransitionResult applySrd55InventoryCommand(const CharacterDocument &d,
                                            const ResolvedRuleset &rules,
                                            const CharacterCommand &command) {
    TransitionResult result{d, {}};
    try {
        if (!command.id.starts_with(prefix))
            throw Invalid("Unknown inventory command.");
        if (!rules.valid() || rules.edition != d.edition || rules.moduleVersion != d.moduleVersion)
            throw Invalid(
                "The exact character rules must resolve before an inventory transaction.");
        if (!command.inputs.is_object())
            throw Invalid("Inventory action inputs must be an object.");
        const auto action = command.id.substr(std::string(prefix).size());
        const auto &in = command.inputs;
        if (action == "initialize") {
            if (initialized(d))
                throw Invalid("Owned inventory has already been initialized. It cannot be rerun to "
                              "restore items or money.");
            const auto evaluation = evaluate(d, rules);
            const auto *seed = at(evaluation.moduleData, "/inventory.creationSeed");
            const auto *money = evaluation.find("money.remainingCp");
            if (!seed || !seed->is_array() || !money || !money->effective.is_number_integer() ||
                money->effective.get<long long>() < 0)
                throw Invalid("Complete valid starting-equipment and purchase selections before "
                              "initializing inventory.");
            for (const auto &m : evaluation.messages)
                if (m.severity == "error" &&
                    (m.path.starts_with("/classEquipment") ||
                     m.path.starts_with("/backgroundEquipment") ||
                     m.path.starts_with("/purchases") || m.path.starts_with("/purchaseQuantities")))
                    throw Invalid(m.text);
            Json inv = {{"initialized", true},
                        {"nextId", 1},
                        {"creationLevel", integerChoice(d.choices, "level", 1)},
                        {"creationCurrencyCp", money->effective},
                        {"instances", Json::array()},
                        {"events", Json::array()},
                        {"permanentEffects", Json::array()}};
            for (const auto &row : *seed) {
                if (!row.is_object())
                    throw Invalid("Creation inventory seed is malformed.");
                const auto itemId = need(row, "itemId");
                const int qty = static_cast<int>(amount(row, "quantity", 1, 100000, 1));
                auto instance = makeInstance(inv, rules, itemId, qty, emptyObject);
                const auto *item = rules.find(itemId);
                const auto profile = baseProfile(*item, instance);
                if (profile == text(d.choices, "/armorId")) {
                    instance["equipped"] = true;
                    instance["slot"] = "armor";
                } else if (profile == text(d.choices, "/weaponId")) {
                    instance["equipped"] = true;
                    instance["slot"] = "main-hand";
                } else if (profile == "srd55:shield-equipment" && b(d.choices, "shield")) {
                    instance["equipped"] = true;
                    instance["slot"] = "off-hand";
                }
                if (b(instance, "equipped") && qty > 1) {
                    Json remainder = instance;
                    remainder["id"] = nextInstanceId(inv);
                    remainder["quantity"] = qty - 1;
                    remainder["equipped"] = false;
                    remainder.erase("slot");
                    instance["quantity"] = 1;
                    inv["instances"].push_back(std::move(remainder));
                }
                inv["instances"].push_back(std::move(instance));
            }
            if (!result.document.resources.contains("currencyCp"))
                result.document.resources["currencyCp"] = money->effective;
            event(inv, command,
                  {{"balanceAfter", result.document.resources["currencyCp"]},
                   {"itemCount", inv["instances"].size()}});
            result.document.resources["inventory"] = std::move(inv);
            return result;
        }
        if (!initialized(d))
            throw Invalid("Initialize owned inventory before using lifecycle actions.");
        auto &inv = result.document.resources["inventory"];
        if (!inv.is_object() || !inv.contains("instances") || !inv["instances"].is_array() ||
            !inv.contains("events") || !inv["events"].is_array())
            throw Invalid("The inventory ledger is malformed; original data is preserved.");
        const auto evaluation = evaluate(d, rules);
        const auto ctx = contextFor(d, rules, &evaluation);
        long long balance = n(d.resources, "currencyCp", -1);
        if (balance < 0)
            throw Invalid("The canonical currencyCp balance is missing or invalid.");
        Json details = {{"balanceBefore", balance}};
        if (action == "adjust-currency") {
            const auto delta = amount(in, "deltaCp", -1000000000, 1000000000);
            const auto why = need(in, "reason");
            if (delta == 0)
                throw Invalid("Record a nonzero income or expense.");
            if (delta < 0 && -delta > balance)
                throw Invalid("The expense exceeds current funds.");
            balance += delta;
            details["deltaCp"] = delta;
            details["reason"] = why;
        } else if (action == "acquire") {
            const auto itemId = need(in, "itemId"), origin = s(in, "source", "loot");
            const auto qty = amount(in, "quantity", 1, 1000, 1),
                       cost = amount(in, "paidCp", 0, 1000000000, 0);
            if (origin != "loot" && origin != "gift" && origin != "purchase" &&
                origin != "starting-guide")
                throw Invalid("Choose a supported acquisition source.");
            if (origin != "purchase" && cost)
                throw Invalid("Only a purchase may deduct money.");
            if (cost > balance)
                throw Invalid("The purchase exceeds current funds.");
            const auto *item = rules.find(itemId);
            if (!item)
                throw Invalid("The item source is missing.");
            Json made = Json::array();
            if (origin == "starting-guide") {
                if (!b(inv, "guideApplied") || s(*item, "kind") != "magic-item")
                    throw Invalid(
                        "This acquisition requires an approved remaining magic-item allowance.");
                Json probe = makeInstance(inv, rules, itemId, 1, in);
                inv["nextId"] = n(inv, "nextId") - 1;
                const auto rank = rarity(*item, probe);
                const auto remaining = n(object(inv, "allowances"), rank);
                if (rank.empty() || rank == "varies" || qty > remaining)
                    throw Invalid("There is no sufficient starting-guide allowance for this item's "
                                  "verified rarity.");
                inv["allowances"][rank] = remaining - qty;
            }
            if (s(*item, "kind") == "magic-item")
                for (int i = 0; i < qty; ++i) {
                    auto instance = makeInstance(inv, rules, itemId, 1, in);
                    made.push_back(instance["id"]);
                    inv["instances"].push_back(std::move(instance));
                }
            else {
                auto instance = makeInstance(inv, rules, itemId, static_cast<int>(qty), in);
                made.push_back(instance["id"]);
                inv["instances"].push_back(std::move(instance));
            }
            balance -= cost;
            details["instances"] = made;
            details["costCp"] = cost;
        } else if (action == "higher-level-guide") {
            if (b(inv, "guideApplied"))
                throw Invalid("The higher-level starting guide was already applied.");
            requireTrue(in, "approved",
                        "Record GM approval of the optional starting-equipment guide.");
            const int level = static_cast<int>(n(inv, "creationLevel", 1));
            if (level < 2 || level > 20)
                throw Invalid("This guide requires a recorded starting level from 2 to 20.");
            const auto die = level >= 5 ? amount(in, "acceptedD10", 1, 10) : 0;
            long long added = 0;
            Json allowances = Json::object();
            if (level < 5)
                allowances = {{"common", 1}};
            else if (level < 11) {
                added = 500 + die * 25;
                allowances = {{"common", 1}, {"uncommon", 1}};
            } else if (level < 17) {
                added = 5000 + die * 250;
                allowances = {{"common", 2}, {"uncommon", 3}, {"rare", 1}};
            } else {
                added = 20000 + die * 250;
                allowances = {{"common", 2}, {"uncommon", 4}, {"rare", 3}, {"very rare", 1}};
            }
            balance += added * 100;
            inv["guideApplied"] = true;
            inv["allowances"] = allowances;
            details["addedCp"] = added * 100;
            details["acceptedD10"] = die;
        } else {
            const auto id = need(in, "instanceId");
            auto &instance = findInstance(inv, id);
            const auto *item = catalog(rules, instance);
            if (!item)
                throw Invalid("The exact item source is unavailable; the instance is preserved.");
            details["instanceId"] = id;
            if (action != "unattune" && action != "release-curse" && action != "recover" &&
                !owned(instance))
                throw Invalid("This physical item is no longer owned.");
            if (action == "dispose") {
                const auto qty = amount(in, "quantity", 1, n(instance, "quantity", 1), 1),
                           proceeds = amount(in, "proceedsCp", 0, 1000000000, 0);
                const auto disposition = s(in, "disposition", "lost");
                if (disposition != "sold" && disposition != "given" && disposition != "lost" &&
                    disposition != "destroyed")
                    throw Invalid("Choose a supported disposition.");
                if (disposition != "sold" && proceeds)
                    throw Invalid("Only a sale can produce proceeds.");
                if (b(instance, "curseActive") && b(object(*item, "curse"), "blocksDisposition"))
                    throw Invalid("This curse makes you unwilling to part with the item. Resolve "
                                  "the curse first.");
                if (disposition != "destroyed")
                    instance["recoverableQuantity"] = n(instance, "recoverableQuantity") + qty;
                instance["quantity"] = n(instance, "quantity") - qty;
                if (n(instance, "quantity") == 0) {
                    instance["status"] = disposition == "destroyed" ? "destroyed" : "disposed";
                    instance["equipped"] = false;
                    if (disposition == "destroyed")
                        instance["attuned"] = false;
                }
                balance += proceeds;
                details["quantity"] = qty;
                details["proceedsCp"] = proceeds;
            } else if (action == "recover") {
                const auto qty = amount(in, "quantity", 1, n(instance, "recoverableQuantity"), 1),
                           cost = amount(in, "paidCp", 0, 1000000000, 0);
                if (cost > balance)
                    throw Invalid("The recovery or repurchase exceeds current funds.");
                if (s(instance, "status") == "destroyed")
                    throw Invalid(
                        "A destroyed instance cannot be recovered by an ordinary acquisition.");
                instance["quantity"] = n(instance, "quantity") + qty;
                instance["recoverableQuantity"] = n(instance, "recoverableQuantity") - qty;
                instance["status"] = "owned";
                balance -= cost;
                details["quantity"] = qty;
                details["paidCp"] = cost;
            } else if (action == "awaken-tome") {
                if (!b(instance, "dormant") || n(instance, "dormantYears") != 100)
                    throw Invalid("This is not a tome waiting for its century of recovery.");
                requireTrue(in, "completed",
                            "Record that a century actually passed since this tome's study.");
                amount(in, "yearsElapsed", 100, 1000000);
                instance["dormant"] = false;
                instance.erase("dormantYears");
            } else if (action == "move") {
                const auto location = need(in, "location");
                if (location != "carried" && location != "stored")
                    throw Invalid("An item can be carried or stored.");
                if (location == "stored" && b(instance, "curseActive") &&
                    b(object(*item, "curse"), "blocksDisposition"))
                    throw Invalid("This curse requires keeping the item within reach.");
                instance["location"] = location;
                if (location == "stored")
                    instance["equipped"] = false;
            } else if (action == "equip") {
                if (s(instance, "location", "carried") != "carried")
                    throw Invalid("Retrieve the stored item before equipping it.");
                auto slot = s(in, "slot", "auto"), expected = defaultSlot(*item, rules, instance);
                if (slot == "auto")
                    slot = expected;
                const auto profile = baseProfile(*item, instance);
                const auto *base = rules.find(profile);
                const bool hand = expected == "main-hand" || expected == "off-hand";
                if (hand) {
                    if (slot != "main-hand" && slot != "off-hand")
                        throw Invalid("This item must be held in a hand.");
                } else if (slot != expected)
                    throw Invalid("Equip this item in its intended location: " + expected + ".");
                if (base && s(*base, "kind") == "shield" && slot != "off-hand")
                    throw Invalid("Equip the Shield in the off hand.");
                const bool exclusive = hand || slot == "armor" || slot == "head" ||
                                       slot == "cloak" || slot == "boots" || slot == "gloves" ||
                                       slot == "bracers";
                if (hand)
                    for (const auto &other : inv["instances"]) {
                        if (s(other, "id") == id || !owned(other) || !b(other, "equipped"))
                            continue;
                        const auto *otherItem = catalog(rules, other);
                        if (!otherItem)
                            continue;
                        const auto *otherBase = rules.find(baseProfile(*otherItem, other));
                        if (s(other, "slot") != slot && ((base && b(*base, "twoHanded")) ||
                                                         (otherBase && b(*otherBase, "twoHanded"))))
                            throw Invalid("A two-handed weapon requires both hands; unequip the "
                                          "conflicting held item first.");
                    }
                if (item->contains("maximumEquipped")) {
                    int orbiting = 0;
                    for (const auto &other : inv["instances"])
                        if (s(other, "id") != id && s(other, "itemId") == s(*item, "id") &&
                            owned(other) && b(other, "equipped"))
                            ++orbiting;
                    if (orbiting >= n(*item, "maximumEquipped"))
                        throw Invalid("This item permits at most " +
                                      std::to_string(n(*item, "maximumEquipped")) +
                                      " copies active at once.");
                }
                if (exclusive)
                    for (auto &other : inv["instances"])
                        if (s(other, "id") != id && s(other, "slot") == slot)
                            other["equipped"] = false;
                if (n(instance, "quantity", 1) > 1) {
                    Json single = instance;
                    instance["quantity"] = n(instance, "quantity") - 1;
                    single["id"] = nextInstanceId(inv);
                    single["quantity"] = 1;
                    single["equipped"] = true;
                    single["slot"] = slot;
                    details["equippedInstanceId"] = single["id"];
                    inv["instances"].push_back(std::move(single));
                } else {
                    instance["equipped"] = true;
                    instance["slot"] = slot;
                }
            } else if (action == "unequip")
                instance["equipped"] = false;
            else if (action == "identify") {
                requireTrue(in, "completed", "Record the already completed identification.");
                const auto method = need(in, "method");
                if (method != "short-rest" && method != "identify-spell")
                    throw Invalid("Choose Short Rest or Identify spell.");
                instance["identified"] = true;
                details["identificationMethod"] = method;
            } else if (action == "attune") {
                requireTrue(in, "completedShortRest",
                            "Attunement requires a separate completed focused Short Rest.");
                if (!b(*item, "requiresAttunement"))
                    throw Invalid("This item does not require attunement.");
                if (b(instance, "attuned"))
                    throw Invalid("This instance is already attuned.");
                if (n(instance, "quantity", 1) != 1)
                    throw Invalid("Attune one physical item instance, not a stack.");
                const auto failure = attunementFailure(ctx, *item, inv);
                if (!failure.empty())
                    throw Invalid(failure);
                int count = 0;
                const auto key = duplicateKey(*item, instance);
                for (const auto &other : inv["instances"])
                    if (b(other, "attuned")) {
                        const auto *otherItem = catalog(rules, other);
                        if (otherItem && attunementFailure(ctx, *otherItem, inv).empty()) {
                            ++count;
                            if (duplicateKey(*otherItem, other) == key)
                                throw Invalid(
                                    "You cannot attune to another copy of the same item variant.");
                        }
                    }
                if (count >= attunementLimit(ctx))
                    throw Invalid("End an existing attunement before exceeding the limit of " +
                                  std::to_string(attunementLimit(ctx)) + ".");
                instance["attuned"] = true;
                if (item->contains("curse"))
                    instance["curseActive"] = true;
            } else if (action == "unattune") {
                if (!b(instance, "attuned"))
                    throw Invalid("This item is not attuned.");
                requireTrue(in, "completed",
                            "Confirm that the stated attunement-ending event occurred.");
                const auto cause = need(in, "cause");
                if (cause == "voluntary-short-rest" && b(instance, "curseActive"))
                    throw Invalid(
                        "A cursed bond cannot be ended voluntarily; resolve the curse first.");
                if (cause == "prerequisites-lost" && attunementFailure(ctx, *item, inv).empty())
                    throw Invalid("The item prerequisites are still satisfied.");
                if (cause != "voluntary-short-rest" && cause != "prerequisites-lost" &&
                    cause != "distance-24-hours" && cause != "death" && cause != "another-owner")
                    throw Invalid("Choose a published reason attunement ended.");
                instance["attuned"] = false;
            } else if (action == "release-curse") {
                requireTrue(in, "resolved",
                            "Record an already resolved Remove Curse or similar effect.");
                if (s(in, "reason").find_first_not_of(" \t\r\n") == std::string::npos)
                    throw Invalid("Record the effect or GM ruling that released the curse.");
                if (!b(instance, "curseActive"))
                    throw Invalid("No active owner curse is recorded for this item.");
                instance["curseActive"] = false;
                instance["attuned"] = false;
            } else if (action == "study") {
                const Json *study = nullptr;
                for (const auto &possible : array(*item, "actions"))
                    if (s(possible, "kind") == "permanent-ability")
                        study = &possible;
                if (!study)
                    throw Invalid("This item has no implemented permanent study effect.");
                if (b(instance, "dormant"))
                    throw Invalid("This manual or tome has already lost its magic for a century.");
                const auto hours = amount(in, "hours", 48, 144), days = amount(in, "days", 2, 6);
                if (hours > days * 24)
                    throw Invalid("The study hours exceed the recorded elapsed days.");
                Json effect = {
                    {"op", "ability-increase"},
                    {"ability", s(*study, "ability")},
                    {"value", n(*study, "value", 2)},
                    {"maxScore", n(*study, "maxScore", 30)},
                    {"acquiredCharacterLevel", integerChoice(d.choices, "level", 1)},
                    {"eventId", "inventory-event-" + std::to_string(inv["events"].size() + 1)},
                    {"itemId", s(*item, "id")},
                    {"instanceId", id},
                    {"source", item->at("source")}};
                if (!inv.contains("permanentEffects"))
                    inv["permanentEffects"] = Json::array();
                inv["permanentEffects"].push_back(effect);
                instance["dormant"] = true;
                instance["dormantYears"] = n(*study, "dormantYears", 100);
                details["permanentEffect"] = effect;
            } else if (action == "recharge") {
                requireTrue(in, "completed",
                            "Record a recharge event that actually occurred in the game.");
                const auto &recharge = object(object(*item, "charges"), "recharge");
                if (recharge.empty())
                    throw Invalid("This item has no supported recharge event.");
                const auto trigger = need(in, "event");
                if (trigger != s(recharge, "event"))
                    throw Invalid("This item recharges at " + s(recharge, "event") +
                                  ", not at that event.");
                const auto reference = need(in, "eventReference");
                for (const auto &prior : inv["events"])
                    if (s(prior, "action") == command.id && s(prior, "instanceId") == id &&
                        s(object(prior, "inputs"), "eventReference") == reference)
                        throw Invalid(
                            "This recharge event has already been recorded for that instance.");
                long long recovered = n(recharge, "fixed", -1);
                if (recovered < 0) {
                    const auto count = n(recharge, "dice"), sides = n(recharge, "sides"),
                               bonus = n(recharge, "bonus");
                    recovered = amount(in, "acceptedRoll", count + bonus, count * sides + bonus);
                }
                const auto before = n(instance, "charges");
                instance["charges"] =
                    std::min(n(object(*item, "charges"), "maximum"), before + recovered);
                details["chargesBefore"] = before;
                details["chargesAfter"] = instance["charges"];
                details["acceptedRecovery"] = recovered;
            } else if (action == "use") {
                const auto property = need(in, "itemActionId");
                const auto *use = actionOf(*item, instance, property);
                if (!use || s(*use, "kind") == "permanent-ability")
                    throw Invalid("Choose an implemented property for this item.");
                if (b(instance, "dormant"))
                    throw Invalid("This item is dormant or no longer magical.");
                if (!equipped(*item, instance))
                    throw Invalid("The item must be worn or held in its intended fashion.");
                if (b(*item, "requiresAttunement") &&
                    (!b(instance, "attuned") || !attunementFailure(ctx, *item, inv).empty()))
                    throw Invalid("The item's magical property requires valid attunement.");
                if (!text(d.resources, "/wildShapeForm").empty() &&
                    text(d.resources, "/formEquipment", "merged") != "worn")
                    throw Invalid("Merged or dropped equipment cannot be used in this Wild Shape.");
                if (s(*use, "kind") == "cast-spell")
                    for (const auto &m : evaluation.messages)
                        if (m.code == "srd55.v2.armor.untrained")
                            throw Invalid(
                                "You cannot cast spells while wearing armor without its training.");
                const auto ordinary = n(*use, "chargeCost");
                auto spent = amount(in, "charges", 0, 50, 0);
                if (spent == 0)
                    spent = ordinary;
                if (!b(*use, "upcastWithCharges") && spent != ordinary)
                    throw Invalid("This property requires exactly " + std::to_string(ordinary) +
                                  " charges or uses.");
                if (spent < ordinary || spent > n(*use, "maximumCharges", 50))
                    throw Invalid(
                        "The selected charge expenditure is outside this property's limits.");
                const auto before = n(instance, "charges", 0);
                if (spent > before)
                    throw Invalid("The item has insufficient charges or uses.");
                bool conserve = false;
                if (spent > 0 && s(object(*item, "charges"), "unit") == "charges" &&
                    attunementLimit(ctx) == 4)
                    conserve = amount(in, "chargeRefundRoll", 1, 6) == 6;
                const auto remaining = before - (conserve ? 0 : spent);
                if (item->contains("charges"))
                    instance["charges"] = remaining;
                if (spent > 0 && remaining == 0 && !conserve) {
                    const auto &depletion = object(object(*item, "charges"), "depletion");
                    if (!depletion.empty()) {
                        const auto roll = amount(in, "depletionRoll", 1, n(depletion, "sides", 20));
                        if (roll == n(depletion, "destroyOn", 1)) {
                            instance["status"] = "destroyed";
                            instance["quantity"] = 0;
                            instance["equipped"] = false;
                            instance["attuned"] = false;
                        }
                    }
                    if (b(object(*item, "charges"), "mundaneWhenEmpty"))
                        instance["dormant"] = true;
                    if (b(object(*item, "charges"), "consumedWhenEmpty")) {
                        instance["status"] = "consumed";
                        instance["quantity"] = 0;
                        instance["equipped"] = false;
                        instance["attuned"] = false;
                    }
                }
                if (b(*use, "usesActorCastingAbility")) {
                    const auto actual = castingAbilities(ctx);
                    const auto ability =
                        s(in, "castingAbility", actual.size() == 1 ? *actual.begin() : "none");
                    if ((actual.empty() && ability != "none") ||
                        (!actual.empty() && !actual.contains(ability)))
                        throw Invalid(
                            "Choose one of your actual spellcasting abilities for this item.");
                    details["castingAbility"] = ability;
                }
                if (s(*use, "kind") == "heal") {
                    const auto recipient = s(in, "healingTarget", "self");
                    if (recipient != "self" && recipient != "other")
                        throw Invalid("Choose self or another healing recipient.");
                    if ((recipient == "other" || b(in, "administeredByOther")) &&
                        !b(*use, "administerable"))
                        throw Invalid("This item property only heals its wearer; it is not an "
                                      "administered potion.");
                    const auto &roll = object(*use, "roll");
                    const auto count = n(roll, "dice"), sides = n(roll, "sides"),
                               bonus = n(roll, "bonus");
                    const auto healed =
                        amount(in, "acceptedRoll", count + bonus, count * sides + bonus);
                    const auto *maximum = evaluation.find("hp.maximum");
                    if (!maximum || !maximum->effective.is_number_integer())
                        throw Invalid("Maximum hit points must be available before healing.");
                    const auto maxHp = maximum->effective.get<long long>();
                    const auto old = n(d.resources, "hp", maxHp);
                    details["healingRoll"] = healed;
                    details["healingTarget"] = recipient;
                    if (recipient == "self") {
                        result.document.resources["hp"] = std::min(maxHp, old + healed);
                        details["hpBefore"] = old;
                        details["hpAfter"] = result.document.resources["hp"];
                    }
                } else if (s(*use, "kind") == "cast-spell") {
                    const auto level = n(*use, "castLevel") +
                                       (b(*use, "upcastWithCharges") ? spent - ordinary : 0);
                    if (level > 9)
                        throw Invalid("The selected charges would exceed spell level 9.");
                    details["spellId"] = use->at("spellId");
                    details["castLevel"] = level;
                    details["spellOutcome"] = "Casting permission and charge expenditure recorded; "
                                              "target resolution follows the spell rules.";
                } else
                    throw Invalid("This item property has no implemented transition.");
                details["chargesBefore"] = before;
                details["chargesAfter"] = remaining;
                details["chargesConserved"] = conserve;
            } else
                throw Invalid("Unknown inventory command.");
        }
        if (balance < 0 || balance > 1000000000000LL)
            throw Invalid("The resulting currency balance is outside the supported range.");
        result.document.resources["currencyCp"] = balance;
        details["balanceAfter"] = balance;
        event(inv, command, details);
    } catch (const std::exception &error) {
        result.document = d;
        result.messages.push_back(
            {"error", "srd55.inventory.command", "/", error.what(), {ref("102")}});
    }
    return result;
}

TransitionResult consumeSrd55InventoryScroll(const CharacterDocument &original,
                                             const ResolvedRuleset &rules,
                                             const std::string &instanceId,
                                             const std::string &reason) {
    TransitionResult result{original, {}};
    try {
        if (!initialized(original) || !rules.valid() || rules.edition != original.edition ||
            rules.moduleVersion != original.moduleVersion)
            throw Invalid("Resolve the exact owned inventory before consuming a scroll.");
        auto &inv = result.document.resources["inventory"];
        auto &scroll = findInstance(inv, instanceId);
        const auto *item = catalog(rules, scroll);
        if (!item || (s(*item, "kind") != "magic-item" || !b(*item, "spellSelection")) ||
            !owned(scroll) || n(scroll, "quantity") != 1 ||
            s(scroll, "location", "carried") != "carried")
            throw Invalid("Select one owned, carried Spell Scroll instance.");
        const auto *spell = rules.find(s(scroll, "spellId"));
        const auto *rank = variant(*item, scroll);
        if (!spell || s(*spell, "kind") != "spell" || !rank ||
            n(*spell, "level", -1) != n(*rank, "scrollLevel", -2) ||
            n(scroll, "scrollLevel", -1) != n(*rank, "scrollLevel", -2) ||
            n(scroll, "scrollSaveDc", -1) != n(*rank, "saveDc", -2) ||
            n(scroll, "scrollAttackBonus", -1) != n(*rank, "attackBonus", -2) ||
            n(scroll, "charges", -1) != 1)
            throw Invalid(
                "The scroll's saved spell, rank, and remaining use do not match its source.");
        if (reason.find_first_not_of(" \t\r\n") == std::string::npos)
            throw Invalid("Record why this scroll was consumed.");
        scroll["quantity"] = 0;
        scroll["charges"] = 0;
        scroll["status"] = "consumed";
        scroll["equipped"] = false;
        scroll["attuned"] = false;
        CharacterCommand command{"srd55.inventory.consume-scroll",
                                 {{"instanceId", instanceId}, {"reason", reason}}};
        event(inv, command,
              {{"instanceId", instanceId},
               {"spellId", scroll["spellId"]},
               {"scrollLevel", scroll["scrollLevel"]},
               {"reason", reason},
               {"balanceBefore", n(original.resources, "currencyCp")},
               {"balanceAfter", n(original.resources, "currencyCp")},
               {"source", item->at("source")}});
    } catch (const std::exception &error) {
        result.document = original;
        result.messages.push_back({"error",
                                   "srd55.inventory.scroll",
                                   "/resources/inventory",
                                   error.what(),
                                   {ref("244")}});
    }
    return result;
}

std::vector<Message> validateSrd55MagicItems(const ContentPack &pack) {
    std::vector<Message> errors;
    const std::set<std::string> ops{"ability-bonus",
                                    "language-grant",
                                    "proficiency-bonus",
                                    "darkvision-minimum",
                                    "no-food-or-water-required",
                                    "regeneration",
                                    "weapon-training",
                                    "ability-check-bonus",
                                    "ability-minimum",
                                    "ac-bonus",
                                    "advantage",
                                    "armor-formula",
                                    "armor-speed-penalty-ignored",
                                    "armor-stealth-disadvantage-ignored",
                                    "armor-strength-requirement-ignored",
                                    "attack-disadvantage",
                                    "breathe-any-environment",
                                    "critical-hit-immunity",
                                    "darkvision-bonus",
                                    "disadvantage-to-attacker",
                                    "disadvantage-to-observer",
                                    "divination-targeting-immunity",
                                    "encumbrance-speed-penalty-ignored",
                                    "feather-fall",
                                    "hp-per-level",
                                    "ignore-difficult-terrain",
                                    "ignore-half-cover",
                                    "immunity",
                                    "jump",
                                    "resistance",
                                    "save-bonus",
                                    "scrying-immunity",
                                    "silent-footsteps",
                                    "speed-minimum",
                                    "speed-reduction-immunity",
                                    "spell-attack-bonus",
                                    "spell-save-bonus",
                                    "vulnerability",
                                    "water-walking",
                                    "weapon-attack-bonus",
                                    "weapon-damage-bonus"};
    const std::set<std::string> numericOps{
        "ability-bonus",       "ability-check-bonus", "ability-minimum",    "ac-bonus",
        "darkvision-minimum",  "darkvision-bonus",    "hp-per-level",       "proficiency-bonus",
        "save-bonus",          "speed-minimum",       "spell-attack-bonus", "spell-save-bonus",
        "weapon-attack-bonus", "weapon-damage-bonus"};
    const std::set<std::string> numericConditions{"", "attuned", "no-armor", "no-armor-or-shield",
                                                  "not-dwarf"};
    auto integer = [](const Json &value, const std::string &key, long long lo, long long hi) {
        return value.is_object() && value.contains(key) && value.at(key).is_number_integer() &&
               n(value, key, lo - 1) >= lo && n(value, key, lo - 1) <= hi;
    };
    auto strings = [](const Json &value) {
        return value.is_array() && std::all_of(value.begin(), value.end(), [](const Json &v) {
                   return v.is_string() && !v.get<std::string>().empty();
               });
    };
    auto ability = [](const std::string &value) {
        return std::find(abilityNames.begin(), abilityNames.end(), value) != abilityNames.end();
    };
    auto fail = [&](const Json &item, const std::string &field, const std::string &why) {
        errors.push_back(
            {"error", "srd55.inventory.content", s(item, "id") + "/" + field, why, {src(item)}});
    };
    for (const auto &item : pack.entries)
        if (s(item, "kind") == "magic-item") {
            for (const auto *key : {"category", "rarity", "metadata", "description", "wearSlot"})
                if (s(item, key).empty())
                    fail(item, key, "Magic item field must be nonempty text.");
            if (!item.contains("requiresAttunement") || !item.at("requiresAttunement").is_boolean())
                fail(item, "requiresAttunement", "Attunement requirement must be a boolean.");
            if (!item.contains("effects") || !item.at("effects").is_array() ||
                !item.contains("actions") || !item.at("actions").is_array())
                fail(item, "effects",
                     "Magic items require effect and action arrays, even when cataloged only.");
            const auto status = s(object(item, "coverage"), "status");
            if (status != "cataloged" && status != "partial" && status != "implemented")
                fail(item, "coverage",
                     "Coverage must explicitly be cataloged, partial, or implemented.");
            for (const auto *key : {"baseProfile"})
                if (item.contains(key) && !item.at(key).is_string())
                    fail(item, key, "Base profiles must be content identifiers.");
            for (const auto *key : {"baseOptions", "variants"})
                if (item.contains(key) && !item.at(key).is_array())
                    fail(item, key, "Item options must be arrays.");
            if (item.contains("baseOptions") && !strings(item.at("baseOptions")))
                fail(item, "baseOptions", "Base options must contain content identifiers.");
            for (const auto *key : {"spellSelection"})
                if (item.contains(key) && !item.at(key).is_boolean())
                    fail(item, key, "Item flags must be booleans.");
            if (item.contains("attunementPrerequisites")) {
                const auto &prerequisites = object(item, "attunementPrerequisites");
                if (!item.at("attunementPrerequisites").is_object())
                    fail(item, "attunementPrerequisites",
                         "Attunement prerequisites must be an object.");
                if (prerequisites.contains("anyClass") && !strings(prerequisites.at("anyClass")))
                    fail(item, "attunementPrerequisites",
                         "Required class alternatives must be identifiers.");
                for (const auto *key : {"spellcaster", "dwarfOrBeltOfDwarvenkind"})
                    if (prerequisites.contains(key) && !prerequisites.at(key).is_boolean())
                        fail(item, "attunementPrerequisites", "Attunement flags must be booleans.");
            }
            std::set<std::string> variants;
            for (const auto &v : array(item, "variants")) {
                if (s(v, "id").empty() || !variants.insert(s(v, "id")).second ||
                    s(v, "rarity").empty())
                    fail(item, "variants", "Each variant needs a unique identifier and rarity.");
            }
            for (const auto &v : array(item, "variants")) {
                for (const auto *key : {"effects", "curseEffects", "actions"})
                    if (v.contains(key) && !v.at(key).is_array())
                        fail(item, "variants",
                             std::string("Variant ") + key + " must be an array.");
                if (b(item, "spellSelection") &&
                    (!integer(v, "scrollLevel", 0, 9) || !integer(v, "saveDc", 1, 30) ||
                     !integer(v, "attackBonus", 0, 30)))
                    fail(item, "variants",
                         "Scroll variants require a rank, fixed save DC, and fixed attack bonus.");
            }
            Json effects = array(item, "effects");
            for (const auto &e : array(object(item, "curse"), "effects"))
                effects.push_back(e);
            for (const auto &v : array(item, "variants"))
                for (const auto *key : {"effects", "curseEffects"})
                    for (const auto &e : array(v, key))
                        effects.push_back(e);
            for (const auto &effect : effects) {
                const auto op = s(effect, "op");
                if (!ops.contains(op)) {
                    fail(item, "effects", "Unsupported item effect operation: " + op);
                    continue;
                }
                if ((numericOps.contains(op) || op == "armor-formula") &&
                    !numericConditions.contains(s(effect, "condition")))
                    fail(item, "effects/condition",
                         "This numeric effect condition is unsupported; it must not silently "
                         "apply.");
                if (numericOps.contains(op) && !integer(effect, "value", -1000, 10000))
                    fail(item, "effects/value",
                         "Numeric effect operations require an explicit bounded integer value.");
                if (op == "ability-bonus" &&
                    (!ability(s(effect, "ability")) || !integer(effect, "maxScore", 3, 30) ||
                     !integer(effect, "value", 1, 30)))
                    fail(item, "effects",
                         "Ability bonuses require a known ability, positive increase, and explicit "
                         "cap.");
                if (effect.contains("condition") && !effect.at("condition").is_string())
                    fail(item, "effects/condition", "Conditions must be text.");
                if (op == "language-grant" && s(effect, "languageId").empty())
                    fail(item, "effects/languageId",
                         "Language grants require a language identifier.");
                if ((op == "weapon-training" || effect.contains("weaponProfiles")) &&
                    (!effect.contains("weaponProfiles") || !strings(effect.at("weaponProfiles")) ||
                     effect.at("weaponProfiles").empty()))
                    fail(item, "effects/weaponProfiles",
                         "Weapon effects require a nonempty array of weapon identifiers.");
                if (op == "speed-minimum" &&
                    !std::set<std::string>{"walk", "swim", "fly", "climb", "burrow"}.contains(
                        s(effect, "mode")))
                    fail(item, "effects/mode", "Speed minimums require a supported movement mode.");
                if ((op == "resistance" || op == "vulnerability" || op == "immunity") &&
                    (!effect.contains("types") || !strings(effect.at("types")) ||
                     effect.at("types").empty()))
                    fail(item, "effects/types",
                         "Damage and condition effects require explicit types.");
                if (op == "ability-minimum" &&
                    (std::find(abilityNames.begin(), abilityNames.end(), s(effect, "ability")) ==
                         abilityNames.end() ||
                     !effect.contains("value") || !effect.at("value").is_number_integer() ||
                     n(effect, "value") < 3 || n(effect, "value") > 30))
                    fail(item, "effects",
                         "Ability minimums require an ability and score from 3 to 30.");
                if (effect.contains("value") &&
                    (!effect.at("value").is_number_integer() || n(effect, "value") < -1000 ||
                     n(effect, "value") > 10000))
                    fail(item, "effects", "Effect values must be bounded whole numbers.");
                if (effect.contains("types") &&
                    (!effect.at("types").is_array() ||
                     !std::all_of(effect.at("types").begin(), effect.at("types").end(),
                                  [](const Json &v) { return v.is_string(); })))
                    fail(item, "effects/types", "Effect types must be a list of strings.");
                if (op == "armor-formula" &&
                    (!effect.contains("base") || !effect.at("base").is_number_integer() ||
                     !effect.contains("abilities") || !effect.at("abilities").is_array()))
                    fail(item, "effects",
                         "Armor formulas require a numeric base and ability list.");
            }
            for (const auto &effect : effects)
                if (s(effect, "op") == "armor-formula") {
                    if (!integer(effect, "base", 0, 100) || !effect.contains("abilities") ||
                        !strings(effect.at("abilities")))
                        fail(item, "effects",
                             "Armor formulas require a bounded base and ability list.");
                    else
                        for (const auto &key : effect.at("abilities"))
                            if (!ability(key.get<std::string>()))
                                fail(item, "effects/abilities", "Unknown armor formula ability.");
                }
            const auto &charge = object(item, "charges");
            if (b(item, "spellSelection") &&
                (array(item, "variants").empty() || s(charge, "unit") != "consumable" ||
                 n(charge, "maximum") != 1 || n(charge, "initial") != 1 ||
                 !b(charge, "consumedWhenEmpty")))
                fail(item, "spellSelection",
                     "Scroll binding requires ranked variants and one consumed physical use.");
            if (item.contains("charges")) {
                if (charge.empty() || !charge.contains("maximum") ||
                    !charge.at("maximum").is_number_integer() || n(charge, "maximum") < 1 ||
                    n(charge, "maximum") > 10000)
                    fail(item, "charges", "Charge capacity must be a positive bounded integer.");
                if (!integer(charge, "initial", 0, n(charge, "maximum")))
                    fail(item, "charges/initial",
                         "Initial charges must be an explicit integer within capacity.");
                if (!std::set<std::string>{"charges", "uses", "consumable"}.contains(
                        s(charge, "unit")))
                    fail(item, "charges/unit",
                         "Charge units must distinguish charges, uses, and consumables.");
                for (const auto *key : {"mundaneWhenEmpty", "consumedWhenEmpty"})
                    if (charge.contains(key) && !charge.at(key).is_boolean())
                        fail(item, "charges", std::string(key) + " must be a boolean.");
                if (charge.contains("depletion")) {
                    const auto &depletion = object(charge, "depletion");
                    if (!integer(depletion, "sides", 2, 100) ||
                        !integer(depletion, "destroyOn", 1, n(depletion, "sides")))
                        fail(item, "charges/depletion",
                             "Depletion requires a supported die and destructive result.");
                }
                if (charge.contains("recharge") && !charge.at("recharge").is_object())
                    fail(item, "charges/recharge", "Recharge must be an object.");
                if (n(charge, "initial", -1) < 0 || n(charge, "initial", -1) > n(charge, "maximum"))
                    fail(item, "charges/initial", "Initial charges must fit capacity.");
                const auto &recharge = object(charge, "recharge");
                if (!recharge.empty()) {
                    if (s(recharge, "event") != "dawn" && s(recharge, "event") != "long-rest")
                        fail(item, "charges/recharge", "Unsupported recharge trigger.");
                    if (recharge.contains("fixed") && !integer(recharge, "fixed", 1, 10000))
                        fail(item, "charges/recharge",
                             "A fixed refill must be a positive bounded integer.");
                    if (!recharge.contains("fixed") && (!integer(recharge, "dice", 1, 100) ||
                                                        !integer(recharge, "sides", 2, 100) ||
                                                        !integer(recharge, "bonus", 0, 10000)))
                        fail(item, "charges/recharge",
                             "Recharge dice require explicit integer count, sides, and bonus.");
                    if (!recharge.contains("fixed") &&
                        (n(recharge, "dice") < 1 || n(recharge, "dice") > 100 ||
                         n(recharge, "sides") < 2 || n(recharge, "sides") > 100 ||
                         n(recharge, "bonus") < 0))
                        fail(item, "charges/recharge",
                             "Recharge dice must be supported positive dice plus a nonnegative "
                             "bonus.");
                }
            }
            auto uniqueActions = [&](const Json &values, std::set<std::string> used) {
                for (const auto &a : values)
                    if (!used.insert(s(a, "id")).second)
                        fail(item, "actions",
                             "Action IDs must be unique within an item and selected variant.");
                return used;
            };
            const auto baseActionIds = uniqueActions(array(item, "actions"), {});
            for (const auto &v : array(item, "variants"))
                uniqueActions(array(v, "actions"), baseActionIds);
            Json checkedActions = array(item, "actions");
            for (const auto &v : array(item, "variants"))
                for (const auto &a : array(v, "actions"))
                    checkedActions.push_back(a);
            for (const auto &a : checkedActions) {
                if (s(a, "id").empty())
                    fail(item, "actions", "Action IDs must be nonempty.");
                const auto kind = s(a, "kind");
                if (kind != "cast-spell" && kind != "heal" && kind != "permanent-ability")
                    fail(item, "actions", "Unsupported magic-item transition kind: " + kind);
                for (const auto *key :
                     {"upcastWithCharges", "selfOnly", "usesActorCastingAbility", "administerable"})
                    if (a.contains(key) && !a.at(key).is_boolean())
                        fail(item, "actions", std::string(key) + " must be a boolean.");
                for (const auto *key :
                     {"chargeCost", "maximumCharges", "saveDc", "attackBonus", "castLevel", "value",
                      "maxScore", "hours", "maximumDays", "dormantYears"})
                    if (a.contains(key) && !a.at(key).is_number_integer())
                        fail(item, "actions", std::string(key) + " must be an integer.");
                if (a.contains("saveDc") && !integer(a, "saveDc", 1, 30))
                    fail(item, "actions", "An item save DC must be between 1 and 30.");
                if (a.contains("attackBonus") && !integer(a, "attackBonus", -10, 30))
                    fail(item, "actions", "An item attack bonus must be between -10 and 30.");
                if (kind == "heal") {
                    const auto &roll = object(a, "roll");
                    if (!integer(roll, "dice", 1, 100) || !integer(roll, "sides", 2, 100) ||
                        !integer(roll, "bonus", 0, 10000))
                        fail(item, "actions/roll",
                             "Healing actions require explicit bounded integer dice and a bonus.");
                }
                if (kind == "permanent-ability" &&
                    (!integer(a, "maxScore", 3, 30) || !integer(a, "value", 1, 30) ||
                     !integer(a, "dormantYears", 100, 100)))
                    fail(
                        item, "actions",
                        "Permanent study must use a legal increase, cap, and century of dormancy.");
                if (n(a, "chargeCost") < 0 || n(a, "chargeCost") > n(charge, "maximum"))
                    fail(item, "actions", "Action cost exceeds item capacity.");
                if (kind == "cast-spell" &&
                    (s(a, "spellId").empty() || n(a, "castLevel", -1) < 0 || n(a, "castLevel") > 9))
                    fail(item, "actions",
                         "Spell properties require a source spell ID and legal cast level.");
                if (kind == "permanent-ability" &&
                    (std::find(abilityNames.begin(), abilityNames.end(), s(a, "ability")) ==
                         abilityNames.end() ||
                     n(a, "value") < 1 || n(a, "maxScore") > 30 || n(a, "hours") != 48 ||
                     n(a, "maximumDays") != 6))
                    fail(item, "actions",
                         "Manual/tome study requires a supported ability increase and the "
                         "published study bounds.");
            }
        }
    return errors;
}
std::vector<Message> validateSrd55InventoryReferences(const ResolvedRuleset &rules) {
    std::vector<Message> errors;
    for (const auto &[id, item] : rules.content)
        if (s(item, "kind") == "magic-item") {
            auto check = [&](const std::string &target, const std::set<std::string> &kinds) {
                const auto *found = rules.find(target);
                if (!found || !kinds.contains(s(*found, "kind")))
                    errors.push_back({"error",
                                      "srd55.inventory.reference",
                                      id,
                                      "Missing or wrong-kind magic-item reference: " + target,
                                      {src(item)}});
            };
            if (item.contains("baseProfile"))
                check(s(item, "baseProfile"), {"weapon", "armor", "shield", "gear"});
            for (const auto &profile : array(item, "baseOptions"))
                if (profile.is_string())
                    check(profile.get<std::string>(), {"weapon", "armor", "shield", "gear"});
            Json effects = array(item, "effects");
            for (const auto &v : array(item, "variants"))
                for (const auto &effect : array(v, "effects"))
                    effects.push_back(effect);
            for (const auto &effect : effects) {
                if (s(effect, "op") == "language-grant")
                    check(s(effect, "languageId"), {"language"});
                for (const auto &profile : array(effect, "weaponProfiles"))
                    if (profile.is_string())
                        check(profile.get<std::string>(), {"weapon"});
            }
            Json referencedActions = array(item, "actions");
            for (const auto &v : array(item, "variants"))
                for (const auto &action : array(v, "actions"))
                    referencedActions.push_back(action);
            for (const auto &action : referencedActions)
                if (action.contains("spellId")) {
                    check(s(action, "spellId"), {"spell"});
                    const auto *spell = rules.find(s(action, "spellId"));
                    if (spell && n(action, "castLevel") < n(*spell, "level"))
                        errors.push_back({"error",
                                          "srd55.inventory.spellLevel",
                                          id,
                                          "An item cannot cast a spell below its own spell level.",
                                          {src(item)}});
                }
        }
    return errors;
}
} // namespace dnd::srd55v2
