#include "dnd/class_actions.hpp"
#include "dnd/companions.hpp"
#include "dnd/lifecycle.hpp"
#include "dnd/srd55_inventory.hpp"
#include "srd55_v2_internal.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace dnd::srd55v2 {
namespace {
int integerArg(const Json &input, const std::string &key, int low, int high, int fallback = -1) {
    if (!input.contains(key)) {
        if (fallback >= low && fallback <= high)
            return fallback;
        throw std::runtime_error("Enter " + key + ".");
    }
    const auto &value = input.at(key);
    if (!value.is_number_integer() || value < low || value > high)
        throw std::runtime_error(key + " must be a whole number from " + std::to_string(low) +
                                 " to " + std::to_string(high) + ".");
    return value.get<int>();
}
bool flagArg(const Json &input, const std::string &key, bool fallback = false) {
    if (!input.contains(key))
        return fallback;
    if (!input[key].is_boolean())
        throw std::runtime_error(key + " must be true or false.");
    return input[key].get<bool>();
}
int stat(const Evaluation &e, const std::string &id) {
    const auto *value = e.find(id);
    if (!value || !value->effective.is_number_integer())
        throw std::runtime_error("Required statistic unavailable: " + id);
    return value->effective.get<int>();
}
std::map<std::string, int> levels(const CharacterDocument &d) {
    std::map<std::string, int> result;
    const int total = std::clamp(number(d.choices, "/level", 1), 1, 20);
    const auto initial = text(d.choices, "/classId");
    for (int i = 1; i <= total; ++i) {
        const auto id = i > 1 && d.choices.value("multiclass", false)
                            ? text(d.choices, "/advancement/" + std::to_string(i) + "/classId")
                            : initial;
        if (!id.empty())
            ++result[id];
    }
    return result;
}
const ResourceDefinition &resource(const Evaluation &e, const std::string &id) {
    const auto found = std::find_if(e.resources.begin(), e.resources.end(),
                                    [&](const auto &r) { return r.id == id; });
    if (found == e.resources.end())
        throw std::runtime_error("This resource is unavailable: " + id);
    return *found;
}
int current(const CharacterDocument &d, const ResourceDefinition &r) {
    if (!d.resources.contains(r.id))
        return r.maximum;
    const auto &value = d.resources.at(r.id);
    if (!value.is_number_integer() || value < 0 || value > r.maximum)
        throw std::runtime_error("Correct the current value of " + r.label + " before using it.");
    return value.get<int>();
}
void spend(CharacterDocument &d, const Evaluation &e, const std::string &id, int amount) {
    const auto &r = resource(e, id);
    const int available = current(d, r);
    if (amount < 1 || amount > available)
        throw std::runtime_error("Insufficient " + r.label + "; current " +
                                 std::to_string(available) + ", required " +
                                 std::to_string(amount) + ".");
    d.resources[id] = available - amount;
}
int regain(CharacterDocument &d, const Evaluation &e, const std::string &id, int amount) {
    const auto &r = resource(e, id);
    const int before = current(d, r);
    const int after = std::min(r.maximum, before + amount);
    d.resources[id] = after;
    return after - before;
}
Field boolean(const std::string &path, const std::string &label, const std::string &help = "") {
    return {path, label, "boolean", 0, 1, {}, false, help};
}
ActionDefinition action(std::string id, std::string label, std::string description,
                        std::vector<Field> fields, bool available, const std::string &page) {
    return {
        std::move(id),
        std::move(label),
        std::move(description),
        std::move(fields),
        available,
        available
            ? ""
            : "Complete the character and resolve its validation errors before using this action.",
        {ref(page)}};
}
std::vector<Choice> resourceOptions(const CharacterDocument &d, const Evaluation &e) {
    std::vector<Choice> result;
    for (const auto &r : e.resources)
        if (r.maximum > 0) {
            int available = 0;
            try {
                available = current(d, r);
            } catch (...) {
            }
            result.push_back({r.id,
                              r.label + " (" + std::to_string(available) + " / " +
                                  std::to_string(r.maximum) + ")",
                              available > 0, available > 0 ? "" : "No uses remain.", r.sources});
        }
    return result;
}
bool scrollDefinition(const ResolvedRuleset &rules, const Json &instance) {
    if (!instance.is_object())
        return false;
    const auto *item = rules.find(instance.value("itemId", ""));
    return item && item->value("kind", "") == "magic-item" && item->value("spellSelection", false);
}
bool recovery(const std::string &rule, const std::string &rest, int &amount) {
    const auto token = rest + ":";
    const auto found = rule.find(token);
    if (found != std::string::npos) {
        const auto end = rule.find(';', found);
        const auto value =
            rule.substr(found + token.size(),
                        end == std::string::npos ? std::string::npos : end - found - token.size());
        if (value == "all") {
            amount = -1;
            return true;
        }
        try {
            std::size_t used = 0;
            const int n = std::stoi(value, &used);
            if (used == value.size() && n >= 0) {
                amount = n;
                return true;
            }
        } catch (...) {
        }
        return false;
    }
    if ((rest == "long-rest" &&
         (rule == "Long Rest" || rule == "Short or Long Rest" || rule == "Short or Long Rest" ||
          rule == "Long Rest restores spent Hit Dice" ||
          rule == "Initiative, Short Rest, or Long Rest")) ||
        (rest == "short-rest" && (rule == "Short Rest" || rule == "Short or Long Rest" ||
                                  rule == "Initiative, Short Rest, or Long Rest"))) {
        amount = -1;
        return true;
    }
    return false;
}
void restResources(CharacterDocument &d, const Evaluation &e, const std::string &rest) {
    for (const auto &r : e.resources) {
        int amount = 0;
        if (!recovery(r.recharge, rest, amount))
            continue;
        d.resources[r.id] = amount < 0 ? r.maximum : std::min(r.maximum, current(d, r) + amount);
    }
}
void requireRestWindow(const CharacterDocument &d) {
    if (text(d.resources, "/lifecycle/restWindow") != "short-rest")
        throw std::runtime_error(
            "First record the qualifying Short Rest; this option belongs to that rest.");
}
std::string slotResource(const Json &input, const Evaluation &e, int &slotLevel) {
    const auto pool = input.value("pool", std::string("spellcasting"));
    if (pool == "pact") {
        slotLevel = stat(e, "pactMagic.level");
        return "pactMagic.slots";
    }
    if (pool != "spellcasting")
        throw std::runtime_error("Choose Spellcasting or Pact Magic slots.");
    slotLevel = integerArg(input, "slotLevel", 1, 9);
    return "spellSlots." + std::to_string(slotLevel);
}
} // namespace

void linkSrd55Resources(Evaluation &e) {
    // Resource capacity and its displayed calculation must follow the same explicit override.
    for (auto &r : e.resources)
        if (r.maximumCalculationId.empty()) {
            if (r.id == "hp" && e.find("hp.maximum"))
                r.maximumCalculationId = "hp.maximum";
            else if (e.find(r.id) && e.find(r.id)->normal.is_number_integer())
                r.maximumCalculationId = r.id;
            else if (const auto colon = r.id.find(':'); colon != std::string::npos) {
                const auto id =
                    "feature." + r.id.substr(0, colon) + ".resource-" + r.id.substr(colon + 1);
                if (e.find(id)) {
                    r.maximumCalculationId = id;
                    r.maximumValuePointer = "/maximum";
                }
            }
        }
}

void appendLifecycleActions(const CharacterDocument &d, const ResolvedRuleset &rules,
                            Evaluation &e) {
    const auto classLevels = levels(d);
    const auto classLevel = [&](const std::string &id) {
        const auto found = classLevels.find("srd55:" + id);
        return found == classLevels.end() ? 0 : found->second;
    };
    const bool valid = e.complete();
    const bool character = e.find("hp.maximum") != nullptr;
    if (character) {
        e.actions.push_back(action("srd55.resources.spend", "Spend a resource",
                                   "Record an already resolved feature or spell-slot cost. "
                                   "Recalculation never spends resources.",
                                   {select("/resource", "Resource", resourceOptions(d, e)),
                                    integer("/amount", "Amount to spend", 1, 1000000)},
                                   valid, "187"));
        e.actions.push_back(action(
            "srd55.resources.short-rest", "Complete a Short Rest",
            "Record at least one uninterrupted hour and apply the published partial/full "
            "recoveries. Hit Dice and rest-based choices are separate explicit actions.",
            {integer("/hours", "Rest duration (hours)", 1, 72),
             boolean("/interrupted", "Interrupted by initiative, leveled spellcasting, or damage")},
            valid, "187"));
        e.actions.back().initialInputs = {{"hours", 1}, {"interrupted", false}};
        const bool trance = text(d.choices, "/speciesId") == "srd55:elf";
        e.actions.push_back(action(
            "srd55.resources.long-rest", "Complete a Long Rest",
            "Record the rest and waiting interval. A completed Long Rest restores HP, spent Hit "
            "Dice and applicable resources; it never changes accepted HP rolls.",
            {integer("/hours", "Rest duration (hours)", 1, 720,
                     trance
                         ? "A complete rest requires four hours of Trance, plus interruption "
                           "extensions."
                         : "A complete rest requires eight hours, plus interruption extensions."),
             integer("/sleepHours", trance ? "Hours in Trance" : "Hours asleep", 0, 720),
             integer("/hoursSincePrevious", "Hours since the previous Long Rest ended", 0, 1000000),
             integer("/interruptions", "Interruptions followed by resumed rest", 0, 100),
             boolean("/unfinished", "The rest was interrupted and not resumed")},
            valid, "185"));
        e.actions.back().initialInputs = {{"hours", trance ? 4 : 8},
                                          {"sleepHours", trance ? 4 : 6},
                                          {"hoursSincePrevious", 16},
                                          {"interruptions", 0},
                                          {"unfinished", false}};
        std::vector<Choice> dice;
        for (const auto &r : e.resources)
            if (r.id.starts_with("hitDice."))
                dice.push_back({r.id, r.label, true, {}, r.sources});
        auto hd = action(
            "srd55.resources.spend-hit-die", "Spend one Hit Die after a Short Rest",
            "Enter the accepted die result. Healing uses the current Constitution modifier and "
            "cannot exceed the effective HP maximum.",
            {select("/die", "Hit Die pool", dice), integer("/roll", "Accepted die result", 1, 12)},
            valid && text(d.resources, "/lifecycle/restWindow") == "short-rest", "187");
        if (!hd.available)
            hd.reason = "Record a qualifying Short Rest before spending a Hit Die.";
        e.actions.push_back(hd);
    }
    if (classLevel("wizard")) {
        std::vector<Field> slots;
        for (int i = 1; i <= 5; ++i)
            slots.push_back(integer("/slots/" + std::to_string(i),
                                    "Level " + std::to_string(i) + " slots to recover", 0, 20));
        if (classLevel("warlock"))
            slots.push_back(
                integer("/pactSlots", "Pact Magic slots to recover", 0, 20,
                        "Use the Pact slot level in the same recovery budget. Pact slots "
                        "normally already recover fully at this Short Rest."));
        auto a = action("srd55.resources.arcane-recovery", "Use Arcane Recovery",
                        "Recover expended spell slots totaling no more than half Wizard "
                        "level, rounded up. Level 6+ slots are excluded.",
                        slots, valid && text(d.resources, "/lifecycle/restWindow") == "short-rest",
                        "78");
        if (!a.available)
            a.reason = "Record the Short Rest at which Arcane Recovery is used.";
        e.actions.push_back(a);
        std::set<std::string> known;
        if (e.moduleData.contains("spellbooks") && e.moduleData["spellbooks"].contains("wizard"))
            for (const auto &id : e.moduleData["spellbooks"]["wizard"])
                known.insert(id.get<std::string>());
        const int highest = std::min(9, (classLevel("wizard") + 1) / 2);
        std::vector<Choice> spells;
        for (const auto &[id, spell] : rules.content)
            if (spell.value("kind", "") == "spell" && spell.value("level", 0) > 0 &&
                spell.value("level", 99) <= highest) {
                const auto lists = strings(spell, "/lists");
                if (std::find(lists.begin(), lists.end(), "wizard") == lists.end())
                    continue;
                spells.push_back({id,
                                  spell.value("name", id),
                                  !known.contains(id),
                                  known.contains(id) ? "Already in the spellbook." : "",
                                  {sourceFromJson(spell.at("source"))}});
            }
        e.actions.push_back(
            action("srd55.spells.copy", "Copy a Wizard spell from a spellbook",
                   "Record copying from another spellbook, spending 50 GP and two hours per spell "
                   "level. The permanent spellbook and current funds change together.",
                   {select("/sourceType", "Source type",
                           {{"spellbook", "Another spellbook", true, {}, {ref("78")}}}),
                    select("/spellId", "Spell", spells),
                    integer("/minutes", "Minutes spent studying", 0, 1000000),
                    integer("/paidCp", "Cost paid (copper pieces; 100 cp = 1 gp)", 0, 1000000000),
                    {"/sourceNote",
                     "Source spellbook",
                     "text",
                     0,
                     0,
                     {},
                     false,
                     "Identify the spellbook provided by the campaign."}},
                   valid, "78"));
        e.actions.back().initialInputs = {{"sourceType", "spellbook"}};
        std::vector<Choice> scrolls;
        const auto *owned = at(d.resources, "/inventory/instances");
        if (owned && owned->is_array())
            for (const auto &item : *owned) {
                if (!scrollDefinition(rules, item) || item.value("status", "") != "owned" ||
                    number(item, "/quantity") < 1 || item.value("location", "carried") != "carried")
                    continue;
                const auto *spell = rules.find(item.value("spellId", ""));
                if (!spell || spell->value("kind", "") != "spell")
                    continue;
                const auto id = spell->value("id", "");
                const auto allowed =
                    std::find_if(spells.begin(), spells.end(),
                                 [&](const auto &option) { return option.id == id; });
                const bool eligible = allowed != spells.end() && allowed->available;
                scrolls.push_back(
                    {item.value("id", ""),
                     spell->value("name", id) + " — " + item.value("id", ""),
                     eligible,
                     eligible ? ""
                     : known.contains(id)
                         ? "Already in the spellbook."
                         : "Requires a level 1+ Wizard spell of a level you can prepare.",
                     {ref("244")}});
            }
        auto copy = action(
            "srd55.spells.copy-scroll", "Copy a Wizard spell from an owned scroll",
            "Pay the copying cost and record the accepted Intelligence (Arcana) check. The scroll "
            "is consumed on success or failure; only success adds the spell to the book.",
            {select("/instanceId", "Owned Spell Scroll", scrolls),
             integer("/minutes", "Minutes spent studying", 0, 1000000),
             integer("/paidCp", "Cost paid (copper pieces)", 0, 1000000000),
             integer("/acceptedD20", "Accepted d20 result", 1, 20),
             integer("/bonus", "Other resolved check bonus", -100000, 100000,
                     "Include only bonuses not already in the displayed Arcana modifier; explain "
                     "any nonzero bonus."),
             {"/bonusReason", "Reason for other check bonus", "text", 0, 0, {}, false, {}}},
            valid && std::any_of(scrolls.begin(), scrolls.end(),
                                 [](const auto &option) { return option.available; }),
            "78, 244");
        if (!copy.available && valid)
            copy.reason = "Acquire an eligible Spell Scroll containing a Wizard spell that is not "
                          "already in the spellbook.";
        copy.initialInputs = {{"bonus", 0}, {"bonusReason", ""}};
        e.actions.push_back(std::move(copy));
    }
    if (classLevel("sorcerer") >= 5)
        e.actions.push_back(action(
            "srd55.resources.sorcerous-restoration", "Use Sorcerous Restoration",
            "Recover up to half Sorcerer level, rounded down, in expended Sorcery Points at a "
            "Short Rest, once per Long Rest.",
            {integer("/points", "Points to recover", 1, std::max(1, classLevel("sorcerer") / 2))},
            valid && text(d.resources, "/lifecycle/restWindow") == "short-rest", "66"));
    if (classLevel("warlock") >= 2)
        e.actions.push_back(
            action("srd55.resources.magical-cunning", "Use Magical Cunning",
                   "After the one-minute rite, recover the published number of expended Pact Magic "
                   "slots. This spends its once-per-Long-Rest use.",
                   {integer("/minutes", "Minutes spent in the rite", 1, 1000)}, valid, "72"));
    if (classLevel("sorcerer") >= 2) {
        const std::vector<Choice> pools = {
            {"spellcasting", "Spellcasting", true, {}, {ref("26")}},
            {"pact", "Pact Magic", classLevel("warlock") > 0, "Requires Pact Magic.", {ref("26")}}};
        e.actions.push_back(action(
            "srd55.resources.slot-to-points", "Convert a spell slot to Sorcery Points",
            "Expend one spell slot and regain its level in Sorcery Points, up to the Sorcerer "
            "maximum.",
            {select("/pool", "Slot pool", pools),
             integer("/slotLevel", "Spellcasting slot level (ignored for Pact Magic)", 1, 9)},
            valid, "65"));
        e.actions.push_back(
            action("srd55.resources.points-to-slot", "Create a spell slot with Sorcery Points",
                   "Use Font of Magic's class-level gates and point costs. Created slots are "
                   "tracked separately and expire at the next Long Rest.",
                   {integer("/slotLevel", "Created slot level", 1, 5)}, valid, "65"));
    }
    if (classLevel("bard") >= 5) {
        std::vector<Choice> pools = {
            {"spellcasting", "Spellcasting", true, {}, {ref("26")}},
            {"pact", "Pact Magic", classLevel("warlock") > 0, "Requires Pact Magic.", {ref("26")}}};
        e.actions.push_back(
            action("srd55.resources.font-of-inspiration",
                   "Restore one Bardic Inspiration with a spell slot",
                   "Expend a spell slot to recover one expended use of Bardic Inspiration.",
                   {select("/pool", "Slot pool", pools),
                    integer("/slotLevel", "Spellcasting slot level", 1, 9)},
                   valid, "32"));
    }
    appendSrd55InventoryActions(d, rules, e);
    appendSrd55CompanionActions(d, rules, e);
    appendSrd55ClassActions(d, rules, e);
    appendSrd55HistoryActions(d, rules, e);
}

static TransitionResult applyLifecycleCommandImpl(const CharacterDocument &before,
                                                  const ResolvedRuleset &rules,
                                                  const CharacterCommand &command) {
    if (command.id.starts_with("srd55.inventory."))
        return applySrd55InventoryCommand(before, rules, command);
    if (command.id.starts_with("srd55.companions."))
        return applySrd55CompanionCommand(before, rules, command);
    if (command.id.starts_with("srd55.classes."))
        return applySrd55ClassCommand(before, rules, command);
    if (command.id.starts_with("srd55.history."))
        return applySrd55HistoryCommand(before, rules, command);
    TransitionResult result{before, {}};
    auto &d = result.document;
    const auto &input = command.inputs;
    const auto e = dnd::evaluate(before, rules);
    const auto classLevels = levels(before);
    const auto level = [&](const std::string &cls) {
        auto found = classLevels.find("srd55:" + cls);
        return found == classLevels.end() ? 0 : found->second;
    };
    try {
        if (!e.complete())
            throw std::runtime_error("Complete the character before applying lifecycle actions.");
        if (command.id == "srd55.resources.spend") {
            spend(d, e, input.value("resource", std::string{}),
                  integerArg(input, "amount", 1, 1000000));
            d.resources["lifecycle"]["restWindow"] = "";
        } else if (command.id == "srd55.resources.short-rest") {
            integerArg(input, "hours", 1, 72);
            if (current(d, resource(e, "hp")) < 1)
                throw std::runtime_error("A Short Rest must start with at least 1 HP.");
            if (flagArg(input, "interrupted"))
                throw std::runtime_error("An interrupted Short Rest confers no benefits.");
            restResources(d, e, "short-rest");
            if (d.resources.contains("barbarian:relentless-rage-attempts"))
                d.resources["barbarian:relentless-rage-attempts"] = 0;
            if (level("ranger") >= 10 && d.resources.contains("exhaustion"))
                d.resources["exhaustion"] =
                    std::max(0, d.resources.at("exhaustion").get<int>() - 1);
            d.resources["lifecycle"]["restWindow"] = "short-rest";
            recordSrd55HistoryTrigger(before, d, "short-rest");
        } else if (command.id == "srd55.resources.long-rest") {
            const bool trance = text(d.choices, "/speciesId") == "srd55:elf";
            const int minimum = trance ? 4 : 8;
            const int hours = integerArg(input, "hours", 1, 720),
                      interruptions = integerArg(input, "interruptions", 0, 100, 0);
            if (current(d, resource(e, "hp")) < 1)
                throw std::runtime_error("A Long Rest must start with at least 1 HP.");
            const int waiting = integerArg(input, "hoursSincePrevious", 0, 1000000, 0);
            if (flagArg(d.resources.value("lifecycle", Json::object()), "hasLongRest") &&
                waiting < 16)
                throw std::runtime_error("Wait at least 16 hours after the previous Long Rest "
                                         "ended before starting another.");
            if (flagArg(input, "unfinished")) {
                if (hours < 1)
                    throw std::runtime_error("No rest benefit was earned.");
                restResources(d, e, "short-rest");
                if (d.resources.contains("barbarian:relentless-rage-attempts"))
                    d.resources["barbarian:relentless-rage-attempts"] = 0;
                if (level("ranger") >= 10 && d.resources.contains("exhaustion"))
                    d.resources["exhaustion"] =
                        std::max(0, d.resources.at("exhaustion").get<int>() - 1);
                d.resources["lifecycle"]["restWindow"] = "short-rest";
                recordSrd55HistoryTrigger(before, d, "short-rest");
                result.messages.push_back(
                    {"info",
                     "rest.partial",
                     "/",
                     "The interrupted Long Rest grants only the completed Short Rest benefits.",
                     {ref("185")}});
            } else {
                if (hours < minimum + interruptions)
                    throw std::runtime_error("The resumed Long Rest needs its normal duration plus "
                                             "one additional hour per interruption.");
                const int sleep = integerArg(input, "sleepHours", 0, 720);
                if (sleep < (trance ? 4 : 6))
                    throw std::runtime_error(trance ? "Complete four hours of Trance."
                                                    : "Complete at least six hours of sleep.");
                if (sleep > hours)
                    throw std::runtime_error(
                        "Sleep or Trance time cannot exceed the recorded rest duration.");
                d.resources.erase("createdSpellSlots");
                d.resources.erase("druidCreatedSpellSlots");
                d.resources.erase("hpMaximumReduction");
                expireSrd55CompanionsOnLongRest(d);
                const auto refreshed = dnd::evaluate(d, rules);
                restResources(d, refreshed, "long-rest");
                d.resources["hp"] = stat(refreshed, "hp.maximum");
                if (d.resources.contains("barbarian:relentless-rage-attempts"))
                    d.resources["barbarian:relentless-rage-attempts"] = 0;
                if (d.resources.contains("wizard:overchannel-uses"))
                    d.resources["wizard:overchannel-uses"] = 0;
                if (d.resources.contains("cleric:divine-intervention-rests"))
                    d.resources["cleric:divine-intervention-rests"] = std::max(
                        0, d.resources.at("cleric:divine-intervention-rests").get<int>() - 1);
                if (d.resources.contains("exhaustion"))
                    d.resources["exhaustion"] =
                        std::max(0, d.resources.at("exhaustion").get<int>() - 1);
                d.resources["lifecycle"]["hasLongRest"] = true;
                d.resources["lifecycle"]["restWindow"] = "long-rest";
                recordSrd55HistoryTrigger(before, d, "long-rest");
            }
        } else if (command.id == "srd55.resources.spend-hit-die") {
            requireRestWindow(d);
            const auto pool = input.value("die", std::string{});
            if (!pool.starts_with("hitDice.d"))
                throw std::runtime_error("Choose a Hit Die pool.");
            const int sides = std::stoi(pool.substr(9));
            const int roll = integerArg(input, "roll", 1, sides);
            spend(d, e, pool, 1);
            const int healed = std::max(1, roll + stat(e, "modifier.constitution"));
            regain(d, e, "hp", healed);
            if (!d.rolls.contains("actions"))
                d.rolls["actions"] = Json::array();
            d.rolls["actions"].push_back(
                {{"action", command.id}, {"sides", sides}, {"result", roll}});
        } else if (command.id == "srd55.resources.arcane-recovery") {
            requireRestWindow(d);
            if (!level("wizard"))
                throw std::runtime_error("Arcane Recovery requires Wizard levels.");
            int cost = 0;
            for (int rank = 1; rank <= 5; ++rank) {
                const int count = number(input, "/slots/" + std::to_string(rank));
                if (count < 0 || count > 20)
                    throw std::runtime_error("Invalid recovered slot count.");
                cost += rank * count;
                if (count) {
                    const auto id = "spellSlots." + std::to_string(rank);
                    const auto &r = resource(e, id);
                    if (current(d, r) + count > r.maximum)
                        throw std::runtime_error(
                            "You cannot recover more slots than were expended.");
                    d.resources[id] = current(d, r) + count;
                }
            }
            const int pact = integerArg(input, "pactSlots", 0, 20, 0);
            if (pact) {
                const int rank = stat(e, "pactMagic.level");
                if (rank > 5)
                    throw std::runtime_error("Arcane Recovery cannot restore level 6+ slots.");
                const auto &r = resource(e, "pactMagic.slots");
                if (current(d, r) + pact > r.maximum)
                    throw std::runtime_error(
                        "Pact Magic slots already recovered at the Short Rest.");
                d.resources[r.id] = current(d, r) + pact;
                cost += rank * pact;
            }
            if (cost < 1 || cost > (level("wizard") + 1) / 2)
                throw std::runtime_error(
                    "Arcane Recovery exceeds its total slot-level allowance, or recovers nothing.");
            spend(d, e, "wizard:arcane-recovery", 1);
        } else if (command.id == "srd55.resources.sorcerous-restoration") {
            requireRestWindow(d);
            if (level("sorcerer") < 5)
                throw std::runtime_error("Sorcerous Restoration requires Sorcerer level 5.");
            const int points = integerArg(input, "points", 1, level("sorcerer") / 2);
            const auto &pool = resource(e, "sorcerer:sorcery-points");
            if (current(d, pool) + points > pool.maximum)
                throw std::runtime_error("Recover only expended Sorcery Points.");
            spend(d, e, "sorcerer:sorcerous-restoration", 1);
            regain(d, e, pool.id, points);
        } else if (command.id == "srd55.resources.magical-cunning") {
            integerArg(input, "minutes", 1, 1000);
            if (level("warlock") < 2)
                throw std::runtime_error("Magical Cunning requires Warlock level 2.");
            const auto &pool = resource(e, "pactMagic.slots");
            if (current(d, pool) == pool.maximum)
                throw std::runtime_error("No Pact Magic slots are expended.");
            spend(d, e, "warlock:magical-cunning", 1);
            regain(d, e, pool.id, level("warlock") == 20 ? pool.maximum : (pool.maximum + 1) / 2);
        } else if (command.id == "srd55.resources.slot-to-points" ||
                   command.id == "srd55.resources.font-of-inspiration") {
            int rank = 0;
            const auto pool = slotResource(input, e, rank);
            const auto target = command.id.ends_with("slot-to-points") ? "sorcerer:sorcery-points"
                                                                       : "bard:inspiration";
            const auto &targetDef = resource(e, target);
            if (current(d, targetDef) == targetDef.maximum)
                throw std::runtime_error("The target resource is already full.");
            spend(d, e, pool, 1);
            regain(d, e, target, command.id.ends_with("slot-to-points") ? rank : 1);
        } else if (command.id == "srd55.resources.points-to-slot") {
            const int rank = integerArg(input, "slotLevel", 1, 5);
            const int minimums[] = {2, 3, 5, 7, 9}, costs[] = {2, 3, 5, 6, 7};
            if (level("sorcerer") < minimums[rank - 1])
                throw std::runtime_error("Your Sorcerer level cannot create this slot level.");
            spend(d, e, "sorcerer:sorcery-points", costs[rank - 1]);
            const auto id = "spellSlots." + std::to_string(rank);
            int available = 0;
            auto found = std::find_if(e.resources.begin(), e.resources.end(),
                                      [&](const auto &r) { return r.id == id; });
            if (found != e.resources.end())
                available = current(d, *found);
            const int created = number(d.resources, "/createdSpellSlots/" + std::to_string(rank));
            d.resources["createdSpellSlots"][std::to_string(rank)] = created + 1;
            d.resources[id] = available + 1;
        } else if (command.id == "srd55.spells.copy" || command.id == "srd55.spells.copy-scroll") {
            const bool fromScroll = command.id == "srd55.spells.copy-scroll";
            if (!fromScroll && input.value("sourceType", std::string("spellbook")) != "spellbook")
                throw std::runtime_error(
                    "Choose the owned-scroll copying action for a Spell Scroll.");
            if (!level("wizard"))
                throw std::runtime_error("Spellbook copying requires Wizard levels.");
            std::string instanceId, id = input.value("spellId", std::string{});
            if (fromScroll) {
                instanceId = input.value("instanceId", std::string{});
                const auto *instances = at(before.resources, "/inventory/instances");
                if (instances && instances->is_array())
                    for (const auto &item : *instances)
                        if (scrollDefinition(rules, item) && item.value("id", "") == instanceId)
                            id = item.value("spellId", "");
            }
            const auto *spell = rules.find(id);
            if (!spell || spell->value("kind", "") != "spell")
                throw std::runtime_error("Choose an existing spell source.");
            const int rank = spell->at("level").get<int>();
            const auto lists = strings(*spell, "/lists");
            if (rank < 1 || rank > std::min(9, (level("wizard") + 1) / 2) ||
                std::find(lists.begin(), lists.end(), "wizard") == lists.end())
                throw std::runtime_error(
                    "Only a Wizard spell of a level you can prepare can be copied.");
            const auto known = strings(e.moduleData, "/spellbooks/wizard");
            if (std::find(known.begin(), known.end(), id) != known.end())
                throw std::runtime_error("This spell is already in the spellbook.");
            const int minutes = integerArg(input, "minutes", rank * 120, 1000000),
                      cost = integerArg(input, "paidCp", rank * 5000, 1000000000);
            const auto note = fromScroll ? "Owned Spell Scroll " + instanceId
                                         : input.value("sourceNote", std::string{});
            if (note.find_first_not_of(" \t\r\n") == std::string::npos)
                throw std::runtime_error("Identify the discovered spell source.");
            if (!d.resources.contains("currencyCp") ||
                !d.resources["currencyCp"].is_number_integer())
                throw std::runtime_error("Initialize the adventuring inventory and funds before "
                                         "paying to copy a spell.");
            if (d.resources["currencyCp"] < cost)
                throw std::runtime_error("Insufficient current funds for copying.");
            bool success = true;
            Json check;
            if (fromScroll) {
                const int die = integerArg(input, "acceptedD20", 1, 20),
                          bonus = integerArg(input, "bonus", -100000, 100000, 0);
                const auto reason = input.value("bonusReason", std::string{});
                if (bonus && reason.find_first_not_of(" \t\r\n") == std::string::npos)
                    throw std::runtime_error("Explain the additional resolved check bonus.");
                const auto trained = strings(e.moduleData, "/skillProficiencies");
                const bool proficient =
                    std::find(trained.begin(), trained.end(), "srd55:arcana") != trained.end();
                const int used = proficient && e.find("feature.rogue.reliableTalent")
                                     ? std::max(die, stat(e, "feature.rogue.reliableTalent"))
                                     : die;
                const int modifier = stat(e, "skill.arcana"), dc = 10 + rank,
                          total = used + modifier + bonus;
                success = total >= dc;
                check = {{"action", command.id}, {"instanceId", instanceId}, {"spellId", id},
                         {"d20", die},           {"effectiveD20", used},     {"modifier", modifier},
                         {"bonus", bonus},       {"bonusReason", reason},    {"dc", dc},
                         {"total", total},       {"success", success}};
                auto consumed = consumeSrd55InventoryScroll(d, rules, instanceId,
                                                            "Wizard spellbook copying attempt");
                if (!consumed.valid()) {
                    for (const auto &m : consumed.messages)
                        if (m.severity == "error")
                            throw std::runtime_error(m.text);
                    throw std::runtime_error("The owned scroll could not be consumed.");
                }
                d = std::move(consumed.document);
                if (!d.rolls.contains("actions"))
                    d.rolls["actions"] = Json::array();
                d.rolls["actions"].push_back(check);
            }
            d.resources["currencyCp"] = d.resources["currencyCp"].get<long long>() - cost;
            d.resources["lifecycle"]["restWindow"] = "";
            if (success) {
                auto &copies = d.choices["spellcasting"]["wizard"]["copiedSpells"];
                if (copies.is_null())
                    copies = Json::array();
                if (!copies.is_array())
                    throw std::runtime_error("Copied spell records are malformed.");
                Json record = {{"spellId", id},
                               {"paidCp", cost},
                               {"minutes", minutes},
                               {"sourceNote", note},
                               {"sourceType", fromScroll ? "scroll" : "spellbook"},
                               {"acquiredCharacterLevel", number(d.choices, "/level", 1)}};
                if (fromScroll) {
                    record["scrollInstanceId"] = instanceId;
                    record["check"] = check;
                }
                copies.push_back(std::move(record));
                recordSrd55HistoryChange(before, d, "copy-spell", input);
            }
            if (fromScroll)
                result.messages.push_back(
                    {"info",
                     success ? "srd55.spells.scroll-copied" : "srd55.spells.scroll-copy-failed",
                     "/",
                     success ? "The Arcana check succeeds. The spell is copied, and the scroll and "
                               "copying funds are consumed."
                             : "The Arcana check fails. The scroll and copying funds are consumed; "
                               "the spellbook is unchanged.",
                     {ref("78"), ref("244")}});
            const auto checked = dnd::evaluate(d, rules);
            if (!checked.complete())
                throw std::runtime_error("The copied spell would produce an invalid spellbook; it "
                                         "may already be known.");
        } else
            throw std::runtime_error("Unsupported lifecycle action.");
    } catch (const std::exception &error) {
        const auto source = std::find_if(e.actions.begin(), e.actions.end(),
                                         [&](const auto &item) { return item.id == command.id; });
        result.messages.push_back(
            {"error", "srd55.lifecycle.invalid", "/", error.what(),
             source == e.actions.end() ? std::vector<SourceRef>{} : source->sources});
        result.document = before;
    }
    return result;
}

TransitionResult applyLifecycleCommand(const CharacterDocument &before,
                                       const ResolvedRuleset &rules,
                                       const CharacterCommand &command) {
    auto result = applyLifecycleCommandImpl(before, rules, command);
    if (!result.valid() || command.id == "srd55.resources.long-rest")
        return result;
    // A campaign ruling may make a Druid conversion a single additional slot
    // that lasts until spent. Consume these grants first; they never become
    // rechargeable class-table capacity. Resting itself is not slot spending.
    if (before.resources.contains("druidPersistentSpellSlots")) {
        const auto e = evaluate(before, rules);
        for (int rank = 1; rank <= 9; ++rank) {
            const auto key = std::to_string(rank), id = "spellSlots." + key;
            const int extra = number(before.resources, "/druidPersistentSpellSlots/" + key);
            if (extra < 1 || !result.document.resources.contains(id) ||
                !result.document.resources[id].is_number_integer())
                continue;
            const auto found = std::find_if(e.resources.begin(), e.resources.end(),
                                            [&](const auto &r) { return r.id == id; });
            if (found == e.resources.end())
                continue;
            const int spent = current(before, *found) - result.document.resources[id].get<int>();
            if (spent > 0)
                result.document.resources["druidPersistentSpellSlots"][key] = std::max(
                    0, number(result.document.resources, "/druidPersistentSpellSlots/" + key) -
                           std::min(extra, spent));
        }
    }
    return result;
}
} // namespace dnd::srd55v2
