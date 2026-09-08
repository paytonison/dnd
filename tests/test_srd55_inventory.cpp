#include "../src/editions/srd55_v2_internal.hpp"
#include "dnd/lifecycle.hpp"
#include "dnd/srd55_inventory.hpp"
#include "srd55_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>

using namespace dnd;
namespace {
const ResolvedRuleset &itemRules() {
    static const auto r = srd55fixtures::rules();
    return r;
}
CharacterDocument hero(const std::string &cls = "fighter", int level = 1) {
    auto completed = srd55fixtures::complete(cls, level, itemRules());
    INFO(srd55fixtures::errors(completed.evaluation));
    REQUIRE(completed.evaluation.complete());
    return completed.document;
}
CharacterDocument command(const CharacterDocument &d, const std::string &id,
                          Json in = Json::object()) {
    const auto result = executeCommand(d, itemRules(), {"srd55.inventory." + id, std::move(in)});
    std::string diagnostic;
    for (const auto &m : result.messages)
        diagnostic += m.text + "\n";
    INFO(id + ": " + diagnostic);
    REQUIRE(result.valid());
    return result.document;
}
void fails(const CharacterDocument &d, const std::string &id, Json in) {
    const auto before = toJson(d);
    const auto result = executeCommand(d, itemRules(), {"srd55.inventory." + id, std::move(in)});
    REQUIRE_FALSE(result.valid());
    REQUIRE(toJson(result.document) == before);
    REQUIRE(toJson(d) == before);
}
const Json &last(const CharacterDocument &d) {
    return d.resources.at("inventory").at("instances").back();
}
const Json &instance(const CharacterDocument &d, const std::string &id) {
    for (const auto &i : d.resources.at("inventory").at("instances"))
        if (i.at("id") == id)
            return i;
    throw std::runtime_error("Instance missing");
}
std::string acquire(CharacterDocument &d, const std::string &item, Json extra = Json::object()) {
    extra["itemId"] = "srd55:magic-" + item;
    extra["quantity"] = 1;
    extra["source"] = "loot";
    d = command(d, "acquire", extra);
    return last(d).at("id");
}
void equip(CharacterDocument &d, const std::string &id) {
    d = command(d, "equip", {{"instanceId", id}, {"slot", "auto"}});
}
void attune(CharacterDocument &d, const std::string &id) {
    d = command(d, "attune", {{"instanceId", id}, {"completedShortRest", true}});
}
srd55v2::Context ctx(const CharacterDocument &d, const std::string &cls = "fighter",
                     int level = 1) {
    srd55v2::Context x(d, itemRules());
    x.initialClass = "srd55:" + cls;
    x.classLevels[x.initialClass] = level;
    x.totalLevel = level;
    x.proficiency = 2 + (level - 1) / 4;
    x.armorTraining = {"light", "medium", "heavy", "shield"};
    for (const auto *a :
         {"strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"}) {
        x.scores[a] = 14;
        x.modifiers[a] = 2;
    }
    if (cls == "wizard")
        x.castingAbilities.insert("intelligence");
    return x;
}
long long statistic(const CharacterDocument &d, const std::string &id) {
    const auto e = evaluate(d, itemRules());
    const auto *result = e.find(id);
    INFO(id);
    REQUIRE(result);
    return result->effective.get<long long>();
}
} // namespace
TEST_CASE(
    "Full SRD magic item catalog is distinct from the spell catalog and preserves source changes",
    "[inventory][content]") {
    int count = 0;
    for (const auto &[id, item] : itemRules().content)
        if (item.at("kind") == "magic-item") {
            ++count;
            REQUIRE(item.contains("coverage"));
            REQUIRE_FALSE(item.at("description").get<std::string>().empty());
        }
    REQUIRE(count == 258);
    REQUIRE(itemRules().find("srd55:magic-weapon")->at("kind") == "spell");
    REQUIRE(itemRules().find("srd55:magic-weapon-bonus")->at("kind") == "magic-item");
    REQUIRE(itemRules().find("srd55:magic-wand-of-the-war-mage")->at("requiresAttunement") == true);
    REQUIRE(itemRules().find("srd55:magic-berserker-axe")->at("requiresAttunement") == true);
    const auto &health = *itemRules().find("srd55:magic-periapt-of-health");
    REQUIRE(health.at("actions").at(0).at("roll") ==
            Json({{"dice", 2}, {"sides", 4}, {"bonus", 2}}));
    REQUIRE(health.at("effects").at(0).at("op") == "advantage");
    const auto &frost = *itemRules().find("srd55:magic-staff-of-frost");
    const auto &spells = frost.at("actions");
    const auto wall = std::find_if(spells.begin(), spells.end(), [](const Json &s) {
        return s.at("spellId") == "srd55:wall-of-ice";
    });
    REQUIRE(wall != spells.end());
    REQUIRE(wall->at("castLevel") == 6);
}
TEST_CASE(
    "Inventory initialization freezes exact physical items and does not restore an existing wallet",
    "[inventory]") {
    auto d = hero("wizard");
    const auto prior = evaluate(d, itemRules());
    const auto seed = prior.moduleData.at("inventory.creationSeed");
    d.resources["currencyCp"] = 123;
    const auto choices = d.choices;
    d = command(d, "initialize");
    REQUIRE(d.resources.at("currencyCp") == 123);
    REQUIRE(d.choices == choices);
    int items = 0;
    for (const auto &entry : d.resources.at("inventory").at("instances"))
        items += entry.at("quantity").get<int>();
    int expected = 0;
    for (const auto &entry : seed)
        expected += entry.at("quantity").get<int>();
    REQUIRE(items == expected);
    fails(d, "initialize", Json::object());
    const auto roundtrip = documentFromJson(toJson(d));
    REQUIRE(roundtrip.resources == d.resources);
}
TEST_CASE("Owned purchases sales and recovery are atomic and never recreate spent item charges",
          "[inventory]") {
    auto d = command(hero(), "initialize");
    d.resources["currencyCp"] = 1000;
    d = command(
        d, "acquire",
        {{"itemId", "srd55:dagger"}, {"quantity", 3}, {"source", "purchase"}, {"paidCp", 600}});
    const std::string id = last(d).at("id");
    REQUIRE(d.resources.at("currencyCp") == 400);
    REQUIRE(last(d).at("quantity") == 3);
    fails(d, "acquire", {{"itemId", "srd55:dagger"}, {"source", "purchase"}, {"paidCp", 500}});
    d = command(
        d, "dispose",
        {{"instanceId", id}, {"quantity", 2}, {"disposition", "sold"}, {"proceedsCp", 200}});
    REQUIRE(d.resources.at("currencyCp") == 600);
    REQUIRE(instance(d, id).at("quantity") == 1);
    d = command(d, "recover", {{"instanceId", id}, {"quantity", 1}, {"paidCp", 100}});
    REQUIRE(instance(d, id).at("quantity") == 2);
    REQUIRE(d.resources.at("currencyCp") == 500);
    fails(d, "recover", {{"instanceId", id}, {"quantity", 2}});
}
TEST_CASE("Attunement gates current item effects and distinct protection items stack",
          "[inventory]") {
    auto d = command(hero(), "initialize");
    const auto amulet = acquire(d, "amulet-of-health");
    equip(d, amulet);
    REQUIRE(srd55v2::resolveInventory(ctx(d)).abilityMinimums.empty());
    attune(d, amulet);
    REQUIRE(srd55v2::resolveInventory(ctx(d)).abilityMinimums.at("constitution") == 19);
    const auto cloak = acquire(d, "cloak-of-protection"), ring = acquire(d, "ring-of-protection");
    equip(d, cloak);
    attune(d, cloak);
    equip(d, ring);
    attune(d, ring);
    const auto resolved = srd55v2::resolveInventory(ctx(d));
    REQUIRE(resolved.armorBonus == 2);
    REQUIRE(resolved.saveBonuses.at("dexterity") == 2);
    const auto gauntlets = acquire(d, "gauntlets-of-ogre-power");
    fails(d, "attune", {{"instanceId", gauntlets}, {"completedShortRest", true}});
    d = command(d, "unattune",
                {{"instanceId", amulet}, {"cause", "voluntary-short-rest"}, {"completed", true}});
    attune(d, gauntlets);
    const auto secondRing = acquire(d, "ring-of-protection");
    d = command(d, "unattune",
                {{"instanceId", cloak}, {"cause", "voluntary-short-rest"}, {"completed", true}});
    fails(d, "attune", {{"instanceId", secondRing}, {"completedShortRest", true}});
}
TEST_CASE("Thief attunes four items but cannot orbit four Ioun Stones", "[inventory]") {
    auto d = command(hero("rogue", 13), "initialize");
    std::vector<std::string> stones;
    for (const auto *variant : {"agility", "fortitude", "insight", "protection"}) {
        auto id = acquire(d, "ioun-stone", {{"variantId", variant}});
        attune(d, id);
        stones.push_back(id);
    }
    REQUIRE(srd55v2::resolveInventory(ctx(d, "rogue", 13)).attunementMaximum == 4);
    for (int i = 0; i < 3; ++i)
        equip(d, stones[static_cast<std::size_t>(i)]);
    fails(d, "equip", {{"instanceId", stones[3]}, {"slot", "auto"}});
}
TEST_CASE(
    "Current ability additions remain separate from minimum-setting effects and permanent study",
    "[inventory]") {
    auto d = command(hero(), "initialize");
    auto stone = acquire(d, "ioun-stone", {{"variantId", "strength"}});
    attune(d, stone);
    equip(d, stone);
    auto gloves = acquire(d, "gauntlets-of-ogre-power");
    attune(d, gloves);
    equip(d, gloves);
    auto effects = srd55v2::resolveInventory(ctx(d));
    REQUIRE(effects.abilityBonuses.at("strength") == 2);
    REQUIRE(effects.abilityBonusCaps.at("strength") == 20);
    REQUIRE(effects.abilityMinimums.at("strength") == 19);
    REQUIRE(effects.permanentEffects.empty());
    const auto book = acquire(d, "manual-of-gainful-exercise");
    d = command(d, "study", {{"instanceId", book}, {"hours", 48}, {"days", 6}});
    REQUIRE(instance(d, book).at("dormant") == true);
    const auto permanent = d.resources.at("inventory").at("permanentEffects");
    REQUIRE(permanent.size() == 1);
    REQUIRE(permanent[0].at("ability") == "strength");
    REQUIRE(permanent[0].at("maxScore") == 30);
    REQUIRE(permanent[0].at("acquiredCharacterLevel") == 1);
    fails(d, "study", {{"instanceId", book}, {"hours", 48}, {"days", 6}});
    d = command(d, "dispose", {{"instanceId", book}, {"quantity", 1}, {"disposition", "given"}});
    REQUIRE(d.resources.at("inventory").at("permanentEffects") == permanent);
    d = command(d, "recover", {{"instanceId", book}, {"quantity", 1}});
    REQUIRE(instance(d, book).at("dormant") == true);
    d = command(d, "awaken-tome",
                {{"instanceId", book}, {"yearsElapsed", 100}, {"completed", true}});
    REQUIRE(instance(d, book).at("dormant") == false);
}
TEST_CASE("Persistent curses survive removal and disposition until a resolved release",
          "[inventory]") {
    auto d = command(hero(), "initialize");
    auto armor = acquire(d, "armor-of-vulnerability",
                         {{"baseProfile", "srd55:chain-mail"}, {"variantId", "slashing"}});
    attune(d, armor);
    equip(d, armor);
    REQUIRE(instance(d, armor).at("curseActive") == true);
    fails(d, "unattune",
          {{"instanceId", armor}, {"cause", "voluntary-short-rest"}, {"completed", true}});
    d = command(d, "unequip", {{"instanceId", armor}});
    d = command(d, "dispose", {{"instanceId", armor}, {"quantity", 1}, {"disposition", "given"}});
    const auto effects = srd55v2::resolveInventory(ctx(d)).effects;
    REQUIRE(std::any_of(effects.begin(), effects.end(),
                        [](const Json &e) { return e.at("op") == "vulnerability"; }));
    REQUIRE_FALSE(std::any_of(effects.begin(), effects.end(),
                              [](const Json &e) { return e.at("op") == "resistance"; }));
    d = command(d, "release-curse",
                {{"instanceId", armor},
                 {"resolved", true},
                 {"reason", "Remove Curse resolved on the character"}});
    REQUIRE(instance(d, armor).at("curseActive") == false);
    REQUIRE(instance(d, armor).at("attuned") == false);
    auto axe = acquire(d, "berserker-axe", {{"baseProfile", "srd55:battleaxe"}});
    attune(d, axe);
    fails(d, "dispose", {{"instanceId", axe}, {"quantity", 1}, {"disposition", "given"}});
}
TEST_CASE("Item spell use charge depletion and recharge preserve exact accepted outcomes",
          "[inventory]") {
    auto d = command(hero("wizard"), "initialize");
    auto wand = acquire(d, "wand-of-magic-missiles");
    equip(d, wand);
    d = command(d, "use",
                {{"instanceId", wand}, {"itemActionId", "cast-magic-missile"}, {"charges", 3}});
    REQUIRE(instance(d, wand).at("charges") == 4);
    REQUIRE(d.resources.at("inventory").at("events").back().at("castLevel") == 3);
    fails(d, "use", {{"instanceId", wand}, {"itemActionId", "cast-magic-missile"}, {"charges", 5}});
    d = command(d, "dispose", {{"instanceId", wand}, {"quantity", 1}, {"disposition", "lost"}});
    d = command(d, "recover", {{"instanceId", wand}, {"quantity", 1}});
    REQUIRE(instance(d, wand).at("charges") == 4);
    equip(d, wand);
    d = command(d, "recharge",
                {{"instanceId", wand},
                 {"event", "dawn"},
                 {"eventReference", "Game day 12"},
                 {"acceptedRoll", 4},
                 {"completed", true}});
    REQUIRE(instance(d, wand).at("charges") == 7);
    fails(d, "recharge",
          {{"instanceId", wand},
           {"event", "dawn"},
           {"eventReference", "Game day 12"},
           {"acceptedRoll", 7},
           {"completed", true}});
    d = command(d, "use",
                {{"instanceId", wand},
                 {"itemActionId", "cast-magic-missile"},
                 {"charges", 7},
                 {"depletionRoll", 1}});
    REQUIRE(instance(d, wand).at("status") == "destroyed");
    fails(d, "recover", {{"instanceId", wand}, {"quantity", 1}});
}
TEST_CASE("Thief charge conservation does not apply to a non-charge daily healing use",
          "[inventory]") {
    auto d = command(hero("rogue", 13), "initialize");
    auto wand = acquire(d, "wand-of-magic-missiles");
    equip(d, wand);
    d = command(d, "use",
                {{"instanceId", wand},
                 {"itemActionId", "cast-magic-missile"},
                 {"charges", 2},
                 {"chargeRefundRoll", 6}});
    REQUIRE(instance(d, wand).at("charges") == 7);
    auto pendant = acquire(d, "periapt-of-health");
    attune(d, pendant);
    equip(d, pendant);
    d.resources["hp"] = 1;
    fails(d, "use", {{"instanceId", pendant}, {"itemActionId", "heal"}, {"acceptedRoll", 3}});
    d = command(d, "use",
                {{"instanceId", pendant}, {"itemActionId", "heal"}, {"acceptedRoll", 10}});
    REQUIRE(instance(d, pendant).at("charges") == 0);
    REQUIRE(d.resources.at("hp") == 11);
}
TEST_CASE("Higher level equipment guidance is optional exact and awarded only once",
          "[inventory]") {
    auto d = command(hero("fighter", 5), "initialize");
    const auto before = d.resources.at("currencyCp").get<long long>();
    d = command(d, "higher-level-guide", {{"approved", true}, {"acceptedD10", 7}});
    REQUIRE(d.resources.at("currencyCp") == before + 67500);
    REQUIRE(d.resources.at("inventory").at("allowances").at("uncommon") == 1);
    fails(d, "higher-level-guide", {{"approved", true}, {"acceptedD10", 10}});
    d = command(d, "acquire",
                {{"itemId", "srd55:magic-cloak-of-protection"},
                 {"quantity", 1},
                 {"source", "starting-guide"}});
    REQUIRE(d.resources.at("inventory").at("allowances").at("uncommon") == 0);
    fails(d, "acquire",
          {{"itemId", "srd55:magic-gauntlets-of-ogre-power"},
           {"quantity", 1},
           {"source", "starting-guide"}});
}
TEST_CASE("Bracers and magic Shields respect real proficiency and armor conditions",
          "[inventory][integration]") {
    auto d = command(hero("wizard"), "initialize");
    d = command(d, "acquire", {{"itemId", "srd55:longbow"}, {"quantity", 1}, {"source", "gift"}});
    const std::string bow = last(d).at("id");
    equip(d, bow);
    const auto before = statistic(d, "attack.weapon");
    auto bracers = acquire(d, "bracers-of-archery");
    attune(d, bracers);
    equip(d, bracers);
    const auto properties = srd55v2::resolveInventory(ctx(d, "wizard"));
    REQUIRE(properties.weaponTraining.contains("srd55:longbow"));
    REQUIRE(properties.weaponDamageBonus == 2);
    REQUIRE(statistic(d, "attack.weapon") == before + statistic(d, "proficiency"));
    auto defense = acquire(d, "bracers-of-defense");
    attune(d, defense);
    equip(d, defense);
    REQUIRE(srd55v2::resolveInventory(ctx(d, "wizard")).armorBonus == 2);
    d = command(d, "acquire",
                {{"itemId", "srd55:shield-equipment"}, {"quantity", 1}, {"source", "gift"}});
    const std::string shield = last(d).at("id");
    d = command(d, "unequip", {{"instanceId", bow}});
    equip(d, shield);
    REQUIRE(srd55v2::resolveInventory(ctx(d, "wizard")).armorBonus == 0);
    auto fighter = command(hero(), "initialize");
    auto enchanted = acquire(fighter, "shield", {{"variantId", "+2"}});
    equip(fighter, enchanted);
    auto early = ctx(fighter);
    early.armorTraining.clear();
    REQUIRE(srd55v2::resolveInventory(early).armorBonus == 2);
}
TEST_CASE("Stored items retain ownership without on-person effects and fixed item DC stays fixed",
          "[inventory][integration]") {
    auto d = command(hero("wizard"), "initialize");
    auto stone = acquire(d, "stone-of-good-luck-luckstone");
    attune(d, stone);
    REQUIRE(srd55v2::resolveInventory(ctx(d, "wizard")).initiativeBonus == 1);
    d = command(d, "move", {{"instanceId", stone}, {"location", "stored"}});
    REQUIRE(instance(d, stone).at("status") == "owned");
    REQUIRE(srd55v2::resolveInventory(ctx(d, "wizard")).initiativeBonus == 0);
    auto robe = acquire(d, "robe-of-the-archmagi");
    attune(d, robe);
    equip(d, robe);
    auto wand = acquire(d, "wand-of-fireballs");
    attune(d, wand);
    equip(d, wand);
    const auto state = srd55v2::resolveInventory(ctx(d, "wizard"));
    REQUIRE(state.spellSaveBonus == 2);
    REQUIRE(state.spellAttackBonus == 2);
    const auto grant =
        std::find_if(state.spellGrants.begin(), state.spellGrants.end(),
                     [](const Json &g) { return g.at("spellId") == "srd55:fireball"; });
    REQUIRE(grant != state.spellGrants.end());
    REQUIRE(grant->at("fixedDC") == 15);
    const auto evaluated = evaluate(d, itemRules());
    const auto *profiles = evaluated.find("spells.profiles");
    REQUIRE(profiles);
    bool found = false;
    for (const auto &profile : profiles->normal)
        if (profile.value("source", "").find("Wand of Fireballs") != std::string::npos) {
            REQUIRE(profile.at("DC") == 15);
            found = true;
        }
    REQUIRE(found);
}
TEST_CASE("Thief magic device rules do not waive attunement class prerequisites",
          "[inventory][integration]") {
    auto d = command(hero("rogue", 13), "initialize");
    const auto robe = acquire(d, "robe-of-the-archmagi");
    fails(d, "attune", {{"instanceId", robe}, {"completedShortRest", true}});
}
TEST_CASE("Canonical currency adjustments require reasons and preserve atomic balances",
          "[inventory]") {
    auto d = command(hero(), "initialize");
    d.resources["currencyCp"] = 100;
    d = command(d, "adjust-currency", {{"deltaCp", 50}, {"reason", "Share of recovered coins"}});
    REQUIRE(d.resources.at("currencyCp") == 150);
    const auto &event = d.resources.at("inventory").at("events").back();
    REQUIRE(event.at("balanceBefore") == 100);
    REQUIRE(event.at("balanceAfter") == 150);
    d = command(d, "adjust-currency", {{"deltaCp", -125}, {"reason", "Room and provisions"}});
    REQUIRE(d.resources.at("currencyCp") == 25);
    fails(d, "adjust-currency", {{"deltaCp", -26}, {"reason", "Too costly"}});
    fails(d, "adjust-currency", {{"deltaCp", 500}, {"reason", "  "}});
}
TEST_CASE("Permanent item study precedes later ASI cap validation", "[inventory][integration]") {
    auto d = command(hero("fighter", 1), "initialize");
    const auto book = acquire(d, "manual-of-gainful-exercise");
    d = command(d, "study", {{"instanceId", book}, {"hours", 48}, {"days", 6}});
    REQUIRE(statistic(d, "ability.strength") == 19); // Initial15 + Soldier2 + manual2.
    d.choices["level"] = 4;
    d.choices["subclasses"]["fighter"] = "srd55:champion";
    d.choices["features"]["fighter"]["weaponMasteries"] = {"srd55:greatsword", "srd55:flail",
                                                           "srd55:javelin", "srd55:longbow"};
    d.choices["feats"]["4"] = {{"id", "srd55:ability-score-improvement"},
                               {"boosts", {{"strength", 2}}}};
    const auto invalid = evaluate(d, itemRules());
    REQUIRE(std::any_of(invalid.messages.begin(), invalid.messages.end(), [](const Message &m) {
        return m.code.ends_with("feat.cap") && m.severity == "error";
    }));
    d.choices["feats"]["4"]["boosts"] = {{"strength", 1}, {"wisdom", 1}};
    const auto valid = evaluate(d, itemRules());
    INFO(srd55fixtures::errors(valid));
    REQUIRE(valid.complete());
    REQUIRE(statistic(d, "ability.strength") == 20);
}
TEST_CASE(
    "Inactive human Magic Initiate choices cannot qualify another species for item attunement",
    "[inventory][integration]") {
    auto d = hero();
    d.choices["humanFeat"] = "srd55:magic-initiate-wizard";
    d.choices["magicInitiate"]["human"]["ability"] = "intelligence";
    d = command(d, "initialize");
    const auto wand = acquire(d, "wand-of-fireballs");
    fails(d, "attune", {{"instanceId", wand}, {"completedShortRest", true}});
}
TEST_CASE("Healing potions have published variants and consume one physical dose", "[inventory]") {
    auto d = command(hero(), "initialize");
    const auto potion = acquire(d, "potions-of-healing", {{"variantId", "healing"}});
    d.resources["hp"] = 1;
    d = command(d, "use",
                {{"instanceId", potion},
                 {"itemActionId", "drink"},
                 {"acceptedRoll", 7},
                 {"healingTarget", "self"}});
    REQUIRE(d.resources.at("hp") == 8);
    REQUIRE(instance(d, potion).at("status") == "consumed");
    REQUIRE(instance(d, potion).at("quantity") == 0);
    fails(d, "use", {{"instanceId", potion}, {"itemActionId", "drink"}, {"acceptedRoll", 7}});
    const auto supreme = acquire(d, "potions-of-healing", {{"variantId", "supreme"}});
    d = command(d, "use",
                {{"instanceId", supreme},
                 {"itemActionId", "drink"},
                 {"acceptedRoll", 60},
                 {"healingTarget", "other"}});
    REQUIRE(d.resources.at("hp") == 8);
}
TEST_CASE(
    "Owned Spell Scrolls preserve exact source rank and support atomic external copy consumption",
    "[inventory]") {
    auto d = command(hero("wizard"), "initialize");
    const auto scroll =
        acquire(d, "spell-scroll", {{"variantId", "level-1"}, {"spellId", "srd55:shield"}});
    REQUIRE(instance(d, scroll).at("scrollLevel") == 1);
    REQUIRE(instance(d, scroll).at("scrollSaveDc") == 13);
    REQUIRE(instance(d, scroll).at("scrollAttackBonus") == 5);
    fails(d, "acquire",
          {{"itemId", "srd55:magic-spell-scroll"},
           {"variantId", "level-1"},
           {"spellId", "srd55:fireball"}});
    const auto before = d.resources.at("currencyCp");
    const auto consumed = srd55v2::consumeSrd55InventoryScroll(d, itemRules(), scroll,
                                                               "Failed spellbook copying check");
    REQUIRE(consumed.valid());
    REQUIRE(instance(consumed.document, scroll).at("status") == "consumed");
    REQUIRE(consumed.document.resources.at("currencyCp") == before);
    const auto repeated =
        srd55v2::consumeSrd55InventoryScroll(consumed.document, itemRules(), scroll, "Retry");
    REQUIRE_FALSE(repeated.valid());
    REQUIRE(toJson(repeated.document) == toJson(consumed.document));
    auto malformed = d;
    for (auto &i : malformed.resources["inventory"]["instances"])
        if (i["id"] == scroll)
            i["scrollSaveDc"] = 99;
    const auto checked = srd55v2::resolveInventory(ctx(malformed, "wizard"));
    REQUIRE_FALSE(checked.evaluation.complete());
}

TEST_CASE("Item imports reject malformed executable mechanics and unsupported numeric conditions",
          "[inventory][content]") {
    const auto valid = *itemRules().find("srd55:magic-wand-of-fireballs");
    auto rejects = [&](Json value) {
        ContentPack pack;
        pack.entries.push_back(std::move(value));
        REQUIRE_FALSE(srd55v2::validateSrd55MagicItems(pack).empty());
    };
    auto value = valid;
    value["effects"] =
        Json::array({{{"op", "ac-bonus"}, {"value", 2}, {"condition", "only-on-tuesday"}}});
    rejects(value);
    value["effects"][0].erase("value");
    rejects(value);
    value = valid;
    value["charges"]["recharge"] = {{"event", "dawn"}, {"fixed", "seven"}};
    rejects(value);
    value = valid;
    value["actions"][0]["saveDc"] = "fifteen";
    rejects(value);
    value = valid;
    value["actions"].push_back(value["actions"][0]);
    rejects(value);
    value = *itemRules().find("srd55:magic-potions-of-healing");
    value["variants"][0]["actions"][0]["roll"]["sides"] = 0;
    rejects(value);
    value = *itemRules().find("srd55:magic-spell-scroll");
    value["variants"][0]["saveDc"] = -1;
    rejects(value);
    value = *itemRules().find("srd55:magic-bracers-of-archery");
    value["effects"][0]["weaponProfiles"] = Json::array({17});
    rejects(value);
}

TEST_CASE("The supported scroll binding works for a namespaced supplemental item",
          "[inventory][content]") {
    auto d = command(hero("wizard"), "initialize");
    auto rules = itemRules();
    Json definition = *rules.find("srd55:magic-spell-scroll");
    definition["id"] = "local:approved-spell-scroll";
    definition["name"] = "Approved supplement scroll";
    rules.content.emplace("local:approved-spell-scroll", definition);
    auto acquired = executeCommand(d, rules,
                                   {"srd55.inventory.acquire",
                                    {{"itemId", "local:approved-spell-scroll"},
                                     {"source", "loot"},
                                     {"variantId", "level-1"},
                                     {"spellId", "srd55:magic-missile"}}});
    REQUIRE(acquired.valid());
    const auto id = last(acquired.document).at("id").get<std::string>();
    auto consumed = srd55v2::consumeSrd55InventoryScroll(acquired.document, rules, id,
                                                         "Validated supplement scroll copy");
    REQUIRE(consumed.valid());
    REQUIRE(instance(consumed.document, id).at("status") == "consumed");
}
