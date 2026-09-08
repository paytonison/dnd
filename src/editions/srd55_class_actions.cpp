#include "dnd/class_actions.hpp"
#include "srd55_v2_internal.hpp"
#include <algorithm>
#include <stdexcept>

namespace dnd::srd55v2 {
namespace {
constexpr const char *prefix = "srd55.classes.";
const ResourceDefinition *findResource(const Evaluation &e, const std::string &id) {
    const auto found = std::find_if(e.resources.begin(), e.resources.end(),
                                    [&](const auto &r) { return r.id == id; });
    return found == e.resources.end() ? nullptr : &*found;
}
const ResourceDefinition &resource(const Evaluation &e, const std::string &id) {
    const auto *found = findResource(e, id);
    if (!found)
        throw std::runtime_error("This resource is unavailable: " + id);
    return *found;
}
int remaining(const CharacterDocument &d, const ResourceDefinition &r) {
    if (!d.resources.contains(r.id))
        return r.maximum;
    const auto &value = d.resources.at(r.id);
    if (!value.is_number_integer() || value < 0 || value > r.maximum)
        throw std::runtime_error("Correct the saved current value of " + r.label +
                                 " before using it.");
    return value.get<int>();
}
int remaining(const CharacterDocument &d, const Evaluation &e, const std::string &id) {
    return remaining(d, resource(e, id));
}
void spend(CharacterDocument &d, const Evaluation &e, const std::string &id, int amount) {
    const int before = remaining(d, e, id);
    if (amount < 1 || amount > before)
        throw std::runtime_error("Insufficient remaining " + resource(e, id).label + ".");
    d.resources[id] = before - amount;
}
void restore(CharacterDocument &d, const Evaluation &e, const std::string &id, int amount) {
    const auto &r = resource(e, id);
    const int before = remaining(d, r);
    if (amount < 1 || amount > r.maximum - before)
        throw std::runtime_error("Recover only expended " + r.label + ".");
    d.resources[id] = before + amount;
}
void minimum(CharacterDocument &d, const Evaluation &e, const std::string &id, int floor) {
    const auto &r = resource(e, id);
    const int before = remaining(d, r);
    if (before < std::min(floor, r.maximum))
        d.resources[id] = std::min(floor, r.maximum);
}
int stat(const Evaluation &e, const std::string &id) {
    const auto *value = e.find(id);
    if (!value || !value->effective.is_number_integer())
        throw std::runtime_error("Required statistic unavailable: " + id);
    return value->effective.get<int>();
}
int integerInput(const Json &input, const std::string &key, int low, int high) {
    if (!input.contains(key) || !input[key].is_number_integer() || input[key] < low ||
        input[key] > high)
        throw std::runtime_error("Enter " + key + " as a whole number from " + std::to_string(low) +
                                 " to " + std::to_string(high) + ".");
    return input[key].get<int>();
}
bool flag(const Json &input, const std::string &key) {
    if (!input.contains(key))
        return false;
    if (!input[key].is_boolean())
        throw std::runtime_error(key + " must be true or false.");
    return input[key].get<bool>();
}
std::string event(const CharacterDocument &d, const CharacterCommand &command,
                  const std::string &key) {
    if (!command.inputs.contains(key) || !command.inputs[key].is_string())
        throw std::runtime_error("Identify the accepted " + key + ".");
    const auto value = command.inputs[key].get<std::string>();
    if (value.size() > 120 || value.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::runtime_error("Use a nonblank " + key + " of at most 120 characters.");
    const auto *accepted = at(d.rolls, "/classActions");
    if (key == "eventId" && accepted && accepted->is_object() && accepted->contains(value))
        throw std::runtime_error("This event already has accepted Initiative roll evidence.");
    for (const auto &entry : d.advancement)
        if (entry.is_object() && entry.value("kind", "") == "command" &&
            entry.value("action", "") == command.id) {
            const auto inputs = entry.find("inputs");
            if (inputs != entry.end() && inputs->is_object() && inputs->contains(key) &&
                (*inputs)[key] == Json(value))
                throw std::runtime_error("This " + key +
                                         " has already been recorded for this action.");
        }
    return value;
}
Field boolean(const std::string &path, const std::string &label) {
    return {path, label, "boolean", 0, 1, {}, false, {}};
}
Field eventField(const std::string &path, const std::string &label) {
    return {path,
            label,
            "text",
            0,
            0,
            {},
            false,
            "Use a distinct campaign event identifier, such as session-4-encounter-2. An already "
            "applied identifier cannot be reused."};
}
std::vector<Choice> slotOptions(const CharacterDocument &d, const Evaluation &e, int exactLevel = 0,
                                bool pactOnly = false) {
    std::vector<Choice> result;
    for (const auto &r : e.resources) {
        const bool pact = r.id == "pactMagic.slots";
        if (!pact && !r.id.starts_with("spellSlots."))
            continue;
        if (pactOnly && !pact)
            continue;
        int rank = pact ? stat(e, "pactMagic.level") : std::stoi(r.id.substr(11));
        if (exactLevel && rank != exactLevel)
            continue;
        int count = 0;
        try {
            count = remaining(d, r);
        } catch (...) {
        }
        result.push_back({r.id, r.label + " (" + std::to_string(count) + " remaining)", count > 0,
                          count > 0 ? "" : "No eligible slots remain.", r.sources});
    }
    return result;
}
void spendSlot(CharacterDocument &d, const Evaluation &e, const Json &input, int exactLevel = 0,
               bool pactOnly = false) {
    const auto id = text(input, "/slotResource");
    const auto opts = slotOptions(d, e, exactLevel, pactOnly);
    if (std::none_of(opts.begin(), opts.end(),
                     [&](const auto &option) { return option.available && option.id == id; }))
        throw std::runtime_error("Choose an available spell slot allowed by this feature.");
    spend(d, e, id, 1);
}
void add(Evaluation &e, const std::string &id, const std::string &label,
         const std::string &description, std::vector<Field> fields, const std::string &page,
         bool available = true, const std::string &reason = {}) {
    const bool valid = e.complete();
    e.actions.push_back(
        {std::string(prefix) + id,
         label,
         description,
         std::move(fields),
         valid && available,
         !valid ? "Resolve character validation errors before using this action." : reason,
         {ref(page)}});
}
std::vector<Field> druidRulingFields() {
    return {select("/policy", "Campaign ruling for Druid-created slots",
                   {{"recover-expended",
                     "Recover one expended slot within normal capacity",
                     true,
                     {},
                     {ref("43")}},
                    {"additional-until-long-rest",
                     "Add one slot until the next Long Rest",
                     true,
                     {},
                     {ref("43")}},
                    {"additional-until-spent",
                     "Add one slot until that extra slot is spent",
                     true,
                     {},
                     {ref("43")}}},
                   false,
                   "The Druid text does not specify when a created slot expires. Select the "
                   "campaign's ruling explicitly."),
            {"/reason",
             "Reason for the campaign ruling",
             "text",
             0,
             0,
             {},
             false,
             "Record the GM's ruling or agreement. It affects this and future conversions, not "
             "extra slots already recorded."}};
}
void seedDruidRuling(const CharacterDocument &d, ActionDefinition &action) {
    const auto *value = at(d.campaign, "/srd55DruidSlotRuling");
    if (value && value->is_object())
        for (const auto *key : {"policy", "reason"})
            if (value->contains(key))
                action.initialInputs[key] = value->at(key);
}
void addCreatedSlot(CharacterDocument &d, const Evaluation &e, int rank, const Json &input) {
    const auto policy = text(input, "/policy"), reason = text(input, "/reason");
    if (policy != "recover-expended" && policy != "additional-until-long-rest" &&
        policy != "additional-until-spent")
        throw std::runtime_error("Select an explicit campaign ruling for Druid-created slots.");
    if (reason.find_first_not_of(" \t\r\n") == std::string::npos)
        throw std::runtime_error("Record the reason for the Druid slot ruling.");
    d.campaign["srd55DruidSlotRuling"] = {{"policy", policy}, {"reason", reason}};
    const auto id = "spellSlots." + std::to_string(rank);
    const auto *pool = findResource(e, id);
    if (policy == "recover-expended") {
        restore(d, e, id, 1);
        return;
    }
    const int current = pool ? remaining(d, *pool) : 0;
    const auto key = policy == "additional-until-long-rest" ? "druidCreatedSpellSlots"
                                                            : "druidPersistentSpellSlots";
    const int created = number(d.resources, "/" + std::string(key) + "/" + std::to_string(rank));
    if (created >= 1000)
        throw std::runtime_error("The supported created-slot limit has been reached.");
    d.resources[key][std::to_string(rank)] = created + 1;
    d.resources[id] = current + 1;
}
struct Reuse {
    const char *id;
    const char *label;
    const char *target;
    const char *cost;
    int amount;
    int slotLevel;
    bool pactOnly;
    const char *page;
};
const Reuse reuses[] = {
    {"restore-intimidating-presence", "Restore Intimidating Presence",
     "barbarian:intimidating-presence", "barbarian:rage", 1, 0, false, "30"},
    {"restore-holy-nimbus", "Restore Holy Nimbus", "paladin:holy-nimbus", "", 1, 5, false, "57"},
    {"restore-dragon-wings", "Restore Dragon Wings", "sorcerer:dragon-wings",
     "sorcerer:sorcery-points", 3, 0, false, "70"},
    {"restore-hurl-through-hell", "Restore Hurl Through Hell", "warlock:hurl-through-hell", "", 1,
     0, true, "76"}};
} // namespace

void appendSrd55ClassActions(const CharacterDocument &d, const ResolvedRuleset &rules, Evaluation &e) {
    const int druid = profileLevel(d.choices, rules, "druid"), monk = profileLevel(d.choices, rules, "monk"),
              sorcerer = profileLevel(d.choices, rules, "sorcerer");
    if (druid >= 5) {
        add(e, "druid.slot-to-wild-shape", "Wild Resurgence: recover Wild Shape",
            "With no Wild Shape uses remaining, expend any spell slot to regain one use. Once on "
            "each of your own turns.",
            {select("/slotResource", "Spell slot to expend", slotOptions(d, e)),
             eventField("/turnId", "Own turn identifier"),
             boolean("/confirmedOwnTurn", "This is my turn")},
            "43");
        add(e, "druid.wild-shape-to-slot", "Wild Resurgence: create a level 1 slot",
            "Expend one Wild Shape use and the separate once-per-Long-Rest conversion permission "
            "to create one level 1 spell slot. The source leaves its expiry unspecified, so an "
            "explicit campaign ruling is required.",
            druidRulingFields(), "43");
        seedDruidRuling(d, e.actions.back());
    }
    if (druid == 20) {
        auto fields = druidRulingFields();
        fields.insert(fields.begin(), integer("/uses", "Wild Shape uses to convert", 1, 4));
        add(e, "druid.nature-magician", "Nature Magician: create a spell slot",
            "Expend unspent Wild Shape uses to create a single slot with two levels per use, once "
            "per Long Rest. The source leaves its expiry unspecified; select the campaign ruling.",
            fields, "43");
        seedDruidRuling(d, e.actions.back());
    }
    if (findResource(e, "druid:natural-recovery-slots")) {
        std::vector<Field> fields;
        for (int rank = 1; rank <= 5; ++rank)
            fields.push_back(integer("/slots/" + std::to_string(rank),
                                     "Level " + std::to_string(rank) + " slots to recover", 0, 20));
        if (findResource(e, "pactMagic.slots"))
            fields.push_back(integer("/pactSlots", "Pact Magic slots to recover", 0, 20,
                                     "Uses the Pact slot level in the same recovery budget. Pact "
                                     "slots normally already recover fully at this Short Rest."));
        add(e, "druid.natural-recovery", "Use Natural Recovery",
            "At a completed Short Rest, recover expended spell slots whose combined levels are at "
            "most half Druid level rounded up. No level 6+ slots; once per Long Rest.",
            fields, "46", text(d.resources, "/lifecycle/restWindow") == "short-rest",
            "First record the qualifying Short Rest.");
    }
    if (profileLevel(d.choices, rules, "barbarian") >= 15 || profileLevel(d.choices, rules, "bard") >= 18 || druid == 20 ||
        monk >= 2) {
        std::vector<Field> fields = {
            eventField("/eventId", "Initiative event identifier"),
            integer("/initiativeRoll", "Accepted Initiative d20 result", 1, 20),
            boolean("/confirmed", "I rolled Initiative for this event")};
        if (profileLevel(d.choices, rules, "barbarian") >= 15)
            fields.push_back(boolean("/usePersistentRage", "Use Persistent Rage recovery"));
        if (monk >= 2) {
            const auto *die = e.find("feature.monk.martialArtsDie");
            const int sides = die && die->effective.is_number_integer()
                                  ? std::clamp(die->effective.get<int>(), 0, 1000)
                                  : 0;
            fields.push_back(boolean("/useUncannyMetabolism", "Use Uncanny Metabolism"));
            fields.push_back(integer("/healingRoll",
                                     "Accepted Martial Arts healing die (0 if unused)", 0, sides));
        }
        add(e, "initiative", "Record Initiative class recoveries",
            "Record this accepted Initiative event once. Apply Superior Inspiration, Evergreen "
            "Wild Shape, and Perfect Focus when eligible; choose whether to spend Persistent Rage "
            "or Uncanny Metabolism. No Initiative roll is generated.",
            fields, "30, 33, 43, 51-52");
    }
    for (const auto &reuse : reuses)
        if (findResource(e, reuse.target)) {
            std::vector<Field> fields;
            if (!*reuse.cost)
                fields.push_back(select("/slotResource", "Spell slot to expend",
                                        slotOptions(d, e, reuse.slotLevel, reuse.pactOnly)));
            add(e, reuse.id, reuse.label,
                std::string("Recover one expended use by paying ") +
                    (*reuse.cost
                         ? std::to_string(reuse.amount) + " " + resource(e, reuse.cost).label
                     : reuse.pactOnly ? "one Pact Magic slot"
                                      : "one level 5 spell slot") +
                    ". This restores the permission; activation remains separate.",
                fields, reuse.page);
        }
    if (sorcerer >= 7)
        add(e, "activate-innate-sorcery-with-points",
            "Activate Innate Sorcery with Sorcery Incarnate",
            "With no Innate Sorcery uses left, spend 2 Sorcery Points as part of its Bonus Action "
            "activation. Saves the existing Innate Sorcery effect flag.",
            {boolean("/confirmedBonusAction", "I am taking the Bonus Action")}, "66");
}

TransitionResult applySrd55ClassCommand(const CharacterDocument &before,
                                        const ResolvedRuleset &rules,
                                        const CharacterCommand &command) {
    TransitionResult result{before, {}};
    try {
        const auto e = evaluate(before, rules);
        if (!e.complete())
            throw std::runtime_error(
                "Resolve character validation errors before applying class actions.");
        const auto action = std::find_if(e.actions.begin(), e.actions.end(), [&](const auto &a) {
            return a.id == command.id && a.available;
        });
        if (action == e.actions.end())
            throw std::runtime_error("This class action is unavailable.");
        auto &d = result.document;
        const auto &input = command.inputs;
        const int druid = profileLevel(before.choices, rules, "druid");
        if (command.id == std::string(prefix) + "druid.slot-to-wild-shape") {
            if (druid < 5 || !flag(input, "confirmedOwnTurn"))
                throw std::runtime_error(
                    "Wild Resurgence requires Druid level 5 and your own turn.");
            event(before, command, "turnId");
            if (remaining(d, e, "druid:wild-shape") != 0)
                throw std::runtime_error("Wild Shape must have no uses remaining.");
            spendSlot(d, e, input);
            restore(d, e, "druid:wild-shape", 1);
        } else if (command.id == std::string(prefix) + "druid.wild-shape-to-slot") {
            if (druid < 5)
                throw std::runtime_error("Wild Resurgence requires Druid level 5.");
            spend(d, e, "druid:wild-shape", 1);
            spend(d, e, "druid:wild-resurgence-slot", 1);
            addCreatedSlot(d, e, 1, input);
        } else if (command.id == std::string(prefix) + "druid.nature-magician") {
            if (druid != 20)
                throw std::runtime_error("Nature Magician requires Druid level 20.");
            const int uses = integerInput(input, "uses", 1, 4);
            spend(d, e, "druid:wild-shape", uses);
            spend(d, e, "druid:nature-magician", 1);
            addCreatedSlot(d, e, uses * 2, input);
        } else if (command.id == std::string(prefix) + "druid.natural-recovery") {
            if (druid < 6 || text(d.resources, "/lifecycle/restWindow") != "short-rest")
                throw std::runtime_error("Natural Recovery belongs to a qualifying Short Rest.");
            int cost = 0;
            for (int rank = 1; rank <= 5; ++rank) {
                const auto *n = at(input, "/slots/" + std::to_string(rank));
                if (!n)
                    continue;
                if (!n->is_number_integer() || *n < 0 || *n > 20)
                    throw std::runtime_error(
                        "Recovered slot counts must be whole numbers from 0 to 20.");
                const int count = n->get<int>();
                cost += rank * count;
                if (count)
                    restore(d, e, "spellSlots." + std::to_string(rank), count);
            }
            if (input.contains("pactSlots")) {
                const int count = integerInput(input, "pactSlots", 0, 20);
                if (count) {
                    const int rank = stat(e, "pactMagic.level");
                    if (rank < 1 || rank > 5)
                        throw std::runtime_error(
                            "Natural Recovery cannot recover level 6+ Pact slots.");
                    cost += rank * count;
                    restore(d, e, "pactMagic.slots", count);
                }
            }
            if (cost < 1 || cost > (druid + 1) / 2)
                throw std::runtime_error("The total recovered slot levels must be between 1 and "
                                         "half Druid level rounded up.");
            spend(d, e, "druid:natural-recovery-slots", 1);
        } else if (command.id == std::string(prefix) + "initiative") {
            if (!flag(input, "confirmed"))
                throw std::runtime_error("Confirm an accepted Initiative roll for this event.");
            const auto id = event(before, command, "eventId");
            const int roll = integerInput(input, "initiativeRoll", 1, 20);
            if (profileLevel(before.choices, rules, "barbarian") >= 15 && flag(input, "usePersistentRage")) {
                const auto &pool = resource(e, "barbarian:rage");
                const int missing = pool.maximum - remaining(d, pool);
                if (missing < 1)
                    throw std::runtime_error("No Rage uses are expended.");
                spend(d, e, "barbarian:persistent-rage", 1);
                restore(d, e, pool.id, missing);
            }
            if (profileLevel(before.choices, rules, "bard") >= 18)
                minimum(d, e, "bard:inspiration", 2);
            if (druid == 20)
                minimum(d, e, "druid:wild-shape", 1);
            const int monk = profileLevel(before.choices, rules, "monk");
            if (monk >= 2 && flag(input, "useUncannyMetabolism")) {
                const int sides = stat(e, "feature.monk.martialArtsDie");
                if (sides < 1 || sides > 1000)
                    throw std::runtime_error(
                        "The current Martial Arts die is outside the supported range.");
                const int healing = integerInput(input, "healingRoll", 1, sides);
                spend(d, e, "monk:uncanny-metabolism", 1);
                d.resources["monk:focus"] = resource(e, "monk:focus").maximum;
                const auto &hp = resource(e, "hp");
                d.resources["hp"] = std::min(hp.maximum, remaining(d, hp) + monk + healing);
                d.rolls["classActions"][id] = {{"action", command.id},
                                               {"initiativeD20", roll},
                                               {"healingDieSides", sides},
                                               {"healingDieResult", healing}};
            } else {
                if (input.contains("healingRoll") && integerInput(input, "healingRoll", 0, 12) != 0)
                    throw std::runtime_error("A healing die is used only with Uncanny Metabolism.");
                if (monk >= 15)
                    minimum(d, e, "monk:focus", 4);
                d.rolls["classActions"][id] = {{"action", command.id}, {"initiativeD20", roll}};
            }
        } else if (command.id == std::string(prefix) + "activate-innate-sorcery-with-points") {
            if (profileLevel(before.choices, rules, "sorcerer") < 7 || !flag(input, "confirmedBonusAction"))
                throw std::runtime_error(
                    "Sorcery Incarnate requires level 7 and its Bonus Action.");
            for (const auto *condition : {"incapacitated", "unconscious"}) {
                const auto *state = at(d.resources, "/effects/" + std::string(condition));
                if (state &&
                    (state->is_boolean() ? state->get<bool>()
                                         : state->is_object() && state->value("active", false)))
                    throw std::runtime_error(
                        "An incapacitated character cannot take the activation Bonus Action.");
            }
            if (remaining(d, e, "hp") == 0)
                throw std::runtime_error(
                    "A character at 0 HP cannot take the activation Bonus Action.");
            if (remaining(d, e, "sorcerer:innate-sorcery") != 0)
                throw std::runtime_error(
                    "Use a remaining Innate Sorcery permission before Sorcery Incarnate.");
            spend(d, e, "sorcerer:sorcery-points", 2);
            d.resources["effects"]["sorcerer:innate-sorcery"] = true;
        } else {
            const auto found =
                std::find_if(std::begin(reuses), std::end(reuses), [&](const auto &reuse) {
                    return command.id == std::string(prefix) + reuse.id;
                });
            if (found == std::end(reuses))
                throw std::runtime_error("Unsupported class action.");
            const auto &pool = resource(e, found->target);
            if (remaining(d, pool) >= pool.maximum)
                throw std::runtime_error("This feature's use is already available.");
            if (*found->cost)
                spend(d, e, found->cost, found->amount);
            else
                spendSlot(d, e, input, found->slotLevel, found->pactOnly);
            restore(d, e, found->target, 1);
        }
        if (command.id != std::string(prefix) + "druid.natural-recovery")
            d.resources["lifecycle"]["restWindow"] = "";
        const auto checked = evaluate(d, rules);
        for (const auto &message : checked.messages)
            if (message.severity == "error")
                result.messages.push_back(message);
        if (result.valid())
            result.messages.push_back(
                {"info", "srd55.classes.applied", "/resources",
                 "Class resource changes are recorded; accepted creation inputs remain unchanged.",
                 action->sources});
    } catch (const std::exception &error) {
        result.messages.push_back({"error", "srd55.classes.invalid", "/", error.what(), {}});
    }
    if (!result.valid())
        result.document = before;
    return result;
}
} // namespace dnd::srd55v2
