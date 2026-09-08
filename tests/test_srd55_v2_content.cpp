#include "dnd/content.hpp"
#include "dnd/persistence.hpp"
#include "srd55_fixture.hpp"
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

namespace {
struct ProfilePackFixture {
    dnd::ContentPack core;
    Json manifest;
    Json entries = Json::array();
    std::filesystem::path root;
    ProfilePackFixture() {
        auto loaded=dnd::loadPack(std::filesystem::path(DND_DATA_DIR)/"srd55-core-v2");
        REQUIRE(loaded.valid());core=loaded.pack;
        root=std::filesystem::temp_directory_path()/("dnd-profile-"+dnd::newCharacter("srd55","2.0.0").id);
        std::filesystem::create_directories(root/"input");
        manifest=core.manifest;manifest["id"]="profile-fixture";manifest["version"]="1.0.0";
        manifest["name"]="Alternate profile fixture";manifest["publisher"]="Profile Test Publisher";manifest["origin"]="homebrew";
        manifest["dependencies"]=Json::array({{{"id","srd55-core"},{"version","2.0.0"}}});manifest["dataFiles"]=Json::array({"content.json"});
    }
    ~ProfilePackFixture(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
    Json definition(const std::string& id)const {
        const auto found=std::find_if(core.entries.begin(),core.entries.end(),[&](const auto& item){return item.at("id")==id;});
        REQUIRE(found!=core.entries.end());return *found;
    }
    void write()const {
        std::ofstream(root/"input"/"manifest.json")<<manifest.dump(2);
        std::ofstream(root/"input"/"content.json")<<entries.dump(2);
    }
    dnd::CharacterDocument document()const {auto result=dnd::newCharacter("srd55","2.0.0");result.packs.push_back({"profile-fixture","1.0.0"});return result;}
};
bool profileErrors(const std::vector<dnd::Message>& messages){return std::any_of(messages.begin(),messages.end(),[](const auto& message){return message.severity=="error";});}
}

TEST_CASE("Imported class profiles retain public choices resources identities and saved actions", "[srd55-v2-content][profiles]") {
    ProfilePackFixture fixture;
    for(const auto& cls:srd55fixtures::classes){
        auto definition=fixture.definition("srd55:"+cls);definition["id"]="profile-fixture:"+cls;definition["name"]="Alternate "+cls;
        definition["source"]={{"publication","Profile Fixture"},{"page","1"}};fixture.entries.push_back(definition);
        auto subclass=fixture.definition("srd55:"+srd55fixtures::subclasses.at(cls));subclass["id"]="profile-fixture:"+srd55fixtures::subclasses.at(cls);
        subclass["classId"]="profile-fixture:"+cls;subclass["source"]={{"publication","Profile Fixture Subclass"},{"page","2"}};
        if(subclass.contains("spellGrants"))for(auto& grant:subclass["spellGrants"])grant["source"]={{"publication","Profile Fixture Spell Grants"},{"page","3"}};
        if(subclass.contains("landSpells"))for(auto& land:subclass["landSpells"])for(auto& grant:land)grant["source"]={{"publication","Profile Fixture Land Grants"},{"page","4"}};
        fixture.entries.push_back(subclass);
    }
    fixture.write();auto loaded=dnd::loadPack(fixture.root/"input");REQUIRE(loaded.valid());
    const auto installed=dnd::installPack(fixture.root/"input",fixture.root/"installed",{fixture.core});REQUIRE_FALSE(profileErrors(installed));
    loaded=dnd::loadPack(fixture.root/"installed"/"profile-fixture-1.0.0");REQUIRE(loaded.valid());
    const auto resolved=dnd::resolveRuleset(fixture.document(),{fixture.core,loaded.pack});
    INFO(Json(resolved.messages.size()).dump());REQUIRE(resolved.valid());
    const auto reversed=dnd::resolveRuleset(fixture.document(),{loaded.pack,fixture.core});REQUIRE(reversed.valid());REQUIRE(reversed.content==resolved.content);
    const auto stock=srd55fixtures::rules();
    for(const auto& cls:srd55fixtures::classes)for(const int classLevel:{3,20}){
        CAPTURE(cls,classLevel);
        const auto original=srd55fixtures::complete(cls,classLevel,stock);INFO(srd55fixtures::errors(original.evaluation));REQUIRE(original.evaluation.complete());
        auto document=original.document;document.packs=fixture.document().packs;document.choices["classId"]="profile-fixture:"+cls;
        document.choices["subclasses"][cls]="profile-fixture:"+srd55fixtures::subclasses.at(cls);
        const auto evaluated=dnd::evaluate(document,resolved);INFO(srd55fixtures::errors(evaluated));REQUIRE(evaluated.complete());
        for(const auto& resource:original.evaluation.resources){
            const auto found=std::find_if(evaluated.resources.begin(),evaluated.resources.end(),[&](const auto& candidate){return candidate.id==resource.id;});
            CAPTURE(resource.id);REQUIRE(found!=evaluated.resources.end());CHECK(found->maximum==resource.maximum);CHECK(found->recharge==resource.recharge);
        }
        std::set<std::string> expectedFields,actualFields;
        for(const auto& stage:original.evaluation.stages)if(stage.id.starts_with("features-"))for(const auto& field:stage.fields)expectedFields.insert(field.path);
        for(const auto& stage:evaluated.stages)if(stage.id.starts_with("features-"))for(const auto& field:stage.fields)actualFields.insert(field.path);
        CHECK(actualFields==expectedFields);
        for(const auto& id:{"hp.maximum","armorClass","proficiency"}){const auto* expected=original.evaluation.find(id);const auto* actual=evaluated.find(id);REQUIRE(expected);REQUIRE(actual);CHECK(actual->normal==expected->normal);}
        const auto path=fixture.root/(cls+".dnd.json");dnd::saveCharacter(path,document);const auto reopened=dnd::loadCharacter(path);REQUIRE_FALSE(reopened.inspectOnly);CHECK(dnd::toJson(reopened.document)==dnd::toJson(document));
        CHECK(dnd::toJson(dnd::evaluate(reopened.document,resolved))==dnd::toJson(evaluated));
        if(cls=="warlock"&&classLevel==3){
            Json inputs;for(std::size_t i=0;i<document.choices["features"]["warlock"]["invocations"].size();++i)inputs["invocationLevels"][std::to_string(i)]=3;
            auto baseline=dnd::executeCommand(document,resolved,{"srd55.history.accept-baseline",inputs});REQUIRE(baseline.valid());
            auto advanced=dnd::executeCommand(baseline.document,resolved,{"srd55.history.begin-advance",{{"classId","profile-fixture:warlock"}}});REQUIRE(advanced.valid());
            srd55fixtures::seedAdvancement(advanced.document,stock,"warlock",4);advanced.document.choices["subclasses"]["warlock"]="profile-fixture:fiend-patron";
            advanced.document.choices["features"]["warlock"]["invocations"][0]="srd55:otherworldly-leap";
            const auto completed=srd55fixtures::finish(advanced.document,resolved);INFO(srd55fixtures::errors(completed.evaluation));REQUIRE(completed.evaluation.complete());
            const auto committed=dnd::executeCommand(completed.document,resolved,{"srd55.history.commit",Json::object()});REQUIRE(committed.valid());
            const auto event=std::find_if(committed.document.advancement.rbegin(),committed.document.advancement.rend(),[](const auto& item){return item.is_object()&&item.value("kind","")=="srd55.history.commit-advance";});
            REQUIRE(event!=committed.document.advancement.rend());CHECK(event->at("acquisitions").at("/features/warlock/invocations/0").at("classLevel")==4);
            const auto rested=dnd::executeCommand(committed.document,resolved,{"srd55.resources.short-rest",{{"hours",1},{"interrupted",false}}});REQUIRE(rested.valid());
            const auto path=fixture.root/"warlock-after-rest.dnd.json";dnd::saveCharacter(path,rested.document);const auto reopened=dnd::loadCharacter(path);REQUIRE_FALSE(reopened.inspectOnly);CHECK(dnd::evaluate(reopened.document,resolved).complete());
        }
        if(cls=="fighter"&&classLevel==3){
            const auto resource=std::find_if(evaluated.resources.begin(),evaluated.resources.end(),[](const auto& value){return value.id=="fighter:second-wind";});REQUIRE(resource!=evaluated.resources.end());CHECK(resource->maximum==2);
            CHECK(std::any_of(resource->sources.begin(),resource->sources.end(),[](const auto& source){return source.publication=="Profile Fixture";}));
            CHECK(dnd::renderSheetHtml(document,evaluated,resolved).find("Alternate fighter")!=std::string::npos);
            auto spent=dnd::executeCommand(document,resolved,{"srd55.resources.spend",{{"resource","fighter:second-wind"},{"amount",1}}});REQUIRE(spent.valid());CHECK(spent.document.resources["fighter:second-wind"]==1);CHECK(spent.document.choices["classId"]=="profile-fixture:fighter");
            auto rested=dnd::executeCommand(spent.document,resolved,{"srd55.resources.short-rest",{{"hours",1},{"interrupted",false}}});REQUIRE(rested.valid());CHECK(rested.document.resources["fighter:second-wind"]==2);
            auto baseline=dnd::executeCommand(rested.document,resolved,{"srd55.history.accept-baseline",Json::object()});REQUIRE(baseline.valid());
            auto advanced=dnd::executeCommand(baseline.document,resolved,{"srd55.history.begin-advance",{{"classId","profile-fixture:fighter"}}});REQUIRE(advanced.valid());
            srd55fixtures::seedAdvancement(advanced.document,stock,"fighter",4);advanced.document.choices["subclasses"]["fighter"]="profile-fixture:champion";
            auto completed=srd55fixtures::finish(advanced.document,resolved);INFO(srd55fixtures::errors(completed.evaluation));REQUIRE(completed.evaluation.complete());
            auto committed=dnd::executeCommand(completed.document,resolved,{"srd55.history.commit",Json::object()});INFO(srd55fixtures::errors(dnd::evaluate(committed.document,resolved)));REQUIRE(committed.valid());CHECK(committed.document.choices["classId"]=="profile-fixture:fighter");
        }
    }
}

TEST_CASE("Supported profile progression reads the selected definition", "[srd55-v2-content][profiles]") {
    ProfilePackFixture fixture;auto fighter=fixture.definition("srd55:fighter");fighter["id"]="profile-fixture:warrior";fighter["progression"]["secondWind"][0]=3;fighter["progression"]["attacks"][0]=2;fixture.entries.push_back(fighter);fixture.write();
    const auto loaded=dnd::loadPack(fixture.root/"input");REQUIRE(loaded.valid());const auto rules=dnd::resolveRuleset(fixture.document(),{fixture.core,loaded.pack});REQUIRE(rules.valid());
    auto original=srd55fixtures::complete("fighter",1,srd55fixtures::rules());REQUIRE(original.evaluation.complete());original.document.packs=fixture.document().packs;original.document.choices["classId"]="profile-fixture:warrior";
    const auto result=dnd::evaluate(original.document,rules);INFO(srd55fixtures::errors(result));REQUIRE(result.complete());
    REQUIRE(result.find("feature.fighter.resource-second-wind"));CHECK(result.find("feature.fighter.resource-second-wind")->normal["maximum"]==3);
    REQUIRE(result.find("attacks"));CHECK(result.find("attacks")->normal==2);
}

TEST_CASE("Unsupported profile bindings and malformed progression fail before installation", "[srd55-v2-content][profiles]") {
    ProfilePackFixture fixture;const auto original=fixture.definition("srd55:fighter");
    for(const auto& change:std::vector<std::pair<std::string,Json>>{
        {"/rulesProfile","unknown-profile"},{"/rulesProfile",123},
        {"/features/0/mechanics/feature","fighter:unimplemented-power"},{"/features/0/mechanics/handler",false},
        {"/features/0/mechanics/extraEffect",42},{"/features/0/level",2},
        {"/progression/secondWind/0","three"},{"/progression/secondWind/0",-1},{"/progression/secondWind/0",1.5},
        {"/progression/attacks/0",0},{"/progression/mystery",Json::array()},
        {"/casting/ability","intelligence"},{"/casting/extraEffect",42},{"/proficiency/0",20},{"/features",Json::array()}}){
        CAPTURE(change.first,change.second);auto invalid=original;invalid["id"]="profile-fixture:invalid";invalid[Json::json_pointer(change.first)]=change.second;fixture.entries=Json::array({invalid});fixture.write();
        const auto loaded=dnd::loadPack(fixture.root/"input");REQUIRE_FALSE(loaded.valid());
        CHECK(std::any_of(loaded.messages.begin(),loaded.messages.end(),[](const auto& message){return message.path.find("profile-fixture:invalid")!=std::string::npos;}));
        const auto installed=dnd::installPack(fixture.root/"input",fixture.root/"installed",{fixture.core});CHECK(profileErrors(installed));CHECK_FALSE(std::filesystem::exists(fixture.root/"installed"/"profile-fixture-1.0.0"));
        auto direct=fixture.core;direct.manifest=fixture.manifest;direct.entries={invalid};const auto rules=dnd::resolveRuleset(fixture.document(),{fixture.core,direct});CHECK_FALSE(rules.valid());
    }
    for(const auto& cls:{"druid","paladin","rogue","sorcerer","warlock"}){
        auto invalid=fixture.definition("srd55:"+std::string(cls));invalid["id"]="profile-fixture:invalid";
        const std::map<std::string,std::string> column={{"druid","knownForms"},{"paladin","layOnHands"},{"rogue","sneakAttack"},{"sorcerer","metamagicCount"},{"warlock","invocations"}};
        invalid["progression"][column.at(cls)][0]=99;fixture.entries=Json::array({invalid});fixture.write();CHECK_FALSE(dnd::loadPack(fixture.root/"input").valid());
    }
    for(const auto& id:{"srd55:agonizing-blast","srd55:quickened-spell"}){
        auto invalid=fixture.definition(id);invalid["mechanics"]["feature"]="unknown:binding";fixture.entries=Json::array({invalid});fixture.write();CHECK_FALSE(dnd::loadPack(fixture.root/"input").valid());
        invalid=fixture.definition(id);invalid["id"]="profile-fixture:unsupported";fixture.entries=Json::array({invalid});fixture.write();CHECK_FALSE(dnd::loadPack(fixture.root/"input").valid());
    }
    auto invalidFeat=fixture.definition("srd55:alert");invalidFeat["id"]="profile-fixture:alert";fixture.entries=Json::array({invalidFeat});fixture.write();CHECK_FALSE(dnd::loadPack(fixture.root/"input").valid());
    for(const auto& id:{"srd55:elf","srd55:alert","srd55:high-elf","srd55:spellbook"}){
        auto invalid=fixture.definition(id);invalid["mechanics"]={{"handler","srd55-v2"},{"feature","unimplemented"}};fixture.entries=Json::array({invalid});fixture.write();CHECK_FALSE(dnd::loadPack(fixture.root/"input").valid());
    }
    for(const auto& path:{"/speed/walk","/saves/strength","/skills/athletics"}){
        auto invalid=fixture.definition("srd55:creature-ape");invalid[Json::json_pointer(path)]="not-a-number";fixture.entries=Json::array({invalid});fixture.write();CHECK_FALSE(dnd::loadPack(fixture.root/"input").valid());
    }
    auto invalidSpells=fixture.definition("srd55:life-domain");invalidSpells["id"]="profile-fixture:invalid";invalidSpells["spellGrants"][0]["spells"][0]=123;fixture.entries=Json::array({invalidSpells});fixture.write();CHECK_FALSE(dnd::loadPack(fixture.root/"input").valid());
    auto subclass=fixture.definition("srd55:champion");subclass["id"]="profile-fixture:invalid";subclass["classId"]="srd55:wizard";fixture.entries=Json::array({subclass});fixture.write();
    const auto loaded=dnd::loadPack(fixture.root/"input");REQUIRE(loaded.valid());const auto resolved=dnd::resolveRuleset(fixture.document(),{fixture.core,loaded.pack});CHECK_FALSE(resolved.valid());
    CHECK(std::any_of(resolved.messages.begin(),resolved.messages.end(),[](const auto& message){return message.code=="content.srd55.v2.profile"&&message.path=="profile-fixture:invalid/classId";}));
    CHECK(profileErrors(dnd::installPack(fixture.root/"input",fixture.root/"installed",{fixture.core})));
}
