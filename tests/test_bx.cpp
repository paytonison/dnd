#include <catch2/catch_test_macros.hpp>
#include "dnd/content.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <map>

using namespace dnd;
namespace {
ContentPack core() {
    auto loaded=loadPack(std::filesystem::path(DND_DATA_DIR)/"bx-core");
    INFO((loaded.messages.empty()?"":loaded.messages.front().text));
    REQUIRE(loaded.valid());return loaded.pack;
}
Evaluation run(const CharacterDocument& d) {return evaluate(d,resolveRuleset(d,{core()}));}
Json value(const Evaluation& e,const std::string& id) {auto* c=e.find(id);REQUIRE(c!=nullptr);return c->effective;}
bool has(const Evaluation& e,const std::string& code) {return std::any_of(e.messages.begin(),e.messages.end(),[&](const auto& m){return m.code==code;});}
// Expected values are transcribed independently from the visually inspected original
// printed X5/X6 tables. Fixture selection may read spell identifiers, never expected math.
const std::map<std::string,std::vector<int>> expectedXp={
 {"cleric",{0,1500,3000,6000,12000,25000,50000,100000,200000,300000,400000,500000,600000,700000}},
 {"dwarf",{0,2200,4400,8800,17000,35000,70000,140000,270000,400000,530000,660000}},
 {"elf",{0,4000,8000,16000,32000,64000,120000,250000,400000,600000}},
 {"fighter",{0,2000,4000,8000,16000,32000,64000,120000,240000,360000,480000,600000,720000,840000}},
 {"halfling",{0,2000,4000,8000,16000,32000,64000,120000}},
 {"magic-user",{0,2500,5000,10000,20000,40000,80000,150000,300000,450000,600000,750000,900000,1050000}},
 {"thief",{0,1200,2400,4800,9600,20000,40000,80000,160000,280000,400000,520000,640000,760000}}
};
const std::vector<std::vector<int>> clericSpells={{0,0,0,0,0},{1,0,0,0,0},{2,0,0,0,0},{2,1,0,0,0},{2,2,0,0,0},{2,2,1,1,0},{2,2,2,1,1},{3,3,2,2,1},{3,3,3,2,2},{4,4,3,3,2},{4,4,4,3,3},{5,5,4,4,3},{5,5,5,4,4},{6,5,5,5,4}};
const std::vector<std::vector<int>> muSpells={{1,0,0,0,0,0},{2,0,0,0,0,0},{2,1,0,0,0,0},{2,2,0,0,0,0},{2,2,1,0,0,0},{2,2,2,0,0,0},{3,2,2,1,0,0},{3,3,2,2,0,0},{3,3,3,2,1,0},{3,3,3,3,2,0},{4,3,3,3,2,1},{4,4,3,3,3,2},{4,4,4,3,3,3},{4,4,4,4,3,3}};
CharacterDocument character(const std::string& cls="fighter",int level=1) {
    auto d=newCharacter("bx");d.name="Acceptance fixture";
    d.choices={{"class","bx:"+cls},{"level",level},{"xp",expectedXp.at(cls).at(static_cast<std::size_t>(level-1))},
        {"abilities",{{"str",10},{"int",10},{"wis",10},{"dex",10},{"con",13},{"cha",10}}},
        {"alignment","lawful"},{"hp",Json::array()},{"moneyRoll",18},{"weapon",cls=="magic-user"?"bx:dagger":cls=="cleric"?"bx:mace":"bx:sword"}};
    for(int i=0;i<std::min(level,9);++i)d.choices["hp"].push_back(2);
    if(cls=="cleric")d.choices["equipment"]={"bx:holy-symbol"};
    if(cls=="thief")d.choices["equipment"]={"bx:thieves-tools"};
    if(cls=="magic-user" || cls=="elf") {
        const auto& counts=muSpells.at(static_cast<std::size_t>(level-1));
        d.choices["spellbook"]=Json::array();
        auto pack=core();
        for(std::size_t sl=0;sl<counts.size();++sl) {
            int n=0;for(const auto& entry:pack.entries)if(entry.value("kind","")=="spell" && entry.value("tradition","")=="magic-user" && entry.value("spellLevel",0)==static_cast<int>(sl+1) && n<counts[sl]) {d.choices["spellbook"].push_back(entry["id"]);++n;}
        }
    }
    return d;
}
}
TEST_CASE("Original B/X all seven classes at every supported level", "[bx][acceptance]") {
    const std::map<std::string,int> fixed={{"cleric",1},{"dwarf",3},{"elf",2},{"fighter",2},{"halfling",0},{"magic-user",1},{"thief",2}};
    for(const auto& [cls,thresholds]:expectedXp)for(std::size_t index=0;index<thresholds.size();++index) {
        const int level=static_cast<int>(index+1);CAPTURE(cls,level);
        auto d=character(cls,level);auto e=run(d);
        INFO(toJson(e)["messages"].dump());REQUIRE(e.complete());
        CHECK(value(e,"level")==level);CHECK(value(e,"xp.minimum")==thresholds[index]);
        CHECK(value(e,"hp.max")==3*std::min(level,9)+fixed.at(cls)*std::max(0,level-9));
        if(index+1<thresholds.size())CHECK(value(e,"xp.next")==thresholds[index+1]);else CHECK(value(e,"xp.next")=="Class limit");
        if(cls=="cleric")CHECK(value(e,"spells.slots")==Json(clericSpells[index]));
        if(cls=="magic-user" || cls=="elf")CHECK(value(e,"spells.slots")==Json(muSpells[index]));
        for(const auto& calculation:e.calculations){CHECK_FALSE(calculation.steps.empty());CHECK_FALSE(calculation.sources.empty());}
    }
}
TEST_CASE("B/X class limits and HP boundaries are enforced", "[bx][acceptance]") {
    const std::map<std::string,int> hitDie={{"cleric",6},{"dwarf",8},{"elf",6},{"fighter",8},{"halfling",6},{"magic-user",4},{"thief",4}};
    for(const auto& [cls,xp]:expectedXp) {
        CAPTURE(cls);auto d=character(cls,static_cast<int>(xp.size()));d.choices["level"]=xp.size()+1;CHECK_FALSE(run(d).complete());
        d=character(cls);d.choices["hp"][0]=hitDie.at(cls)+1;CHECK(has(run(d),"bx.hp.roll"));
        d=character(cls);d.choices["hp"][0]=hitDie.at(cls);CHECK(value(run(d),"hp.max")==hitDie.at(cls)+1);
    }
    auto d=character("fighter",10);d.choices["abilities"]["con"]=3;auto e=run(d);CHECK(e.complete());CHECK(value(e,"hp.max")==11); // Nine min-1 dice, then fixed +2.
    d=character("dwarf",12);d.choices["abilities"]["con"]=18;e=run(d);CHECK(value(e,"hp.max")==54); // 9*(2+3) + 3*3.
    d.resources["hp"]=4;CHECK(value(run(d),"hp.max")==54);CHECK(value(run(d),"hp.current")==4);
    d.choices["hp"].erase(0);CHECK(has(run(d),"bx.hp.roll"));
}
TEST_CASE("B13 Morgan Ironwolf worked example", "[bx][acceptance]") {
    auto d=character();d.name="Morgan Ironwolf";
    d.choices["abilities"]={{"str",15},{"int",7},{"wis",11},{"dex",13},{"con",14},{"cha",8}};
    d.choices["adjustments"]={{"str",1},{"wis",-2}};d.choices["hp"]={5};d.choices["moneyRoll"]=11;d.choices["armor"]="bx:chain";d.choices["shield"]=true;
    d.choices["equipment"]={"bx:shortbow","bx:arrows","bx:silver-arrow","bx:rope","bx:wooden-pole","bx:iron-spikes","bx:torches","bx:rations","bx:large-sack","bx:wine","bx:waterskin"};
    d.choices["options"]["encumbrance"]="detailed";
    auto e=run(d);INFO(toJson(e)["messages"].dump());REQUIRE(e.complete());
    CHECK(value(e,"ability.str")==16);CHECK(value(e,"ability.wis")==9);CHECK(value(e,"xp.adjustment")==10);
    CHECK(value(e,"hp.max")==6);CHECK(value(e,"ac")==3);CHECK(value(e,"money.spent")==108);CHECK(value(e,"money.remaining")==2);
    CHECK(value(e,"attack.melee")["9"]==8);CHECK(value(e,"attack.missile")["9"]==9);
    CHECK(value(e,"reaction")==-1);CHECK(value(e,"retainers.max")==3);
    // B20's example rounds by omitting her two remaining coins: 670; saved inventory includes them.
    CHECK(value(e,"encumbrance")==672);CHECK(value(e,"movement")==60);
}
TEST_CASE("B/X ability eligibility and adjustment edge cases", "[bx]") {
    for(const auto& cls:{"dwarf","halfling"}){auto d=character(cls);d.choices["abilities"]["con"]=8;CHECK(has(run(d),"bx.class.eligibility"));d.choices["abilities"]["con"]=9;CHECK_FALSE(has(run(d),"bx.class.eligibility"));}
    auto elf=character("elf");elf.choices["abilities"]["int"]=8;elf.choices["adjustments"]={{"int",1},{"wis",-2}};CHECK(has(run(elf),"bx.class.eligibility"));
    auto half=character("halfling");half.choices["abilities"]["dex"]=8;CHECK(has(run(half),"bx.class.eligibility"));
    auto d=character();d.choices["adjustments"]={{"str",1},{"con",-2}};CHECK(has(run(d),"bx.adjustment.donor"));
    d.choices["adjustments"]={{"str",1},{"int",-2}};CHECK(has(run(d),"bx.adjustment.donor")); // Would finish at 8.
    d.choices["abilities"]["int"]=11;CHECK_FALSE(has(run(d),"bx.adjustment.donor"));CHECK(value(run(d),"ability.str")==11);
    d.choices["adjustments"]={{"dex",1},{"int",-2}};CHECK(has(run(d),"bx.adjustment.prime"));
    d.choices["adjustments"]={{"str",1}};CHECK(has(run(d),"bx.adjustment.exchange"));
    elf=character("elf");elf.choices["abilities"]["str"]=13;elf.choices["abilities"]["int"]=13;CHECK(value(run(elf),"xp.adjustment")==5);elf.choices["abilities"]["int"]=16;CHECK(value(run(elf),"xp.adjustment")==10);
    half=character("halfling");half.choices["abilities"]["str"]=13;CHECK(value(run(half),"xp.adjustment")==5);half.choices["abilities"]["dex"]=13;CHECK(value(run(half),"xp.adjustment")==10);
    const std::array<std::pair<int,int>,10> xpCases={{{3,-20},{5,-20},{6,-10},{8,-10},{9,0},{12,0},{13,5},{15,5},{16,10},{18,10}}};
    for(const auto& [ability,xp]:xpCases){d=character();d.choices["abilities"]["str"]=ability;CHECK(value(run(d),"xp.adjustment")==xp);}
}
TEST_CASE("B/X saving throws and attacks use original level bands", "[bx][acceptance]") {
    struct Saves {std::string cls;int step;std::vector<std::vector<int>> rows;};
    const std::vector<Saves> expected={
        {"cleric",4,{{11,12,14,16,15},{9,10,12,14,12},{6,7,9,11,9},{3,5,7,8,7}}},
        {"dwarf",3,{{8,9,10,13,12},{6,7,8,10,10},{4,5,6,7,8},{2,3,4,4,6}}},
        {"elf",3,{{12,13,13,15,15},{10,11,11,13,12},{8,9,9,10,10},{6,7,8,8,8}}},
        {"fighter",3,{{12,13,14,15,16},{10,11,12,13,14},{8,9,10,10,12},{6,7,8,8,10},{4,5,6,5,8}}},
        {"halfling",3,{{8,9,10,13,12},{6,7,8,10,10},{4,5,6,7,8}}},
        {"magic-user",5,{{13,14,13,16,15},{11,12,11,14,12},{8,9,8,11,8}}},
        {"thief",4,{{13,14,13,16,15},{12,13,11,14,13},{10,11,9,12,10},{8,9,7,10,8}}}
    };
    const std::array<std::string,5> ids={"death","wands","paralysis","breath","spells"};
    for(const auto& x:expected)for(int level=1;level<=static_cast<int>(expectedXp.at(x.cls).size());++level) {
        CAPTURE(x.cls,level);auto e=run(character(x.cls,level));
        for(std::size_t i=0;i<ids.size();++i)CHECK(value(e,"save."+ids[i])==x.rows.at(static_cast<std::size_t>((level-1)/x.step))[i]);
    }
    const std::vector<std::array<int,3>> warriorRows={{1,19,20},{3,19,20},{4,17,20},{6,17,20},{7,14,17},{9,14,17},{10,12,15},{12,12,15},{13,10,13},{14,10,13}};
    for(const auto& row:warriorRows){auto e=run(character("fighter",row[0]));CHECK(value(e,"attack.base")["0"]==row[1]);CHECK(value(e,"attack.base")["-3"]==row[2]);}
    auto d=character("cleric",9);d.choices["abilities"]["wis"]=18;auto e=run(d);CHECK(value(e,"save.spells")==9);CHECK(value(e,"save.spellsMagic")==6);CHECK(value(e,"save.breath")==11);
}
TEST_CASE("B/X class special abilities retain printed exceptions", "[bx]") {
    auto d=character("thief",6);auto e=run(d);CHECK(value(e,"thief.skills")["Hide in shadows"]==36);CHECK(value(e,"thief.skills")["Hear noise (d6)"]==3);
    d=character("thief",14);e=run(d);CHECK(value(e,"thief.skills")["Pick pockets"]==125);CHECK(value(e,"thief.skills")["Hear noise (d6)"]==5);CHECK(value(e,"thief.backstab")==4);
    d=character("cleric");e=run(d);CHECK(value(e,"turnUndead")["Skeleton"]=="7");CHECK(value(e,"turnUndead")["Wight"]=="-");
    d=character("cleric",7);e=run(d);CHECK(value(e,"turnUndead")["Wight"]=="D");CHECK(value(e,"turnUndead")["Vampire"]=="9");
    d=character("cleric",11);e=run(d);CHECK(value(e,"turnUndead")["Vampire"]=="D");
    d=character("halfling");d.choices["abilities"]["dex"]=16;d.choices["options"]["individualInitiative"]=true;e=run(d);CHECK(value(e,"attack.missileBonus")==3);CHECK(value(e,"ac")==7);CHECK(value(e,"ac.largeOpponents")==5);CHECK(value(e,"initiative")==2);
}
TEST_CASE("B/X spellbooks memorization and reversal follow X11", "[bx]") {
    auto d=character("magic-user",2);d.choices["spellbook"]={"bx:spell-magic-user-light","bx:spell-magic-user-read-magic"};d.choices["prepared"]["1"]={"bx:spell-magic-user-light:reverse","bx:spell-magic-user-light"};auto e=run(d);CHECK(e.complete());CHECK(value(e,"spells.prepared")["1"][0]=="Darkness");
    d.choices["prepared"]["1"]={"bx:spell-magic-user-light","bx:spell-magic-user-light"};CHECK(run(d).complete());
    d.choices["prepared"]["1"][0]="bx:spell-magic-user-magic-missile";CHECK(has(run(d),"bx.spells.choice"));
    d.choices["spellbook"].push_back("bx:spell-magic-user-magic-missile");CHECK(has(run(d),"bx.spellbook.count"));
    d=character("magic-user");d.choices["spellbook"]=Json::array();CHECK(has(run(d),"bx.spellbook.count"));
    d=character("cleric",6);e=run(d);CHECK(value(e,"spells.slots")==Json::array({2,2,1,1,0}));
    d.choices["prepared"]["4"]={"bx:spell-cleric-cure-serious-wounds:reverse"};CHECK(run(d).complete());
    d.choices["prepared"]["6"]={"bx:spell-magic-user-death-spell"};CHECK(has(run(d),"bx.spells.extraLevel"));
    d=character("elf",10);CHECK(value(run(d),"spells.slots")==Json::array({3,3,3,3,2,0}));
    d.choices["spellbook"].push_back("bx:spell-magic-user-death-spell");CHECK(has(run(d),"bx.spellbook.level"));
}
TEST_CASE("B/X equipment permissions budgets and optional damage", "[bx]") {
    auto d=character("cleric");d.choices["weapon"]="bx:sword";CHECK(has(run(d),"bx.equipment.restriction"));
    d=character("magic-user");d.choices["armor"]="bx:leather";CHECK(has(run(d),"bx.equipment.restriction"));
    d=character("thief");d.choices["armor"]="bx:chain";d.choices["shield"]=true;auto e=run(d);CHECK(has(e,"bx.equipment.restriction"));CHECK(has(e,"bx.shield.restriction"));
    for(const auto& cls:{"dwarf","halfling"}){d=character(cls);d.choices["weapon"]="bx:longbow";CHECK(has(run(d),"bx.equipment.restriction"));d.choices["weapon"]="bx:two-handed-sword";CHECK(has(run(d),"bx.equipment.restriction"));}
    d=character();d.choices["weapon"]="bx:two-handed-sword";d.choices["shield"]=true;CHECK(has(run(d),"bx.shield.hands"));
    d=character();d.choices["moneyRoll"]=3;d.choices["armor"]="bx:plate";CHECK(has(run(d),"bx.money.budget"));
    d=character();CHECK(value(run(d),"weapon.damage")=="1d6");d.choices["options"]["variableWeaponDamage"]=true;CHECK(value(run(d),"weapon.damage")=="1d8");
    d.choices["abilities"]["str"]=18;d.choices["weapon"]="bx:shortbow";CHECK(value(run(d),"weapon.damage")=="1d6");
    d.choices["equipment"]={"bx:rope"};d.choices["quantities"]["bx:rope"]=3;CHECK(value(run(d),"money.spent")==28);
    d.choices["weapon"]="bx:removed-weapon";CHECK(has(run(d),"bx.equipment.unavailable"));
}
TEST_CASE("B/X XP awards do not skip levels or retroactively change saved XP", "[bx]") {
    auto d=character();d.choices["abilities"]["str"]=16;d.choices["xpAward"]=5000;auto e=run(d);CHECK(value(e,"xp.awardAdjusted")==5500);CHECK(value(e,"xp.afterAward")==3999);CHECK(value(e,"xp")==0);
    d=character("fighter",3);d.choices["xp"]=4500;d.choices["abilities"]["str"]=5;e=run(d);CHECK(value(e,"xp")==4500);CHECK(value(e,"xp.adjustment")==-20);
    d.choices["xp"]=3999;CHECK(has(run(d),"bx.xp.level"));
    d=character();d.choices["xp"]=2000;CHECK(has(run(d),"bx.advance.ready"));CHECK(value(run(d),"level")==1);
}
TEST_CASE("B/X drafts remain deterministic and source removal preserves choices", "[bx]") {
    auto draft=newCharacter("bx");auto before=toJson(draft);auto e=run(draft);CHECK_FALSE(e.complete());CHECK(has(e,"bx.class.missing"));CHECK(toJson(draft)==before);
    auto d=character("elf",7);d.rolls={{"accepted",Json::array({3,4,6})}};d.advancement={{{"level",7},{"acceptedHp",2}}};before=toJson(d);
    CHECK(toJson(run(d))==toJson(run(d)));CHECK(toJson(d)==before);
    auto pack=core();pack.entries.erase(std::remove_if(pack.entries.begin(),pack.entries.end(),[](const auto& item){return item["id"]=="bx:elf";}),pack.entries.end());
    e=evaluate(d,resolveRuleset(d,{pack}));CHECK(has(e,"bx.class.missing"));CHECK(toJson(d)==before);
    d=character();d.choices["languages"]={12};CHECK_FALSE(run(d).complete());
    d=character();d.overrides={{{"target","hp.max"},{"value",42},{"reason","DM bonus for campaign premise"}}};e=run(d);CHECK(e.complete());CHECK(value(e,"hp.max")==42);CHECK(e.find("hp.max")->normal==3);
}
TEST_CASE("B/X content validator rejects corrupt mechanics before availability", "[bx][content]") {
    auto pack=core();const auto validate=bxModule().validateContent;REQUIRE(validate);CHECK(validate(pack).empty());
    auto bad=pack;bad.entries[0]["xp"][1]=0;CHECK_FALSE(validate(bad).empty());
    bad=pack;bad.entries[0]["spellSlots"][0]={1,2};CHECK_FALSE(validate(bad).empty());
    bad=pack;bad.entries[0]["rulesProfile"]="invented-unsupported-mechanic";CHECK_FALSE(validate(bad).empty());
    bad=pack;bad.entries[0]["kind"]="prestige-power";CHECK_FALSE(validate(bad).empty());
    bad=pack;bad.entries[0]["minimumAbilities"]["luck"]=12;CHECK_FALSE(validate(bad).empty());
    bad=pack;for(auto& entry:bad.entries)if(entry["id"]=="bx:combat-tables")entry["attackRows"][0].erase(0);CHECK_FALSE(validate(bad).empty());
    bad=pack;for(auto& entry:bad.entries)if(entry["kind"]=="weapon")entry["missileRange"]=nullptr;CHECK_FALSE(validate(bad).empty());
    ContentPack addition;addition.manifest=pack.manifest;addition.manifest["id"]="homebrew-gear";addition.entries={{{"id","house:rope"},{"kind","equipment"},{"name","Extra rope"},{"cost",2},{"source",{{"publication","Campaign equipment"},{"page","1"}}}}};CHECK(validate(addition).empty());
}
TEST_CASE("B/X supplemental content uses supported mechanics and exact source publications", "[bx][content]") {
    auto pack=core();auto d=character("magic-user");Json dagger;
    for(const auto& item:pack.entries)if(item["id"]=="bx:dagger")dagger=item;
    dagger["id"]="house:ceremonial-dagger";dagger["name"]="Ceremonial dagger";dagger["cost"]=10;pack.entries.push_back(dagger);
    d.choices["weapon"]="house:ceremonial-dagger";
    auto e=evaluate(d,resolveRuleset(d,{pack}));INFO(toJson(e)["messages"].dump());CHECK(e.complete());CHECK(value(e,"money.spent")==10);
    auto* attack=e.find("attack.melee");REQUIRE(attack);REQUIRE(attack->sources.size()==2);CHECK(attack->sources[0].page=="B7");CHECK(attack->sources[0].publication.find("Basic")!=std::string::npos);CHECK(attack->sources[1].page=="X26");CHECK(attack->sources[1].publication.find("Expert")!=std::string::npos);
    Json fighter;for(const auto& item:pack.entries)if(item["id"]=="bx:fighter")fighter=item;
    fighter["id"]="house:campaign-fighter";fighter["name"]="Campaign fighter";pack.entries.push_back(fighter);d=character();d.choices["class"]="house:campaign-fighter";
    e=evaluate(d,resolveRuleset(d,{pack}));CHECK(e.complete());CHECK(value(e,"hp.max")==3);
    pack.entries.erase(std::remove_if(pack.entries.begin(),pack.entries.end(),[](const auto& item){return item["id"]=="bx:combat-tables";}),pack.entries.end());
    auto broken=resolveRuleset(d,{pack});CHECK_FALSE(broken.valid());CHECK_FALSE(evaluate(d,broken).complete());
}
