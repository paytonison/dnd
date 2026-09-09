#include "dnd/lifecycle.hpp"
#include "dnd/persistence.hpp"
#include "srd55_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cctype>

using namespace dnd;
namespace {
const ResolvedRuleset& rules() { static const auto value = srd55fixtures::rules(); return value; }
TransitionResult command(const CharacterDocument& d, const std::string& id, Json inputs = Json::object()) {
    return executeCommand(d, rules(), {id, std::move(inputs)});
}
CharacterDocument applied(const CharacterDocument& d, const std::string& id, Json inputs = Json::object()) {
    auto result = command(d, id, std::move(inputs));
    std::string errors; for (const auto& message : result.messages) errors += message.code + ": " + message.text + "\n";
    INFO(errors); REQUIRE(result.valid());
    INFO(srd55fixtures::errors(evaluate(result.document, rules())));
    REQUIRE(evaluate(result.document, rules()).complete());
    return result.document;
}
const Json& item(const CharacterDocument& d, const std::string& id) {
    for (const auto& record : d.resources.at("inventory").at("instances")) if (record.at("id") == id) return record;
    FAIL("Missing physical instance " + id); return d.resources;
}
std::string initial(const CharacterDocument& d) { return d.resources.at("wizardSpellbooks").at("initialInstanceId"); }
CharacterDocument hero(int level = 3) {
    auto d = srd55fixtures::base("wizard", rules(), level);
    if (level == 3) {
        d.choices["spellcasting"]["wizard"]["spellbook"] = {
            {"1", {"srd55:shield", "srd55:magic-missile", "srd55:mage-armor", "srd55:sleep", "srd55:find-familiar", "srd55:detect-magic"}},
            {"2", {"srd55:burning-hands", "srd55:color-spray"}}, {"3", {"srd55:misty-step", "srd55:mirror-image"}}};
        d.choices["spellcasting"]["wizard"]["preparedSpells"] =
            {"srd55:shield", "srd55:magic-missile", "srd55:mage-armor", "srd55:sleep", "srd55:misty-step", "srd55:mirror-image"};
    }
    auto finished = srd55fixtures::finish(d, rules());
    INFO(srd55fixtures::errors(finished.evaluation)); REQUIRE(finished.evaluation.complete());
    d = applied(finished.document, "srd55.inventory.initialize");
    d = applied(d, "srd55.inventory.adjust-currency", {{"deltaCp", 100000}, {"reason", "Adventure treasure for independent copying examples"}});
    d = applied(d, "srd55.history.accept-baseline");
    std::string id; for (const auto& record : d.resources["inventory"]["instances"]) if (record["itemId"] == "srd55:spellbook") id = record["id"];
    REQUIRE_FALSE(id.empty());
    return applied(d, "srd55.spellbooks.initialize", {{"instanceId", id}});
}
std::pair<CharacterDocument, std::string> blank(CharacterDocument d) {
    d = applied(d, "srd55.inventory.acquire", {{"itemId", "srd55:spellbook"}, {"quantity", 1}, {"source", "gift"}, {"paidCp", 0}, {"reason", "Blank replacement book supplied by the campaign"}});
    const std::string id = d.resources["inventory"]["instances"].back()["id"];
    return {applied(d, "srd55.spellbooks.register", {{"instanceId", id}}), id};
}
CharacterDocument lose(const CharacterDocument& d, const std::string& id, const std::string& disposition = "lost") {
    return applied(d, "srd55.inventory.dispose", {{"instanceId", id}, {"quantity", 1}, {"disposition", disposition}, {"proceedsCp", 0}, {"reason", "Book lost during the adventure"}});
}
bool available(const CharacterDocument& d, const std::string& actionId) {
    const auto e = evaluate(d, rules());
    const auto found = std::find_if(e.actions.begin(), e.actions.end(), [&](const auto& action) { return action.id == actionId; });
    return found != e.actions.end() && found->available;
}
std::string replacement(std::string path) {
    for (auto& c : path) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
    return "srd55.history.replace." + path;
}
void rejected(const CharacterDocument& d, const std::string& action, const Json& inputs) {
    const auto before = toJson(d); const auto result = command(d, action, inputs);
    REQUIRE_FALSE(result.valid()); REQUIRE(toJson(result.document) == before);
}
}

TEST_CASE("Physical Wizard books copy exact spells and published own-book costs atomically", "[srd55-v2][spellbooks]") {
    auto [d, backup] = blank(hero()); const auto original = initial(d); const auto before = toJson(d);
    const auto balance = d.resources["currencyCp"].get<int>();
    const Json input = {{"sourceId", original}, {"destinationId", backup}, {"spells", {"srd55:shield", "srd55:misty-step"}}, {"minutes", 180}, {"paidCp", 3000}};
    const auto copied = applied(d, "srd55.spellbooks.copy", input);
    CHECK(copied.resources["currencyCp"] == balance - 3000);
    CHECK(item(copied, backup)["spellbook"]["spells"] == Json::array({"srd55:shield", "srd55:misty-step"}));
    CHECK(copied.choices == d.choices); CHECK(copied.rolls == d.rolls); CHECK(toJson(d) == before);
    CHECK(toJson(command(d, "srd55.spellbooks.copy", input).document) == toJson(copied));
    rejected(copied, "srd55.spellbooks.copy", input);
    auto bad = input; bad["minutes"] = 179; rejected(d, "srd55.spellbooks.copy", bad);
    bad = input; bad["paidCp"] = 2999; rejected(d, "srd55.spellbooks.copy", bad);
    bad = input; bad["spells"] = {"srd55:shield", "srd55:shield"}; rejected(d, "srd55.spellbooks.copy", bad);
    bad = input; bad["destinationId"] = original; rejected(d, "srd55.spellbooks.copy", bad);
    auto poor = d; poor.resources["currencyCp"] = 2999; rejected(poor, "srd55.spellbooks.copy", input);
    const auto reopened = documentFromJson(toJson(copied));
    CHECK(toJson(reopened) == toJson(copied));
    CHECK(toJson(evaluate(reopened, rules())) == toJson(evaluate(copied, rules())));
}

TEST_CASE("Spellbook loss preserves prepared casting and blocks only book-dependent access", "[srd55-v2][spellbooks]") {
    auto d = hero(); const auto id = initial(d); const auto choices = d.choices;
    REQUIRE(available(d, "srd55.companions.cast.wizard-book-ritual"));
    d = lose(d, id);
    REQUIRE(evaluate(d, rules()).complete()); CHECK(d.choices == choices);
    CHECK(evaluate(d, rules()).moduleData["spellbooks"]["wizard"].empty());
    CHECK_FALSE(available(d, "srd55.companions.cast.wizard-book-ritual"));
    d = applied(d, "srd55.resources.short-rest", {{"hours", 1}, {"interrupted", false}});
    CHECK_FALSE(available(d, "srd55.resources.arcane-recovery"));
    d = applied(d, "srd55.resources.long-rest", {{"hours", 8}, {"sleepHours", 6}, {"hoursSincePrevious", 16}, {"interruptions", 0}, {"unfinished", false}});
    CHECK(d.choices == choices);
    CHECK_FALSE(available(d, replacement("/spellcasting/wizard/preparedSpells")));
    const auto reopened = documentFromJson(toJson(d));
    CHECK(evaluate(reopened, rules()).complete()); CHECK(reopened.choices == choices);
    CHECK_FALSE(available(reopened, "srd55.spells.copy"));
    rejected(d, "srd55.spellbooks.initialize", {{"instanceId", id}});
}

TEST_CASE("Prepared-spell reconstruction restores only written spells and exact old-book recovery", "[srd55-v2][spellbooks]") {
    auto [d, replacementBook] = blank(hero()); const auto original = initial(d);
    const auto originalContents = item(d, original)["spellbook"]["spells"];
    d = lose(d, original);
    d = applied(d, "srd55.spellbooks.destination", {{"instanceId", replacementBook}});
    const auto balance = d.resources["currencyCp"].get<int>();
    const Json input = {{"sourceId", "prepared"}, {"destinationId", replacementBook}, {"spells", {"srd55:shield", "srd55:misty-step"}}, {"minutes", 180}, {"paidCp", 3000}};
    d = applied(d, "srd55.spellbooks.copy", input);
    CHECK(d.resources["currencyCp"] == balance - 3000);
    CHECK(item(d, replacementBook)["spellbook"]["spells"].size() == 2);
    auto invalid = input; invalid["spells"] = {"srd55:find-familiar"}; rejected(d, "srd55.spellbooks.copy", invalid);
    invalid = input; invalid["sourceId"] = original; invalid["spells"] = {"srd55:magic-missile"}; rejected(d, "srd55.spellbooks.copy", invalid);
    d = applied(d, "srd55.spells.copy", {{"sourceType", "spellbook"}, {"spellId", "srd55:blur"}, {"minutes", 240}, {"paidCp", 10000}, {"sourceNote", "A newly discovered source book"}});
    REQUIRE(item(d, replacementBook)["spellbook"]["spells"].size() == 3);
    d = applied(d, "srd55.inventory.recover", {{"instanceId", original}, {"quantity", 1}, {"paidCp", 0}, {"reason", "The original book was recovered"}});
    CHECK(item(d, original)["spellbook"]["spells"] == originalContents);
    CHECK(item(d, replacementBook)["spellbook"]["spells"].size() == 3);
    CHECK(available(d, "srd55.companions.cast.wizard-book-ritual"));
    CHECK(evaluate(documentFromJson(toJson(d)), rules()).complete());
}

TEST_CASE("External recopying of a lost learned spell preserves acquisition history", "[srd55-v2][spellbooks]") {
    auto [d, replacementBook] = blank(hero()); d = lose(d, initial(d));
    d = applied(d, "srd55.spellbooks.destination", {{"instanceId", replacementBook}});
    const auto choices = d.choices;
    d = applied(d, "srd55.spells.copy", {{"sourceType", "spellbook"}, {"spellId", "srd55:find-familiar"}, {"minutes", 120}, {"paidCp", 5000}, {"sourceNote", "Found the lost spell in another Wizard's book"}});
    CHECK(d.choices == choices);
    CHECK(item(d, replacementBook)["spellbook"]["spells"] == Json::array({"srd55:find-familiar"}));
    CHECK(available(d, "srd55.companions.cast.wizard-book-ritual"));
    CHECK(dnd::srd55v2::validateSrd55History(d, rules()).empty());
}

TEST_CASE("Stored and destroyed spellbooks cannot supply copying or automatic restoration", "[srd55-v2][spellbooks]") {
    auto [d, backup] = blank(hero()); const auto original = initial(d);
    d = applied(d, "srd55.inventory.move", {{"instanceId", original}, {"location", "stored"}});
    const Json input = {{"sourceId", original}, {"destinationId", backup}, {"spells", {"srd55:shield"}}, {"minutes", 60}, {"paidCp", 1000}};
    rejected(d, "srd55.spellbooks.copy", input);
    d = applied(d, "srd55.inventory.move", {{"instanceId", original}, {"location", "carried"}});
    d = lose(d, original, "destroyed");
    rejected(d, "srd55.inventory.recover", {{"instanceId", original}, {"quantity", 1}, {"paidCp", 0}, {"reason", "Cannot ordinarily recover a destroyed book"}});
    auto corrupt = d; corrupt.resources["wizardSpellbooks"]["version"] = 2;
    CHECK_FALSE(evaluate(corrupt, rules()).complete()); CHECK(toJson(corrupt) == toJson(documentFromJson(toJson(corrupt))));
    corrupt = d; for (auto& record : corrupt.resources["inventory"]["instances"]) if (record["id"] == backup) record["spellbook"]["spells"] = {"srd55:shield"};
    CHECK_FALSE(evaluate(corrupt, rules()).complete());
}

TEST_CASE("Wizard advancement writes new research only to the selected physical book", "[srd55-v2][spellbooks]") {
    auto [d, backup] = blank(hero());
    const auto oldOriginal = item(d, initial(d))["spellbook"]["spells"];
    d = applied(d, "srd55.spellbooks.destination", {{"instanceId", backup}});
    auto begin = command(d, "srd55.history.begin-advance", {{"classId", "srd55:wizard"}}); REQUIRE(begin.valid());
    auto pending = begin.document; srd55fixtures::seedAdvancement(pending, rules(), "wizard", 4);
    const auto finished = srd55fixtures::finish(pending, rules());
    INFO(srd55fixtures::errors(finished.evaluation)); REQUIRE(finished.evaluation.complete());
    const auto committed = applied(finished.document, "srd55.history.commit");
    CHECK(item(committed, initial(committed))["spellbook"]["spells"] == oldOriginal);
    CHECK(item(committed, backup)["spellbook"]["spells"].size() == 2);
    CHECK(item(committed, backup)["spellbook"]["receipts"].back()["classLevel"] == 4);
    CHECK(dnd::srd55v2::validateSrd55History(committed, rules()).empty());
    const auto lost = lose(d, backup);
    rejected(lost, "srd55.history.begin-advance", {{"classId", "srd55:wizard"}});
}

TEST_CASE("Already accepted Spell Mastery and Signature Spells survive physical book loss", "[srd55-v2][spellbooks]") {
    auto d = hero(20); const auto before = d.choices;
    const auto oldProfiles = evaluate(d, rules()).moduleData["castingProfiles"];
    d = lose(d, initial(d));
    CHECK(d.choices == before); CHECK(evaluate(d, rules()).complete());
    const auto profiles = evaluate(d, rules()).moduleData["castingProfiles"];
    for (const auto& profile : oldProfiles)
        if (profile.value("reason", "") == "Spell Mastery" || profile.value("reason", "") == "Signature Spells")
            CHECK(std::find(profiles.begin(), profiles.end(), profile) != profiles.end());
    auto [withBlank, newBook] = blank(d);
    const auto mastery = srd55fixtures::at(before, "/features/wizard/spellMastery/1");
    const auto copied = applied(withBlank, "srd55.spellbooks.copy", {{"sourceId", "prepared"}, {"destinationId", newBook}, {"spells", Json::array({mastery})}, {"minutes", 60}, {"paidCp", 1000}});
    CHECK(item(copied, newBook)["spellbook"]["spells"] == Json::array({mastery}));
}

TEST_CASE("Replacing a retained preparation removes its later reconstruction permission", "[srd55-v2][spellbooks]") {
    auto [d, replacementBook] = blank(hero()); d = lose(d, initial(d));
    d = applied(d, "srd55.spellbooks.destination", {{"instanceId", replacementBook}});
    d = applied(d, "srd55.spells.copy", {{"sourceType", "spellbook"}, {"spellId", "srd55:blur"}, {"minutes", 240}, {"paidCp", 10000}, {"sourceNote", "Another Wizard's book"}});
    d = applied(d, "srd55.resources.long-rest", {{"hours", 8}, {"sleepHours", 6}, {"hoursSincePrevious", 16}, {"interruptions", 0}, {"unfinished", false}});
    auto selected = d.choices["spellcasting"]["wizard"]["preparedSpells"];
    for (auto& id : selected) if (id == "srd55:magic-missile") id = "srd55:blur";
    d = applied(d, replacement("/spellcasting/wizard/preparedSpells"), {{"value", selected}});
    rejected(d, "srd55.spellbooks.copy", {{"sourceId", "prepared"}, {"destinationId", replacementBook}, {"spells", {"srd55:magic-missile"}}, {"minutes", 60}, {"paidCp", 1000}});
    d = applied(d, "srd55.spellbooks.copy", {{"sourceId", "prepared"}, {"destinationId", replacementBook}, {"spells", {"srd55:shield"}}, {"minutes", 60}, {"paidCp", 1000}});
    CHECK(item(d, replacementBook)["spellbook"]["spells"] == Json::array({"srd55:blur", "srd55:shield"}));
}

TEST_CASE("Origin-only Wizard-list preparations cannot reconstruct a Wizard spellbook", "[srd55-v2][spellbooks]") {
    auto base = srd55fixtures::base("wizard", rules());
    base.choices["backgroundId"] = "srd55:sage";
    base.choices["backgroundBoosts"] = {{"intelligence", 2}, {"constitution", 1}};
    base.choices["classSkills"] = {"srd55:insight", "srd55:investigation"};
    base.choices["magicInitiate"]["background"] = {{"ability", "wisdom"}, {"cantrips", {"srd55:light", "srd55:mage-hand"}}, {"spell", "srd55:grease"}};
    base.choices["spellcasting"]["wizard"]["spellbook"]["1"] = {"srd55:shield", "srd55:magic-missile", "srd55:mage-armor", "srd55:sleep", "srd55:find-familiar", "srd55:detect-magic"};
    auto finished = srd55fixtures::finish(base, rules());
    INFO(srd55fixtures::errors(finished.evaluation)); REQUIRE(finished.evaluation.complete());
    auto d = applied(finished.document, "srd55.inventory.initialize");
    d = applied(d, "srd55.history.accept-baseline");
    std::string book; for (const auto& record : d.resources["inventory"]["instances"]) if (record["itemId"] == "srd55:spellbook") book = record["id"];
    d = applied(d, "srd55.spellbooks.initialize", {{"instanceId", book}});
    auto [withBlank, newBook] = blank(lose(d, book));
    rejected(withBlank, "srd55.spellbooks.copy", {{"sourceId", "prepared"}, {"destinationId", newBook}, {"spells", {"srd55:grease"}}, {"minutes", 60}, {"paidCp", 1000}});
    CHECK(evaluate(withBlank, rules()).complete());
}

TEST_CASE("Untracked book loss cannot bypass physical access or migrate free replacement contents", "[srd55-v2][spellbooks]") {
    auto tracked = hero(); const auto original = initial(tracked);
    auto untracked = tracked; untracked.resources.erase("wizardSpellbooks");
    for (auto& record : untracked.resources["inventory"]["instances"]) record.erase("spellbook");
    REQUIRE(evaluate(untracked, rules()).complete());
    rejected(untracked, "srd55.inventory.dispose", {{"instanceId", original}, {"quantity", 1}, {"disposition", "lost"}, {"proceedsCp", 0}, {"reason", "Tracking must precede removal"}});
    rejected(untracked, "srd55.inventory.move", {{"instanceId", original}, {"location", "stored"}});
    // An older file may already contain a loss. Preserve it without inventing a
    // reconstruction or erasing its still-prepared spells.
    auto legacy = lose(tracked, original); legacy.resources.erase("wizardSpellbooks");
    for (auto& record : legacy.resources["inventory"]["instances"]) record.erase("spellbook");
    REQUIRE(evaluate(legacy, rules()).complete());
    CHECK_FALSE(available(legacy, "srd55.companions.cast.wizard-book-ritual"));
    legacy = applied(legacy, "srd55.resources.short-rest", {{"hours", 1}, {"interrupted", false}});
    CHECK_FALSE(available(legacy, "srd55.resources.arcane-recovery"));
    legacy = applied(legacy, "srd55.inventory.acquire", {{"itemId", "srd55:spellbook"}, {"quantity", 1}, {"source", "gift"}, {"paidCp", 0}, {"reason", "Blank replacement"}});
    const auto newBook = legacy.resources["inventory"]["instances"].back()["id"];
    rejected(legacy, "srd55.spellbooks.initialize", {{"instanceId", newBook}});
    CHECK_FALSE(available(legacy, "srd55.spells.copy"));
}

TEST_CASE("Alternate Wizard profiles use their accepted slot progression for physical books", "[srd55-v2][spellbooks][profiles]") {
    auto loaded = loadPack(std::filesystem::path(DND_DATA_DIR) / "srd55-core-v2"); REQUIRE(loaded.valid());
    Json alternate;
    for (const auto& entry : loaded.pack.entries) if (entry["id"] == "srd55:wizard") alternate = entry;
    alternate["id"] = "test:early-wizard"; alternate["name"] = "Early-study Wizard";
    alternate["casting"]["slots"][1][1] = 1; // Explicit compatible class content, not a stock-rule claim.
    loaded.pack.entries.push_back(alternate);
    auto r = resolveRuleset(newCharacter("srd55", "2.0.0"), {loaded.pack});
    for (const auto& message : r.messages) INFO(message.code + " " + message.text);
    REQUIRE(r.valid());
    auto d = srd55fixtures::base("wizard", r, 2); d.choices["classId"] = "test:early-wizard";
    d.choices["spellcasting"]["wizard"]["spellbook"]["2"] = {"srd55:misty-step", "srd55:mirror-image"};
    auto finished = srd55fixtures::finish(d, r); INFO(srd55fixtures::errors(finished.evaluation)); REQUIRE(finished.evaluation.complete());
    d = finished.document;
    auto run = [&](const std::string& action, Json inputs = Json::object()) {
        const auto result = executeCommand(d, r, {action, inputs});
        for (const auto& message : result.messages) INFO(message.code + " " + message.text);
        REQUIRE(result.valid()); d = result.document; REQUIRE(evaluate(d, r).complete());
    };
    run("srd55.inventory.initialize"); run("srd55.history.accept-baseline");
    std::string id; for (const auto& record : d.resources["inventory"]["instances"]) if (record["itemId"] == "srd55:spellbook") id = record["id"];
    run("srd55.spellbooks.initialize", {{"instanceId", id}});
    const auto& contents = item(d, id)["spellbook"]["spells"];
    CHECK(std::find(contents.begin(), contents.end(), Json("srd55:misty-step")) != contents.end());
    CHECK(std::find(contents.begin(), contents.end(), Json("srd55:mirror-image")) != contents.end());
}

TEST_CASE("Entering Wizard after inventory initialization grants exactly one initial physical book", "[srd55-v2][spellbooks][multiclass]") {
    auto finished = srd55fixtures::complete("fighter", 1, rules()); REQUIRE(finished.evaluation.complete());
    auto d = applied(finished.document, "srd55.inventory.initialize");
    d = applied(d, "srd55.history.accept-baseline");
    const auto beforeCount = d.resources["inventory"]["instances"].size();
    auto begin = command(d, "srd55.history.begin-advance", {{"classId", "srd55:wizard"}}); REQUIRE(begin.valid());
    auto pending = begin.document; srd55fixtures::seedAdvancement(pending, rules(), "wizard", 1);
    pending.choices["level"] = 2; // The new Wizard level is character level two.
    finished = srd55fixtures::finish(pending, rules()); INFO(srd55fixtures::errors(finished.evaluation)); REQUIRE(finished.evaluation.complete());
    d = applied(finished.document, "srd55.history.commit");
    REQUIRE(d.resources["inventory"]["instances"].size() == beforeCount + 1);
    const std::string id = d.resources["inventory"]["wizardInitialBookId"];
    d = applied(d, "srd55.spellbooks.initialize", {{"instanceId", id}});
    CHECK(item(d, id)["spellbook"]["spells"].size() == 6);
    CHECK(evaluate(documentFromJson(toJson(d)), rules()).complete());
}
