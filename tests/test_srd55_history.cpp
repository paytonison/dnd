#include "dnd/lifecycle.hpp"
#include "dnd/persistence.hpp"
#include "srd55_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cctype>

using namespace dnd;
namespace history=dnd::srd55v2;
namespace {
const ResolvedRuleset& rules(){static const auto r=srd55fixtures::rules();return r;}
CharacterDocument complete(const std::string& cls,int level){auto r=srd55fixtures::complete(cls,level,rules());INFO(r.stopped);INFO(srd55fixtures::errors(r.evaluation));REQUIRE(r.evaluation.complete());return r.document;}
void valid(const TransitionResult& r){std::string errors;for(const auto& m:r.messages)errors+=m.code+": "+m.text+"\n";INFO(errors);REQUIRE(r.valid());}
void validHistory(const CharacterDocument& d){const auto messages=history::validateSrd55History(d,rules());std::string errors;for(const auto& m:messages)errors+=m.code+" "+m.path+": "+m.text+"\n";INFO(errors);REQUIRE(messages.empty());}
CharacterDocument accept(CharacterDocument d,Json inputs=Json::object()){
    if(inputs.empty()){const auto inv=srd55fixtures::at(d.choices,"/features/warlock/invocations");if(inv.is_array())for(std::size_t i=0;i<inv.size();++i)inputs["invocationLevels"][std::to_string(i)]=d.choices.at("level");}
    auto r=executeCommand(d,rules(),{"srd55.history.accept-baseline",inputs});valid(r);validHistory(r.document);return r.document;
}
Json& lastHistory(CharacterDocument& d){for(auto it=d.advancement.rbegin();it!=d.advancement.rend();++it)if(it->is_object()&&it->value("kind","").starts_with("srd55.history."))return *it;throw std::runtime_error("No typed history event exists");}
std::string replacement(const std::string& path){std::string id=path;for(char& c:id)if(!std::isalnum(static_cast<unsigned char>(c)))c='_';return "srd55.history.replace."+id;}
const Field* field(const Evaluation& e,const std::string& path){for(const auto& s:e.stages)for(const auto& f:s.fields)if(f.path==path)return &f;return nullptr;}
Json replaceOne(const CharacterDocument& d,const std::string& path){auto e=evaluate(d,rules());const auto* f=field(e,path);REQUIRE(f);Json value=srd55fixtures::at(d.choices,path);REQUIRE(value.is_array());REQUIRE_FALSE(value.empty());for(const auto& o:f->options)if(o.available&&!o.id.empty()&&std::find(value.begin(),value.end(),Json(o.id))==value.end()){value[0]=o.id;return value;}FAIL("No distinct eligible replacement option");return {};}
CharacterDocument rest(CharacterDocument d,const std::string& kind){auto after=d;history::recordSrd55HistoryTrigger(d,after,kind);validHistory(after);return after;}
CharacterDocument advance(CharacterDocument d,const std::string& cls,int targetLevel){auto r=executeCommand(d,rules(),{"srd55.history.begin-advance",{{"classId","srd55:"+cls}}});valid(r);auto pending=r.document;srd55fixtures::seedAdvancement(pending,rules(),cls,targetLevel);auto finished=srd55fixtures::finish(pending,rules());INFO(finished.stopped);INFO(srd55fixtures::errors(finished.evaluation));REQUIRE(finished.evaluation.complete());r=executeCommand(finished.document,rules(),{"srd55.history.commit",Json::object()});valid(r);validHistory(r.document);return r.document;}
}
TEST_CASE("Accepted history locks progression choices while drafts and current state remain editable", "[srd55-v2][history]"){
    auto d=complete("fighter",1);auto draft=d;draft.choices["features"]["fighter"]["fightingStyle"][0]="srd55:defense";REQUIRE(evaluate(draft,rules()).complete());REQUIRE(history::validateSrd55History(draft,rules()).empty());
    d=accept(d);auto e=evaluate(d,rules());const auto* ability=field(e,"/abilities/strength");REQUIRE(ability);CHECK_FALSE(ability->editable);CHECK_FALSE(ability->readOnlyReason.empty());const auto* armor=field(e,"/armorId");REQUIRE(armor);CHECK(armor->editable);
    d.name="Renamed after play";d.resources["hp"]=0;d.resources["notes"]="Current encounter notes";validHistory(d);
    auto edited=d;edited.choices["abilities"]["strength"]=18;CHECK_FALSE(history::validateSrd55History(edited,rules()).empty());CHECK_FALSE(evaluate(edited,rules()).complete());
    edited=d;edited.advancement=Json::array();CHECK_FALSE(history::validateSrd55History(edited,rules()).empty());
    const auto reopened=documentFromJson(toJson(d));validHistory(reopened);CHECK(toJson(reopened)==toJson(d));
}
TEST_CASE("Wizard cantrip replacement requires Long Rest and cannot bank repeated changes", "[srd55-v2][history]"){
    auto d=accept(complete("wizard",1));const std::string path="/spellcasting/wizard/cantrips";auto next=replaceOne(d,path);const auto original=toJson(d);
    auto denied=executeCommand(d,rules(),{replacement(path),{{"value",next}}});CHECK_FALSE(denied.valid());CHECK(toJson(d)==original);CHECK(toJson(denied.document)==original);
    d=rest(d,"short-rest");CHECK_FALSE(executeCommand(d,rules(),{replacement(path),{{"value",next}}}).valid());
    d=rest(d,"long-rest");auto changed=executeCommand(d,rules(),{replacement(path),{{"value",next}}});valid(changed);d=changed.document;validHistory(d);CHECK(srd55fixtures::at(d.choices,path)==next);
    auto second=executeCommand(d,rules(),{replacement(path),{{"value",replaceOne(d,path)}}});CHECK_FALSE(second.valid());CHECK(toJson(second.document)==toJson(d));
    d=rest(d,"long-rest");changed=executeCommand(d,rules(),{replacement(path),{{"value",replaceOne(d,path)}}});valid(changed);validHistory(changed.document);
}
TEST_CASE("Bard level permissions permit one cantrip and one spell replacement but no rest retraining", "[srd55-v2][history]"){
    auto d=accept(complete("bard",1));d=rest(d,"long-rest");const std::string cantrips="/spellcasting/bard/cantrips",prepared="/spellcasting/bard/preparedSpells";
    CHECK_FALSE(executeCommand(d,rules(),{replacement(cantrips),{{"value",replaceOne(d,cantrips)}}}).valid());
    d=advance(d,"bard",2);
    auto r=executeCommand(d,rules(),{replacement(cantrips),{{"value",replaceOne(d,cantrips)}}});valid(r);d=r.document;
    r=executeCommand(d,rules(),{replacement(prepared),{{"value",replaceOne(d,prepared)}}});valid(r);d=r.document;validHistory(d);
    CHECK_FALSE(executeCommand(d,rules(),{replacement(prepared),{{"value",replaceOne(d,prepared)}}}).valid());
}
TEST_CASE("Pending advancement permits new choices and preserves earlier choices and dice", "[srd55-v2][history]"){
    auto d=accept(complete("wizard",2));const auto before=d.choices,rolls=d.rolls;auto r=executeCommand(d,rules(),{"srd55.history.begin-advance",{{"classId","srd55:wizard"}}});valid(r);auto pending=r.document;
    pending.choices["abilities"]["intelligence"]=18;CHECK_FALSE(history::validateSrd55History(pending,rules()).empty());
    pending.rolls["newAcceptedRoll"]={{"sides",6},{"result",4}};r=executeCommand(pending,rules(),{"srd55.history.cancel",Json::object()});valid(r);CHECK(r.document.choices==before);CHECK(r.document.rolls.contains("newAcceptedRoll"));validHistory(r.document);
    d=advance(r.document,"wizard",3);CHECK(d.choices["level"]==3);CHECK(d.rolls["abilities"]==rolls["abilities"]);CHECK(srd55fixtures::at(d.choices,"/spellcasting/wizard/spellbook/1")==srd55fixtures::at(before,"/spellcasting/wizard/spellbook/1"));
    auto bad=d;bad.rolls["abilities"]["intelligence"]["total"]=1;CHECK_FALSE(history::validateSrd55History(bad,rules()).empty());
}
TEST_CASE("Metamagic retraining is limited to one option when Sorcerer advances", "[srd55-v2][history]"){
    auto d=accept(complete("sorcerer",2));const std::string path="/features/sorcerer/metamagic";d=rest(d,"long-rest");CHECK_FALSE(executeCommand(d,rules(),{replacement(path),{{"value",replaceOne(d,path)}}}).valid());
    d=advance(d,"sorcerer",3);auto r=executeCommand(d,rules(),{replacement(path),{{"value",replaceOne(d,path)}}});valid(r);d=r.document;validHistory(d);CHECK_FALSE(executeCommand(d,rules(),{replacement(path),{{"value",replaceOne(d,path)}}}).valid());
}
TEST_CASE("Invocation baseline declaration and later events preserve actual acquisition levels", "[srd55-v2][history]"){
    auto d=complete("warlock",2);auto missing=executeCommand(d,rules(),{"srd55.history.accept-baseline",Json::object()});CHECK_FALSE(missing.valid());
    d=accept(d,{{"invocationLevels",{{"0",1},{"1",2},{"2",2}}}});auto acquired=history::srd55HistoryAcquisitionLevels(d);CHECK(acquired.at("/features/warlock/invocations/0")==1);CHECK(acquired.at("/features/warlock/invocations/2")==2);
    d=advance(d,"warlock",3);auto r=executeCommand(d,rules(),{replacement("/features/warlock/invocations/1"),{{"value","srd55:pact-of-the-blade"}}});valid(r);
    auto pending=r.document;pending.choices["features"]["warlock"]["pactWeapon"]="srd55:greatsword";r=executeCommand(pending,rules(),{"srd55.history.commit",Json::object()});valid(r);d=r.document;validHistory(d);acquired=history::srd55HistoryAcquisitionLevels(d);CHECK(acquired.at("/features/warlock/invocations/1")==3);CHECK(acquired.at("/features/warlock/invocations/0")==1);
    auto tampered=d;tampered.advancement[0]["acquisitions"]["/features/warlock/invocations/0"]["characterLevel"]=20;CHECK_FALSE(history::validateSrd55History(tampered,rules()).empty());
}
TEST_CASE("A retained invocation prerequisite cannot be replaced", "[srd55-v2][history]"){
    auto d=complete("warlock",5);d.choices["features"]["warlock"]["invocations"]={"srd55:pact-of-the-blade","srd55:thirsting-blade","srd55:eldritch-mind","srd55:armor-of-shadows","srd55:devils-sight"};d.choices["features"]["warlock"]["pactWeapon"]="srd55:greatsword";REQUIRE(evaluate(d,rules()).complete());d=accept(d);auto start=executeCommand(d,rules(),{"srd55.history.begin-advance",{{"classId","srd55:warlock"}}});valid(start);auto finished=srd55fixtures::finish(start.document,rules());INFO(srd55fixtures::errors(finished.evaluation));REQUIRE(finished.evaluation.complete());auto commit=executeCommand(finished.document,rules(),{"srd55.history.commit",Json::object()});valid(commit);d=commit.document;
    auto denied=executeCommand(d,rules(),{replacement("/features/warlock/invocations/0"),{{"value","srd55:fiendish-vigor"}}});CHECK_FALSE(denied.valid());CHECK(toJson(denied.document)==toJson(d));
}
TEST_CASE("History replay rejects altered committed events and supports valid spell-copy records", "[srd55-v2][history]"){
    auto d=accept(complete("wizard",1));d=rest(d,"long-rest");const auto path="/spellcasting/wizard/cantrips";auto changed=executeCommand(d,rules(),{replacement(path),{{"value",replaceOne(d,path)}}});valid(changed);d=changed.document;
    auto tampered=d;lastHistory(tampered)["after"]["choices"]["abilities"]["strength"]=18;CHECK_FALSE(history::validateSrd55History(tampered,rules()).empty());
    tampered=d;lastHistory(tampered)["sequence"]=999;CHECK_FALSE(history::validateSrd55History(tampered,rules()).empty());
    std::set<std::string> known;for(const auto& row:srd55fixtures::at(d.choices,"/spellcasting/wizard/spellbook"))if(row.is_array())for(const auto& id:row)known.insert(id.get<std::string>());
    std::string copied;for(const auto& [id,spell]:rules().content)if(spell.value("kind","")=="spell"&&spell.value("level",0)==1&&!known.contains(id)){const auto lists=spell.value("lists",Json::array());if(std::find(lists.begin(),lists.end(),Json("wizard"))!=lists.end()){copied=id;break;}}REQUIRE_FALSE(copied.empty());
    auto after=d;after.choices["spellcasting"]["wizard"]["copiedSpells"]=Json::array({{{"spellId",copied},{"paidCp",5000},{"minutes",120}}});history::recordSrd55HistoryChange(d,after,"copy-spell",{{"spellId",copied}});validHistory(after);
    auto invalid=after;lastHistory(invalid)["after"]["choices"]["spellcasting"]["wizard"]["copiedSpells"][0]["paidCp"]=1;invalid.choices["spellcasting"]["wizard"]["copiedSpells"][0]["paidCp"]=1;CHECK_FALSE(history::validateSrd55History(invalid,rules()).empty());
}
TEST_CASE("Rest preparation can revise a whole Cleric list once and Wizard Memorize Spell replaces one", "[srd55-v2][history]"){
    auto cleric=accept(complete("cleric",1));cleric=rest(cleric,"long-rest");const std::string path="/spellcasting/cleric/preparedSpells";
    auto e=evaluate(cleric,rules());const auto* f=field(e,path);REQUIRE(f);auto values=srd55fixtures::at(cleric.choices,path);const auto old=values;int changed=0;for(const auto& o:f->options)if(o.available&&!o.id.empty()&&std::find(old.begin(),old.end(),Json(o.id))==old.end()&&changed<2)values[changed++]=o.id;REQUIRE(changed==2);
    auto r=executeCommand(cleric,rules(),{replacement(path),{{"value",values}}});valid(r);validHistory(r.document);CHECK_FALSE(executeCommand(r.document,rules(),{replacement(path),{{"value",replaceOne(r.document,path)}}}).valid());
    auto wizard=accept(complete("wizard",5));auto rested=executeCommand(wizard,rules(),{"srd55.resources.short-rest",{{"hours",1},{"interrupted",false}}});valid(rested);wizard=rested.document;
    const std::string wizardPath="/spellcasting/wizard/preparedSpells";r=executeCommand(wizard,rules(),{replacement(wizardPath),{{"value",replaceOne(wizard,wizardPath)}}});valid(r);validHistory(r.document);CHECK_FALSE(executeCommand(r.document,rules(),{replacement(wizardPath),{{"value",replaceOne(r.document,wizardPath)}}}).valid());
}
TEST_CASE("Magic Initiate shares one level-change allowance between its cantrips and leveled spell", "[srd55-v2][history]"){
    auto draft=srd55fixtures::base("wizard",rules(),1);draft.choices["backgroundId"]="srd55:sage";draft.choices["backgroundBoosts"]={{"intelligence",2},{"wisdom",1}};draft.choices["classSkills"]={"srd55:insight","srd55:investigation"};auto made=srd55fixtures::finish(draft,rules());INFO(srd55fixtures::errors(made.evaluation));REQUIRE(made.evaluation.complete());auto d=accept(made.document);
    const std::string cantrips="/magicInitiate/background/cantrips",spell="/magicInitiate/background/spell";d=rest(d,"long-rest");CHECK_FALSE(executeCommand(d,rules(),{replacement(cantrips),{{"value",replaceOne(d,cantrips)}}}).valid());d=advance(d,"wizard",2);
    auto changed=executeCommand(d,rules(),{replacement(cantrips),{{"value",replaceOne(d,cantrips)}}});valid(changed);d=changed.document;auto e=evaluate(d,rules());const auto* f=field(e,spell);REQUIRE(f);std::string next;for(const auto& o:f->options)if(o.available&&Json(o.id)!=srd55fixtures::at(d.choices,spell)){next=o.id;break;}REQUIRE_FALSE(next.empty());CHECK_FALSE(executeCommand(d,rules(),{replacement(spell),{{"value",next}}}).valid());validHistory(d);
}
TEST_CASE("Druid form replacement preserves count and obeys the one-form Long Rest limit", "[srd55-v2][history]"){
    auto d=accept(complete("druid",2));const std::string path="/features/druid/forms";d=rest(d,"long-rest");auto e=evaluate(d,rules());const auto* f=field(e,path);REQUIRE(f);auto values=srd55fixtures::at(d.choices,path);const auto old=values;int n=0;for(const auto& o:f->options)if(o.available&&!o.id.empty()&&std::find(old.begin(),old.end(),Json(o.id))==old.end()&&n<2)values[n++]=o.id;REQUIRE(n==2);CHECK_FALSE(executeCommand(d,rules(),{replacement(path),{{"value",values}}}).valid());auto changed=executeCommand(d,rules(),{replacement(path),{{"value",replaceOne(d,path)}}});valid(changed);validHistory(changed.document);
}
TEST_CASE("A declared late Lessons acquisition cannot retroactively satisfy Scholar", "[srd55-v2][history]"){
    auto d=srd55fixtures::base("warlock",rules(),1);d.choices["level"]=7;d.choices["multiclass"]=true;d.choices["classSkills"]={"srd55:deception","srd55:nature"};
    for(int n=2;n<=7;++n)d.choices["advancement"][std::to_string(n)]["classId"]=(n==3||n==4)?"srd55:wizard":"srd55:warlock";
    d.choices["features"]["wizard"]["scholar"]={"srd55:arcana"};d.choices["subclasses"]["warlock"]="srd55:fiend-patron";d.choices["feats"]["6"]={{"id","srd55:ability-score-improvement"},{"boosts",{{"dexterity",2}}}};
    d.choices["features"]["warlock"]["invocations"]={"srd55:eldritch-mind","srd55:armor-of-shadows","srd55:lessons-of-the-first-ones","srd55:fiendish-vigor","srd55:mask-of-many-faces"};d.choices["features"]["warlock"]["invocationTargets"]={"","","srd55:skilled"};d.choices["features"]["warlock"]["invocationGrants"]["2"]["skilledChoices"]={"srd55:arcana","srd55:history","srd55:religion"};auto made=srd55fixtures::finish(d,rules());INFO(srd55fixtures::errors(made.evaluation));REQUIRE(made.evaluation.complete());d=made.document;
    Json declarations={{"invocationLevels",{{"0",1},{"1",2},{"2",5},{"3",5},{"4",5}}}};auto rejected=executeCommand(d,rules(),{"srd55.history.accept-baseline",declarations});CHECK_FALSE(rejected.valid());CHECK(toJson(rejected.document)==toJson(d));
    declarations["invocationLevels"]["2"]=2;auto accepted=executeCommand(d,rules(),{"srd55.history.accept-baseline",declarations});valid(accepted);validHistory(accepted.document);CHECK(history::srd55HistoryAcquisitionLevels(accepted.document).at("/features/warlock/invocations/2")==2);
}
TEST_CASE("A newly always-prepared domain spell leaves the counted list without spending retraining", "[srd55-v2][history]"){
    auto d=complete("cleric",2);d.choices["spellcasting"]["cleric"]["preparedSpells"]={"srd55:bless","srd55:cure-wounds","srd55:command","srd55:detect-magic","srd55:healing-word"};REQUIRE(evaluate(d,rules()).complete());d=accept(d);d=advance(d,"cleric",3);const auto prepared=srd55fixtures::at(d.choices,"/spellcasting/cleric/preparedSpells");CHECK(std::find(prepared.begin(),prepared.end(),Json("srd55:bless"))==prepared.end());CHECK(std::find(prepared.begin(),prepared.end(),Json("srd55:cure-wounds"))==prepared.end());CHECK(prepared.size()==6);validHistory(d);
}
TEST_CASE("An invocation's changed binding consumes its shared replacement allowance and records acquisition", "[srd55-v2][history]"){
    auto d=complete("warlock",2);d.choices["spellcasting"]["warlock"]["cantrips"]={"srd55:eldritch-blast","srd55:chill-touch"};d.choices["features"]["warlock"]["invocations"]={"srd55:eldritch-mind","srd55:armor-of-shadows","srd55:agonizing-blast"};d.choices["features"]["warlock"]["invocationTargets"]["2"]="srd55:eldritch-blast";REQUIRE(evaluate(d,rules()).complete());d=accept(d);
    auto begin=executeCommand(d,rules(),{"srd55.history.begin-advance",{{"classId","srd55:warlock"}}});valid(begin);auto filled=srd55fixtures::finish(begin.document,rules());INFO(srd55fixtures::errors(filled.evaluation));REQUIRE(filled.evaluation.complete());auto committed=executeCommand(filled.document,rules(),{"srd55.history.commit",Json::object()});valid(committed);d=committed.document;
    const auto path="/features/warlock/invocationTargets/2";auto changed=executeCommand(d,rules(),{replacement(path),{{"value","srd55:chill-touch"}}});valid(changed);validHistory(changed.document);CHECK(history::srd55HistoryAcquisitionLevels(changed.document).at("/features/warlock/invocations/2")==3);CHECK_FALSE(executeCommand(changed.document,rules(),{replacement(path),{{"value","srd55:eldritch-blast"}}}).valid());
}
