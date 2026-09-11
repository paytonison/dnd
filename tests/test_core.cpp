#include <catch2/catch_test_macros.hpp>
#include "dnd/content.hpp"
#include "dnd/persistence.hpp"
#include <fstream>
#include <random>

using namespace dnd;
namespace {
struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("dnd-test-" + std::to_string(std::random_device{}()));
    TemporaryDirectory() { std::filesystem::create_directories(path); }
    ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
std::vector<ContentPack> bundled() {
    std::vector<Message> messages;
    auto packs = loadPackDirectory(DND_DATA_DIR, messages);
    for (const auto& m : messages) INFO(m.text);
    REQUIRE(messages.empty()); REQUIRE(packs.size() == 4);
    return packs;
}
ContentPack basePack() {
    auto pack = loadPack(std::filesystem::path(DND_DATA_DIR) / "bx-core");
    REQUIRE(pack.valid()); return pack.pack;
}
bool hasCode(const std::vector<Message>& messages, const std::string& code) {
    return std::any_of(messages.begin(), messages.end(), [&](const auto& m) { return m.code == code; });
}
}
TEST_CASE("Character storage preserves edition-specific shapes, rolls, resources and advancement") {
    auto character = newCharacter("bx");
    character.name = "Rook";
    character.choices = {{"class", "bx:fighter"}, {"future-multiclass", Json::array({{{"class", "mage"}, {"level", 2}}})}};
    character.rolls = {{"abilities", Json::array({11, 12, 13, 9, 16, 7})}, {"hp", Json::array({7, 4})}};
    character.advancement = Json::array({{{"level", 2}, {"acceptedHp", 4}, {"choices", {{"skill", "test"}}}}});
    character.resources = {{"hp", 3}};
    character.overrides = Json::array({{{"target", "hp"}, {"value", 20}, {"reason", "DM award"}}});
    character.campaign = {{"optional", true}};
    TemporaryDirectory temp;
    const auto path = temp.path / "rook.dnd.json";
    saveCharacter(path, character);
    const auto loaded = loadCharacter(path);
    REQUIRE_FALSE(loaded.inspectOnly);
    REQUIRE(toJson(loaded.document) == toJson(character));
    const auto original = toJson(character);
    character.name = "Updated";
    writeAutosave(path, character);
    REQUIRE(toJson(loadCharacter(path).document) == original);
    REQUIRE(loadCharacter(autosavePath(path)).document.name == "Updated");
    // A process interrupted before rename can leave a temp file, but the save remains readable.
    std::ofstream(path.string() + ".interrupted.tmp") << "{partial";
    REQUIRE(toJson(loadCharacter(path).document) == original);
    saveCharacter(path, character);
    REQUIRE(loadCharacter(path).document.name == "Updated");
}
TEST_CASE("Future and malformed documents remain available for inspection without being rewritten") {
    TemporaryDirectory temp;
    const auto path = temp.path / "future.json";
    auto original = toJson(newCharacter("bx")); original["schemaVersion"] = 99; original["futureData"] = "preserve";
    std::ofstream(path) << original.dump();
    const auto loaded = loadCharacter(path);
    REQUIRE(loaded.inspectOnly); REQUIRE(loaded.original == original);
    std::ifstream file(path); REQUIRE(Json::parse(file) == original);
    original["schemaVersion"] = 1; original["rolls"] = "wrong type";
    REQUIRE_THROWS(documentFromJson(original));
    auto invalid = newCharacter("bx"); invalid.schemaVersion = 99;
    REQUIRE_THROWS(saveCharacter(path, invalid));
    std::ifstream unchanged(path); REQUIRE(Json::parse(unchanged)["schemaVersion"] == 99);
}
TEST_CASE("Resolution requires exact module and content versions without substitutions") {
    auto character = newCharacter("bx"); const auto packs = bundled();
    REQUIRE(resolveRuleset(character, packs).valid());
    character.packs.front().version = "99.0.0";
    const auto missing = resolveRuleset(character, packs);
    REQUIRE_FALSE(missing.valid()); REQUIRE(hasCode(missing.messages, "pack.missing"));
    REQUIRE(evaluate(character, missing).calculations.empty());
    character = newCharacter("bx"); character.moduleVersion = "2.0.0";
    REQUIRE_FALSE(resolveRuleset(character, packs).valid());
    character = newCharacter("bx"); character.packs.push_back({"srd55-core", "1.0.0"});
    REQUIRE(hasCode(resolveRuleset(character, packs).messages, "pack.edition"));
}
TEST_CASE("Dependencies, conflicts, duplicates and replacements never depend on install order") {
    auto base = basePack(); auto addon = base;
    addon.manifest["id"] = "addon"; addon.entries.clear();
    addon.manifest["dependencies"] = Json::array({{{"id", "bx-core"}, {"version", "1.0.0"}}});
    auto character = newCharacter("bx"); character.packs.push_back({"addon", "1.0.0"});
    REQUIRE(resolveRuleset(character, {base, addon}).valid());
    character.packs.erase(character.packs.begin());
    REQUIRE(hasCode(resolveRuleset(character, {base, addon}).messages, "pack.dependency"));
    character = newCharacter("bx"); character.packs.push_back({"addon", "1.0.0"});
    addon.manifest["conflicts"] = Json::array({"bx-core"});
    REQUIRE(hasCode(resolveRuleset(character, {base, addon}).messages, "pack.conflict"));
    addon.manifest["conflicts"] = Json::array(); addon.entries.push_back(base.entries.front());
    REQUIRE(hasCode(resolveRuleset(character, {base, addon}).messages, "content.duplicate"));
    addon.entries.front()["id"] = "addon:replacement";
    addon.entries.front()["replaces"] = base.entries.front()["id"];
    const auto first = resolveRuleset(character, {base, addon}); const auto second = resolveRuleset(character, {addon, base});
    REQUIRE(first.valid()); REQUIRE(first.content == second.content);
    auto other = addon; other.manifest["id"] = "other"; other.entries.front()["id"] = "other:replacement";
    character.packs.push_back({"other", "1.0.0"});
    REQUIRE(hasCode(resolveRuleset(character, {other, base, addon}).messages, "content.conflict"));
    REQUIRE(hasCode(resolveRuleset(newCharacter("bx"), {base, base}).messages, "pack.ambiguous"));
}
TEST_CASE("Content imports validate the entire directory before availability") {
    TemporaryDirectory temp;
    const auto source = temp.path / "source"; std::filesystem::create_directory(source);
    const auto pack = basePack();
    auto manifest = pack.manifest; manifest["dataFiles"] = Json::array({"content.json"});
    std::ofstream(source / "manifest.json") << manifest.dump();
    auto entries = pack.entries;
    std::ofstream(source / "content.json") << Json(entries).dump();
    REQUIRE(loadPack(source).valid());
    REQUIRE(installPack(source, temp.path / "installed").empty());
    REQUIRE(std::filesystem::exists(temp.path / "installed/bx-core-1.0.0/manifest.json"));
    REQUIRE_FALSE(installPack(source, temp.path / "installed").empty());
    entries.push_back(entries.front()); std::ofstream(source / "content.json") << Json(entries).dump();
    REQUIRE_FALSE(loadPack(source).valid());
    REQUIRE_FALSE(installPack(source, temp.path / "rejected").empty());
    REQUIRE_FALSE(std::filesystem::exists(temp.path / "rejected/bx-core-1.0.0"));
    manifest["dataFiles"] = Json::array({"../escape.json"});
    std::ofstream(source / "manifest.json") << manifest.dump();
    REQUIRE(hasCode(loadPack(source).messages, "pack.path"));
}
TEST_CASE("Incomplete drafts evaluate deterministically and are safe to print") {
    const auto packs = bundled();
    for (const auto& module : editions()) {
        auto character = newCharacter(module.id, module.version); character.name = "<script>alert('no')</script>";
        const auto rules = resolveRuleset(character, packs);
        const auto first = evaluate(character, rules); const auto second = evaluate(character, rules);
        REQUIRE(toJson(first) == toJson(second)); REQUIRE_FALSE(first.complete());
        REQUIRE_FALSE(first.stages.empty());
        const auto html = renderSheetHtml(character, first, rules);
        REQUIRE(html.find("<script>") == std::string::npos); REQUIRE(html.find("&lt;script&gt;") != std::string::npos);
        character.overrides = Json::array({{{"target", "nonexistent"}, {"value", 42}, {"reason", "A reason"}}});
        REQUIRE(hasCode(evaluate(character, rules).messages, "override.unsupported"));
    }
}
TEST_CASE("Imported supplements require resolvable dependencies and retain publisher metadata") {
    TemporaryDirectory temp;
    const auto example = std::filesystem::path(DND_DATA_DIR).parent_path().parent_path() / "templates/content-pack";
    const auto loaded = loadPack(example);
    for (const auto& message : loaded.messages) INFO(message.text);
    REQUIRE(loaded.valid());
    const auto missing = installPack(example, temp.path / "without-core");
    REQUIRE(hasCode(missing, "pack.dependency"));
    REQUIRE_FALSE(std::filesystem::exists(temp.path / "without-core/example-homebrew-1.0.0"));
    REQUIRE(installPack(example, temp.path / "with-core", {basePack()}).empty());
    std::vector<Message> messages;
    auto packs = loadPackDirectory(temp.path / "with-core", messages);
    REQUIRE(messages.empty()); REQUIRE(packs.size() == 1);
    REQUIRE(packs.front().manifest["origin"] == "homebrew");
    packs.push_back(basePack());
    auto character = newCharacter("bx"); character.packs.push_back({"example-homebrew", "1.0.0"});
    const auto rules = resolveRuleset(character, packs);
    REQUIRE(rules.valid()); REQUIRE(rules.find("example-homebrew:iron-cudgel"));
}
TEST_CASE("Pack import validates dependency compatibility for every advertised module version") {
    TemporaryDirectory temp;
    auto legacy = loadPack(std::filesystem::path(DND_DATA_DIR) / "srd55-core");
    auto current = loadPack(std::filesystem::path(DND_DATA_DIR) / "srd55-core-v2");
    REQUIRE(legacy.valid()); REQUIRE(current.valid());
    auto manifest = legacy.pack.manifest;
    manifest["id"] = "cross-version-addon";
    manifest["moduleVersions"] = {"1.0.0", "2.0.0"};
    manifest["dependencies"] = Json::array({{{"id", "srd55-core"}, {"version", "1.0.0"}}});
    auto armor = *std::find_if(legacy.pack.entries.begin(), legacy.pack.entries.end(), [](const auto& e) { return e.at("kind") == "armor"; });
    armor["id"] = "cross-version-addon:armor";
    std::ofstream(temp.path / "content.json") << Json::array({armor}).dump();
    for (int order = 0; order < 2; ++order) {
        std::ofstream(temp.path / "manifest.json") << manifest.dump();
        REQUIRE(loadPack(temp.path).valid()); // Each module understands the entry's shape.
        const auto imported = installPack(temp.path, temp.path / "installed", {legacy.pack, current.pack});
        REQUIRE(hasCode(imported, "pack.module_version"));
        REQUIRE_FALSE(std::filesystem::exists(temp.path / "installed/cross-version-addon-1.0.0"));
        std::reverse(manifest["moduleVersions"].begin(), manifest["moduleVersions"].end());
    }
}

TEST_CASE("Replacement preflight preserves mechanical roles and the usable installation", "[content][replacement]") {
    TemporaryDirectory temp;
    const auto installed = temp.path / "installed";
    REQUIRE(installPack(std::filesystem::path(DND_DATA_DIR) / "bx-core", installed).empty());
    auto base = basePack();
    const auto originalTable = *std::find_if(base.entries.begin(), base.entries.end(), [](const auto& item) {
        return item.at("id") == "bx:combat-tables";
    });
    const auto language = *std::find_if(base.entries.begin(), base.entries.end(), [](const auto& item) {
        return item.at("kind") == "language";
    });
    auto addon = base;
    addon.manifest["id"] = "table-addon";
    addon.manifest["dependencies"] = Json::array({{{"id", "bx-core"}, {"version", "1.0.0"}}});
    addon.manifest["dataFiles"] = {"content.json"};
    const auto source = temp.path / "addon";
    std::filesystem::create_directories(source);
    std::ofstream(source / "manifest.json") << addon.manifest.dump();
    auto document = newCharacter("bx");
    document.name = "Replacement acceptance";
    document.choices = {{"class", "bx:fighter"}, {"level", 1}, {"xp", 0},
        {"abilities", {{"str", 10}, {"int", 10}, {"wis", 10}, {"dex", 10}, {"con", 10}, {"cha", 10}}},
        {"alignment", "lawful"}, {"hp", {4}}, {"moneyRoll", 18}, {"weapon", "bx:sword"}};
    const auto baseline = evaluate(document, resolveRuleset(document, {base}));
    REQUIRE(baseline.complete());
    document.packs.push_back({"table-addon", "1.0.0"});

    SECTION("Wrong-kind and malformed tables fail load, import, and direct resolution") {
        for (const bool wrongKind : {true, false}) {
            CAPTURE(wrongKind);
            auto replacement = wrongKind ? language : originalTable;
            replacement["id"] = "table-addon:combat";
            replacement["replaces"] = "bx:combat-tables";
            if (!wrongKind) replacement.erase("attackRows");
            addon.entries = {replacement};
            std::ofstream(source / "content.json") << Json(addon.entries).dump();
            const auto loaded = loadPack(source);
            CHECK_FALSE(loaded.valid());
            CHECK(hasCode(loaded.messages, "pack.bx.mechanics"));
            const auto rejected = installPack(source, installed);
            CHECK(hasCode(rejected, "pack.bx.mechanics"));
            CHECK_FALSE(std::filesystem::exists(installed / "table-addon-1.0.0"));
            CHECK_FALSE(std::filesystem::exists(installed / ".table-addon-1.0.0.importing"));
            for (const auto& catalog : {std::vector<ContentPack>{base, addon}, std::vector<ContentPack>{addon, base}}) {
                const auto resolved = resolveRuleset(document, catalog);
                CHECK_FALSE(resolved.valid());
                const auto evaluated = evaluate(document, resolved);
                CHECK_FALSE(evaluated.complete());
                CHECK(evaluated.calculations.empty());
                CHECK_FALSE(hasCode(evaluated.messages, "evaluation.invalid_input"));
            }
            std::vector<Message> messages;
            const auto usable = loadPackDirectory(installed, messages);
            REQUIRE(messages.empty());
            REQUIRE(usable.size() == 1);
            auto originalDocument = document;
            originalDocument.packs.pop_back();
            CHECK(toJson(evaluate(originalDocument, resolveRuleset(originalDocument, usable))) == toJson(baseline));
        }
    }
    SECTION("Compatible table replacement is independent of pack and pin order") {
        auto replacement = originalTable;
        replacement["id"] = "table-addon:combat";
        replacement["replaces"] = "bx:combat-tables";
        replacement["name"] = "Campaign attack matrix";
        replacement["source"] = {{"publication", "Acceptance campaign table"}, {"page", "Combat"}};
        // Explicit homebrew table change: first-level target against AC 9 is 11.
        replacement["attackRows"][0][0] = 11;
        addon.entries = {replacement};
        std::ofstream(source / "content.json") << Json(addon.entries).dump();
        REQUIRE(loadPack(source).valid());
        REQUIRE(installPack(source, installed).empty());
        std::vector<Message> messages;
        const auto catalog = loadPackDirectory(installed, messages);
        REQUIRE(messages.empty());
        const auto expectedRules = resolveRuleset(document, catalog);
        REQUIRE(expectedRules.valid());
        const auto expected = evaluate(document, expectedRules);
        REQUIRE(expected.complete());
        REQUIRE(expected.find("attack.base"));
        CHECK(expected.find("attack.base")->effective.at("9") == 11);
        CHECK(std::any_of(expected.find("attack.base")->sources.begin(), expected.find("attack.base")->sources.end(), [](const auto& source) {
            return source.publication == "Acceptance campaign table" && source.page == "Combat";
        }));
        CHECK(expectedRules.find("bx:combat-tables")->at("replacementId") == "table-addon:combat");
        CHECK(expectedRules.find("bx:combat-tables")->at("source") == replacement.at("source"));
        for (int pinOrder = 0; pinOrder < 2; ++pinOrder) {
            for (const auto& order : {std::vector<ContentPack>{base, addon}, std::vector<ContentPack>{addon, base}}) {
                const auto rules = resolveRuleset(document, order);
                REQUIRE(rules.valid());
                CHECK(rules.content == expectedRules.content);
                CHECK(toJson(evaluate(document, rules)) == toJson(expected));
            }
            std::reverse(document.packs.begin(), document.packs.end());
        }
    }
    SECTION("Generic replacement kind mismatches fail dependency-aware installation") {
        auto replacement = language;
        replacement["id"] = "table-addon:not-a-weapon";
        replacement["replaces"] = "bx:sword";
        addon.entries = {replacement};
        std::ofstream(source / "content.json") << Json(addon.entries).dump();
        REQUIRE(loadPack(source).valid()); // The target's kind requires its dependency.
        const auto rejected = installPack(source, installed);
        CHECK(hasCode(rejected, "content.replacement_kind"));
        CHECK_FALSE(std::filesystem::exists(installed / "table-addon-1.0.0"));
        const auto rules = resolveRuleset(document, {addon, base});
        CHECK_FALSE(rules.valid());
        CHECK(hasCode(rules.messages, "content.replacement_kind"));
    }
}

TEST_CASE("Direct resolution validates generic entry fields before edition mechanics", "[content][in-memory]") {
    const auto base=basePack();const auto document=newCharacter("bx");
    const auto entryId=base.entries.back().at("id").get<std::string>();
    for(const auto& change:std::vector<std::pair<std::string,Json>>{
        {"/replaces",123},{"/source/publication",123},{"/source/page",false},{"/source/url",123},
        {"/source",Json::array()},{"/kind",123},{"/name",""},{"/id",123},{"/id","unnamespaced"}}){
        CAPTURE(change.first,change.second);
        auto bad=base;bad.entries.back()[Json::json_pointer(change.first)]=change.second;
        ResolvedRuleset rules;REQUIRE_NOTHROW(rules=resolveRuleset(document,{bad}));CHECK_FALSE(rules.valid());
        const auto path=change.first=="/id"?change.first:"/"+entryId+change.first;
        CHECK(std::any_of(rules.messages.begin(),rules.messages.end(),[&](const auto& issue){return issue.path.starts_with("/packs/bx-core/")&&issue.path.ends_with(path);}));
        const auto result=evaluate(document,rules);CHECK_FALSE(result.complete());CHECK(result.calculations.empty());CHECK_FALSE(hasCode(result.messages,"evaluation.invalid_input"));
    }
    for(const auto& malformed:Json::array({nullptr,Json::array(),123})){auto bad=base;bad.entries.back()=malformed;ResolvedRuleset rules;REQUIRE_NOTHROW(rules=resolveRuleset(document,{bad}));CHECK_FALSE(rules.valid());CHECK(hasCode(rules.messages,"pack.entry"));CHECK(evaluate(document,rules).calculations.empty());}
    auto duplicated=base;duplicated.entries.push_back(duplicated.entries.back());const auto rules=resolveRuleset(document,{duplicated});CHECK_FALSE(rules.valid());CHECK(hasCode(rules.messages,"pack.duplicate"));
    auto unselected=base;unselected.manifest["id"]="unselected";unselected.entries.back()["source"]=123;
    CHECK(resolveRuleset(document,{base,unselected}).valid()); // Unselected payloads do not change the active ruleset.
    CHECK(resolveRuleset(document,{base}).valid());
}

TEST_CASE("Direct resolution validates manifests before typed catalog and dependency access", "[content][in-memory]") {
    const auto base=basePack();const auto document=newCharacter("bx");
    for(const auto& change:std::vector<std::pair<std::string,Json>>{
        {"/id",123},{"/version",123},{"/edition",false},{"/schemaVersion",2},{"/publisher",nullptr},
        {"/sources/0/publication",123},{"/sources/0/url",false},{"/license/text",123},
        {"/dependencies",123},{"/dependencies",Json::array({{{"id","bx-core"},{"version",123}}})},
        {"/conflicts",Json::array({123})},{"/moduleVersions",123},{"/moduleVersions",Json::array({123})},
        {"/dataFiles",Json::array({123})},{"/dataFiles",Json::array({"../outside.json"})}}){
        CAPTURE(change.first,change.second);auto bad=base;bad.manifest[Json::json_pointer(change.first)]=change.second;
        ResolvedRuleset rules;REQUIRE_NOTHROW(rules=resolveRuleset(document,{bad}));CHECK_FALSE(rules.valid());
        CHECK(std::any_of(rules.messages.begin(),rules.messages.end(),[&](const auto& issue){return issue.path.starts_with("/packs/")&&issue.path.find(change.first)!=std::string::npos;}));
        CHECK(evaluate(document,rules).calculations.empty());
    }
    for(const auto& manifest:Json::array({nullptr,Json::array(),123})){auto bad=base;bad.manifest=manifest;ResolvedRuleset rules;REQUIRE_NOTHROW(rules=resolveRuleset(document,{bad}));CHECK_FALSE(rules.valid());CHECK(hasCode(rules.messages,"pack.manifest"));}
}

TEST_CASE("Loader installer and direct resolution share metadata rejection without damaging installed data", "[content][in-memory]") {
    TemporaryDirectory temp;const auto base=basePack();const auto source=temp.path/"source",installed=temp.path/"installed";
    std::filesystem::create_directories(source);REQUIRE(installPack(std::filesystem::path(DND_DATA_DIR)/"bx-core",installed).empty());
    auto manifest=base.manifest;manifest["dataFiles"]={"content.json"};std::ofstream(source/"manifest.json")<<manifest.dump();
    for(const auto& field:{"replaces","source"}){
        CAPTURE(field);auto bad=base;
        if(std::string(field)=="replaces")bad.entries.back()[field]=123;
        else bad.entries.back()[field]={{"publication",123},{"page","B7"}};
        std::ofstream(source/"content.json")<<Json(bad.entries).dump();
        const auto loaded=loadPack(source);CHECK_FALSE(loaded.valid());const auto rejected=installPack(source,installed);CHECK_FALSE(rejected.empty());CHECK_FALSE(std::filesystem::exists(installed/".bx-core-1.0.0.importing"));
        const auto current=loadPack(installed/"bx-core-1.0.0");REQUIRE(current.valid());CHECK(current.pack.manifest==base.manifest);CHECK(current.pack.entries==base.entries);
    }
    // A malformed caller-supplied catalog is rejected before installer pinning.
    auto malformed=base;malformed.manifest["dependencies"]=123;
    const auto example=std::filesystem::path(DND_DATA_DIR).parent_path().parent_path()/"templates/content-pack";
    std::vector<Message> result;REQUIRE_NOTHROW(result=installPack(example,installed,{malformed}));CHECK_FALSE(result.empty());CHECK_FALSE(std::filesystem::exists(installed/"example-homebrew-1.0.0"));
}

TEST_CASE("Throwing mechanical validation becomes a pack diagnostic with the offending record", "[content][in-memory]") {
    auto bad=basePack();const auto spell=std::find_if(bad.entries.begin(),bad.entries.end(),[](const auto& entry){return entry.at("kind")=="spell";});REQUIRE(spell!=bad.entries.end());
    const auto id=spell->at("id").get<std::string>();(*spell)["tradition"]=123;
    ResolvedRuleset rules;REQUIRE_NOTHROW(rules=resolveRuleset(newCharacter("bx"),{bad}));CHECK_FALSE(rules.valid());
    CHECK(std::any_of(rules.messages.begin(),rules.messages.end(),[&](const auto& issue){return issue.code=="pack.mechanics"&&issue.path=="/packs/bx-core/"+id;}));
    CHECK(evaluate(newCharacter("bx"),rules).calculations.empty());
}
