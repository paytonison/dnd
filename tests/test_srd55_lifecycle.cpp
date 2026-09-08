#include "dnd/lifecycle.hpp"
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
TransitionResult command(const CharacterDocument &d, const std::string &id, Json inputs) {
    return executeCommand(d, rules(), {id, std::move(inputs)});
}
CharacterDocument applied(const CharacterDocument &d, const std::string &id, Json inputs) {
    auto result = command(d, id, std::move(inputs));
    std::string errors;
    for (const auto &m : result.messages)
        errors += m.code + ": " + m.text + "\n";
    INFO(errors);
    REQUIRE(result.valid());
    return result.document;
}
const ResourceDefinition &resource(const Evaluation &e, const std::string &id) {
    auto found = std::find_if(e.resources.begin(), e.resources.end(),
                              [&](const auto &r) { return r.id == id; });
    REQUIRE(found != e.resources.end());
    return *found;
}
int amount(const CharacterDocument &d, const std::string &id) {
    auto e = evaluate(d, rules());
    return d.resources.value(id, resource(e, id).maximum);
}
} // namespace
TEST_CASE("Resource spending is atomic, deterministic, and keeps accepted rolls",
          "[srd55-v2][lifecycle]") {
    const auto d = hero("fighter", 3);
    const auto original = toJson(d);
    const auto a =
        command(d, "srd55.resources.spend", {{"resource", "fighter:second-wind"}, {"amount", 1}});
    const auto b =
        command(d, "srd55.resources.spend", {{"resource", "fighter:second-wind"}, {"amount", 1}});
    REQUIRE(a.valid());
    REQUIRE(toJson(a.document) == toJson(b.document));
    REQUIRE(amount(a.document, "fighter:second-wind") == 1);
    REQUIRE(a.document.rolls == d.rolls);
    REQUIRE(toJson(d) == original);
    for (const auto &invalid : Json::array({-1, 0, 3, 1.5})) {
        auto bad = command(d, "srd55.resources.spend",
                           {{"resource", "fighter:second-wind"}, {"amount", invalid}});
        REQUIRE_FALSE(bad.valid());
        REQUIRE(toJson(bad.document) == original);
    }
    auto unknown = command(d, "srd55.resources.spend", {{"resource", "unknown"}, {"amount", 1}});
    REQUIRE_FALSE(unknown.valid());
    REQUIRE(toJson(unknown.document) == original);
    auto extra =
        command(d, "srd55.resources.spend",
                {{"resource", "fighter:second-wind"}, {"amount", 1}, {"silentlyIgnored", true}});
    REQUIRE_FALSE(extra.valid());
    REQUIRE(toJson(extra.document) == original);
}
TEST_CASE("Short Rest restores partial class uses and accepted mixed Hit Dice heal explicitly",
          "[srd55-v2][lifecycle]") {
    auto d = hero("fighter", 3);
    d.resources["hp"] = 1;
    d.resources["fighter:second-wind"] = 0;
    auto rested = applied(d, "srd55.resources.short-rest", {{"hours", 1}, {"interrupted", false}});
    REQUIRE(amount(rested, "fighter:second-wind") == 1);
    REQUIRE(amount(rested, "hp") == 1);
    auto healed =
        applied(rested, "srd55.resources.spend-hit-die", {{"die", "hitDice.d10"}, {"roll", 5}});
    REQUIRE(amount(healed, "hp") == 9);
    REQUIRE(amount(healed, "hitDice.d10") == 2);
    REQUIRE(healed.rolls["actions"].back()["result"] == 5);
    REQUIRE(healed.rolls["abilities"] == d.rolls["abilities"]);
    REQUIRE_FALSE(healed.resources.contains("wizard:overchannel-uses"));
    auto blocked =
        command(d, "srd55.resources.spend-hit-die", {{"die", "hitDice.d10"}, {"roll", 5}});
    REQUIRE_FALSE(blocked.valid());
    REQUIRE(toJson(blocked.document) == toJson(d));
    auto interrupted =
        command(d, "srd55.resources.short-rest", {{"hours", 1}, {"interrupted", true}});
    REQUIRE_FALSE(interrupted.valid());
    REQUIRE(toJson(interrupted.document) == toJson(d));
    d.resources["hp"] = 0;
    REQUIRE_FALSE(command(d, "srd55.resources.short-rest", {{"hours", 1}}).valid());
}
TEST_CASE("Long Rest restores defined capacities and respects elapsed time and Trance",
          "[srd55-v2][lifecycle]") {
    auto d = hero("fighter", 3);
    d.resources["hp"] = 1;
    d.resources["hitDice.d10"] = 0;
    d.resources["fighter:second-wind"] = 0;
    d.resources["externalNote"] = "Keep this";
    auto rested =
        applied(d, "srd55.resources.long-rest",
                {{"hours", 8}, {"sleepHours", 6}, {"hoursSincePrevious", 0}, {"interruptions", 0}});
    REQUIRE(amount(rested, "hp") == 34);
    REQUIRE(amount(rested, "hitDice.d10") == 3);
    REQUIRE(amount(rested, "fighter:second-wind") == 2);
    REQUIRE(rested.resources["externalNote"] == "Keep this");
    REQUIRE(rested.rolls == d.rolls);
    REQUIRE_FALSE(rested.resources.contains("wizard:overchannel-uses"));
    REQUIRE_FALSE(rested.resources.contains("barbarian:relentless-rage-attempts"));
    auto tooSoon = command(rested, "srd55.resources.long-rest",
                           {{"hours", 8}, {"sleepHours", 6}, {"hoursSincePrevious", 15}});
    REQUIRE_FALSE(tooSoon.valid());
    REQUIRE(toJson(tooSoon.document) == toJson(rested));
    REQUIRE(command(rested, "srd55.resources.long-rest",
                    {{"hours", 8}, {"sleepHours", 6}, {"hoursSincePrevious", 16}})
                .valid());
    REQUIRE_FALSE(
        command(d, "srd55.resources.long-rest",
                {{"hours", 8}, {"sleepHours", 6}, {"hoursSincePrevious", 0}, {"interruptions", 1}})
            .valid());
    auto shortOnly =
        applied(d, "srd55.resources.long-rest",
                {{"hours", 1}, {"sleepHours", 0}, {"hoursSincePrevious", 0}, {"unfinished", true}});
    REQUIRE(amount(shortOnly, "hp") == 1);
    REQUIRE(amount(shortOnly, "fighter:second-wind") == 1);
}
TEST_CASE("Arcane Recovery spends its own use and never restores higher slots or excess capacity",
          "[srd55-v2][lifecycle]") {
    auto d = hero("wizard", 5);
    d.resources["spellSlots.1"] = 0;
    d.resources["spellSlots.2"] = 0;
    d = applied(d, "srd55.resources.short-rest", {{"hours", 1}});
    auto restored =
        applied(d, "srd55.resources.arcane-recovery", {{"slots", {{"1", 1}, {"2", 1}}}});
    REQUIRE(amount(restored, "spellSlots.1") == 1);
    REQUIRE(amount(restored, "spellSlots.2") == 1);
    REQUIRE(amount(restored, "wizard:arcane-recovery") == 0);
    REQUIRE_FALSE(
        command(restored, "srd55.resources.arcane-recovery", {{"slots", {{"1", 1}}}}).valid());
    auto tooMany = command(d, "srd55.resources.arcane-recovery", {{"slots", {{"2", 2}}}});
    REQUIRE_FALSE(tooMany.valid());
    REQUIRE(toJson(tooMany.document) == toJson(d));
    REQUIRE_FALSE(
        command(d, "srd55.resources.arcane-recovery", {{"slots", {{"1", 1}, {"6", 1}}}}).valid());
}
TEST_CASE("Font of Magic created slots are distinct temporary capacity and expire on Long Rest",
          "[srd55-v2][lifecycle]") {
    auto d = hero("sorcerer", 5);
    auto made = applied(d, "srd55.resources.points-to-slot", {{"slotLevel", 1}});
    REQUIRE(amount(made, "sorcerer:sorcery-points") == 3);
    REQUIRE(amount(made, "spellSlots.1") == 5);
    REQUIRE(made.resources["createdSpellSlots"]["1"] == 1);
    auto spent =
        applied(made, "srd55.resources.spend", {{"resource", "spellSlots.1"}, {"amount", 1}});
    REQUIRE(amount(spent, "spellSlots.1") == 4);
    auto rested = applied(spent, "srd55.resources.long-rest",
                          {{"hours", 8}, {"sleepHours", 6}, {"hoursSincePrevious", 0}});
    REQUIRE_FALSE(rested.resources.contains("createdSpellSlots"));
    REQUIRE(amount(rested, "spellSlots.1") == 4);
    REQUIRE(amount(rested, "sorcerer:sorcery-points") == 5);
    auto tooHigh =
        command(hero("sorcerer", 2), "srd55.resources.points-to-slot", {{"slotLevel", 2}});
    REQUIRE_FALSE(tooHigh.valid());
}
TEST_CASE("Effective HP overrides also govern resource capacity and transaction previews",
          "[srd55-v2][lifecycle]") {
    auto d = hero("fighter", 3);
    d.overrides =
        Json::array({{{"target", "hp.maximum"}, {"value", 40}, {"reason", "Campaign boon"}}});
    d.resources["hp"] = 39;
    auto e = evaluate(d, rules());
    REQUIRE(e.complete());
    REQUIRE(resource(e, "hp").maximum == 40);
    auto spent = applied(d, "srd55.resources.spend", {{"resource", "hp"}, {"amount", 1}});
    REQUIRE(amount(spent, "hp") == 38);
    REQUIRE(evaluate(spent, rules()).find("hp.maximum")->normal == 34);
}
TEST_CASE("Elf Trance supplies its published shorter Long Rest without changing accepted abilities",
          "[srd55-v2][lifecycle]") {
    auto d = hero("fighter", 1);
    d.choices["speciesId"] = "srd55:elf";
    d.choices["lineageId"] = "srd55:wood-elf";
    d.choices["speciesCastingAbility"] = "wisdom";
    d.choices["speciesSkill"] = "srd55:insight";
    d.resources["hp"] = 1;
    auto e = evaluate(d, rules());
    INFO(srd55fixtures::errors(e));
    REQUIRE(e.complete());
    auto rested = applied(d, "srd55.resources.long-rest",
                          {{"hours", 4}, {"sleepHours", 4}, {"hoursSincePrevious", 0}});
    REQUIRE(amount(rested, "hp") == 13);
    REQUIRE(rested.choices == d.choices);
    REQUIRE(rested.rolls == d.rolls);
}
TEST_CASE("Long Rest decrements the actual Wish cooldown and partial rest includes Tireless",
          "[srd55-v2][lifecycle]") {
    auto cleric = hero("cleric", 20);
    cleric.resources["cleric:divine-intervention-rests"] = 5;
    auto rested = applied(cleric, "srd55.resources.long-rest",
                          {{"hours", 8}, {"sleepHours", 6}, {"hoursSincePrevious", 0}});
    REQUIRE(rested.resources["cleric:divine-intervention-rests"] == 4);
    REQUIRE_FALSE(rested.resources.contains("cleric:wish-rests-remaining"));
    auto ranger = hero("ranger", 10);
    ranger.resources["exhaustion"] = 3;
    ranger.resources["hp"] = 1;
    auto partial =
        applied(ranger, "srd55.resources.long-rest",
                {{"hours", 1}, {"sleepHours", 0}, {"hoursSincePrevious", 0}, {"unfinished", true}});
    REQUIRE(partial.resources["exhaustion"] == 2);
    REQUIRE(amount(partial, "hp") == 1);
}
TEST_CASE("Scroll copying couples its accepted check, funds, consumed instance, and spellbook",
          "[srd55-v2][lifecycle][scroll-copy]") {
    auto d = applied(hero("wizard", 1), "srd55.inventory.initialize", Json::object());
    d = applied(d, "srd55.inventory.adjust-currency",
                {{"deltaCp", 10000}, {"reason", "Adventure treasure"}});
    auto evaluation = evaluate(d, rules());
    const auto &book = evaluation.moduleData["spellbooks"]["wizard"];
    std::string spellId;
    for (const auto &[id, item] : rules().content)
        if (item.value("kind", "") == "spell" && item.value("level", 0) == 1 &&
            std::find(book.begin(), book.end(), Json(id)) == book.end()) {
            const auto &lists = item.at("lists");
            if (std::find(lists.begin(), lists.end(), Json("wizard")) != lists.end()) {
                spellId = id;
                break;
            }
        }
    REQUIRE_FALSE(spellId.empty());
    d = applied(d, "srd55.inventory.acquire",
                {{"itemId", "srd55:magic-spell-scroll"},
                 {"quantity", 1},
                 {"source", "loot"},
                 {"variantId", "level-1"},
                 {"spellId", spellId}});
    const auto instance = d.resources["inventory"]["instances"].back()["id"];
    d = applied(d, "srd55.history.accept-baseline", Json::object());
    const auto before = toJson(d);
    const auto balance = d.resources["currencyCp"].get<int>();
    Json inputs = {{"instanceId", instance},
                   {"minutes", 120},
                   {"paidCp", 5000},
                   {"acceptedD20", 20},
                   {"bonus", 0}};
    auto success = command(d, "srd55.spells.copy-scroll", inputs);
    INFO(srd55fixtures::errors(evaluate(success.document, rules())));
    REQUIRE(success.valid());
    REQUIRE(success.document.resources["currencyCp"] == balance - 5000);
    REQUIRE(success.document.resources["inventory"]["instances"].back()["status"] == "consumed");
    REQUIRE(success.document.resources["inventory"]["instances"].back()["quantity"] == 0);
    REQUIRE(success.document.choices["spellcasting"]["wizard"]["copiedSpells"].back()["spellId"] ==
            spellId);
    REQUIRE(success.document.rolls["actions"].back()["success"] == true);
    REQUIRE(evaluate(success.document, rules()).complete());
    REQUIRE_FALSE(command(success.document, "srd55.spells.copy-scroll", inputs).valid());
    REQUIRE(toJson(d) == before);
    REQUIRE(toJson(command(d, "srd55.spells.copy-scroll", inputs).document) ==
            toJson(success.document));
    inputs["acceptedD20"] = 1;
    auto failedCheck = command(d, "srd55.spells.copy-scroll", inputs);
    REQUIRE(failedCheck.valid());
    REQUIRE(failedCheck.document.choices == d.choices);
    REQUIRE(failedCheck.document.resources["currencyCp"] == balance - 5000);
    REQUIRE(failedCheck.document.resources["inventory"]["instances"].back()["status"] ==
            "consumed");
    REQUIRE(failedCheck.document.rolls["actions"].back()["success"] == false);
    REQUIRE(evaluate(failedCheck.document, rules()).complete());
    inputs["bonus"] = 1;
    auto noReason = command(d, "srd55.spells.copy-scroll", inputs);
    REQUIRE_FALSE(noReason.valid());
    REQUIRE(toJson(noReason.document) == before);
    inputs["bonus"] = 0;
    inputs["minutes"] = 119;
    auto tooShort = command(d, "srd55.spells.copy-scroll", inputs);
    REQUIRE_FALSE(tooShort.valid());
    REQUIRE(toJson(tooShort.document) == before);
}
TEST_CASE(
    "Created slot records reject malformed ranks and counts instead of silently granting capacity",
    "[srd55-v2][lifecycle]") {
    auto d = hero("sorcerer", 5);
    for (const auto &invalid :
         Json::array({Json{{"1", "one"}}, Json{{"10", 1}}, Json{{"1", -1}}, Json::array({1})})) {
        d.resources["createdSpellSlots"] = invalid;
        REQUIRE_FALSE(evaluate(d, rules()).complete());
    }
}

TEST_CASE("Arcane Recovery does not duplicate Pact slots already restored by its Short Rest",
          "[srd55-v2][lifecycle]") {
    auto d = hero("wizard", 5);
    d.choices["level"] = 6;
    d.choices["multiclass"] = true;
    for (int level = 2; level <= 6; ++level)
        d.choices["advancement"][std::to_string(level)]["classId"] =
            level == 6 ? "srd55:warlock" : "srd55:wizard";
    const auto finished = srd55fixtures::finish(d, rules());
    INFO(srd55fixtures::errors(finished.evaluation));
    REQUIRE(finished.evaluation.complete());
    d = finished.document;
    d.resources["pactMagic.slots"] = 0;
    d.resources["spellSlots.1"] = 0;
    d = applied(d, "srd55.resources.short-rest", {{"hours", 1}});
    REQUIRE(amount(d, "pactMagic.slots") == 1);
    const auto rejected = command(d, "srd55.resources.arcane-recovery", {{"pactSlots", 1}});
    REQUIRE_FALSE(rejected.valid());
    REQUIRE(toJson(rejected.document) == toJson(d));
    const auto recovered =
        applied(d, "srd55.resources.arcane-recovery", {{"slots", {{"1", 1}}}, {"pactSlots", 0}});
    REQUIRE(amount(recovered, "spellSlots.1") == 1);
    REQUIRE(amount(recovered, "pactMagic.slots") == 1);
}
