#include "dnd/content.hpp"
#include "dnd/persistence.hpp"
#include "srd55_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <regex>

using namespace dnd;
namespace {
const ContentPack& pack(){static const auto value=[] {auto p=loadPack(std::filesystem::path(DND_DATA_DIR)/"srd55-core-v2");for(const auto& m:p.messages)INFO(m.path+": "+m.text);REQUIRE(p.valid());return p.pack;}();return value;}
Evaluation run(const CharacterDocument& d){return evaluate(d,resolveRuleset(d,{pack()}));}
CharacterDocument fighter(int level=3){
 auto d=newCharacter("srd55","2.0.0");d.name="Expanded fighter";
 d.choices={{"classId","srd55:fighter"},{"level",level},{"speciesId","srd55:dwarf"},{"backgroundId","srd55:soldier"},{"alignment","Neutral Good"},{"languages",{"srd55:dwarvish","srd55:draconic"}},
 {"abilities",{{"strength",15},{"dexterity",14},{"constitution",13},{"intelligence",8},{"wisdom",10},{"charisma",12}}},{"backgroundBoosts",{{"strength",2},{"constitution",1}}},
 {"classSkills",{"srd55:perception","srd55:survival"}},{"gamingSet","srd55:dice"},{"classEquipment","A"},{"backgroundEquipment","B"},{"armorId","srd55:chain-mail"},{"weaponId","srd55:greatsword"},
 {"features",{{"fighter",{{"fightingStyle",Json::array({"srd55:defense"})},{"weaponMasteries",{"srd55:greatsword","srd55:flail","srd55:javelin"}}}}}}};
 if(level>=3)d.choices["subclasses"]["fighter"]="srd55:champion";
 return d;
}
void complete(const Evaluation& e){const auto messages=toJson(e).at("messages").dump(2);INFO(messages);REQUIRE(e.complete());}
int value(const Evaluation& e,const std::string& key){const auto* c=e.find(key);REQUIRE(c);return c->normal.get<int>();}
bool error(const Evaluation& e,const std::string& suffix){return std::any_of(e.messages.begin(),e.messages.end(),[&](const auto& m){return m.severity=="error"&&m.code.ends_with(suffix);});}
}
TEST_CASE("Exact SRD module versions preserve the first slice and refuse cross-version pack substitution","[srd55-v2][versions]"){
 auto legacy=newCharacter("srd55"),modern=newCharacter("srd55","2.0.0");
 REQUIRE(legacy.moduleVersion=="1.0.0");REQUIRE(modern.moduleVersion=="2.0.0");
 REQUIRE(findEdition("srd55","1.0.0"));REQUIRE(findEdition("srd55","2.0.0"));REQUIRE_FALSE(findEdition("srd55","9.0.0"));
 REQUIRE_FALSE(resolveRuleset(legacy,{pack()}).valid());
 auto old=loadPack(std::filesystem::path(DND_DATA_DIR)/"srd55-core");REQUIRE(old.valid());
 REQUIRE(resolveRuleset(legacy,{old.pack,pack()}).valid());REQUIRE(resolveRuleset(modern,{old.pack,pack()}).valid());
 modern.packs={{"srd55-core","1.0.0"}};REQUIRE_FALSE(resolveRuleset(modern,{old.pack,pack()}).valid());
}
TEST_CASE("Expanded Fighter preserves independent first-slice calculations","[srd55-v2]"){
 for(int level=1;level<=3;++level){const auto e=run(fighter(level));complete(e);REQUIRE(value(e,"hp.maximum")==std::array<int,3>{13,22,31}[static_cast<std::size_t>(level-1)]);REQUIRE(value(e,"armorClass")==17);REQUIRE(value(e,"attack.weapon")==5);REQUIRE(value(e,"proficiency")==2);}
}
TEST_CASE("Expanded advancement applies Constitution increases retroactively and keeps HP rolls","[srd55-v2]"){
 auto d=fighter(5);d.choices["feats"]["4"]={{"id","srd55:ability-score-improvement"},{"boosts",{{"constitution",2}}}};
 d.choices["features"]["fighter"]["weaponMasteries"]={"srd55:greatsword","srd55:flail","srd55:javelin","srd55:longbow"};
 const auto e=run(d);complete(e);REQUIRE(value(e,"ability.constitution")==16);REQUIRE(value(e,"hp.maximum")==54); // 10+4*6 +5*3 Con+5 dwarf.
 REQUIRE(value(e,"proficiency")==3);REQUIRE(value(e,"attacks")==2);REQUIRE(value(e,"armorClass")==17);
 d.choices["hpMethod"]="rolled";d.choices["hp"]={{"2",1},{"3",2},{"4",3},{"5",4}};
 const auto rolled=run(d);complete(rolled);REQUIRE(value(rolled,"hp.maximum")==40); // 10+1+2+3+4+15+5.
 REQUIRE(rolled.rollRequests.size()==4);REQUIRE(toJson(rolled)==toJson(run(documentFromJson(toJson(d)))));
}
TEST_CASE("Ordered multiclass levels determine HP dice and preserve class preparation limits","[srd55-v2][multiclass]"){
 auto d=fighter(2);d.choices["abilityMethod"]="rolled";d.choices["abilities"]["intelligence"]=13;
 d.choices["multiclass"]=true;d.choices["advancement"]["2"]["classId"]="srd55:wizard";d.choices["hpMethod"]="rolled";d.choices["hp"]["2"]=4;
 auto e=run(d);REQUIRE_FALSE(error(e,"multiclass.prerequisite"));REQUIRE(value(e,"hp.maximum")==20); // Fighter10+2, Wizard4+2, dwarf2.
 const auto hpRequest=std::find_if(e.rollRequests.begin(),e.rollRequests.end(),[](const auto& r){return r.category=="hitPoints";});
 REQUIRE(hpRequest!=e.rollRequests.end());REQUIRE(hpRequest->sides==6);REQUIRE(hpRequest->path=="/hp/2");
 REQUIRE(value(e,"spellSlots.1")==2);REQUIRE(value(e,"save.constitution")==4);REQUIRE(value(e,"save.intelligence")==1);
 d.choices["abilities"]["intelligence"]=12;REQUIRE(error(run(d),"multiclass.prerequisite"));
}
TEST_CASE("Version migration makes an explicit copy retaining the complete original and exact roll inputs","[srd55-v2][versions]"){
 auto original=newCharacter("srd55");original.name="Legacy hero";original.choices={{"classId","srd55:fighter"},{"fightingStyle","srd55:defense"},{"weaponMasteries",{"srd55:greatsword"}},{"hp",{{"2",7}}}};original.rolls={{"hp",{{"2",7}}}};
 const auto bytes=toJson(original);const auto result=migrateCharacterVersion(original,"2.0.0");REQUIRE(result.valid());
 REQUIRE(toJson(original)==bytes);REQUIRE(result.document.id!=original.id);REQUIRE(result.document.rolls==original.rolls);
 REQUIRE(result.document.choices["features"]["fighter"]["fightingStyle"][0]=="srd55:defense");
 REQUIRE(result.document.advancement.back()["originalDocument"]==bytes);REQUIRE(result.document.packs.front().version=="2.0.0");
 REQUIRE_FALSE(migrateCharacterVersion(original,"99.0.0").valid());
 auto supplement=original;supplement.packs.push_back({"third-party","1.0.0"});REQUIRE_FALSE(migrateCharacterVersion(supplement,"2.0.0").valid());
}
TEST_CASE("Expanded content validation rejects malformed twenty-level mechanics and unknown effects","[srd55-v2][content]"){
 auto p=pack();auto& cls=*std::find_if(p.entries.begin(),p.entries.end(),[](const auto& j){return j.at("id")=="srd55:fighter";});
 cls["casting"]["slots"].erase(0);REQUIRE_FALSE(srd55FullModule().validateContent(p).empty());
 p=pack();p.entries.front()["arbitraryEffect"]=42;REQUIRE_FALSE(srd55FullModule().validateContent(p).empty());
 auto d=fighter();auto r=resolveRuleset(d,{pack()});r.content.erase("srd55:greatsword");REQUIRE_FALSE(srd55FullModule().validateRuleset(r).empty());
}
TEST_CASE("Evocation Savant stops granting spells after the ninth slot level is unlocked","[srd55-v2][regression]"){
 auto d=fighter(20);d.choices["classId"]="srd55:wizard";d.choices["subclasses"]["wizard"]="srd55:evoker";
 const auto e=run(d);int savantFields=0;
 for(const auto& stage:e.stages)for(const auto& field:stage.fields)if(field.path.starts_with("/spellcasting/wizard/savant/")){
  ++savantFields;REQUIRE(field.path!="/spellcasting/wizard/savant/19");
 }
 REQUIRE(savantFields==8); // Two at3; one at5,7,9,11,13,15,17. Minimum level20 spellbook is44+9=53.
}
TEST_CASE("An ability already above twenty does not block increasing a different ability with ASI","[srd55-v2][regression]"){
 auto d=fighter(20);d.choices["classId"]="srd55:wizard";d.choices["multiclass"]=true;d.choices["abilityMethod"]="rolled";
 d.choices["abilities"]={{"strength",18},{"dexterity",14},{"constitution",14},{"intelligence",14},{"wisdom",14},{"charisma",14}};
 d.choices["backgroundBoosts"]={{"strength",2},{"constitution",1}};
 for(int level=2;level<=20;++level)d.choices["advancement"][std::to_string(level)]["classId"]=level<=3||level==20?"srd55:wizard":"srd55:fighter";
 d.choices["feats"]["19"]={{"id","srd55:boon-of-combat-prowess"},{"boosts",{{"strength",1}}}};
 d.choices["feats"]["20"]={{"id","srd55:ability-score-improvement"},{"boosts",{{"wisdom",2}}}};
 const auto e=run(d);REQUIRE(value(e,"ability.strength")==21);REQUIRE(value(e,"ability.wisdom")==16);REQUIRE_FALSE(error(e,"feat.cap"));
}
TEST_CASE("Monk tool choices use only the published categories and starting tool matches proficiency","[srd55-v2][regression]"){
 auto d=fighter(1);d.choices["classId"]="srd55:monk";d.choices["classSkills"]={"srd55:acrobatics","srd55:stealth"};
 d.choices["tools"]["monk"]["choice-0"]={"srd55:thieves-tools"};
 auto e=run(d);REQUIRE(error(e,"choice.unavailable"));
 const Field* tool=nullptr;for(const auto& stage:e.stages)for(const auto& f:stage.fields)if(f.path=="/tools/monk/choice-0")tool=&f;
 REQUIRE(tool);REQUIRE_FALSE(std::any_of(tool->options.begin(),tool->options.end(),[](const auto& o){return o.id=="srd55:thieves-tools";}));
}
TEST_CASE("A staff focus grants its Quarterstaff weapon profile through actual ownership","[srd55-v2][regression]"){
 auto d=fighter(1);d.choices["classId"]="srd55:wizard";d.choices["classEquipment"]="A";d.choices["armorId"]="none";d.choices["weaponId"]="srd55:quarterstaff";
 auto e=run(d);const Field* weapon=nullptr;for(const auto& stage:e.stages)for(const auto& f:stage.fields)if(f.path=="/weaponId")weapon=&f;
 REQUIRE(weapon);const auto option=std::find_if(weapon->options.begin(),weapon->options.end(),[](const auto& o){return o.id=="srd55:quarterstaff";});REQUIRE(option!=weapon->options.end());REQUIRE(option->available);
}
TEST_CASE("Species free castings have separate persistent capacities","[srd55-v2][regression]"){
 auto d=fighter(5);d.choices["speciesId"]="srd55:elf";d.choices["lineageId"]="srd55:high-elf";d.choices["speciesCastingAbility"]="intelligence";d.choices["speciesSkill"]="srd55:insight";d.choices["lineageCantrip"]="srd55:light";
 d.choices["feats"]["4"]={{"id","srd55:ability-score-improvement"},{"boosts",{{"constitution",2}}}};
 d.choices["features"]["fighter"]["weaponMasteries"].push_back("srd55:longbow");
 auto e=run(d);complete(e);int free=0;for(const auto& r:e.resources)if(r.id.starts_with("innate.")){++free;REQUIRE(r.maximum==1);d.resources[r.id]=0;}
 REQUIRE(free==2);const auto saved=toJson(d);auto reopened=documentFromJson(saved);REQUIRE(reopened.resources==d.resources);REQUIRE(toJson(run(reopened))==toJson(run(d)));REQUIRE(toJson(d)==saved);
}
TEST_CASE("Innate Sorcery applies the same scoped DC bonus to cantrips and prepared spells","[srd55-v2][regression]"){
 auto d=fighter(1);d.choices["classId"]="srd55:sorcerer";d.choices["classSkills"]={"srd55:arcana","srd55:deception"};d.choices["abilityMethod"]="rolled";d.choices["abilities"]["charisma"]=16;
 d.choices["classEquipment"]="B";d.choices["armorId"]="none";d.choices["weaponId"]="none";
 d.choices["spellcasting"]["sorcerer"]["cantrips"]={"srd55:acid-splash","srd55:light","srd55:fire-bolt","srd55:mage-hand"};d.choices["spellcasting"]["sorcerer"]["preparedSpells"]={"srd55:burning-hands","srd55:shield"};
 d.resources["effects"]["sorcerer:innate-sorcery"]=true;
 auto e=run(d);complete(e);REQUIRE(value(e,"sorcerer.spellDc")==14);REQUIRE(e.find("spells.profiles"));
 for(const auto& profile:e.find("spells.profiles")->normal)if(profile.at("source")=="Sorcerer")REQUIRE(profile.at("DC")==14);
}
TEST_CASE("A real legacy Wizard migrates to a complete same-statistics expanded character","[srd55-v2][versions]"){
 const auto path=std::filesystem::path(DND_DATA_DIR).parent_path().parent_path()/"tests/fixtures/srd55-v1-wizard3.json";
 auto legacy=loadCharacter(path);REQUIRE_FALSE(legacy.inspectOnly);const auto original=toJson(legacy.document);
 auto oldPack=loadPack(std::filesystem::path(DND_DATA_DIR)/"srd55-core");REQUIRE(oldPack.valid());
 auto before=evaluate(legacy.document,resolveRuleset(legacy.document,{oldPack.pack}));complete(before);
 const auto migrated=migrateCharacterVersion(legacy.document,"2.0.0");REQUIRE(migrated.valid());auto after=run(migrated.document);complete(after);
 REQUIRE(value(after,"hp.maximum")==23);REQUIRE(value(after,"armorClass")==11);REQUIRE(value(after,"wizard.spellDc")==13);REQUIRE(value(after,"skill.arcana")==7);REQUIRE(value(after,"spellSlots.1")==4);REQUIRE(value(after,"spellSlots.2")==2);
 REQUIRE(toJson(legacy.document)==original);REQUIRE(migrated.document.rolls==legacy.document.rolls);
}
TEST_CASE("Same-class always-prepared spells never consume ordinary preparation allowance","[srd55-v2][regression]"){
 auto d=fighter(3);d.choices["classId"]="srd55:cleric";d.choices["subclasses"]["cleric"]="srd55:life-domain";d.choices["features"]["cleric"]["divineOrder"]="protector";
 auto e=run(d);const Field* prepared=nullptr;for(const auto& stage:e.stages)for(const auto& field:stage.fields)if(field.path=="/spellcasting/cleric/preparedSpells")prepared=&field;
 REQUIRE(prepared);const auto cure=std::find_if(prepared->options.begin(),prepared->options.end(),[](const auto& o){return o.id=="srd55:cure-wounds";});REQUIRE(cure!=prepared->options.end());REQUIRE_FALSE(cure->available);
 REQUIRE(cure->reason.find("always prepared")!=std::string::npos);
}
TEST_CASE("Repeatable Skilled instances survive full character evaluation with six distinct grants","[srd55-v2][regression]"){
 const auto rules=srd55fixtures::rules();auto d=srd55fixtures::base("warlock",rules,2);
 d.choices["speciesId"]="srd55:human";d.choices["size"]="Medium";d.choices["speciesSkill"]="srd55:survival";d.choices["humanFeat"]="srd55:skilled";d.choices["skilledChoices"]={"srd55:history","srd55:insight","srd55:perception"};
 d.choices["features"]["warlock"]["invocations"]={"srd55:eldritch-mind","srd55:lessons-of-the-first-ones","srd55:armor-of-shadows"};
 d.choices["features"]["warlock"]["invocationTargets"]["1"]="srd55:skilled";
 d.choices["features"]["warlock"]["invocationGrants"]["1"]["skilledChoices"]={"srd55:religion","srd55:medicine","srd55:nature"};
 const auto finished=srd55fixtures::finish(d,rules);complete(finished.evaluation);
 for(const auto& skill:{"history","insight","perception","religion","medicine","nature"})REQUIRE(value(finished.evaluation,"skill."+std::string(skill))==4);
 REQUIRE(finished.document.choices["skilledChoices"].size()==3);REQUIRE(finished.document.choices["features"]["warlock"]["invocationGrants"]["1"]["skilledChoices"].size()==3);
}
TEST_CASE("Earlier Lessons skill grants support later multiclass Expertise in a complete character","[srd55-v2][multiclass][regression]"){
 const auto rules=srd55fixtures::rules();auto d=srd55fixtures::base("warlock",rules,2);
 d.choices["level"]=4;d.choices["multiclass"]=true;d.choices["advancement"]={{"2",{{"classId","srd55:warlock"}}},{"3",{{"classId","srd55:bard"}}},{"4",{{"classId","srd55:bard"}}}};
 d.choices["classSkills"]={"srd55:deception","srd55:investigation"};
 d.choices["features"]["warlock"]["invocations"]={"srd55:eldritch-mind","srd55:lessons-of-the-first-ones","srd55:armor-of-shadows"};
 d.choices["features"]["warlock"]["invocationTargets"]["1"]="srd55:skilled";
 d.choices["features"]["warlock"]["invocationGrants"]["1"]["skilledChoices"]={"srd55:arcana","srd55:medicine","srd55:persuasion"};
 d.choices["features"]["bard"]["expertise2"]={"srd55:arcana","srd55:medicine"};
 const auto finished=srd55fixtures::finish(d,rules);complete(finished.evaluation);
 REQUIRE(value(finished.evaluation,"skill.arcana")==6);REQUIRE(value(finished.evaluation,"skill.medicine")==6);REQUIRE(value(finished.evaluation,"hp.maximum")==39);
 REQUIRE(value(finished.evaluation,"pactMagic.slots")==2);REQUIRE(value(finished.evaluation,"spellSlots.1")==3);
}
