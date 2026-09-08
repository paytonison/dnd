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
    REQUIRE(messages.empty()); REQUIRE(packs.size() == 3);
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
