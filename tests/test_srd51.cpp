#include "srd51_fixture.hpp"
#include "dnd/persistence.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <fstream>
#include <set>
#include <tuple>

namespace {
using namespace dnd;
using srd51fixtures::fighter;
bool errors(const std::vector<Message>& messages) {
    return std::any_of(messages.begin(), messages.end(), [](const auto& message) { return message.severity == "error"; });
}
void complete(const Evaluation& evaluation) {
    std::string errors;
    for (const auto& message : evaluation.messages)
        if (message.severity == "error") errors += message.path + ": " + message.text + "\n";
    INFO(errors);
    REQUIRE(evaluation.complete());
}
const Calculation& calculation(const Evaluation& evaluation, const std::string& id) {
    const auto* value = evaluation.find(id);
    REQUIRE(value);
    return *value;
}
Json value(const Evaluation& evaluation, const std::string& id) {
    return calculation(evaluation, id).effective;
}
Evaluation run(const CharacterDocument& document) {
    return evaluate(document, srd51fixtures::rules());
}
bool errorAt(const Evaluation& evaluation, const std::string& path) {
    return std::any_of(evaluation.messages.begin(), evaluation.messages.end(), [&](const auto& message) {
        return message.severity == "error" && message.path == path;
    });
}
const Field& field(const Evaluation& evaluation, const std::string& path) {
    for (const auto& stage : evaluation.stages)
        for (const auto& candidate : stage.fields)
            if (candidate.path == path) return candidate;
    FAIL("Missing field " << path);
    throw std::runtime_error("Missing field");
}
bool source(const Calculation& result, const std::string& publication, const std::string& page) {
    return std::any_of(result.sources.begin(), result.sources.end(), [&](const auto& ref) {
        return ref.publication == publication && ref.page == page;
    });
}
std::string steps(const Calculation& result) {
    std::string text;
    for (const auto& step : result.steps) text += step + "\n";
    return text;
}
struct PackFixture {
    ContentPack core = srd51fixtures::pack();
    Json manifest = core.manifest;
    Json entries = Json::array();
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("dnd-srd51-" + fighter().id);
    PackFixture() {
        std::filesystem::create_directories(root / "input");
        manifest["id"] = "srd51-test";
        manifest["name"] = "2014 profile test";
        manifest["publisher"] = "2014 Test Publisher";
        manifest["origin"] = "homebrew";
        manifest["dependencies"] = Json::array({{{"id", "srd51-core"}, {"version", "1.0.0"}}});
    }
    ~PackFixture() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    Json definition(const std::string& id) const {
        const auto found = std::find_if(core.entries.begin(), core.entries.end(), [&](const auto& entry) { return entry.at("id") == id; });
        REQUIRE(found != core.entries.end());
        return *found;
    }
    void write() const {
        std::ofstream(root / "input" / "manifest.json") << manifest.dump(2);
        std::ofstream(root / "input" / "content.json") << entries.dump(2);
    }
    CharacterDocument document(int level = 1) const {
        auto result = fighter(level);
        result.packs.push_back({"srd51-test", "1.0.0"});
        return result;
    }
};
}

TEST_CASE("Original 2014 Human Fighter levels one through three have independent source totals", "[srd51][creation]") {
    // SRD 5.1 pp. 5, 24-25, 56, 61, 63, 66 and 76-83:
    // Human +1 each; d10/6 fixed + Con2; chain16 + shield2 + Defense1.
    for (int level = 1; level <= 3; ++level) {
        INFO("Level " << level);
        const auto document = fighter(level);
        const auto evaluation = run(document);
        complete(evaluation);
        const std::array<const char*, 6> abilities{"strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"};
        const std::array<int, 6> totals{16, 15, 14, 9, 13, 11};
        for (std::size_t i = 0; i < abilities.size(); ++i)
            CHECK(value(evaluation, "ability." + std::string(abilities[i])) == totals[i]);
        CHECK(value(evaluation, "hp.maximum") == std::array<int, 3>{12, 20, 28}[static_cast<std::size_t>(level - 1)]);
        CHECK(value(evaluation, "armorClass") == 19);
        CHECK(value(evaluation, "attack.weapon") == 5);
        CHECK(value(evaluation, "damage.weapon") == "1d8+3 slashing");
        CHECK(value(evaluation, "proficiency") == 2);
        CHECK(value(evaluation, "initiative") == 2);
        CHECK(value(evaluation, "speed") == 30);
        CHECK(value(evaluation, "save.strength") == 5);
        CHECK(value(evaluation, "save.constitution") == 4);
        CHECK(value(evaluation, "save.wisdom") == 1);
        CHECK(value(evaluation, "skill.srd51:athletics") == 5);
        CHECK(value(evaluation, "skill.srd51:perception") == 3);
        CHECK(value(evaluation, "skill.srd51:insight") == 3);
        CHECK(value(evaluation, "skill.srd51:religion") == 1);
        CHECK(value(evaluation, "secondWind.uses") == 1);
        CHECK(value(evaluation, "actionSurge.uses") == (level == 1 ? 0 : 1));
        CHECK(value(evaluation, "critical.minimum") == (level == 3 ? 19 : 20));
        if (level == 3) CHECK(value(evaluation, "subclass") == "Champion");
        else CHECK_FALSE(evaluation.find("subclass"));
        CHECK(value(evaluation, "money.remainingCp") == 1500);
        CHECK(value(evaluation, "languages") == Json::array({"Common", "Dwarvish", "Elvish", "Orc"}));
        CHECK(evaluation.rollRequests.empty());
        CHECK(toJson(evaluation) == toJson(run(document)));
    }
}

TEST_CASE("2014 fighting styles apply their published loadout conditions", "[srd51][styles]") {
    // SRD 5.1 p.24 and pp.64-66,95. Reaction/damage-die adjudication
    // remains a sourced sheet instruction rather than fabricated flat bonuses.
    auto document = fighter();
    SECTION("Defense requires armor, while a shield alone does not activate it") {
        document.choices["armorId"] = "none";
        const auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "armorClass") == 14); // 10 + Dex2 + shield2.
    }
    SECTION("Archery adds two to a longbow attack and not its damage") {
        document.choices["fightingStyle"] = "srd51:archery";
        document.choices["startingEquipment"]["armor"] = "leather";
        document.choices["armorId"] = "srd51:leather";
        document.choices["weaponId"] = "srd51:longbow";
        document.choices["shieldId"] = "none";
        const auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "attack.weapon") == 6); // Dex2 + proficiency2 + Archery2.
        CHECK(value(evaluation, "damage.weapon") == "1d8+2 piercing");
        CHECK(value(evaluation, "armorClass") == 13);
        document.choices["weaponId"] = "srd51:handaxe";
        const auto thrown = run(document); complete(thrown);
        CHECK(value(thrown, "attack.weapon") == 5); // A thrown melee weapon is not a ranged weapon.
    }
    SECTION("Dueling works with a shield and stops when using two hands") {
        document.choices["fightingStyle"] = "srd51:dueling";
        const auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "damage.weapon") == "1d8+5 slashing");
        document.choices["shieldId"] = "none";
        document.choices["twoHands"] = true;
        const auto versatile = run(document); complete(versatile);
        CHECK(value(versatile, "damage.weapon") == "1d10+3 slashing");
    }
    SECTION("Great Weapon Fighting retains its reroll instruction without changing flat damage") {
        document.choices["fightingStyle"] = "srd51:great-weapon-fighting";
        document.choices["startingWeapons"]["weapon1"] = "srd51:greatsword";
        document.choices["weaponId"] = "srd51:greatsword";
        document.choices["shieldId"] = "none";
        const auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "damage.weapon") == "2d6+3 slashing");
        const auto& style = calculation(evaluation, "fightingStyle");
        CHECK(steps(style).find("reroll") != std::string::npos);
        CHECK(steps(style).find("1 or 2") != std::string::npos);
        CHECK(steps(style).find("two hands") != std::string::npos);
        CHECK(source(style, "System Reference Document 5.1", "24"));
    }
    SECTION("Protection records the shield, reaction and nearby-other-target conditions") {
        document.choices["fightingStyle"] = "srd51:protection";
        const auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "armorClass") == 18);
        const auto& style = calculation(evaluation, "fightingStyle");
        for (const auto* text : {"shield", "reaction", "someone else", "5 feet", "disadvantage"})
            CHECK(steps(style).find(text) != std::string::npos);
        CHECK(source(style, "System Reference Document 5.1", "24"));
    }
    SECTION("Two Weapon Fighting adds the selected ability to the offhand damage") {
        document.choices["shieldId"] = "none";
        document.choices["weaponId"] = "srd51:handaxe";
        document.choices["offhandId"] = "srd51:handaxe";
        auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "damage.offhand") == "1d6+0 slashing");
        document.choices["fightingStyle"] = "srd51:two-weapon-fighting";
        evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "damage.offhand") == "1d6+3 slashing");
        CHECK(value(evaluation, "attack.offhand") == 5);
        document.choices["abilityMethod"] = "rolled";
        document.choices["abilities"]["strength"] = 6; // Human7: floor(-3/2)=-2.
        document.choices["fightingStyle"] = "srd51:defense";
        evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "damage.offhand") == "1d6-2 slashing");
    }
}

TEST_CASE("2014 creation distinguishes point buy, duplicate training, armor and ownership", "[srd51][creation]") {
    auto document = fighter();
    SECTION("Point-buy permission inherits per key and explicit false overrides it") {
        // Basic Rules v0.3 pp.7-8: 9+7+5+0+4+2 = 27.
        document.choices["abilityMethod"] = "point-buy";
        document.campaign["allowPointBuy"] = true;
        complete(run(document));
        document.choices["options"]["unrelated"] = false;
        const auto inherited = run(document);
        complete(inherited);
        CHECK_FALSE(errorAt(inherited, "/abilityMethod"));
        const auto& method = field(inherited, "/abilityMethod");
        const auto option = std::find_if(method.options.begin(), method.options.end(), [](const auto& value) { return value.id == "point-buy"; });
        REQUIRE(option != method.options.end()); CHECK(option->available);
        document.choices["options"]["allowPointBuy"] = false;
        CHECK_FALSE(run(document).complete());
        CHECK(errorAt(run(document), "/abilityMethod"));
        document.choices["options"].erase("allowPointBuy");
        document.choices["options"]["unrelated"] = true;
        CHECK(errorAt(run(document), "/options/unrelated"));
        document.choices["options"]["unrelated"] = false;
        document.choices["abilities"]["strength"] = 14; // 25 points, incomplete spending.
        CHECK(errorAt(run(document), "/abilities"));
    }
    SECTION("Duplicated background skill grants a different replacement skill") {
        document.choices["classSkills"] = {"srd51:insight", "srd51:athletics"};
        CHECK(errorAt(run(document), "/replacementSkills"));
        document.choices["replacementSkills"] = {"srd51:stealth"};
        const auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "skill.srd51:insight") == 3);
        CHECK(value(evaluation, "skill.srd51:stealth") == 4);
    }
    SECTION("Heavy armor reduces speed under its Strength threshold") {
        document.choices["abilities"]["strength"] = 8;
        document.choices["abilities"]["intelligence"] = 15;
        const auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "armorClass") == 19);
        CHECK(value(evaluation, "speed") == 20);
    }
    SECTION("One separately owned light weapon cannot occupy both hands") {
        document.choices["startingWeapons"]["weapon1"] = "srd51:shortsword";
        document.choices["weaponId"] = "srd51:shortsword";
        document.choices["offhandId"] = "srd51:shortsword";
        document.choices["shieldId"] = "none";
        CHECK(errorAt(run(document), "/offhandId"));
    }
    SECTION("A greatsword cannot be wielded with a shield") {
        document.choices["startingWeapons"]["weapon1"] = "srd51:greatsword";
        document.choices["weaponId"] = "srd51:greatsword";
        CHECK(errorAt(run(document), "/shieldId"));
    }
    SECTION("Starting purchases require enough background money") {
        document.choices["purchases"] = {"srd51:plate"};
        CHECK(errorAt(run(document), "/purchases"));
        document.choices["purchases"] = {"srd51:dagger"};
        const auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "money.remainingCp") == 1300);
    }
}

TEST_CASE("2014 malformed and unsupported choices remain intact and cannot complete", "[srd51][validation]") {
    const std::vector<std::pair<std::string, Json>> invalid{
        {"/classId", "srd55:fighter"}, {"/raceId", "srd51:elf"}, {"/level", 4},
        {"/abilities/strength", "15"}, {"/abilities/strength", 19},
        {"/hp/level2", "6"}, {"/hp/level2", 0}, {"/classSkills/0", 123},
        {"/languages/1", "srd51:dwarvish"}, {"/twoHands", "false"},
        {"/fightingStyle", "srd55:defense"}, {"/prepared", Json::object()},
        {"/feats", Json::array({"srd55:tough"})}, {"/multiclass", true},
        {"/abilities/luck", 18}, {"/startingEquipment/magic", "free-item"},
        {"/startingWeapons/weapon2", 123}, {"/weaponAbility", 123},
        {"/subclassId", 123}};
    for (const auto& [path, input] : invalid) {
        INFO(path << " = " << input);
        auto document = fighter();
        if (path.starts_with("/hp/")) document.choices["hp"] = Json::object();
        document.choices[Json::json_pointer(path)] = input;
        const auto before = toJson(document);
        const auto evaluation = run(document);
        CHECK_FALSE(evaluation.complete());
        CHECK(errors(evaluation.messages));
        CHECK(toJson(document) == before);
        CHECK(toJson(documentFromJson(before)) == before);
    }
    auto draft = newCharacter("srd51");
    CHECK_FALSE(run(draft).complete());
    CHECK(errorAt(run(draft), "/classId"));
}

TEST_CASE("2014 HP gain and rest healing use distinct minimums with negative Constitution", "[srd51][actions]") {
    // Basic Rules 2018 pp.12,70: later level gain minimum1; rest healing minimum0.
    const auto rules = srd51fixtures::rules();
    auto document = fighter(2);
    document.choices["abilityMethod"] = "rolled";
    document.choices["abilities"]["constitution"] = 3;
    document.choices["hp"] = {{"level2", 1}};
    const auto evaluation = evaluate(document, rules); complete(evaluation);
    CHECK(value(evaluation, "ability.constitution") == 4);
    CHECK(value(evaluation, "ability.constitution.mod") == -3);
    CHECK(value(evaluation, "hp.maximum") == 8); // First10-3 + max(1,1-3).
    document.resources = {{"hp", 4}, {"hitDice", 2}};
    const auto rested = executeCommand(document, rules, {"srd51.short-rest", {{"eligible", true}, {"die1", 1}}});
    REQUIRE(rested.valid());
    CHECK(rested.document.resources.at("hp") == 4);
    CHECK(rested.document.resources.at("hitDice") == 1);
}

TEST_CASE("2014 ammunition purchases grant the printed bundle and price", "[srd51][equipment]") {
    // SRD 5.1 p.69: arrows20 and bolts20 each1gp; blowgun needles50
    // cost1gp; sling bullets20 cost4cp. Costs are not per projectile.
    for (const auto& [id, count, price] : std::vector<std::tuple<std::string, int, int>>{
             {"arrow", 20, 100}, {"bolt", 20, 100}, {"needle", 50, 100}, {"bullet", 20, 4}}) {
        INFO(id);
        auto document = fighter();
        document.choices["purchases"] = {"srd51:" + id};
        const auto evaluation = run(document); complete(evaluation);
        CHECK(value(evaluation, "money.remainingCp") == 1500 - price);
        const auto inventory = value(evaluation, "inventory");
        const auto name = srd51fixtures::rules().find("srd51:" + id)->at("name");
        const auto found = std::find_if(inventory.begin(), inventory.end(), [&](const auto& row) { return row.at("Item") == name; });
        REQUIRE(found != inventory.end());
        CHECK(found->at("Quantity") == count);
    }
}

TEST_CASE("An alternate 2014 Fighter alone retains the stock Champion profile", "[srd51][content][profiles]") {
    PackFixture fixture;
    auto entry = fixture.definition("srd51:fighter");
    entry["id"] = "srd51-test:fighter";
    entry["name"] = "Independent Fighter";
    fixture.entries = Json::array({entry}); fixture.write();
    REQUIRE_FALSE(errors(installPack(fixture.root / "input", fixture.root / "installed", {fixture.core})));
    const auto installed = loadPack(fixture.root / "installed" / "srd51-test-1.0.0"); REQUIRE(installed.valid());
    auto document = fixture.document(2);
    document.choices["classId"] = "srd51-test:fighter";
    document.choices["xp"] = 900;
    const auto rules = resolveRuleset(document, {fixture.core, installed.pack}); REQUIRE(rules.valid());
    complete(evaluate(document, rules));
    const auto third = executeCommand(document, rules, {"srd51.advance-fixed", {{"subclassId", "srd51:champion"}}});
    REQUIRE(third.valid());
    const auto evaluation = evaluate(third.document, rules); complete(evaluation);
    CHECK(value(evaluation, "critical.minimum") == 19);
    CHECK(value(evaluation, "hp.maximum") == 28);
    CHECK(value(evaluation, "secondWind.uses") == 1);
    CHECK(third.document.choices.at("classId") == "srd51-test:fighter");
}

TEST_CASE("2014 compatible replacements resolve independently of pack order", "[srd51][content][replacement]") {
    PackFixture fixture;
    auto replacement = fixture.definition("srd51:creation");
    replacement["id"] = "srd51-test:creation";
    replacement["replaces"] = "srd51:creation";
    replacement["name"] = "Replacement creation table";
    fixture.entries = Json::array({replacement}); fixture.write();
    REQUIRE_FALSE(errors(installPack(fixture.root / "input", fixture.root / "installed", {fixture.core})));
    const auto installed = loadPack(fixture.root / "installed" / "srd51-test-1.0.0"); REQUIRE(installed.valid());
    const auto document = fixture.document();
    const auto forward = resolveRuleset(document, {fixture.core, installed.pack}); REQUIRE(forward.valid());
    const auto reverse = resolveRuleset(document, {installed.pack, fixture.core}); REQUIRE(reverse.valid());
    CHECK(forward.content == reverse.content);
    REQUIRE(forward.find("srd51:creation"));
    CHECK(forward.find("srd51:creation")->at("name") == "Replacement creation table");
    const auto evaluation = evaluate(document, forward); complete(evaluation);
    CHECK(value(evaluation, "hp.maximum") == 12);
}

TEST_CASE("2014 wrong-kind and malformed replacements cannot damage the installed core", "[srd51][content][replacement]") {
    PackFixture fixture;
    REQUIRE_FALSE(errors(installPack(fixture.core.directory, fixture.root / "installed")));
    const auto installedCore = loadPack(fixture.root / "installed" / "srd51-core-1.0.0"); REQUIRE(installedCore.valid());
    for (const bool wrongKind : {true, false}) {
        auto replacement = fixture.definition(wrongKind ? "srd51:common" : "srd51:creation");
        replacement["id"] = "srd51-test:creation";
        replacement["replaces"] = "srd51:creation";
        if (!wrongKind) replacement.erase("pointCosts");
        fixture.entries = Json::array({replacement}); fixture.write();
        CHECK(errors(installPack(fixture.root / "input", fixture.root / "installed", {installedCore.pack})));
        CHECK_FALSE(std::filesystem::exists(fixture.root / "installed" / "srd51-test-1.0.0"));
        auto direct = fixture.core; direct.manifest = fixture.manifest; direct.entries = {replacement};
        CHECK_FALSE(resolveRuleset(fixture.document(), {installedCore.pack, direct}).valid());
    }
    const auto preserved = loadPack(fixture.root / "installed" / "srd51-core-1.0.0"); REQUIRE(preserved.valid());
    CHECK(preserved.pack.entries == installedCore.pack.entries);
    complete(evaluate(fighter(), resolveRuleset(fighter(), {preserved.pack})));
}

TEST_CASE("2014 source and exact-version failures never substitute 5.5E content", "[srd51][versions]") {
    const auto originalPack = srd51fixtures::pack();
    const auto revised = loadPack(std::filesystem::path(DND_DATA_DIR) / "srd55-core-v2");
    REQUIRE(revised.valid());
    auto document = fighter();
    CHECK(document.edition == "srd51");
    CHECK(document.moduleVersion == "1.0.0");
    REQUIRE(document.packs.size() == 1);
    CHECK(document.packs.front().id == "srd51-core");
    CHECK(document.packs.front().version == "1.0.0");
    REQUIRE(findEdition("srd51", "1.0.0"));
    CHECK(findEdition("srd51", "1.0.0")->name.starts_with("5E (2014)"));
    CHECK_FALSE(findEdition("srd51", "2.0.0"));
    const auto before = toJson(document);
    for (const auto& available : std::vector<std::vector<ContentPack>>{{}, {revised.pack}}) {
        const auto resolved = resolveRuleset(document, available);
        CHECK_FALSE(resolved.valid());
        CHECK_FALSE(evaluate(document, resolved).complete());
        CHECK(toJson(document) == before);
    }
    document.packs = {{"srd51-core", "2.0.0"}};
    CHECK_FALSE(resolveRuleset(document, {originalPack, revised.pack}).valid());
    document.packs = {{"srd55-core", "2.0.0"}};
    CHECK_FALSE(resolveRuleset(document, {originalPack, revised.pack}).valid());
    document = fighter();
    document.moduleVersion = "9.0.0";
    CHECK_FALSE(resolveRuleset(document, {originalPack}).valid());
}

TEST_CASE("2014 accepted HP remains stable when future generation method changes", "[srd51][persistence]") {
    auto document = fighter(3);
    document.choices["hpMethod"] = "rolled";
    document.choices["hp"] = {{"level2", 1}, {"level3", 10}};
    document.rolls = {{"hp", {{"2", 1}, {"3", 10}}}};
    auto evaluation = run(document); complete(evaluation);
    CHECK(value(evaluation, "hp.maximum") == 27); // 10+2 + 1+2 + 10+2.
    document.choices["hpMethod"] = "fixed";
    evaluation = run(document); complete(evaluation);
    CHECK(value(evaluation, "hp.maximum") == 27);
    CHECK(evaluation.rollRequests.size() == 2);
    for (const auto& roll : evaluation.rollRequests) {
        CHECK(roll.sides == 10); CHECK(roll.count == 1); CHECK(roll.dropLowest == 0);
    }
    PackFixture fixture;
    const auto path = fixture.root / "accepted-hp.dnd.json";
    saveCharacter(path, document);
    const auto loaded = loadCharacter(path);
    CHECK_FALSE(loaded.inspectOnly);
    CHECK(toJson(loaded.document) == toJson(document));
    CHECK(toJson(run(loaded.document)) == toJson(evaluation));
}

TEST_CASE("The saved 2014 acceptance fixture opens with its exact edition and creation choices", "[srd51][persistence]") {
    const auto path = std::filesystem::path(DND_DATA_DIR).parent_path().parent_path() / "tests/fixtures/srd51-human-fighter1.json";
    const auto loaded = loadCharacter(path);
    REQUIRE_FALSE(loaded.inspectOnly);
    CHECK(loaded.document.edition == "srd51");
    CHECK(loaded.document.moduleVersion == "1.0.0");
    CHECK(loaded.document.choices == fighter().choices);
    const auto evaluation = run(loaded.document); complete(evaluation);
    CHECK(value(evaluation, "hp.maximum") == 12);
    CHECK(value(evaluation, "armorClass") == 19);
}

TEST_CASE("2014 level advancement preserves wounds and spent resources and records accepted inputs", "[srd51][actions]") {
    const auto rules = srd51fixtures::rules();
    auto document = fighter();
    document.choices["xp"] = 300;
    document.resources = {{"hp", 4}, {"hitDice", 0}, {"secondWind", 0}, {"actionSurge", 0}};
    const auto before = toJson(document);
    const auto advance = executeCommand(document, rules, {"srd51.advance-fixed"});
    REQUIRE(advance.valid());
    CHECK(toJson(document) == before); // A transition is also its reviewable preview.
    CHECK(advance.document.choices.at("level") == 2);
    CHECK(advance.document.choices.at("hp").at("level2") == 6);
    CHECK(advance.document.resources.at("hp") == 12); // Existing eight wounds preserved.
    CHECK(advance.document.resources.at("hitDice") == 1);
    CHECK(advance.document.resources.at("secondWind") == 0);
    CHECK(advance.document.resources.at("actionSurge") == 1);
    REQUIRE_FALSE(advance.document.advancement.empty());
    CHECK(advance.document.advancement.front().at("hpBase") == 6);
    const auto evaluation = evaluate(advance.document, rules); complete(evaluation);
    CHECK_FALSE(field(evaluation, "/level").editable);
    CHECK_FALSE(field(evaluation, "/hp/level2").editable);
    document = advance.document;
    document.choices["xp"] = 900;
    const auto third = executeCommand(document, rules, {"srd51.advance-rolled", {{"hpRoll", 4}, {"subclassId", "srd51:champion"}}});
    REQUIRE(third.valid());
    CHECK(third.document.choices.at("hp").at("level3") == 4);
    CHECK(value(evaluate(third.document, rules), "hp.maximum") == 26);
    CHECK(third.document.resources.at("hp") == 18);
    CHECK(third.document.resources.at("secondWind") == 0);
    CHECK(value(evaluate(third.document, rules), "critical.minimum") == 19);
    auto tampered = third.document;
    tampered.choices["hp"]["level2"] = 10;
    CHECK(errorAt(evaluate(tampered, rules), "/advancement/0"));
}

TEST_CASE("2014 invalid actions return the original complete document without partial costs", "[srd51][actions]") {
    const auto rules = srd51fixtures::rules();
    auto document = fighter(2);
    document.resources = {{"hp", 5}, {"hitDice", 1}, {"secondWind", 1}, {"actionSurge", 1}};
    document.choices["xp"] = 900;
    const auto before = toJson(document);
    const std::vector<CharacterCommand> invalid{
        {"srd51.second-wind", {{"roll", 11}}},
        {"srd51.advance-rolled", {{"hpRoll", 0}, {"subclassId", "srd51:champion"}}},
        {"srd51.advance-fixed", {{"subclassId", "srd55:champion"}}},
        {"srd51.short-rest", {{"eligible", false}, {"die1", 5}}},
        {"srd51.short-rest", {{"eligible", true}, {"die1", 11}}},
        {"srd51.short-rest", {{"eligible", true}, {"die1", 5}, {"die2", 5}}},
        {"srd51.long-rest", {{"eligible", false}}},
        {"srd55.short-rest", Json::object()}};
    for (const auto& command : invalid) {
        INFO(command.id << " " << command.inputs);
        const auto rejected = executeCommand(document, rules, command);
        CHECK_FALSE(rejected.valid());
        CHECK(toJson(rejected.document) == before);
        CHECK(toJson(document) == before);
    }
    document = fighter(3);
    CHECK_FALSE(executeCommand(document, rules, {"srd51.advance-fixed"}).valid());
}

TEST_CASE("2014 Second Wind and rests preserve original recovery limits across save and reopen", "[srd51][actions][persistence]") {
    // SRD 5.1 pp.24-25,87: SW d10+level; a level3 long rest recovers
    // floor(3/2)=1 Hit Die, not all dice, and short rest recovers class uses.
    PackFixture fixture;
    const auto rules = srd51fixtures::rules();
    auto document = fighter(3);
    document.resources = {{"hp", 3}, {"hitDice", 2}, {"secondWind", 1}, {"actionSurge", 1}};
    auto result = executeCommand(document, rules, {"srd51.second-wind", {{"roll", 5}}});
    REQUIRE(result.valid());
    CHECK(result.document.resources.at("hp") == 11);
    CHECK(result.document.resources.at("secondWind") == 0);
    const auto spent = toJson(result.document);
    CHECK_FALSE(executeCommand(result.document, rules, {"srd51.second-wind", {{"roll", 5}}}).valid());
    CHECK(toJson(result.document) == spent);
    result = executeCommand(result.document, rules, {"srd51.action-surge"});
    REQUIRE(result.valid()); CHECK(result.document.resources.at("actionSurge") == 0);
    result = executeCommand(result.document, rules, {"srd51.short-rest", {{"eligible", true}, {"die1", 4}, {"die2", 1}}});
    REQUIRE(result.valid());
    CHECK(result.document.resources.at("hp") == 20); // 11 + (4+2) + (1+2).
    CHECK(result.document.resources.at("hitDice") == 0);
    CHECK(result.document.resources.at("secondWind") == 1);
    CHECK(result.document.resources.at("actionSurge") == 1);
    result = executeCommand(result.document, rules, {"srd51.long-rest", {{"eligible", true}}});
    REQUIRE(result.valid());
    CHECK(result.document.resources.at("hp") == 28);
    CHECK(result.document.resources.at("hitDice") == 1);
    const auto path = fixture.root / "rested.dnd.json";
    saveCharacter(path, result.document);
    const auto loaded = loadCharacter(path);
    CHECK_FALSE(loaded.inspectOnly);
    CHECK(toJson(loaded.document) == toJson(result.document));
    CHECK(toJson(evaluate(loaded.document, rules)) == toJson(evaluate(result.document, rules)));
    document.resources["hp"] = 0;
    const auto zero = executeCommand(document, rules, {"srd51.long-rest", {{"eligible", true}}});
    CHECK_FALSE(zero.valid()); CHECK(toJson(zero.document) == toJson(document));
}

TEST_CASE("2014 composite calculations retain arithmetic sources and normal override values", "[srd51][explanations]") {
    auto document = fighter();
    document.overrides = {{{"target", "armorClass"}, {"value", 21}, {"reason", "Campaign protective blessing"}}};
    const auto evaluation = run(document); complete(evaluation);
    const auto& ac = calculation(evaluation, "armorClass");
    CHECK(ac.normal == 19); CHECK(ac.effective == 21);
    CHECK(ac.overrideReason == "Campaign protective blessing");
    CHECK(steps(ac).find("16 + (0) + 2 + 1 = 19") != std::string::npos);
    CHECK(source(ac, "System Reference Document 5.1", "24"));
    CHECK(source(ac, "System Reference Document 5.1", "64"));
    const auto& athletics = calculation(evaluation, "skill.srd51:athletics");
    CHECK(steps(athletics).find("3 + skill proficiency 2 = 5") != std::string::npos);
    for (const auto* id : {"armorClass", "speed", "initiative", "hp.maximum", "attack.weapon", "skill.srd51:athletics"}) {
        CHECK_FALSE(calculation(evaluation, id).steps.empty());
        CHECK_FALSE(calculation(evaluation, id).sources.empty());
    }
}

TEST_CASE("2014 alternate profile records survive real import resolution evaluation and save", "[srd51][content][profiles]") {
    PackFixture fixture;
    for (const auto* id : {"fighter", "human", "acolyte", "champion", "defense"}) {
        auto entry = fixture.definition("srd51:" + std::string(id));
        entry["id"] = "srd51-test:" + std::string(id);
        entry["name"] = "Alternate " + entry.at("name").get<std::string>();
        entry["source"] = {{"publication", "2014 Profile Fixture"}, {"page", "1"}};
        if (entry.at("kind") == "subclass") entry["classId"] = "srd51-test:fighter";
        if (entry.at("kind") == "class") entry["styles"] = {"srd51-test:defense"};
        fixture.entries.push_back(entry);
    }
    fixture.write();
    const auto loaded = loadPack(fixture.root / "input"); REQUIRE(loaded.valid());
    CHECK_FALSE(errors(installPack(fixture.root / "input", fixture.root / "installed", {fixture.core})));
    const auto installed = loadPack(fixture.root / "installed" / "srd51-test-1.0.0"); REQUIRE(installed.valid());
    CHECK(installed.pack.manifest.at("publisher") == "2014 Test Publisher");
    auto document = fixture.document(3);
    for (const auto* key : {"classId", "raceId", "backgroundId", "subclassId", "fightingStyle"}) {
        auto id = document.choices.at(key).get<std::string>();
        document.choices[key] = "srd51-test:" + id.substr(id.find(':') + 1);
    }
    const auto rules = resolveRuleset(document, {fixture.core, installed.pack}); REQUIRE(rules.valid());
    const auto reversed = resolveRuleset(document, {installed.pack, fixture.core}); REQUIRE(reversed.valid());
    CHECK(reversed.content == rules.content);
    const auto evaluation = evaluate(document, rules); complete(evaluation);
    CHECK(value(evaluation, "hp.maximum") == 28);
    CHECK(value(evaluation, "armorClass") == 19);
    CHECK(value(evaluation, "secondWind.uses") == 1);
    CHECK(value(evaluation, "actionSurge.uses") == 1);
    CHECK(value(evaluation, "critical.minimum") == 19);
    CHECK(value(evaluation, "identity") == "Alternate Fighter 3 / Alternate Human / Alternate Acolyte");
    CHECK(value(evaluation, "subclass") == "Alternate Champion");
    CHECK(source(calculation(evaluation, "identity"), "2014 Profile Fixture", "1"));
    CHECK(source(calculation(evaluation, "secondWind.uses"), "2014 Profile Fixture", "1"));
    document.resources["hp"] = 1;
    const auto healed = executeCommand(document, rules, {"srd51.second-wind", {{"roll", 2}}}); REQUIRE(healed.valid());
    CHECK(healed.document.resources.at("hp") == 6);
    const auto path = fixture.root / "clone.dnd.json";
    saveCharacter(path, healed.document);
    const auto reopened = loadCharacter(path);
    CHECK_FALSE(reopened.inspectOnly);
    CHECK(toJson(reopened.document) == toJson(healed.document));
    complete(evaluate(reopened.document, rules));
    const auto withoutSource = resolveRuleset(reopened.document, {fixture.core});
    CHECK_FALSE(withoutSource.valid()); CHECK_FALSE(evaluate(reopened.document, withoutSource).complete());
    CHECK(reopened.document.choices.at("classId") == "srd51-test:fighter");
}

TEST_CASE("2014 malformed profiles fail validation and installation before availability", "[srd51][content][validation]") {
    PackFixture fixture;
    REQUIRE_FALSE(errors(installPack(fixture.core.directory, fixture.root / "installed")));
    const auto installedCore = loadPack(fixture.root / "installed" / "srd51-core-1.0.0"); REQUIRE(installedCore.valid());
    for (const auto* mutation : {"profile", "progression", "reference", "unknown"}) {
        INFO(mutation);
        auto entry = fixture.definition("srd51:fighter"); entry["id"] = "srd51-test:fighter";
        if (std::string(mutation) == "profile") entry["rulesProfile"] = "wizard";
        if (std::string(mutation) == "progression") entry["progression"][1]["proficiency"] = "2";
        if (std::string(mutation) == "reference") entry["creationId"] = "srd51:common";
        if (std::string(mutation) == "unknown") entry["spellcasting"] = true;
        fixture.entries = Json::array({entry}); fixture.write();
        const auto loaded = loadPack(fixture.root / "input");
        if (std::string(mutation) != "reference") CHECK_FALSE(loaded.valid());
        const auto rejected = installPack(fixture.root / "input", fixture.root / "installed", {installedCore.pack});
        CHECK(errors(rejected));
        CHECK_FALSE(std::filesystem::exists(fixture.root / "installed" / "srd51-test-1.0.0"));
        auto direct = fixture.core; direct.manifest = fixture.manifest; direct.entries = {entry};
        const auto rules = resolveRuleset(fixture.document(), {installedCore.pack, direct});
        CHECK_FALSE(rules.valid()); CHECK_FALSE(evaluate(fixture.document(), rules).complete());
    }
    const auto preserved = loadPack(fixture.root / "installed" / "srd51-core-1.0.0"); REQUIRE(preserved.valid());
    CHECK(preserved.pack.entries == installedCore.pack.entries);
    complete(evaluate(fighter(), resolveRuleset(fighter(), {preserved.pack})));
}
