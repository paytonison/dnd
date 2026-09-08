#include "srd55_fixture.hpp"
#include "dnd/persistence.hpp"
#include <catch2/catch_test_macros.hpp>

namespace {
const dnd::ResolvedRuleset& completeRules() { static const auto value = srd55fixtures::rules(); return value; }
int statistic(const dnd::Evaluation& e, const std::string& id) { const auto* value = e.find(id); INFO(id); REQUIRE(value); REQUIRE(value->normal.is_number_integer()); return value->normal.get<int>(); }
void accepted(const srd55fixtures::Result& result) {
    INFO(result.stopped); INFO(srd55fixtures::errors(result.evaluation));
    REQUIRE(result.evaluation.complete());
    REQUIRE(result.document.overrides.empty());
    REQUIRE(result.document.resources.empty());
}
// Fixed per-level HP amounts from the printed class Hit Dice, independent of
// pack progression tables and the completion helper's legal-option selection.
int expectedHp(const std::string& cls, int level) {
    const int die = cls == "barbarian" ? 12 : cls == "fighter" || cls == "paladin" || cls == "ranger" ? 10 : cls == "wizard" || cls == "sorcerer" ? 6 : 8;
    const int constitution = cls == "barbarian" && level == 20 ? 5 : 3;
    const int draconic = cls == "sorcerer" && level >= 3 ? level : 0;
    return die + (level - 1) * (die / 2 + 1) + level * (constitution + 1) + draconic;
}
int spellbookCount(const dnd::CharacterDocument& document) {
    int count = 0;
    for (const auto* path : {"/spellcasting/wizard/spellbook", "/spellcasting/wizard/savant"})
        for (const auto& row : srd55fixtures::at(document.choices, path)) if (row.is_array()) count += static_cast<int>(row.size());
    return count;
}
int expectedEvokerBook(int level) {
    // SRD5.2.1 Evocation Savant: two spells at3, then one upon each new
    // spell-slot level (class5,7,9,11,13,15,17); no additional grant at19.
    // https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf
    return 6 + 2 * (level - 1) + (level < 3 ? 0 : 2 + (std::min(level,17) - 3) / 2);
}
}

TEST_CASE("All twelve SRD classes are complete public-evaluation characters at levels one and twenty", "[srd55-v2][complete-characters]") {
    for (const auto& cls : srd55fixtures::classes) for (const int level : {1,20}) {
        DYNAMIC_SECTION(cls << " at level " << level) {
            const auto result = srd55fixtures::complete(cls, level, completeRules()); accepted(result);
            CHECK(statistic(result.evaluation, "hp.maximum") == expectedHp(cls, level));
            CHECK(statistic(result.evaluation, "proficiency") == (level == 1 ? 2 : 6));
            CHECK(statistic(result.evaluation, "ability.constitution") == (cls == "barbarian" && level == 20 ? 20 : 16));
            if (cls == "wizard") CHECK(spellbookCount(result.document) == expectedEvokerBook(level));
            const auto before = dnd::toJson(result.document);
            CHECK(dnd::toJson(dnd::evaluate(result.document, completeRules())) == dnd::toJson(result.evaluation));
            CHECK(dnd::toJson(result.document) == before);
            const auto reopened = dnd::documentFromJson(before);
            CHECK(dnd::evaluate(reopened, completeRules()).complete());
            CHECK(dnd::toJson(reopened) == before);
        }
    }
}

TEST_CASE("All 240 single-class SRD advancement states remain complete through the public contract", "[srd55-v2][complete-characters][advancement]") {
    for (const auto& cls : srd55fixtures::classes) {
        DYNAMIC_SECTION(cls << " complete levels 1-20") {
            auto document = srd55fixtures::base(cls, completeRules());
            for (int level = 1; level <= 20; ++level) {
                CAPTURE(cls, level);
                srd55fixtures::seedAdvancement(document, completeRules(), cls, level);
                const auto result = srd55fixtures::finish(document, completeRules()); accepted(result);
                CHECK(statistic(result.evaluation, "hp.maximum") == expectedHp(cls, level));
                CHECK(statistic(result.evaluation, "proficiency") == 2 + (level - 1) / 4);
                if (cls == "wizard") CHECK(spellbookCount(result.document) == expectedEvokerBook(level));
                document = result.document;
            }
        }
    }
}
