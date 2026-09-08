#include "srd55_v2_internal.hpp"
#include "dnd/srd55_inventory.hpp"
#include <algorithm>
#include <set>

namespace dnd::srd55v2 {
namespace {
const std::set<std::string> classProfiles={"barbarian","bard","cleric","druid","fighter","monk","paladin","ranger","rogue","sorcerer","warlock","wizard"};
const std::set<std::string> subclassProfiles={"path-of-the-berserker","college-of-lore","life-domain","circle-of-the-land","champion","warrior-of-the-open-hand","oath-of-devotion","hunter","thief","draconic-sorcery","fiend-patron","evoker"};
const std::set<std::string> abilities={"strength","dexterity","constitution","intelligence","wisdom","charisma"};
bool integer(const Json& j,int minimum,int maximum){if(!j.is_number_integer())return false;try{const auto n=j.get<long long>();return n>=minimum&&n<=maximum;}catch(...){return false;}}
bool array(const Json& j,std::size_t size,int minimum,int maximum){return j.is_array()&&j.size()==size&&std::all_of(j.begin(),j.end(),[&](const auto& n){return integer(n,minimum,maximum);});}
bool texts(const Json& j){return j.is_array()&&std::all_of(j.begin(),j.end(),[](const auto& n){return n.is_string();});}
bool source(const Json& j){return j.is_object()&&j.contains("publication")&&j["publication"].is_string()&&j.contains("page")&&j["page"].is_string();}
const std::map<std::string,std::set<std::string>> fields={
 {"class",{"rulesProfile","hitDie","fixedHp","maxLevel","saves","skillCount","skills","armorTraining","weaponTraining","primaryAbilities","primaryAbilityMode","multiclassSkillCount","multiclassSkills","multiclassTraining","toolProficiencies","initialToolChoices","multiclassToolChoices","kits","casting","featLevels","features","proficiency","progression","xp"}},
 {"subclass",{"rulesProfile","classId","minimumLevel","features","spellGrants","landSpells"}},
 {"species",{"sizes","speed","darkvision","hpPerLevel","progression"}},
 {"lineage",{"speciesId","cantrips","chooseCantrip","darkvision","freeUses","level1Spell","level3Spell","level5Spell","resistance","speed"}},
 {"background",{"abilities","skills","feat","tool","kits"}},
 {"feat",{"category","prerequisites","abilityIncrease","repeatable","repeatGroup","spellList"}},
 {"spell",{"level","school","lists","tableLists","descriptionLists","listSources","castingTime","range","components","duration","ritual","concentration","materialCost","componentCosts","materialConsumed","materialConsumption","effectCoverage","attackType","requiresAttackRoll","rangeFeet","savingThrows","damageTypes","dealsDamage","upcastDescription","listDiscrepancy","cantripUpgrade"}},
 {"weapon",{"category","ranged","damage","damageType","properties","mastery","costCp","finesse","twoHanded","heavy"}},
 {"armor",{"category","ac","dexCap","strength","costCp","stealthDisadvantage"}},
 {"shield",{"ac","costCp"}}, {"gear",{"costCp","startingOnly","weaponProfile","focus"}},
 {"tool",{"category","ability","costCp"}}, {"language",{"category"}}, {"skill",{"ability"}},
 {"invocation",{"prerequisites","prerequisiteDescription","repeatable"}}, {"metamagic",{"sorceryPointCost"}},
 {"creature",{"type","size","cr","ac","hp","speed","abilities","saves","skills","wildShapeEligible","chainFamiliar"}}
};
}
std::vector<Message> validateContent(const ContentPack& pack){
    std::vector<Message> out=validateSrd55MagicItems(pack);
    auto fail=[&](const Json& j,const std::string& key,const std::string& message){out.push_back({"error","pack.srd55.v2.schema","/packs/"+pack.manifest.value("id","")+"/"+j.value("id","")+"/"+key,message,{sourceFromJson(j.value("source",Json::object()))}});};
    for(const auto& j:pack.entries){
        const auto kind=j.value("kind","");if(kind=="magic-item")continue;if(!fields.contains(kind)){fail(j,"kind","No mechanics interpreter supports this content kind.");continue;}
        for(auto it=j.begin();it!=j.end();++it)if(!fields.at(kind).contains(it.key())&&it.key()!="id"&&it.key()!="kind"&&it.key()!="name"&&it.key()!="source"&&it.key()!="replaces"&&it.key()!="description"&&it.key()!="notes"&&it.key()!="mechanics")fail(j,it.key(),"Unsupported property; new mechanics require an edition-module extension.");
        auto textField=[&](const std::string& key,bool required=true){if(!j.contains(key)){if(required)fail(j,key,"Missing text field.");}else if(!j.at(key).is_string())fail(j,key,"Expected text.");};
        auto intField=[&](const std::string& key,int low,int high,bool required=true){if(!j.contains(key)){if(required)fail(j,key,"Missing numeric field.");}else if(!integer(j.at(key),low,high))fail(j,key,"Expected bounded whole number.");};
        auto stringArray=[&](const std::string& key,bool required=true){if(!j.contains(key)){if(required)fail(j,key,"Missing identifier array.");}else if(!texts(j.at(key)))fail(j,key,"Expected array of strings.");};
        auto boolField=[&](const std::string& key,bool required=true){if(!j.contains(key)){if(required)fail(j,key,"Missing boolean field.");}else if(!j.at(key).is_boolean())fail(j,key,"Expected boolean.");};
        textField("description",false);stringArray("notes",false);
        if(j.contains("costCp"))intField("costCp",0,1000000000);
        if(kind=="class"){
            textField("rulesProfile");if(!classProfiles.contains(j.value("rulesProfile","")))fail(j,"rulesProfile","Unsupported class mechanics profile.");
            intField("hitDie",4,12);intField("fixedHp",3,7);intField("maxLevel",20,20);intField("skillCount",0,4);intField("multiclassSkillCount",0,4);
            for(const auto* key:{"saves","skills","armorTraining","weaponTraining","primaryAbilities","multiclassSkills","toolProficiencies"})stringArray(key);
            if(j.contains("primaryAbilities")&&texts(j["primaryAbilities"]))for(const auto& a:j["primaryAbilities"])if(!abilities.contains(a.get<std::string>()))fail(j,"primaryAbilities","Unknown ability prerequisite.");
            if(j.value("primaryAbilityMode","")!="all"&&j.value("primaryAbilityMode","")!="any")fail(j,"primaryAbilityMode","Expected all or any prerequisites.");
            for(const auto* key:{"xp","proficiency"})if(!j.contains(key)||!array(j[key],20,0,1000000000))fail(j,key,"Progression must have exactly twenty bounded integer rows.");
            if(!j.contains("featLevels")||!j["featLevels"].is_array()||!std::all_of(j["featLevels"].begin(),j["featLevels"].end(),[](const auto& n){return integer(n,1,20);}))fail(j,"featLevels","Feat levels must be character class levels 1–20.");
            if(!j.contains("casting")||!j["casting"].is_object())fail(j,"casting","Class requires a casting profile, including noncasters.");
            else{
                const auto& cast=j["casting"];const auto mode=cast.value("kind","");if(mode!="none"&&mode!="full"&&mode!="half"&&mode!="pact")fail(j,"casting/kind","Unknown casting interpretation.");
                for(const auto* key:{"cantrips","prepared","pactSlots","pactSlotLevel"})if(!cast.contains(key)||!array(cast[key],20,0,30))fail(j,std::string("casting/")+key,"Casting progression requires twenty bounded entries.");
                if(!cast.contains("slots")||!cast["slots"].is_array()||cast["slots"].size()!=20)fail(j,"casting/slots","Spell slots require twenty rows.");
                else for(const auto& row:cast["slots"])if(!array(row,9,0,20))fail(j,"casting/slots","Each spell-slot row requires nine bounded counts.");
                if(mode!="none"&&!abilities.contains(cast.value("ability","")))fail(j,"casting/ability","Unsupported spellcasting ability.");
            }
            if(!j.contains("multiclassTraining")||!j["multiclassTraining"].is_object())fail(j,"multiclassTraining","Class requires explicit multiclass training.");
            else for(const auto* key:{"armor","weapons","tools"})if(!j["multiclassTraining"].contains(key)||!texts(j["multiclassTraining"][key]))fail(j,"multiclassTraining","Training requires armor/weapons/tools arrays.");
            if(!j.contains("progression")||!j["progression"].is_object())fail(j,"progression","Class resource progression must be an object.");
            else for(auto it=j["progression"].begin();it!=j["progression"].end();++it)if(!it.value().is_array()||it.value().size()!=20)fail(j,"progression/"+it.key(),"Each class-specific progression requires twenty rows.");
        }
        if(kind=="class"||kind=="subclass"){
            if(kind=="subclass"){textField("classId");textField("rulesProfile");intField("minimumLevel",3,3);if(!subclassProfiles.contains(j.value("rulesProfile","")))fail(j,"rulesProfile","Unsupported subclass profile.");}
            if(!j.contains("features")||!j["features"].is_array())fail(j,"features","Features must be an array.");
            else for(const auto& f:j["features"]){
                if(!f.is_object()||!f.contains("level")||!integer(f["level"],1,20)||!f.contains("name")||!f["name"].is_string()||!f.contains("description")||!f["description"].is_string()||!f.contains("source")||!source(f["source"]))fail(j,"features","Every feature requires level, name, description and printed source.");
                else if(!f.contains("mechanics")||!f["mechanics"].is_object()||f["mechanics"].value("handler","")!="srd55-v2"||!f["mechanics"].contains("feature")||!f["mechanics"]["feature"].is_string())fail(j,"features/mechanics","Unknown feature mechanics handler.");
            }
        }
        if(kind=="class"||kind=="background"){
            if(!j.contains("kits")||!j["kits"].is_object())fail(j,"kits","Starting equipment kits must be an object.");
            else for(const auto& kit:j["kits"]){if(!kit.is_object()||!kit.contains("gold")||!integer(kit["gold"],0,1000000)||!kit.contains("items")||!texts(kit["items"]))fail(j,"kits","Each kit requires gold and item identifiers.");}
        }
        if(kind=="background"){for(const auto* key:{"abilities","skills"})stringArray(key);textField("feat");textField("tool");}
        if(kind=="species"){stringArray("sizes");intField("speed",0,200);intField("darkvision",0,1000);intField("hpPerLevel",0,10);}
        if(kind=="lineage"){textField("speciesId");stringArray("cantrips",false);for(const auto* key:{"level1Spell","level3Spell","level5Spell","resistance"})textField(key,false);intField("speed",0,200,false);intField("darkvision",0,1000,false);}
        if(kind=="skill"){textField("ability");if(!abilities.contains(j.value("ability","")))fail(j,"ability","Unknown skill ability.");}
        if(kind=="language")textField("category",false);
        if(kind=="spell"){
            intField("level",0,9);for(const auto* key:{"school","castingTime","range","components","duration","description"})textField(key);stringArray("lists");
            for(const auto* key:{"ritual","concentration","materialCost","materialConsumed","dealsDamage"})boolField(key);
        }
        if(kind=="feat"){
            textField("category");boolField("repeatable",false);
            if(j.contains("abilityIncrease")){const auto& inc=j["abilityIncrease"];if(!inc.is_object()||!inc.contains("points")||!integer(inc["points"],1,2)||!inc.contains("maxPerAbility")||!integer(inc["maxPerAbility"],1,2)||!inc.contains("maxScore")||!integer(inc["maxScore"],20,30)||!inc.contains("abilities")||!texts(inc["abilities"]))fail(j,"abilityIncrease","Invalid ability-increase definition.");}
        }
        if(kind=="weapon"){textField("category");textField("damage");textField("properties");textField("mastery");for(const auto* key:{"ranged","finesse","heavy","twoHanded"})boolField(key);intField("costCp",0,1000000000);}
        if(kind=="armor"){textField("category");intField("ac",0,30);intField("dexCap",0,99);intField("strength",0,30);boolField("stealthDisadvantage");}
        if(kind=="shield")intField("ac",0,10);
        if(kind=="tool")textField("category",false);
        if(kind=="invocation"||kind=="metamagic")if(!j.contains("mechanics")||!j["mechanics"].is_object()||j["mechanics"].value("handler","")!="srd55-v2")fail(j,"mechanics","Unsupported mechanic handler.");
        if(kind=="creature"){
            for(const auto* key:{"type","size","description"})textField(key);intField("ac",0,40);intField("hp",1,10000);
            if(!j.contains("cr")||!j["cr"].is_number()||j["cr"]<0||j["cr"]>30)fail(j,"cr","Challenge must be between zero and thirty.");
            for(const auto* key:{"abilities","saves","skills","speed"})if(!j.contains(key)||!j[key].is_object())fail(j,key,"Creature stat group must be an object.");
            if(j.contains("abilities")&&j["abilities"].is_object())for(const auto& a:abilities)if(!j["abilities"].contains(a)||!integer(j["abilities"][a],1,30))fail(j,"abilities/"+a,"Creature requires all six bounded ability scores.");
        }
    }
    return out;
}
std::vector<Message> validateRuleset(const ResolvedRuleset& rules){
    std::vector<Message> out=validateSrd55InventoryReferences(rules);
    auto require=[&](const Json& owner,const std::string& target,const std::string& kind){
        if(target.empty()||target=="srd55:gaming-set")return;const auto* found=rules.find(target);
        const bool equipment=kind=="equipment"&&found&&(found->value("kind","")=="armor"||found->value("kind","")=="weapon"||found->value("kind","")=="shield"||found->value("kind","")=="gear"||found->value("kind","")=="tool");
        if(!found||(!equipment&&found->value("kind","")!=kind))out.push_back({"error","content.srd55.v2.reference",owner.value("id",""),"Required "+kind+" reference is unavailable: "+target+".",{sourceFromJson(owner.at("source"))}});
    };
    for(const auto& [id,j]:rules.content){(void)id;const auto kind=j.value("kind","");
        if(kind=="class"||kind=="background"){
            for(const auto& skill:strings(j,"/skills"))require(j,skill,"skill");
            for(const auto& kit:j.at("kits"))for(const auto& item:strings(kit,"/items"))require(j,item,"equipment");
        }
        if(kind=="background"){require(j,j.value("feat",""),"feat");require(j,j.value("tool",""),"tool");}
        if(kind=="subclass")require(j,j.value("classId",""),"class");
        if(kind=="lineage"){
            require(j,j.value("speciesId",""),"species");for(const auto& spell:strings(j,"/cantrips"))require(j,spell,"spell");
            for(const auto* key:{"level1Spell","level3Spell","level5Spell"})require(j,j.value(key,""),"spell");
        }
        if(kind=="invocation")for(const auto& required:strings(j,"/prerequisites/invocations"))require(j,required,"invocation");
        if(kind=="gear")require(j,j.value("weaponProfile",""),"weapon");
        if(kind=="subclass")for(const auto& grant:j.value("spellGrants",Json::array()))if(grant.is_object())for(const auto& spell:strings(grant,"/spells"))require(j,spell,"spell");
    }
    return out;
}
} // namespace dnd::srd55v2
