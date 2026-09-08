#include "dnd/class_actions.hpp"
#include "srd55_fixture.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace dnd;
namespace {
const ResolvedRuleset &rules() {
    static const auto value = srd55fixtures::rules();
    return value;
}
CharacterDocument hero(const std::string &cls, int level) {
    auto result = srd55fixtures::complete(cls, level, rules());
    INFO(srd55fixtures::errors(result.evaluation));
    REQUIRE(result.evaluation.complete());
    return result.document;
}
TransitionResult run(const CharacterDocument &d, const std::string &suffix,
                     Json inputs = Json::object()) {
    return executeCommand(d, rules(), {"srd55.classes." + suffix, std::move(inputs)});
}
CharacterDocument apply(const CharacterDocument &d, const std::string &suffix,
                        Json inputs = Json::object()) {
    auto result = run(d, suffix, std::move(inputs));
    std::string errors;
    for (const auto &m : result.messages)
        errors += m.code + ": " + m.text + "\n";
    INFO(errors);
    REQUIRE(result.valid());
    REQUIRE(evaluate(result.document, rules()).complete());
    return result.document;
}
int maximum(const CharacterDocument &d, const std::string &id) {
    auto e = evaluate(d, rules());
    const auto found = std::find_if(e.resources.begin(), e.resources.end(),
                                    [&](const auto &r) { return r.id == id; });
    REQUIRE(found != e.resources.end());
    return found->maximum;
}
int amount(const CharacterDocument &d, const std::string &id) {
    return d.resources.value(id, maximum(d, id));
}
void rejected(const CharacterDocument &d, const std::string &suffix, Json inputs = Json::object()) {
    const auto result = run(d, suffix, std::move(inputs));
    REQUIRE_FALSE(result.valid());
    REQUIRE(toJson(result.document) == toJson(d));
}
Json ruling(const std::string &policy) {
    return {{"policy", policy}, {"reason", "Recorded campaign ruling: use this slot treatment."}};
}
Json initiative(const std::string &id) {
    return {{"eventId", id}, {"initiativeRoll", 12}, {"confirmed", true}};
}
CharacterDocument rest(const CharacterDocument &d, bool shortRest = false) {
    auto result =
        executeCommand(d, rules(),
                       {shortRest ? "srd55.resources.short-rest" : "srd55.resources.long-rest",
                        shortRest ? Json{{"hours", 1}, {"interrupted", false}}
                                  : Json{{"hours", 8},
                                         {"sleepHours", 6},
                                         {"hoursSincePrevious", 16},
                                         {"interruptions", 0},
                                         {"unfinished", false}}});
    INFO(srd55fixtures::errors(evaluate(result.document, rules())));
    REQUIRE(result.valid());
    return result.document;
}
} // namespace

TEST_CASE("Wild Resurgence spends a slot only with zero Wild Shape and a new own turn",
          "[srd55-v2][class-actions]") {
    auto d = hero("druid", 5);
    const Json inputs = {{"slotResource", "spellSlots.1"},
                         {"turnId", "encounter-2-turn-1"},
                         {"confirmedOwnTurn", true}};
    rejected(d, "druid.slot-to-wild-shape", inputs);
    d.resources["druid:wild-shape"] = 0;
    const auto original = toJson(d);
    const auto a = run(d, "druid.slot-to-wild-shape", inputs),
               b = run(d, "druid.slot-to-wild-shape", inputs);
    REQUIRE(a.valid());
    REQUIRE(toJson(a.document) == toJson(b.document));
    REQUIRE(toJson(d) == original);
    REQUIRE(amount(a.document, "spellSlots.1") == 3);
    REQUIRE(amount(a.document, "druid:wild-shape") == 1);
    REQUIRE(a.document.choices == d.choices);
    REQUIRE(a.document.rolls == d.rolls);
    auto after = a.document;
    after.resources["druid:wild-shape"] = 0;
    rejected(after, "druid.slot-to-wild-shape", inputs);
    auto next = inputs;
    next["turnId"] = "encounter-2-turn-2";
    REQUIRE(run(after, "druid.slot-to-wild-shape", next).valid());
    auto invalid = inputs;
    invalid["confirmedOwnTurn"] = false;
    rejected(d, "druid.slot-to-wild-shape", invalid);
    invalid = inputs;
    invalid["turnId"] = "  ";
    rejected(d, "druid.slot-to-wild-shape", invalid);
    invalid = inputs;
    invalid["slotResource"] = "hp";
    rejected(d, "druid.slot-to-wild-shape", invalid);
    rejected(hero("druid", 4), "druid.slot-to-wild-shape", inputs);
}

TEST_CASE(
    "Druid slot conversion requires a recorded ruling and atomically spends its own permission",
    "[srd55-v2][class-actions]") {
    auto d = hero("druid", 5);
    rejected(d, "druid.wild-shape-to-slot");
    auto missing = ruling("recover-expended");
    missing["reason"] = " ";
    rejected(d, "druid.wild-shape-to-slot", missing);
    rejected(d, "druid.wild-shape-to-slot", ruling("recover-expended"));
    d.resources["spellSlots.1"] = 3;
    auto recovered = apply(d, "druid.wild-shape-to-slot", ruling("recover-expended"));
    REQUIRE(amount(recovered, "spellSlots.1") == 4);
    REQUIRE(maximum(recovered, "spellSlots.1") == 4);
    REQUIRE(amount(recovered, "druid:wild-shape") == 1);
    REQUIRE(amount(recovered, "druid:wild-resurgence-slot") == 0);
    REQUIRE(recovered.campaign["srd55DruidSlotRuling"]["policy"] == "recover-expended");
    REQUIRE_FALSE(recovered.resources.contains("createdSpellSlots"));
    recovered.resources["spellSlots.1"] = 3;
    rejected(recovered, "druid.wild-shape-to-slot", ruling("recover-expended"));
    auto restored = rest(recovered);
    REQUIRE(amount(restored, "druid:wild-resurgence-slot") == 1);
    const auto e = evaluate(restored, rules());
    const auto form = std::find_if(e.actions.begin(), e.actions.end(), [](const auto &action) {
        return action.id == "srd55.classes.druid.wild-shape-to-slot";
    });
    REQUIRE(form != e.actions.end());
    REQUIRE(form->initialInputs == ruling("recover-expended"));
}

TEST_CASE("Druid additional slot rulings retain separate expiry and spending behavior",
          "[srd55-v2][class-actions]") {
    auto d = hero("druid", 5);
    const auto temporary =
        apply(d, "druid.wild-shape-to-slot", ruling("additional-until-long-rest"));
    REQUIRE(amount(temporary, "spellSlots.1") == 5);
    REQUIRE(maximum(temporary, "spellSlots.1") == 5);
    REQUIRE(temporary.resources["druidCreatedSpellSlots"]["1"] == 1);
    const auto longRest = rest(temporary);
    REQUIRE(amount(longRest, "spellSlots.1") == 4);
    REQUIRE_FALSE(longRest.resources.contains("druidCreatedSpellSlots"));
    const auto persistent = apply(d, "druid.wild-shape-to-slot", ruling("additional-until-spent"));
    REQUIRE(amount(persistent, "spellSlots.1") == 5);
    REQUIRE(maximum(persistent, "spellSlots.1") == 5);
    REQUIRE(persistent.resources["druidPersistentSpellSlots"]["1"] == 1);
    const auto afterRest = rest(persistent);
    REQUIRE(amount(afterRest, "spellSlots.1") == 5);
    REQUIRE(maximum(afterRest, "spellSlots.1") == 5);
    const auto spent =
        executeCommand(afterRest, rules(),
                       {"srd55.resources.spend", {{"resource", "spellSlots.1"}, {"amount", 1}}});
    REQUIRE(spent.valid());
    REQUIRE(amount(spent.document, "spellSlots.1") == 4);
    REQUIRE(maximum(spent.document, "spellSlots.1") == 4);
    REQUIRE(
        (srd55fixtures::at(spent.document.resources, "/druidPersistentSpellSlots/1").is_null() ||
         srd55fixtures::at(spent.document.resources, "/druidPersistentSpellSlots/1") == 0));
    const auto familiar = executeCommand(persistent, rules(),
                                         {"srd55.companions.cast.wild-companion-slot",
                                          {{"form", "srd55:creature-cat"},
                                           {"slotResource", "spellSlots.1"},
                                           {"minutes", 0},
                                           {"completed", true},
                                           {"interrupted", false}}});
    std::string castErrors;
    for (const auto &message : familiar.messages)
        castErrors += message.text + "\n";
    INFO(castErrors);
    REQUIRE(familiar.valid());
    REQUIRE(amount(familiar.document, "spellSlots.1") == 4);
    REQUIRE(maximum(familiar.document, "spellSlots.1") == 4);
    REQUIRE(familiar.document.resources["familiars"]["druid"]["form"] == "srd55:creature-cat");
}

TEST_CASE("Archdruid Nature Magician creates one slot at two levels per Wild Shape use",
          "[srd55-v2][class-actions]") {
    auto d = hero("druid", 20);
    auto inputs = ruling("additional-until-long-rest");
    inputs["uses"] = 2;
    auto made = apply(d, "druid.nature-magician", inputs);
    REQUIRE(amount(made, "druid:wild-shape") == 2);
    REQUIRE(amount(made, "druid:nature-magician") == 0);
    REQUIRE(amount(made, "druid:wild-resurgence-slot") == 1);
    REQUIRE(made.resources["druidCreatedSpellSlots"]["4"] == 1);
    REQUIRE(amount(made, "spellSlots.4") == 4);
    rejected(made, "druid.nature-magician", inputs);
    inputs["uses"] = 4;
    auto eighth = apply(d, "druid.nature-magician", inputs);
    REQUIRE(amount(eighth, "druid:wild-shape") == 0);
    REQUIRE(eighth.resources["druidCreatedSpellSlots"]["8"] == 1);
    inputs["uses"] = 5;
    rejected(d, "druid.nature-magician", inputs);
    rejected(hero("druid", 19), "druid.nature-magician", inputs);
}

TEST_CASE("Natural Recovery validates its own budget rest window and independent daily use",
          "[srd55-v2][class-actions]") {
    auto d = hero("druid", 6);
    d.resources["spellSlots.3"] = 0;
    const Json inputs = {{"slots", {{"3", 1}}}};
    rejected(d, "druid.natural-recovery", inputs);
    d = rest(d, true);
    auto recovered = apply(d, "druid.natural-recovery", inputs);
    REQUIRE(amount(recovered, "spellSlots.3") == 1);
    REQUIRE(amount(recovered, "druid:natural-recovery-slots") == 0);
    REQUIRE(amount(recovered, "druid:natural-recovery-cast") == 1);
    rejected(recovered, "druid.natural-recovery", inputs);
    d.resources["spellSlots.1"] = 0;
    rejected(d, "druid.natural-recovery", {{"slots", {{"1", 1}, {"3", 1}}}});
    rejected(d, "druid.natural-recovery", {{"slots", {{"6", 1}}}});
    rejected(d, "druid.natural-recovery", {{"slots", {{"3", 1.5}}}});
    rejected(d, "druid.natural-recovery", {{"slots", {{"1", 0}}}});
    auto full = rest(d);
    full = rest(full, true);
    rejected(full, "druid.natural-recovery", inputs);
}

TEST_CASE("Druid multiclass conversions can spend Pact slots but recovery cannot exceed the rested "
          "Pact pool",
          "[srd55-v2][class-actions]") {
    auto d = hero("druid", 6);
    d.choices["level"] = 7;
    d.choices["multiclass"] = true;
    for (int n = 2; n <= 7; ++n)
        d.choices["advancement"][std::to_string(n)]["classId"] =
            n == 7 ? "srd55:warlock" : "srd55:druid";
    const auto fixture = srd55fixtures::finish(d, rules());
    INFO(srd55fixtures::errors(fixture.evaluation));
    REQUIRE(fixture.evaluation.complete());
    d = fixture.document;
    d.resources["druid:wild-shape"] = 0;
    const auto converted = apply(d, "druid.slot-to-wild-shape",
                                 {{"slotResource", "pactMagic.slots"},
                                  {"turnId", "pact-own-turn"},
                                  {"confirmedOwnTurn", true}});
    REQUIRE(amount(converted, "pactMagic.slots") == 0);
    REQUIRE(amount(converted, "druid:wild-shape") == 1);
    const auto rested = rest(converted, true);
    REQUIRE(amount(rested, "pactMagic.slots") == 1);
    rejected(rested, "druid.natural-recovery", {{"pactSlots", 1}});
    REQUIRE(amount(rested, "druid:natural-recovery-slots") == 1);
}

TEST_CASE(
    "Initiative minimum recoveries require an accepted unique event and never reduce resources",
    "[srd55-v2][class-actions]") {
    auto bard = hero("bard", 18);
    bard.resources["bard:inspiration"] = 0;
    const auto input = initiative("session-2-initiative-1");
    const auto recovered = apply(bard, "initiative", input);
    REQUIRE(amount(recovered, "bard:inspiration") == 2);
    REQUIRE(recovered.rolls["classActions"]["session-2-initiative-1"]["initiativeD20"] == 12);
    REQUIRE(recovered.rolls["abilities"] == bard.rolls["abilities"]);
    rejected(recovered, "initiative", input);
    bard.choices["abilities"]["charisma"] = 18;
    bard.rolls["abilities"]["charisma"] = {
        {"dice", {6, 6, 6, 1}}, {"dropLowest", 1}, {"total", 18}};
    bard.resources["bard:inspiration"] = 3;
    REQUIRE(amount(apply(bard, "initiative", input), "bard:inspiration") == 3);
    auto druid = hero("druid", 20);
    druid.resources["druid:wild-shape"] = 0;
    REQUIRE(amount(apply(druid, "initiative", input), "druid:wild-shape") == 1);
    auto invalid = input;
    invalid["confirmed"] = false;
    rejected(druid, "initiative", invalid);
    invalid = input;
    invalid["initiativeRoll"] = 21;
    rejected(druid, "initiative", invalid);
    rejected(hero("fighter", 20), "initiative", input);
}

TEST_CASE("Persistent Rage initiative recovery spends its once per Long Rest permission",
          "[srd55-v2][class-actions]") {
    auto d = hero("barbarian", 15);
    d.resources["barbarian:rage"] = 0;
    auto input = initiative("rage-initiative-1");
    input["usePersistentRage"] = true;
    const auto recovered = apply(d, "initiative", input);
    REQUIRE(amount(recovered, "barbarian:rage") == 5);
    REQUIRE(amount(recovered, "barbarian:persistent-rage") == 0);
    auto spent = recovered;
    spent.resources["barbarian:rage"] = 0;
    input["eventId"] = "rage-initiative-2";
    rejected(spent, "initiative", input);
    auto rested = rest(spent);
    rested.resources["barbarian:rage"] = 0;
    REQUIRE(run(rested, "initiative", input).valid());
}

TEST_CASE(
    "Uncanny Metabolism accepts its healing die once and excludes Perfect Focus for that event",
    "[srd55-v2][class-actions]") {
    auto d = hero("monk", 2);
    d.resources["hp"] = 1;
    d.resources["monk:focus"] = 0;
    auto input = initiative("monk-initiative-1");
    input["useUncannyMetabolism"] = true;
    input["healingRoll"] = 6;
    const auto accepted =
        executeCommand(d, rules(), {"srd55.history.accept-baseline", Json::object()});
    REQUIRE(accepted.valid());
    d = accepted.document;
    const auto a = run(d, "initiative", input), b = run(d, "initiative", input);
    REQUIRE(a.valid());
    REQUIRE(toJson(a.document) == toJson(b.document));
    REQUIRE(amount(a.document, "hp") == 9);
    REQUIRE(amount(a.document, "monk:focus") == 2);
    REQUIRE(amount(a.document, "monk:uncanny-metabolism") == 0);
    REQUIRE(a.document.rolls["classActions"]["monk-initiative-1"]["healingDieSides"] == 6);
    REQUIRE(a.document.rolls["abilities"] == d.rolls["abilities"]);
    const auto reopened = documentFromJson(toJson(a.document));
    REQUIRE(toJson(reopened) == toJson(a.document));
    REQUIRE(evaluate(reopened, rules()).complete());
    input["healingRoll"] = 7;
    rejected(d, "initiative", input);
    input["healingRoll"] = 0;
    rejected(d, "initiative", input);
    auto high = hero("monk", 15);
    high.resources["hp"] = 1;
    high.resources["monk:focus"] = 0;
    input["useUncannyMetabolism"] = false;
    const auto focus = apply(high, "initiative", input);
    REQUIRE(amount(focus, "monk:focus") == 4);
    REQUIRE(amount(focus, "hp") == 1);
    REQUIRE(amount(focus, "monk:uncanny-metabolism") == 1);
    input["useUncannyMetabolism"] = true;
    input["healingRoll"] = 10;
    const auto healed = apply(high, "initiative", input);
    REQUIRE(amount(healed, "hp") == 26);
    REQUIRE(amount(healed, "monk:focus") == 15);
    REQUIRE(amount(healed, "monk:uncanny-metabolism") == 0);
    input["eventId"] = "monk-initiative-2";
    rejected(healed, "initiative", input);
    input["useUncannyMetabolism"] = false;
    rejected(high, "initiative", input);
}

TEST_CASE("Paid class reuses restore permission with exactly the published coupled cost",
          "[srd55-v2][class-actions]") {
    struct Example {
        const char *cls;
        int level;
        const char *action;
        const char *target;
        const char *source;
        int cost;
    };
    const Example examples[] = {
        {"barbarian", 14, "restore-intimidating-presence", "barbarian:intimidating-presence",
         "barbarian:rage", 1},
        {"paladin", 20, "restore-holy-nimbus", "paladin:holy-nimbus", "spellSlots.5", 1},
        {"sorcerer", 14, "restore-dragon-wings", "sorcerer:dragon-wings", "sorcerer:sorcery-points",
         3},
        {"warlock", 14, "restore-hurl-through-hell", "warlock:hurl-through-hell", "pactMagic.slots",
         1}};
    for (const auto &example : examples) {
        INFO(example.action);
        auto d = hero(example.cls, example.level);
        d.resources[example.target] = 0;
        const int prior = amount(d, example.source);
        Json inputs = Json::object();
        if (std::string(example.source).find("Slots.") != std::string::npos ||
            std::string(example.source) == "pactMagic.slots")
            inputs["slotResource"] = example.source;
        const auto recovered = apply(d, example.action, inputs);
        REQUIRE(amount(recovered, example.target) == 1);
        REQUIRE(amount(recovered, example.source) == prior - example.cost);
        REQUIRE(recovered.choices == d.choices);
        REQUIRE(recovered.rolls == d.rolls);
        rejected(recovered, example.action, inputs);
        d.resources[example.source] = 0;
        rejected(d, example.action, inputs);
    }
    auto paladin = hero("paladin", 20);
    paladin.resources["paladin:holy-nimbus"] = 0;
    rejected(paladin, "restore-holy-nimbus", {{"slotResource", "spellSlots.4"}});
}

TEST_CASE("Sorcery Incarnate activation spends points only when Innate Sorcery uses are exhausted",
          "[srd55-v2][class-actions]") {
    auto d = hero("sorcerer", 7);
    const Json inputs = {{"confirmedBonusAction", true}};
    rejected(d, "activate-innate-sorcery-with-points", inputs);
    d.resources["sorcerer:innate-sorcery"] = 0;
    rejected(d, "activate-innate-sorcery-with-points", {{"confirmedBonusAction", false}});
    const auto before = evaluate(d, rules());
    auto active = apply(d, "activate-innate-sorcery-with-points", inputs);
    REQUIRE(amount(active, "sorcerer:sorcery-points") == 5);
    REQUIRE(active.resources["effects"]["sorcerer:innate-sorcery"] == true);
    REQUIRE(amount(active, "sorcerer:innate-sorcery") == 0);
    REQUIRE(evaluate(active, rules()).find("sorcerer.spellDc")->effective.get<int>() ==
            before.find("sorcerer.spellDc")->effective.get<int>() + 1);
    d.resources["sorcerer:sorcery-points"] = 1;
    rejected(d, "activate-innate-sorcery-with-points", inputs);
    d.resources["sorcerer:sorcery-points"] = 7;
    d.resources["effects"]["incapacitated"] = true;
    rejected(d, "activate-innate-sorcery-with-points", inputs);
}
