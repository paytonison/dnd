#include "dnd/content.hpp"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <fstream>
#include <set>

namespace {
using dnd::Json;
Json readV2(const std::string& file) {
    std::ifstream stream(std::filesystem::path(DND_DATA_DIR)/"srd55-core-v2"/file);
    REQUIRE(stream.good());Json value;stream>>value;return value;
}
std::map<std::string,Json> v2Entries(){
    std::map<std::string,Json> entries;for(const auto& entry:readV2("content.json"))REQUIRE(entries.emplace(entry.at("id").get<std::string>(),entry).second);return entries;
}
std::size_t countKind(const std::map<std::string,Json>& entries,const std::string& kind){
    std::size_t count=0;for(const auto& [id,e]:entries){(void)id;if(e.at("kind")==kind)++count;}return count;
}
bool hasString(const Json& values,const std::string& target){return std::find(values.begin(),values.end(),Json(target))!=values.end();}
}

TEST_CASE("SRD version two content preserves separate exact-version licensing and identities","[srd55-v2-content]"){
    const auto manifest=readV2("manifest.json");
    REQUIRE(manifest.at("id")=="srd55-core");REQUIRE(manifest.at("version")=="2.0.0");
    REQUIRE(manifest.at("moduleVersions")==Json::array({"2.0.0"}));
    REQUIRE(manifest.at("license").at("id")=="CC-BY-4.0");
    REQUIRE(manifest.at("license").at("text").get<std::string>().find("This work includes material from the System Reference Document 5.2.1")!=std::string::npos);
    REQUIRE(manifest.at("sourceArtifact").at("pages")==364);
    REQUIRE(manifest.at("coverage").at("status")=="experimental");
    const auto entries=v2Entries();
    REQUIRE(countKind(entries,"class")==12);REQUIRE(countKind(entries,"subclass")==12);
    REQUIRE(countKind(entries,"spell")==339);REQUIRE(countKind(entries,"feat")==19);
    REQUIRE(countKind(entries,"invocation")==28);REQUIRE(countKind(entries,"metamagic")==10);
    REQUIRE(countKind(entries,"lineage")==24);REQUIRE(countKind(entries,"language")==18);
    REQUIRE(entries.at("srd55:infernal").at("kind")=="lineage");
    REQUIRE(entries.at("srd55:abyssal").at("kind")=="lineage");
    REQUIRE(entries.at("srd55:language-infernal").at("kind")=="language");
    REQUIRE(entries.at("srd55:language-abyssal").at("category")=="rare");
    REQUIRE(entries.at("srd55:infernal").at("level5Spell")=="srd55:darkness");
    for(const auto& [id,e]:entries){
        INFO(id);REQUIRE(e.at("source").at("publication")=="System Reference Document 5.2.1");
        REQUIRE_FALSE(e.at("source").at("page").get<std::string>().empty());
        if(e.at("kind")=="language")REQUIRE_FALSE(e.contains("level5Spell"));
    }
    std::ifstream old(std::filesystem::path(DND_DATA_DIR)/"srd55-core"/"manifest.json");Json legacy;old>>legacy;REQUIRE(legacy.at("version")=="1.0.0");
}
TEST_CASE("Every SRD class has twenty levels of casting progression and complete class entry metadata","[srd55-v2-content]"){
    const auto entries=v2Entries();
    for(const auto& [id,e]:entries)if(e.at("kind")=="class"){
        INFO(id);REQUIRE(e.at("maxLevel")==20);REQUIRE(e.at("fixedHp")==e.at("hitDie").get<int>()/2+1);
        REQUIRE(e.at("saves").size()==2);REQUIRE(e.at("skillCount").get<int>()>=2);
        REQUIRE_FALSE(e.at("primaryAbilities").empty());REQUIRE(e.at("multiclassTraining").at("armor").is_array());
        REQUIRE(e.at("casting").at("slots").size()==20);REQUIRE(e.at("casting").at("cantrips").size()==20);REQUIRE(e.at("casting").at("prepared").size()==20);
        for(const auto& row:e.at("casting").at("slots")){REQUIRE(row.size()==9);for(const auto& cell:row)REQUIRE(cell.is_number_integer());}
        REQUIRE(e.at("proficiency")==Json::array({2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,6,6,6,6}));
        REQUIRE(e.at("xp").at(19)==355000);REQUIRE_FALSE(e.at("features").empty());
        std::set<std::string> features;for(const auto& f:e.at("features")){
            REQUIRE(features.insert(f.at("id").get<std::string>()).second);
            REQUIRE(f.at("level").get<int>()>=1);REQUIRE(f.at("level").get<int>()<=20);
            REQUIRE_FALSE(f.at("description").get<std::string>().empty());
            REQUIRE(f.at("mechanics").at("handler")=="srd55-v2");
        }
        for(const auto& kit:e.at("kits"))for(const auto& item:kit.at("items"))REQUIRE(entries.contains(item.get<std::string>()));
    }
    REQUIRE(entries.at("srd55:fighter").at("primaryAbilityMode")=="any");
    REQUIRE(entries.at("srd55:paladin").at("primaryAbilities")==Json::array({"strength","charisma"}));
    REQUIRE(entries.at("srd55:ranger").at("primaryAbilities")==Json::array({"dexterity","wisdom"}));
    REQUIRE(entries.at("srd55:monk").at("weaponTraining")==Json::array({"simple","martial-light"}));
    REQUIRE(entries.at("srd55:rogue").at("weaponTraining")==Json::array({"simple","martial-finesse-or-light"}));
    REQUIRE(entries.at("srd55:bard").at("skillCount")==3);REQUIRE(entries.at("srd55:rogue").at("skillCount")==4);
    REQUIRE(entries.at("srd55:fighter").at("featLevels")==Json::array({4,6,8,12,14,16,19}));
    REQUIRE(entries.at("srd55:rogue").at("featLevels")==Json::array({4,8,10,12,16,19}));
}
TEST_CASE("SRD published caster and class resource boundaries are independently transcribed","[srd55-v2-content]"){
    const auto e=v2Entries();
    REQUIRE(e.at("srd55:wizard").at("casting").at("slots").at(19)==Json::array({4,3,3,3,3,2,2,1,1}));
    REQUIRE(e.at("srd55:wizard").at("casting").at("prepared").at(19)==25);
    REQUIRE(e.at("srd55:sorcerer").at("casting").at("prepared").at(0)==2);
    REQUIRE(e.at("srd55:bard").at("casting").at("prepared").at(0)==4);
    REQUIRE(e.at("srd55:paladin").at("casting").at("slots").at(0)==Json::array({2,0,0,0,0,0,0,0,0}));
    REQUIRE(e.at("srd55:ranger").at("casting").at("slots").at(4)==Json::array({4,2,0,0,0,0,0,0,0}));
    REQUIRE(e.at("srd55:ranger").at("casting").at("slots").at(19)==Json::array({4,3,3,3,2,0,0,0,0}));
    REQUIRE(e.at("srd55:warlock").at("casting").at("pactSlots").at(10)==3);
    REQUIRE(e.at("srd55:warlock").at("casting").at("pactSlots").at(16)==4);
    REQUIRE(e.at("srd55:warlock").at("casting").at("pactSlotLevel").at(8)==5);
    REQUIRE(e.at("srd55:warlock").at("progression").at("invocations").at(1)==3);
    REQUIRE(e.at("srd55:barbarian").at("progression").at("rages").at(16)==6);
    REQUIRE(e.at("srd55:barbarian").at("progression").at("rageDamage").at(15)==4);
    REQUIRE(e.at("srd55:fighter").at("progression").at("attacks").at(19)==4);
    REQUIRE(e.at("srd55:fighter").at("progression").at("weaponMastery").at(15)==6);
    REQUIRE(e.at("srd55:monk").at("progression").at("martialArtsDie").at(16)=="1d12");
    REQUIRE(e.at("srd55:monk").at("progression").at("unarmoredMovement").at(17)==30);
    REQUIRE(e.at("srd55:rogue").at("progression").at("sneakAttack").at(18)=="10d6");
    REQUIRE(e.at("srd55:druid").at("progression").at("wildShape").at(16)==4);
}
TEST_CASE("Full SRD spells retain metadata source discrepancies and cantrip eligibility","[srd55-v2-content]"){
    const auto entries=v2Entries();int conflicts=0;
    for(const auto& [id,e]:entries)if(e.at("kind")=="spell"){
        INFO(id);for(const auto* key:{"castingTime","range","components","duration","description"})REQUIRE_FALSE(e.at(key).get<std::string>().empty());
        REQUIRE(e.at("effectCoverage")=="reference-only");
        REQUIRE(e.at("level").get<int>()>=0);REQUIRE(e.at("level").get<int>()<=9);
        REQUIRE_FALSE(e.at("lists").empty());
        REQUIRE(e.at("description").get<std::string>().find("@@PAGE:")==std::string::npos);
        if(e.contains("listDiscrepancy"))++conflicts;
    }
    REQUIRE(conflicts==2);
    REQUIRE(entries.at("srd55:floating-disk").at("listSources").at(0).at("page")=="79");
    REQUIRE(entries.at("srd55:vicious-mockery").at("listSources").at(0).at("page")=="33");
    const auto& phantasmal=entries.at("srd55:phantasmal-force");
    REQUIRE(phantasmal.at("tableLists").empty());REQUIRE(phantasmal.at("descriptionLists")==Json::array({"bard","sorcerer","wizard"}));
    REQUIRE(phantasmal.at("source").at("page")=="151");
    REQUIRE(hasString(entries.at("srd55:mind-spike").at("lists"),"sorcerer"));
    REQUIRE_FALSE(hasString(entries.at("srd55:mind-spike").at("tableLists"),"sorcerer"));
    REQUIRE(entries.at("srd55:chromatic-orb").at("componentCosts")==Json::array({{{"amount",50},{"currency","gp"}}}));
    REQUIRE(entries.at("srd55:find-familiar").at("materialConsumed")==true);
    REQUIRE(entries.at("srd55:forbiddance").at("materialConsumption").at("mode")=="conditional");
    REQUIRE(entries.at("srd55:barkskin").at("castingTime")=="Bonus Action");
    REQUIRE(entries.at("srd55:true-strike").at("requiresAttackRoll")==true);
    REQUIRE(entries.at("srd55:true-strike").at("attackType")=="weapon");
    REQUIRE(entries.at("srd55:true-strike").at("rangeFeet")==0);
    REQUIRE(entries.at("srd55:eldritch-blast").at("rangeFeet")==120);
}
TEST_CASE("SRD feats invocations and subclass spells express sourced prerequisites and references","[srd55-v2-content]"){
    const auto entries=v2Entries();
    const auto& asi=entries.at("srd55:ability-score-improvement");
    REQUIRE(asi.at("prerequisites").at("minLevel")==4);REQUIRE(asi.at("abilityIncrease").at("points")==2);REQUIRE(asi.at("abilityIncrease").at("maxScore")==20);REQUIRE(asi.at("repeatable")==true);
    REQUIRE(entries.at("srd55:grappler").at("prerequisites").at("abilityAny")==Json({{"strength",13},{"dexterity",13}}));
    REQUIRE(entries.at("srd55:boon-of-spell-recall").at("prerequisites").at("requiresFeature")=="spellcasting");
    REQUIRE(entries.at("srd55:boon-of-spell-recall").at("abilityIncrease").at("abilities")==Json::array({"intelligence","wisdom","charisma"}));
    REQUIRE(entries.at("srd55:devouring-blade").at("prerequisites").at("minWarlockLevel")==12);
    REQUIRE(entries.at("srd55:devouring-blade").at("prerequisites").at("invocations")==Json::array({"srd55:thirsting-blade"}));
    REQUIRE(entries.at("srd55:quickened-spell").at("sorceryPointCost")==2);
    REQUIRE(entries.at("srd55:heightened-spell").at("sorceryPointCost")==2);
    REQUIRE(entries.at("srd55:life-domain").at("spellGrants").at(0).at("spells").size()==4);
    REQUIRE(entries.at("srd55:circle-of-the-land").at("landSpells").size()==4);
    for(const auto& [id,e]:entries){
        INFO(id);
        if(e.at("kind")=="subclass"){
            REQUIRE(entries.at(e.at("classId").get<std::string>()).at("kind")=="class");
            for(const auto& grant:e.value("spellGrants",Json::array()))for(const auto& spell:grant.at("spells"))REQUIRE(entries.at(spell.get<std::string>()).at("kind")=="spell");
            if(e.contains("landSpells"))for(const auto& grants:e.at("landSpells"))for(const auto& grant:grants)for(const auto& spell:grant.at("spells"))REQUIRE(entries.at(spell.get<std::string>()).at("kind")=="spell");
        }
        if(e.at("kind")=="invocation")for(const auto& dependency:e.at("prerequisites").at("invocations"))REQUIRE(entries.at(dependency.get<std::string>()).at("kind")=="invocation");
    }
}
TEST_CASE("SRD Wild Shape and familiar source records have real physical stats and distinct kinds","[srd55-v2-content]"){
    const auto entries=v2Entries();REQUIRE(countKind(entries,"creature")==70);std::size_t beasts=0;
    for(const auto& [id,e]:entries)if(e.at("kind")=="creature"){
        INFO(id);REQUIRE(e.at("abilities").size()==6);REQUIRE(e.at("saves").size()==6);
        REQUIRE(e.at("hp").get<int>()>0);REQUIRE(e.at("cr").get<double>()<=1);
        REQUIRE_FALSE(e.at("description").get<std::string>().empty());if(e.at("type")=="Beast")++beasts;
    }
    REQUIRE(beasts==64);
    REQUIRE(entries.at("srd55:creature-ape").at("abilities").at("strength")==16);
    REQUIRE(entries.at("srd55:creature-ape").at("skills").at("athletics")==5);
    REQUIRE(entries.at("srd55:creature-ape").at("cr")==.5);
    REQUIRE(entries.at("srd55:creature-imp").at("type")=="Fiend");
    REQUIRE(entries.at("srd55:creature-imp").at("chainFamiliar")==true);
    REQUIRE(entries.at("srd55:creature-sphinx-of-wonder").at("type")=="Celestial");
}
