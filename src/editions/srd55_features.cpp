#include "srd55_v2_internal.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace dnd::srd55v2 {
namespace {
const std::array<std::string,6> abilityNames={"strength","dexterity","constitution","intelligence","wisdom","charisma"};
const std::map<std::string,std::string> skillAbilities={{"acrobatics","dexterity"},{"animal-handling","wisdom"},{"arcana","intelligence"},{"athletics","strength"},{"deception","charisma"},{"history","intelligence"},{"insight","wisdom"},{"intimidation","charisma"},{"investigation","intelligence"},{"medicine","wisdom"},{"nature","intelligence"},{"perception","wisdom"},{"performance","charisma"},{"persuasion","charisma"},{"religion","intelligence"},{"sleight-of-hand","dexterity"},{"stealth","dexterity"},{"survival","wisdom"}};
int level(const Context& x,const std::string& cls){return profileLevel(x,cls);}
int mod(const Context& x,const std::string& a){auto it=x.modifiers.find(a);return it==x.modifiers.end()?0:it->second;}
int score(const Context& x,const std::string& a){auto it=x.scores.find(a);return it==x.scores.end()?10:it->second;}
int table(const Context& x,const std::string& cls,const std::string& column,int fallback){const auto* data=x.rules.find(selectedClassId(x,cls));if(!data)return fallback;const auto* value=at(*data,"/progression/"+column+"/"+std::to_string(level(x,cls)-1));return value&&value->is_number_integer()?value->get<int>():fallback;}
int tableDie(const Context& x,const std::string& cls,const std::string& column,int fallback){const auto* data=x.rules.find(selectedClassId(x,cls));if(!data)return fallback;const auto value=text(*data,"/progression/"+column+"/"+std::to_string(level(x,cls)-1));const auto d=value.find('d');if(d==std::string::npos)return fallback;try{return std::stoi(value.substr(d+1));}catch(...){return fallback;}}
std::string slug(const std::string& id){auto at=id.find(':');return at==std::string::npos?id:id.substr(at+1);}
bool active(const Context& x,const std::string& id){const auto* p=at(x.document.resources,"/effects/"+id);return p && (p->is_boolean()?p->get<bool>():p->is_object() && p->value("active",false));}
bool incapacitated(const Context& x){return active(x,"incapacitated") || active(x,"unconscious");}
bool subclass(const Context& x,const std::string& cls){return selectedSubclass(x,cls)!=nullptr;}
int spellLevel(const Json& item){return item.value("spellLevel",item.value("level",0));}
bool inList(const Json& item,const std::string& cls){for(const auto& key:{"lists","classes","classLists","spellLists"})if(item.contains(key) && item[key].is_array())for(const auto& value:item[key])if(value==cls || value=="srd55:"+cls)return true;return false;}
std::string name(const Context& x,const std::string& id){const auto* item=x.rules.find(id);return item?item->value("name",id):id;}
std::vector<Choice> constants(std::initializer_list<std::pair<const char*,const char*>> values,const std::string& page){std::vector<Choice> out;for(const auto& [id,label]:values)out.push_back({id,label,true,{}, {srd55v2::ref(page)}});return out;}
struct Builder {
    const Context& x;FeatureResult& out;std::string cls,page;int l;Stage stage;
    Builder(const Context& context,FeatureResult& result,std::string c,std::string p,bool current=false):x(context),out(result),cls(std::move(c)),page(std::move(p)),l(level(context,cls)),stage{(current?"current-features-":"features-")+cls,cls+(current?" current feature state":" class features"),{}}{}
    ~Builder(){if(!stage.fields.empty())out.evaluation.stages.push_back(stage);}
    std::string path(const std::string& key)const{return "/features/"+cls+"/"+key;}
    int characterLevelAt(int classLevel)const{const auto events=x.classLevelEvents.find(selectedClassId(x,cls));return events!=x.classLevelEvents.end()&&classLevel>0&&events->second.size()>=static_cast<std::size_t>(classLevel)?events->second.at(static_cast<std::size_t>(classLevel-1)):classLevel;}
    std::vector<SourceRef> sources(const std::string& printedPage="")const {
        std::vector<SourceRef> result={srd55v2::ref(printedPage.empty()?page:printedPage)};
        if(const auto* definition=x.rules.find(selectedClassId(x,cls)))result.push_back(sourceFromJson(definition->at("source")));
        if(const auto* definition=selectedSubclass(x,cls))result.push_back(sourceFromJson(definition->at("source")));
        return result;
    }
    void armorFormula(const std::string& label, int base, const std::vector<std::string>& abilities, const std::string& source) {
        int total=base; std::string arithmetic=std::to_string(base);
        CalculationTrace trace; trace.sources=sources(source);
        for(const auto& ability:abilities){const int modifier=mod(x,ability);total+=modifier;arithmetic+=" + "+ability+" modifier ("+std::to_string(modifier)+")";}
        trace.steps.push_back(label+": "+arithmetic+" = "+std::to_string(total)+".");
        out.armorFormulas[label]=total;out.armorFormulaTraces[label]=std::move(trace);out.armorFormulaAbilities[label]=abilities;
    }
    void speedBonus(const std::string& label,int value,const std::string& source) {
        out.speedBonus+=value;out.speedTrace.steps.push_back(label+": +"+std::to_string(value)+" feet.");
        const auto refs=sources(source);out.speedTrace.sources.insert(out.speedTrace.sources.end(),refs.begin(),refs.end());
    }
    void skillBonus(const std::string& skill,const std::string& label,int value,const std::string& arithmetic,const std::string& source) {
        out.skillBonuses[skill]+=value;auto& trace=out.skillBonusTraces[skill];
        out.skillBonusAbilities[skill]={"wisdom"}; // Both supported knowledge-order bonuses use Wisdom.
        trace.steps.push_back(label+": "+arithmetic+" = "+std::to_string(value)+".");
        const auto refs=sources(source);trace.sources.insert(trace.sources.end(),refs.begin(),refs.end());
    }
    void calc(const std::string& key,const std::string& label,Json value,const std::string& reason,const std::string& source=""){addCalculation(out.evaluation,"feature."+cls+"."+key,label,std::move(value),{reason},sources(source));}
    void mode(const std::string& id,const std::string& label,Json mechanics,const std::string& condition,const std::string& source=""){
        const auto p=source.empty()?page:source;mechanics["id"]=cls+":"+id;mechanics["label"]=label;mechanics["condition"]=condition;mechanics["source"]={{"publication","System Reference Document 5.2.1"},{"page",p},{"url","https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf"}};out.attackModes.push_back(mechanics);calc(id,label,mechanics,condition,p);
    }
    std::string single(const std::string& key,const std::string& label,std::vector<Choice> choices,bool required=true){auto f=select(path(key),label,std::move(choices));stage.fields.push_back(f);return pick(out.evaluation,x.document.choices,f,required);}
    std::vector<std::string> multi(const std::string& key,const std::string& label,std::vector<Choice> choices,int count){auto f=select(path(key),label,std::move(choices),true);stage.fields.push_back(f);return picks(out.evaluation,x.document.choices,f,count);}
    void resource(const std::string& key,const std::string& label,int maximum,const std::string& recharge,const std::string& detail=""){
        const auto id=cls+":"+key;out.evaluation.resources.push_back({id,label,maximum,recharge,sources()});
        const int current=number(x.document.resources,"/"+id,maximum);calc("resource-"+key,label,{{"maximum",maximum},{"current",current},{"recharge",recharge}},detail.empty()?"Derived capacity; current uses are saved separately and are never changed by evaluation.":detail);
        if(current<0 || current>maximum)issue(out.evaluation,"srd55.resource.capacity","/resources/"+id,"Saved resource lies outside its current capacity; value is preserved.",page,"warning");
    }
    void currentNumber(const std::string& key,const std::string& label,int maximum,const std::string& help){Field f=integer("/"+cls+":"+key,label,0,maximum,help);f.scope="resources";f.advanced=true;stage.fields.push_back(f);}
    void effectSwitch(const std::string& key,const std::string& label){Field f;f.path="/effects/"+cls+":"+key;f.label=label;f.kind="boolean";f.advanced=true;f.help="Saved current effect; activation and spending its cost are explicit actions outside recalculation.";f.scope="resources";stage.fields.push_back(f);}
    void grant(const std::string& spell,const std::string& ability,const std::string& reason,int freeUses=0,const std::string& recharge="long-rest"){
        const auto id=spell.starts_with("srd55:")?spell:"srd55:"+spell;
        if(!x.rules.find(id)){issue(out.evaluation,"srd55.feature.spell_missing",path("spells"),"Required feature spell '"+id+"' is unavailable; no substitute is selected.",page);return;}
        out.spellGrants.push_back({{"spellId",id},{"classId",selectedClassId(x,cls)},{"ability",ability},{"reason",reason},{"freeUses",freeUses},{"recharge",recharge},{"source",{{"publication","System Reference Document 5.2.1"},{"page",page},{"url","https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf"}}}});
    }
    std::vector<Choice> spells(const std::vector<std::string>& lists,int minimum,int maximum,const std::set<std::string>* known=nullptr){
        std::vector<Choice> result;for(const auto& [id,item]:x.rules.content)if(item.value("kind","")=="spell" && spellLevel(item)>=minimum && spellLevel(item)<=maximum){bool match=lists.empty();for(const auto& list:lists)match=match||inList(item,list);if(!match)continue;bool available=!known || known->contains(id);result.push_back({id,item.value("name",id),available,available?"":"This spell must be in your spellbook.",{sourceFromJson(item.at("source"))}});}return result;
    }
    void skills(const std::string& key,const std::string& label,int count,bool expertise,int grantClassLevel,const std::set<std::string>& allowed={}){
        const int grantCharacterLevel=characterLevelAt(grantClassLevel);
        auto acquisition=[&](const std::string& skill){
            int earliest=999;
            if(x.skillProficiencies.contains(skill)){const auto found=x.skillAcquisitionLevels.find(skill);earliest=found==x.skillAcquisitionLevels.end()?1:found->second;}
            if(out.skillProficiencies.contains(skill)){const auto found=out.skillAcquisitionLevels.find(skill);earliest=std::min(earliest,found==out.skillAcquisitionLevels.end()?1:found->second);}
            return earliest;
        };
        auto choices=options(x.rules,"skill");
        for(auto& o:choices){
            if(!allowed.empty()&&!allowed.contains(slug(o.id))&&!allowed.contains(o.id)){o.available=false;o.reason="This skill is outside the feature's skill list.";}
            const int acquired=acquisition(o.id);const bool proficientAtGrant=acquired<=grantCharacterLevel;
            if(!expertise&&proficientAtGrant){o.available=false;o.reason="Already proficient at this feature's acquisition; choose an additional skill.";}
            if(expertise&&!proficientAtGrant){o.available=false;o.reason=acquired==999?"Expertise requires proficiency in this skill when the feature is gained.":"Requires proficiency by character level "+std::to_string(grantCharacterLevel)+"; this skill is acquired at character level "+std::to_string(acquired)+".";}
            if(expertise&&(x.expertise.contains(o.id)||out.expertise.contains(o.id))){o.available=false;o.reason="Expertise is already granted by another feature.";}
        }
        for(const auto& id:multi(key,label,std::move(choices),count)){
            if(expertise){out.expertise.insert(id);out.expertiseSources[id]=sources();}
            else {out.skillProficiencies.insert(id);out.skillProficiencySources[id]=sources();const auto previous=out.skillAcquisitionLevels.find(id);if(previous==out.skillAcquisitionLevels.end())out.skillAcquisitionLevels[id]=grantCharacterLevel;else previous->second=std::min(previous->second,grantCharacterLevel);}
        }
    }
};
void spellTable(Builder& b,const std::string& ability,const std::vector<std::pair<int,std::vector<std::string>>>& rows,const std::string& reason){for(const auto& [level,spells]:rows)if(b.l>=level)for(const auto& spell:spells)b.grant(spell,ability,reason);}
std::vector<Choice> styleOptions(const Context& x){std::vector<Choice> result;for(auto id:{"srd55:archery","srd55:defense","srd55:great-weapon-fighting","srd55:two-weapon-fighting"})if(const auto* entry=x.rules.find(id))result.push_back({id,entry->value("name",id),!x.feats.contains(id),x.feats.contains(id)?"This Fighting Style is already owned.":"", {sourceFromJson(entry->at("source"))}});return result;}
void style(Builder& b,int count,bool casterAlternative=false){std::set<std::string> selected;for(int i=0;i<count;++i){auto choices=styleOptions(b.x);if(casterAlternative)choices.push_back({b.cls=="paladin"?"blessed-warrior":"druidic-warrior",b.cls=="paladin"?"Blessed Warrior":"Druidic Warrior",true,{}, {srd55v2::ref(b.page)}});for(auto& c:choices)if(selected.contains(c.id)||b.out.feats.contains(c.id)){c.available=false;c.reason="Choose a different Fighting Style.";}const auto id=b.single("fightingStyle/"+std::to_string(i),"Fighting Style "+std::to_string(i+1),choices);selected.insert(id);if(id.starts_with("srd55:"))b.out.feats.insert(id);else if(id=="blessed-warrior"||id=="druidic-warrior"){for(const auto& spell:b.multi("warriorCantrips","Warrior cantrips",b.spells({b.cls=="paladin"?"cleric":"druid"},0,0),2))b.grant(spell,b.cls=="paladin"?"charisma":"wisdom",id);}}}
int invocationCapacity(int l){return l>=18?10:l>=15?9:l>=12?8:l>=9?7:l>=7?6:l>=5?5:l>=2?3:1;}
std::set<std::string> invocationIds(const Context& x){auto ids=strings(x.document.choices,"/features/warlock/invocations");std::set<std::string> result;for(const auto& id:ids)result.insert(slug(id));return result;}
int invocationMinimum(const std::string& key){if(key=="witch-sight")return 15;if(key=="devouring-blade")return 12;if(key=="gift-of-the-protectors"||key=="lifedrinker"||key=="visions-of-distant-realms")return 9;if(key=="whispers-of-the-grave")return 7;if(key=="ascendant-step"||key=="eldritch-smite"||key=="gaze-of-two-minds"||key=="gift-of-the-depths"||key=="investment-of-the-chain-master"||key=="master-of-myriad-forms"||key=="one-with-shadows"||key=="thirsting-blade")return 5;if(key=="armor-of-shadows"||key=="eldritch-mind"||key.starts_with("pact-of-the-"))return 1;return 2;}
std::string invocationDependency(const std::string& key){if(key=="devouring-blade")return "thirsting-blade";if(key=="eldritch-smite"||key=="lifedrinker"||key=="thirsting-blade")return "pact-of-the-blade";if(key=="gift-of-the-protectors")return "pact-of-the-tome";if(key=="investment-of-the-chain-master")return "pact-of-the-chain";return {};}
}

FeatureResult advancementAbilityGrant(const std::string& profile,int classLevel){
    FeatureResult result;if(classLevel!=20)return result;
    if(profile=="barbarian"){result.abilityBonuses={{"strength",4},{"constitution",4}};result.abilityCaps={{"strength",25},{"constitution",25}};}
    if(profile=="monk"){result.abilityBonuses={{"dexterity",4},{"wisdom",4}};result.abilityCaps={{"dexterity",25},{"wisdom",25}};}
    return result;
}

FeatureResult resolveClassChoices(const Context& sourceX){
    // Later traversal order must not hide an earlier, selected Lessons/Skilled
    // proficiency from Bard, Ranger, or Rogue Expertise. The actual grant is
    // validated and emitted once in the Warlock block below.
    Context x=sourceX;
    if(level(x,"warlock")>=2){
        int acquiredAt=2;const auto events=x.classLevelEvents.find(selectedClassId(x,"warlock"));if(events!=x.classLevelEvents.end()&&events->second.size()>=2)acquiredAt=events->second[1];
        const auto invocations=strings(x.document.choices,"/features/warlock/invocations");
        for(std::size_t i=0;i<invocations.size()&&i<static_cast<std::size_t>(invocationCapacity(level(x,"warlock")));++i){
            if(invocations[i]!="srd55:lessons-of-the-first-ones"||text(x.document.choices,"/features/warlock/invocationTargets/"+std::to_string(i))!="srd55:skilled")continue;
            const auto acquisition=x.choiceAcquisitionLevels.find("/features/warlock/invocations/"+std::to_string(i));const int grantAt=acquisition==x.choiceAcquisitionLevels.end()?acquiredAt:acquisition->second;
            for(const auto& id:strings(x.document.choices,"/features/warlock/invocationGrants/"+std::to_string(i)+"/skilledChoices")){
                const auto* entry=x.rules.find(id);if(!entry||entry->value("kind","")!="skill")continue;
                const bool existed=x.skillProficiencies.contains(id);x.skillProficiencies.insert(id);const auto earlier=x.skillAcquisitionLevels.find(id);
                if(earlier==x.skillAcquisitionLevels.end())x.skillAcquisitionLevels[id]=existed?1:grantAt;else earlier->second=std::min(earlier->second,grantAt);
            }
        }
    }
    FeatureResult out;
    for(const auto& [classId,classLevel]:x.classLevels){const auto grant=advancementAbilityGrant(classProfile(x.rules,classId),classLevel);for(const auto& [ability,bonus]:grant.abilityBonuses)out.abilityBonuses[ability]+=bonus;for(const auto& [ability,cap]:grant.abilityCaps)out.abilityCaps[ability]=cap;}

    if(int l=level(x,"barbarian")){
        Builder b(x,out,"barbarian","28-30");if(l>=3)b.skills("primalKnowledge","Primal Knowledge skill",1,false,3,{"animal-handling","athletics","intimidation","nature","perception","survival"});

    }
    if(int l=level(x,"bard")){
        Builder b(x,out,"bard","31-35");if(l>=2)b.skills("expertise2","Bard level 2 Expertise",2,true,2);
        if(subclass(x,"bard"))b.skills("loreSkills","College of Lore bonus skills",3,false,3);if(l>=9)b.skills("expertise9","Bard level 9 Expertise",2,true,9);
        if(subclass(x,"bard") && l>=6)for(const auto& spell:b.multi("magicalDiscoveries","Magical Discoveries",b.spells({"cleric","druid","wizard"},0,std::min(9,(l+1)/2)),2))b.grant(spell,"charisma","Lore: Magical Discoveries");
    }
    if(int l=level(x,"cleric")){
        Builder b(x,out,"cleric","37-40");const auto order=b.single("divineOrder","Divine Order",constants({{"protector","Protector"},{"thaumaturge","Thaumaturge"}},"37"));
        if(order=="protector"){out.weaponTraining.insert("martial");out.armorTraining.insert("heavy");}else if(order=="thaumaturge")out.cantripBonuses[selectedClassId(x,"cleric")]++;
        if(l>=7)b.single("blessedStrikes","Blessed Strikes",constants({{"divine-strike","Divine Strike"},{"potent-spellcasting","Potent Spellcasting"}},"38"));
    }
    if(int l=level(x,"druid")){
        Builder b(x,out,"druid","42-46");out.languages.insert("srd55:language-druidic");const auto order=b.single("primalOrder","Primal Order",constants({{"magician","Magician"},{"warden","Warden"}},"42"));
        if(order=="warden"){out.weaponTraining.insert("martial");out.armorTraining.insert("medium");}else if(order=="magician")out.cantripBonuses[selectedClassId(x,"druid")]++;
        if(l>=7)b.single("elementalFury","Elemental Fury",constants({{"primal-strike","Primal Strike"},{"potent-spellcasting","Potent Spellcasting"}},"43"));
        if(subclass(x,"druid"))b.single("land","Circle land (change after a Long Rest)",constants({{"arid","Arid"},{"polar","Polar"},{"temperate","Temperate"},{"tropical","Tropical"}},"46"));
        if(l>=2){const double maximum=l>=8?1.0:l>=4?.5:.25;std::vector<Choice> choices;for(const auto& [id,item]:x.rules.content)if(item.value("kind","")=="creature" && item.value("type","")=="Beast"){
            const auto speed=item.value("speed",Json::object());const bool valid=item.value("cr",99.0)<=maximum && (l>=8||speed.value("fly",0)==0);choices.push_back({id,item.value("name",id),valid,valid?"":"Wild Shape CR or Fly Speed exceeds the class-level limit.",{sourceFromJson(item.at("source"))}});}
            const auto forms=b.multi("forms","Known Wild Shape forms",choices,l>=8?8:l>=4?6:4);Field form=select("/wildShapeForm","Current Wild Shape form",choices);form.scope="resources";form.advanced=true;b.stage.fields.push_back(form);
            Field equipment=select("/formEquipment","Equipment in Wild Shape",constants({{"merged","Merged (no equipment effects)"},{"dropped","Dropped"},{"worn","Worn if the form can use it"}},"43"));equipment.scope="resources";equipment.advanced=true;b.stage.fields.push_back(equipment);
            const auto formId=text(x.document.resources,"/wildShapeForm");if(!formId.empty()){
                const auto* beast=x.rules.find(formId);const bool known=std::find(forms.begin(),forms.end(),formId)!=forms.end();
                if(!beast || !known)issue(out.evaluation,"srd55.form.unavailable","/resources/wildShapeForm","The active form must be an eligible known Beast; the saved choice is retained.","42-43");
                else {out.transformedForm=*beast;for(const auto& ability:{"strength","dexterity","constitution"})out.physicalAbilityOverrides[ability]=beast->at("abilities").value(ability,10);}
            }
        }
    }
    if(int l=level(x,"fighter")){Builder b(x,out,"fighter","47-49");style(b,subclass(x,"fighter")&&l>=7?2:1);}
    if(int l=level(x,"monk")){
        if(l>=14)out.saveProficiencies.insert(abilityNames.begin(),abilityNames.end());

    }
    if(level(x,"paladin")>=2){Builder b(x,out,"paladin","54-57");style(b,1,true);}
    if(int l=level(x,"ranger")){
        Builder b(x,out,"ranger","59-61");if(l>=2){style(b,1,true);b.skills("expertise2","Deft Explorer Expertise",1,true,2);for(const auto& id:b.multi("languages","Deft Explorer languages",options(x.rules,"language"),2))out.languages.insert(id);}if(l>=9)b.skills("expertise9","Ranger level 9 Expertise",2,true,9);
        if(subclass(x,"ranger")){b.single("huntersPrey","Hunter's Prey",constants({{"colossus-slayer","Colossus Slayer"},{"horde-breaker","Horde Breaker"}},"61"));if(l>=7)b.single("defensiveTactics","Defensive Tactics",constants({{"escape-the-horde","Escape the Horde"},{"multiattack-defense","Multiattack Defense"}},"61"));}
    }
    if(int l=level(x,"rogue")){
        Builder b(x,out,"rogue","61-64");b.skills("expertise1","Rogue level 1 Expertise",2,true,1);if(l>=6)b.skills("expertise6","Rogue level 6 Expertise",2,true,6);
        const auto language=b.single("language","Additional Thieves' Cant language",options(x.rules,"language"));if(!language.empty())out.languages.insert(language);out.languages.insert("srd55:language-thieves-cant");
        if(l>=15){out.saveProficiencies.insert("wisdom");out.saveProficiencies.insert("charisma");}
    }
    if(int l=level(x,"sorcerer")){
        Builder b(x,out,"sorcerer","65-70");if(l>=2)b.multi("metamagic","Metamagic options",options(x.rules,"metamagic"),l>=17?6:l>=10?4:2);
        if(subclass(x,"sorcerer")&&l>=6)b.single("elementalAffinity","Draconic Elemental Affinity",constants({{"acid","Acid"},{"cold","Cold"},{"fire","Fire"},{"lightning","Lightning"},{"poison","Poison"}},"70"));
    }
    if(int l=level(x,"warlock")){
        Builder b(sourceX,out,"warlock","71-76");const auto all=options(x.rules,"invocation");const auto selected=strings(x.document.choices,b.path("invocations"));const auto ids=invocationIds(x);std::set<std::string> unique,repeatTargets;
        for(int i=0;i<invocationCapacity(l);++i){auto choices=all;for(auto& o:choices){const auto key=slug(o.id),dependency=invocationDependency(key);if(l<invocationMinimum(key)){o.available=false;o.reason="Requires Warlock level "+std::to_string(invocationMinimum(key))+".";}else if(!dependency.empty()&&!ids.contains(dependency)){o.available=false;o.reason="Requires the "+dependency+" invocation.";}}
            const auto id=b.single("invocations/"+std::to_string(i),"Eldritch Invocation "+std::to_string(i+1),choices);if(id.empty())continue;const auto key=slug(id);const bool repeat=key=="agonizing-blast"||key=="eldritch-spear"||key=="repelling-blast"||key=="lessons-of-the-first-ones";
            if(!unique.insert(key).second&&!repeat)issue(out.evaluation,"srd55.invocation.duplicate",b.path("invocations"),"This invocation is not repeatable.","72-74");
            if(key=="agonizing-blast"||key=="eldritch-spear"||key=="repelling-blast"){
                const auto known=strings(x.document.choices,"/spellcasting/warlock/cantrips");auto opts=b.spells({"warlock"},0,0);
                for(auto& opt:opts){const auto* spell=x.rules.find(opt.id);bool damage=spell&&spell->value("dealsDamage",false);bool valid=damage&&std::find(known.begin(),known.end(),opt.id)!=known.end();if(key=="repelling-blast")valid=valid&&spell&&spell->value("requiresAttackRoll",false);if(key=="eldritch-spear")valid=valid&&spell&&spell->value("rangeFeet",0)>=10;if(!valid){opt.available=false;opt.reason="Requires a known Warlock damaging cantrip (and an attack roll for Repelling Blast).";}}
                const auto target=b.single("invocationTargets/"+std::to_string(i),"Cantrip for "+name(x,id),opts);if(!repeatTargets.insert(key+":"+target).second)issue(out.evaluation,"srd55.invocation.target_duplicate",b.path("invocationTargets"),"Repeated invocations must affect distinct cantrips.","72-74");
            }else if(key=="lessons-of-the-first-ones"){
                auto opts=options(x.rules,"feat");opts.erase(std::remove_if(opts.begin(),opts.end(),[&](const auto& opt){const auto* feat=x.rules.find(opt.id);return !feat || (feat->value("category","")!="origin" && feat->value("featCategory","")!="origin");}),opts.end());for(auto& option:opts){const auto* existing=x.rules.find(option.id);if(x.feats.contains(option.id)&&existing&&(!existing->value("repeatable",false)||existing->value("repeatGroup","")=="magic-initiate")){option.available=false;option.reason=existing->value("repeatGroup","")=="magic-initiate"?"Magic Initiate is already owned for this spell list; choose a different list.":"This nonrepeatable Origin feat is already owned.";}}const auto feat=b.single("invocationTargets/"+std::to_string(i),"Origin feat from Lessons of the First Ones",opts);if(!repeatTargets.insert(key+":"+feat).second)issue(out.evaluation,"srd55.invocation.target_duplicate",b.path("invocationTargets"),"Each Lessons invocation must grant a different Origin feat.","73");if(!feat.empty()){
                    const auto recorded=sourceX.choiceAcquisitionLevels.find("/features/warlock/invocations/"+std::to_string(i));const int acquiredAt=recorded==sourceX.choiceAcquisitionLevels.end()?b.characterLevelAt(2):recorded->second;const auto grantKey="invocationGrants/"+std::to_string(i);const auto grantPath=b.path(grantKey);
                    out.featGrants.push_back({{"id",feat},{"path",grantPath},{"level",acquiredAt},{"source",{{"publication","System Reference Document 5.2.1"},{"page","73"},{"url","https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=73"}}},{"acquisitionAssumption",recorded==sourceX.choiceAcquisitionLevels.end()?"earliest-warlock-level-2":"recorded-history"}});
                    if(feat=="srd55:skilled"){
                        auto choices=options(x.rules,"skill");const auto toolOptions=options(x.rules,"tool");choices.insert(choices.end(),toolOptions.begin(),toolOptions.end());
                        for(auto& option:choices){const auto* entry=x.rules.find(option.id);const bool skill=entry&&entry->value("kind","")=="skill";int earliest=999;
                            if(skill){if(sourceX.skillProficiencies.contains(option.id))earliest=sourceX.skillAcquisitionLevels.contains(option.id)?sourceX.skillAcquisitionLevels.at(option.id):1;if(out.skillProficiencies.contains(option.id))earliest=std::min(earliest,out.skillAcquisitionLevels.contains(option.id)?out.skillAcquisitionLevels.at(option.id):1);}
                            else {if(sourceX.toolProficiencies.contains(option.id))earliest=sourceX.toolAcquisitionLevels.contains(option.id)?sourceX.toolAcquisitionLevels.at(option.id):1;if(out.toolProficiencies.contains(option.id))earliest=std::min(earliest,acquiredAt);}
                            if(earliest<=acquiredAt){option.available=false;option.reason="Already proficient when this Skilled grant is acquired; choose a different skill or tool.";}
                        }
                        for(const auto& trained:b.multi(grantKey+"/skilledChoices","Skilled from invocation "+std::to_string(i+1)+": three proficiencies",choices,3)){
                            const auto* entry=x.rules.find(trained);if(!entry)continue;
                            if(entry->value("kind","")=="skill"){out.skillProficiencies.insert(trained);const auto previous=out.skillAcquisitionLevels.find(trained);if(previous==out.skillAcquisitionLevels.end())out.skillAcquisitionLevels[trained]=acquiredAt;else previous->second=std::min(previous->second,acquiredAt);}
                            else if(entry->value("kind","")=="tool")out.toolProficiencies.insert(trained);
                        }
                    }
                }
            }
        }
        if(static_cast<int>(selected.size())>invocationCapacity(l))issue(out.evaluation,"srd55.invocation.count",b.path("invocations"),"More invocations are retained than this Warlock level permits.","71");
        if(ids.contains("pact-of-the-tome")){std::set<std::string> alreadyPrepared=x.preparedSpells;
            for(const auto& grant:out.spellGrants)if(grant.contains("spellId")&&grant["spellId"].is_string())alreadyPrepared.insert(grant["spellId"].get<std::string>());
            for(const auto& grant:out.featGrants){const auto* feat=x.rules.find(grant.value("id",""));if(!feat||!feat->contains("spellList"))continue;const auto base=grant.value("path","");const auto spell=text(x.document.choices,base+"/spell");if(!spell.empty())alreadyPrepared.insert(spell);for(const auto& cantrip:strings(x.document.choices,base+"/cantrips"))alreadyPrepared.insert(cantrip);}

            // These always-prepared class grants are emitted in the second pass,
            // but already exist when Book of Shadows spells are selected.
            if(level(x,"druid")>=1)alreadyPrepared.insert("srd55:speak-with-animals");
            if(level(x,"ranger")>=1)alreadyPrepared.insert("srd55:hunters-mark");
            if(level(x,"paladin")>=2)alreadyPrepared.insert("srd55:divine-smite");
            if(level(x,"paladin")>=5)alreadyPrepared.insert("srd55:find-steed");
            if(level(x,"warlock")>=9)alreadyPrepared.insert("srd55:contact-other-plane");
            if(level(x,"bard")>=20){alreadyPrepared.insert("srd55:power-word-heal");alreadyPrepared.insert("srd55:power-word-kill");}
            for(const auto& [classId,classLevel]:x.classLevels){(void)classLevel;for(const auto& key:{"cantrips","preparedSpells"})for(const auto& id:strings(x.document.choices,"/spellcasting/"+classProfile(x.rules,classId)+"/"+key))alreadyPrepared.insert(id);}auto tomeCantrips=b.spells({},0,0);for(auto& o:tomeCantrips)if(alreadyPrepared.contains(o.id)){o.available=false;o.reason="Book of Shadows must grant a spell you do not already have prepared.";}for(const auto& spell:b.multi("tomeCantrips","Book of Shadows cantrips",tomeCantrips,3))b.grant(spell,"charisma","Pact of the Tome");auto opts=b.spells({},1,1);for(auto& o:opts)if(alreadyPrepared.contains(o.id)){o.available=false;o.reason="Book of Shadows must grant a spell you do not already have prepared.";}for(auto& o:opts){const auto* entry=x.rules.find(o.id);if(!entry->value("ritual",false)){o.available=false;o.reason="Pact of the Tome requires first-level Ritual spells.";}}for(const auto& spell:b.multi("tomeRituals","Book of Shadows rituals",opts,2))b.grant(spell,"charisma","Pact of the Tome");}
        if(ids.contains("pact-of-the-blade")){auto opts=options(x.rules,"weapon");for(auto& o:opts)if(x.rules.find(o.id)->value("ranged",false)&&!x.rules.find(o.id)->value("magical",false)){o.available=false;o.reason="A conjured pact weapon must be Melee; bonding a ranged weapon requires a magic weapon.";}const auto weapon=b.single("pactWeapon","Bonded Pact weapon",opts);if(!weapon.empty())out.weaponTraining.insert(weapon);}
        if(subclass(x,"warlock")&&l>=10)b.single("fiendishResilience","Fiendish Resilience damage type",constants({{"acid","Acid"},{"bludgeoning","Bludgeoning"},{"cold","Cold"},{"fire","Fire"},{"lightning","Lightning"},{"necrotic","Necrotic"},{"piercing","Piercing"},{"poison","Poison"},{"psychic","Psychic"},{"radiant","Radiant"},{"slashing","Slashing"},{"thunder","Thunder"}},"76"));
        for(const auto& [threshold,sl]:std::vector<std::pair<int,int>>{{11,6},{13,7},{15,8},{17,9}})if(l>=threshold){const auto spell=b.single("arcanum/"+std::to_string(sl),"Mystic Arcanum: spell level "+std::to_string(sl),b.spells({"warlock"},sl,sl));if(!spell.empty())b.grant(spell,"charisma","Mystic Arcanum",1);}
    }
    if(int l=level(x,"wizard")){
        Builder b(x,out,"wizard","78-82");
        if(l>=2)b.skills("scholar","Scholar Expertise",1,true,2,{"arcana","history","investigation","medicine","nature","religion"});
        const auto acquired=wizardAcquiredSpells(x);
        const auto books=resolveWizardSpellbooks(x.document,x.rules);
        if(l>=18)for(int sl:{1,2}){
            auto eligible=books.tracked?books.accessibleSpells:acquired;
            for(const auto& retained:wizardRetainedSelection(x.document,b.path("spellMastery/"+std::to_string(sl))))
                if(acquired.contains(retained))eligible.insert(retained);
            auto opts=b.spells({"wizard"},sl,sl,&eligible);
            for(auto& o:opts){const auto* spell=x.rules.find(o.id);const auto casting=spell->value("castingTime","");if(casting!="Action" && casting!="1 action" && casting!="1 Action"){o.available=false;o.reason="Spell Mastery requires a casting time of an action.";}}
            const auto id=b.single("spellMastery/"+std::to_string(sl),"Spell Mastery level "+std::to_string(sl),opts);
            if(!id.empty())b.grant(id,"intelligence","Spell Mastery",-1);
        }
        if(l>=20){
            auto eligible=books.tracked?books.accessibleSpells:acquired;
            for(const auto& id:wizardRetainedSelection(x.document,b.path("signatureSpells")))if(acquired.contains(id))eligible.insert(id);
            for(const auto& id:b.multi("signatureSpells","Signature Spells",b.spells({"wizard"},3,3,&eligible),2))b.grant(id,"intelligence","Signature Spells",1,"short-rest");
        }
    }
    for(const auto& cls:{"barbarian","fighter","paladin","ranger","rogue"})if(level(x,cls)>0) {
        Builder b(x,out,cls,cls==std::string("barbarian")?"29":cls==std::string("fighter")?"48":cls==std::string("paladin")?"54":cls==std::string("ranger")?"58-59":"62");
        const int count=table(x,cls,"weaponMastery",cls==std::string("fighter")?3:2);auto opts=options(x.rules,"weapon");
        for(auto& option:opts){const auto* weapon=x.rules.find(option.id);const bool melee=!weapon->value("ranged",false);bool trained=x.weaponTraining.contains(option.id)||x.weaponTraining.contains(weapon->value("category",""));const auto properties=weapon->value("properties","");trained=trained||((x.weaponTraining.contains("martial-finesse")||x.weaponTraining.contains("martial-finesse-or-light"))&&weapon->value("finesse",false))||((x.weaponTraining.contains("martial-light")||x.weaponTraining.contains("martial-finesse-or-light"))&&properties.find("Light")!=std::string::npos);if(cls==std::string("barbarian")&&!melee){option.available=false;option.reason="Barbarian mastery choices are Melee weapons only.";}else if(!trained){option.available=false;option.reason="Weapon Mastery requires proficiency with this weapon.";}}
        const auto mastered=b.multi("weaponMasteries","Weapon Masteries",opts,count);b.calc("weaponMasteries","Weapon Masteries",mastered,"Only selected trained weapons receive their mastery properties. Replacement follows the class's rest rules.");
    }
    for(const auto& cls:{"druid","warlock"})if((cls==std::string("druid")&&level(x,cls)>=2)||(cls==std::string("warlock")&&invocationIds(x).contains("pact-of-the-chain"))) {
        Builder b(x,out,cls,cls==std::string("druid")?"43":"73-74");std::vector<Choice> choices={{"","No familiar currently summoned",true,{}, {srd55v2::ref(b.page)}}};
        for(const auto& [id,item]:x.rules.content)if(item.value("kind","")=="creature") {
            const bool normal=item.value("type","")=="Beast"&&item.value("cr",99.0)==0;const bool allowed=normal||(cls==std::string("warlock")&&item.value("chainFamiliar",false));if(allowed)choices.push_back({id,item.value("name",id),true,{}, {sourceFromJson(item.at("source"))}});
        }
        Field f=select("/familiars/"+std::string(cls)+"/form","Current "+std::string(cls)+" familiar",choices);f.scope="resources";f.advanced=true;b.stage.fields.push_back(f);
        if(cls==std::string("warlock")) {Field type=select("/familiars/warlock/type","Familiar spirit type",constants({{"Celestial","Celestial"},{"Fey","Fey"},{"Fiend","Fiend"}},"130"));type.scope="resources";type.advanced=true;b.stage.fields.push_back(type);const auto chosenType=text(x.document.resources,type.path,"Fey");if(chosenType!="Celestial"&&chosenType!="Fey"&&chosenType!="Fiend")issue(out.evaluation,"srd55.familiar.type",type.path,"A familiar spirit must be Celestial, Fey, or Fiend.","130");}
        if(cls==std::string("warlock")&&invocationIds(x).contains("investment-of-the-chain-master"))b.single("chainMovement","Investment of the Chain Master movement",constants({{"fly","Fly40 feet"},{"swim","Swim40 feet"}},"73"));
        const auto selected=text(x.document.resources,f.path);if(!selected.empty()&&std::none_of(choices.begin(),choices.end(),[&](const auto& o){return o.id==selected;}))issue(out.evaluation,"srd55.familiar.form",f.path,"This is not an eligible familiar form for the owning class feature.",b.page);
    }
    return out;
}

FeatureResult evaluateClassFeatures(const Context& x){
    FeatureResult out;
    const int strength=mod(x,"strength"),dexterity=mod(x,"dexterity"),constitution=mod(x,"constitution"),intelligence=mod(x,"intelligence"),wisdom=mod(x,"wisdom"),charisma=mod(x,"charisma");
    if(int l=level(x,"barbarian")){
        Builder b(x,out,"barbarian","28-30",true);const int rages=table(x,"barbarian","rages",l>=17?6:l>=12?5:l>=6?4:l>=3?3:2),damage=table(x,"barbarian","rageDamage",l>=16?4:l>=9?3:2);const bool rage=active(x,"barbarian:rage")&&x.armorCategory!="heavy"&&!(l>=15?active(x,"unconscious"):incapacitated(x));
        b.resource("rage","Rage uses",rages,"short-rest:1;long-rest:all");b.calc("rageDamage","Rage damage bonus",damage,"Applies only to Strength weapon attacks or Unarmed Strikes while Rage is active.");b.effectSwitch("rage","Rage active");
        b.mode("rage","Rage",{{"active",rage},{"damageBonus",damage},{"ability","strength"},{"resistances",{"bludgeoning","piercing","slashing"}},{"advantage",{"strength-checks","strength-saves"}},{"spellcastingAllowed",false},{"concentrationAllowed",false},{"durationMinutes",10}},"No Heavy armor; first requires active Rage. Before level15 extend every round and Incapacitated ends it; at15 Unconscious replaces Incapacitated.");
        if(!x.armored)b.armorFormula("Barbarian Unarmored Defense",10,{"dexterity","constitution"},"28");
        if(l>=2){b.mode("dangerSense","Danger Sense",{{"advantage",{"dexterity-saves"}},{"active",!incapacitated(x)}},"Unavailable while Incapacitated.");b.mode("recklessAttack","Reckless Attack",{{"attackAbility","strength"},{"advantage",true},{"attacksAgainstAdvantage",true}},"Choose on first attack of your turn; both benefits/risks last until your next turn.");}
        if(l>=3&&rage)for(const auto& key:{"acrobatics","intimidation","perception","stealth","survival"}){const std::string id="srd55:"+std::string(key);out.skillAbilityOverrides[id]="strength";out.skillAbilitySources[id]=b.sources("29");}
        if(l>=5){out.attackCount=std::max(out.attackCount,2);if(x.armorCategory!="heavy")b.speedBonus("Fast Movement",10,"29");}
        if(l>=7){b.mode("feralInstinct","Feral Instinct",{{"initiativeAdvantage",true}},"Always applies to Initiative rolls.");b.mode("instinctivePounce","Instinctive Pounce",{{"movementFraction",0.5},{"action","part-of-rage-bonus-action"}},"Move up to half Speed when entering Rage.");}
        if(l>=9){Json effects={{"forceful-blow",{{"pushFeet",15},{"followSpeedFraction",.5}}},{"hamstring-blow",{{"speedPenaltyFeet",15},{"duration","until-your-next-turn"}}}};if(l>=13){effects["staggering-blow"]={{"nextSaveDisadvantage",true},{"opportunityAttacks",false}};effects["sundering-blow"]={{"nextOtherCreatureAttackBonus",5}};}b.mode("brutalStrike","Brutal Strike",{{"damageDice",l>=17?"2d10":"1d10"},{"maximumEffects",l>=17?2:1},{"effects",effects}},"Once on your turn's chosen Strength attack: use Reckless Attack and forgo its Advantage; chosen roll cannot have Disadvantage. Different selected effects do not stack with themselves.");}
        if(l>=11){b.currentNumber("relentless-rage-attempts","Relentless Rage uses since last rest",100,"Each use raises DC by5; reset this counter to0 after a Short or Long Rest.");const int uses=number(x.document.resources,"/barbarian:relentless-rage-attempts",0);b.calc("relentlessRageDC","Relentless Rage current DC",10+5*uses,"DC10, +5 for each prior use since a Short or Long Rest.");b.calc("relentlessRageHp","Relentless Rage HP on success",2*l,"When reduced to0 HP in Rage without dying outright, Constitution save changes HP to twice Barbarian level.");}
        if(l>=15)b.resource("persistent-rage","Persistent Rage initiative recovery",1,"long-rest:all","At Initiative, spend this permission to restore all expended Rages; never automatic on refresh.");
        if(l>=18){out.statMinimums["strength-check"]=score(x,"strength");out.statMinimums["strength-save"]=score(x,"strength");b.calc("indomitableMight","Minimum Strength check/save total",score(x,"strength"),"Replace a lower total with the Strength score.");}
        if(subclass(x,"barbarian")){
            b.mode("frenzy","Berserker Frenzy",{{"extraDamageDice",std::to_string(damage)+"d6"}},"First target hit on your turn with a Strength attack, only when Reckless Attack and Rage are active.");
            if(l>=6)b.mode("mindlessRage","Mindless Rage",{{"active",rage},{"conditionImmunities",{"charmed","frightened"}}},"Only during Rage; entering Rage also ends these conditions.");
            if(l>=10)b.mode("retaliation","Retaliation",{{"action","reaction"},{"attacks",1},{"rangeFeet",5}},"After taking damage from a creature within5 feet, make one melee weapon or Unarmed attack against it.");
            if(l>=14){b.resource("intimidating-presence","Intimidating Presence",1,"long-rest:all","May restore by expending one Rage use.");b.mode("intimidatingPresence","Intimidating Presence",{{"saveDC",8+strength+x.proficiency},{"saveAbility","wisdom"},{"emanationFeet",30},{"condition","frightened"},{"durationMinutes",1},{"repeatSave","end-of-target-turn"}},"Bonus Action; spend one use, choose targets within30 feet.");}
        }
    }
    if(int l=level(x,"bard")){
        Builder b(x,out,"bard","31-35",true);const int die=tableDie(x,"bard","bardicDie",l>=15?12:l>=10?10:l>=5?8:6);const int uses=std::max(1,charisma);b.resource("inspiration","Bardic Inspiration",uses,l>=5?"short-rest:all;long-rest:all":"long-rest:all");b.calc("inspirationDie","Bardic Inspiration die",die,"d6 initially, d8 at5, d10 at10, d12 at15; die expires after1 hour and is expended on its roll.");
        b.mode("bardicInspiration","Bardic Inspiration",{{"action","bonus-action"},{"rangeFeet",60},{"dieSides",die},{"maximumUses",uses},{"durationMinutes",60}},"Another creature able to see/hear you receives one die; it can add the die after a failed D20 Test. Only one Inspiration die at a time.");
        if(l>=2){out.halfProficiency=x.proficiency/2;out.halfProficiencySources=b.sources("32");}
        if(l>=5)b.mode("fontOfInspiration","Font of Inspiration conversion",{{"spellSlotCost",1},{"restoreInspiration",1},{"action","none"}},"Expend any spell slot to regain one expended Inspiration use; Short Rests also restore all uses.");
        if(l>=7)b.mode("countercharm","Countercharm",{{"action","reaction"},{"rangeFeet",30},{"rerollAdvantage",true}},"You or an ally fails a save against Charmed/Frightened: reroll it with Advantage.");
        if(l>=10)b.calc("magicalSecrets","Magical Secrets lists",Json::array({"bard","cleric","druid","wizard"}),"New or replaced Bard preparations may use these lists; class-level spell limits still apply.");
        if(l>=18)b.mode("superiorInspiration","Superior Inspiration",{{"trigger","initiative"},{"restoreToMinimum",2}},"Regain expended uses until you have2, only if currently below2.");
        if(l==20){b.grant("power-word-heal","charisma","Words of Creation");b.grant("power-word-kill","charisma","Words of Creation");b.mode("wordsOfCreation","Words of Creation",{{"maximumTargets",2},{"secondaryWithinFeet",10}},"Power Word Heal or Power Word Kill can target a second creature within10 feet of the first.");}
        if(subclass(x,"bard")){b.mode("cuttingWords","Cutting Words",{{"action","reaction"},{"rangeFeet",60},{"subtractDie",die},{"resourceCost",{{"bard:inspiration",1}}}},"Visible creature succeeds on a check/attack or rolls damage; subtract the Inspiration die.");if(l>=14)b.mode("peerlessSkill","Peerless Skill",{{"addDie",die},{"resourceCost",{{"bard:inspiration",1}}},{"refundOnFailure",true}},"After failing your own ability check/attack; Inspiration is spent only if the addition succeeds.");}
    }
    if(int l=level(x,"cleric")){
        Builder b(x,out,"cleric","37-40",true);const auto order=text(x.document.choices,b.path("divineOrder"));if(order=="thaumaturge"){for(const auto& skill:{"srd55:arcana","srd55:religion"})b.skillBonus(skill,"Thaumaturge",std::max(1,wisdom),"max(1, Wisdom modifier "+std::to_string(wisdom)+")","37");b.calc("thaumaturgeKnowledge","Thaumaturge Arcana/Religion check bonus",std::max(1,wisdom),"Wisdom modifier, minimum+1, added to Intelligence Arcana/Religion checks; distinct from proficiency.");}
        if(l>=2){const int uses=table(x,"cleric","channelDivinity",l>=18?4:l>=6?3:2);b.resource("channel-divinity","Cleric Channel Divinity",uses,"short-rest:1;long-rest:all");b.mode("divineSpark","Divine Spark",{{"action","magic"},{"rangeFeet",30},{"dice",std::to_string(l>=18?4:l>=13?3:l>=7?2:1)+"d8"},{"bonus",wisdom},{"saveDC",8+wisdom+x.proficiency},{"saveAbility","constitution"},{"types",{"healing","necrotic","radiant"}},{"saveDamageFraction",.5},{"resourceCost",{{"cleric:channel-divinity",1}}}},"Another creature: restore HP or deal chosen damage; successful Constitution save halves damage.");b.mode("turnUndead","Turn Undead",{{"rangeFeet",30},{"saveDC",8+wisdom+x.proficiency},{"saveAbility","wisdom"},{"conditions",{"frightened","incapacitated"}},{"durationMinutes",1},{"resourceCost",{{"cleric:channel-divinity",1}}}},"Chosen Undead; ends on damage, your Incapacitated condition, or death.");}
        if(l>=5)b.calc("searUndead","Sear Undead radiant damage dice",std::to_string(std::max(1,wisdom))+"d8","Added against Undead failing Turn Undead; this damage does not end the turning effect.");
        if(l>=7){const auto choice=text(x.document.choices,b.path("blessedStrikes"));if(choice=="divine-strike")b.mode("divineStrike","Divine Strike",{{"extraDamageDice",l>=14?"2d8":"1d8"},{"types",{"necrotic","radiant"}}},"Once on your turn when a weapon attack hits.");else if(choice=="potent-spellcasting"){b.mode("potentSpellcasting","Potent Spellcasting",{{"classId",selectedClassId(x,"cleric")},{"spellLevel",0},{"damageBonus",wisdom}},"Add Wisdom to damage dealt by Cleric cantrips.");if(l>=14)b.calc("potentTemporaryHp","Improved Potent Spellcasting temporary HP",2*wisdom,"After dealing damage with a Cleric cantrip, grant this temporary HP to yourself or one creature within60 feet.");}}
        if(l>=10){const int cooldown=number(x.document.resources,"/cleric:divine-intervention-rests",0);if(l==20)b.currentNumber("divine-intervention-rests","Divine Intervention Long Rests remaining after Wish",8,"Record accepted2d4 result after using Wish; decrease by1 per completed Long Rest until0.");b.resource("divine-intervention","Divine Intervention",1,cooldown>0?"counted-long-rests:"+std::to_string(cooldown):"long-rest:all");b.mode("divineIntervention","Divine Intervention",{{"classList","cleric"},{"maximumSpellLevel",5},{"reactionSpellsAllowed",false},{"slotRequired",false},{"materialRequired",false},{"wishAllowed",l==20},{"remainingLongRestCooldown",cooldown},{"available",cooldown==0}},"Magic action. At20 may choose Wish instead; a Wish casting delays recovery for2d4 accepted Long Rests.");}
        if(subclass(x,"cleric")){
            spellTable(b,"wisdom",{{3,{"aid","bless","cure-wounds","lesser-restoration"}},{5,{"mass-healing-word","revivify"}},{7,{"aura-of-life","death-ward"}},{9,{"greater-restoration","mass-cure-wounds"}}},"Life Domain Spells");
            Json healing=Json::object();for(int sl=1;sl<=9;++sl)healing[std::to_string(sl)]=2+sl;b.mode("discipleOfLife","Disciple of Life",{{"healingBonusBySlotLevel",healing}},"A spell cast with a spell slot restores HP on its casting turn; add2+slot level to that creature's healing. Does not affect nonslot healing or later turns.");
            b.calc("preserveLife","Preserve Life shared healing pool",5*l,"Spend one Channel Divinity as a Magic action; divide HP among Bloodied creatures within30 feet, each capped at half its maximum HP.");
            if(l>=6)b.mode("blessedHealer","Blessed Healer",{{"selfHealingBySlotLevel",healing}},"Immediately after a slotted spell restores HP to someone other than you, heal yourself2+slot level once.");
            if(l>=17)b.mode("supremeHealing","Supreme Healing",{{"maximizeHealingDice",true},{"sources",{"spell","channel-divinity"}}},"Use highest possible result of healing dice for a spell or Channel Divinity; other sources are unaffected.");
        }
    }
    if(int l=level(x,"druid")){
        Builder b(x,out,"druid","42-46",true);b.grant("speak-with-animals","wisdom","Druidic");if(text(x.document.choices,b.path("primalOrder"))=="magician"){for(const auto& skill:{"srd55:arcana","srd55:nature"})b.skillBonus(skill,"Magician",std::max(1,wisdom),"max(1, Wisdom modifier "+std::to_string(wisdom)+")","42");b.calc("magicianKnowledge","Magician Arcana/Nature check bonus",std::max(1,wisdom),"Wisdom modifier, minimum+1, added to Intelligence Arcana/Nature checks; distinct from proficiency.");}
        if(l>=2){b.resource("wild-shape","Wild Shape",table(x,"druid","wildShape",l>=17?4:l>=6?3:2),"short-rest:1;long-rest:all");b.mode("wildShape","Wild Shape limits",{{"knownForms",l>=8?8:l>=4?6:4},{"maximumCR",l>=8?1.0:l>=4?.5:.25},{"flyAllowed",l>=8},{"durationHours",l/2.0},{"temporaryHP",l},{"keepOriginalHP",true}},"Bonus Action; chosen known Beast replaces physical scores/stat block but retains normal HP/HitDice, mental scores, features, feats, languages and proficiencies.");b.mode("wildCompanion","Wild Companion",{{"spellId","srd55:find-familiar"},{"castingAction","magic"},{"materialRequired",false},{"creatureType","Fey"},{"costOptions",{{"spellSlot",1},{"druid:wild-shape",1}}},{"expires","long-rest"}},"Expend a slot or Wild Shape use; familiar disappears at your next Long Rest.");}
        if(l>=5){b.resource("wild-resurgence-slot","Wild Resurgence slot conversion",1,"long-rest:all");b.mode("wildResurgence","Wild Resurgence",{{"slotToWildShape",1},{"requiresZeroWildShape",true},{"maximumPerTurn",1},{"wildShapeToSlotLevel",1},{"slotConversionDailyUses",1}},"No action. When no Wild Shape remains, spend any slot to restore1; opposite conversion consumes the once-per-Long-Rest permission.");}
        if(l>=7){const auto fury=text(x.document.choices,b.path("elementalFury"));if(fury=="primal-strike")b.mode("primalStrike","Primal Strike",{{"extraDamageDice",l>=15?"2d8":"1d8"},{"types",{"cold","fire","lightning","thunder"}}},"Once on your turn when a weapon or Wild Shape Beast attack hits.");else if(fury=="potent-spellcasting")b.mode("potentSpellcasting","Druid Potent Spellcasting",{{"classId",selectedClassId(x,"druid")},{"spellLevel",0},{"damageBonus",wisdom},{"rangeBonusFeet",l>=15?300:0},{"rangeBonusMinimumBaseFeet",10}},"Add Wisdom to Druid cantrip damage. At15, cantrips with base range10+ feet gain300 feet.");}
        if(l>=18)b.mode("beastSpells","Beast Spells",{{"castInBeastForm",true},{"costlyMaterialAllowed",false},{"consumedMaterialAllowed",false}},"While Wild Shaped, casting is allowed except costly or consumed Material components.");
        if(l==20){b.resource("nature-magician","Archdruid Nature Magician",1,"long-rest:all");b.mode("archdruid","Archdruid",{{"initiativeMinimumWildShape",1},{"spellLevelsPerWildShape",2},{"slotConversionsPerLongRest",1},{"agingMultiplier",.1}},"At Initiative with0 Wild Shapes regain1. Convert unexpended uses into one slot,2 spell levels each, once per Long Rest.");}
        if(subclass(x,"druid")){
            const auto land=text(x.document.choices,b.path("land"));const std::map<std::string,std::vector<std::pair<int,std::vector<std::string>>>> spells={
                {"arid",{{3,{"blur","burning-hands","fire-bolt"}},{5,{"fireball"}},{7,{"blight"}},{9,{"wall-of-stone"}}}},
                {"polar",{{3,{"fog-cloud","hold-person","ray-of-frost"}},{5,{"sleet-storm"}},{7,{"ice-storm"}},{9,{"cone-of-cold"}}}},
                {"temperate",{{3,{"misty-step","shocking-grasp","sleep"}},{5,{"lightning-bolt"}},{7,{"freedom-of-movement"}},{9,{"tree-stride"}}}},
                {"tropical",{{3,{"acid-splash","ray-of-sickness","web"}},{5,{"stinking-cloud"}},{7,{"polymorph"}},{9,{"insect-plague"}}}}};
            if(spells.contains(land))spellTable(b,"wisdom",spells.at(land),"Circle of the Land: "+land);
            b.mode("landsAid","Land's Aid",{{"action","magic"},{"rangeFeet",60},{"radiusFeet",10},{"healingDice",std::to_string(l>=14?4:l>=10?3:2)+"d6"},{"damageDice",std::to_string(l>=14?4:l>=10?3:2)+"d6"},{"damageType","necrotic"},{"saveDC",8+wisdom+x.proficiency},{"saveAbility","constitution"},{"resourceCost",{{"druid:wild-shape",1}}}},"Chosen enemies in Sphere save for half damage; one chosen creature there regains healing.");
            if(l>=6){b.resource("natural-recovery-cast","Natural Recovery free Circle spell",1,"long-rest:all");b.resource("natural-recovery-slots","Natural Recovery spell-slot recovery",1,"long-rest:all");b.calc("naturalRecoveryLevels","Natural Recovery total slot levels",(l+1)/2,"At a Short Rest, recover slots with combined levels up to half Druid level rounded up; no slot of level6+. Separate once-per-Long-Rest free Circle spell casting.");}
            if(l>=10){const std::map<std::string,std::string> resist={{"arid","fire"},{"polar","cold"},{"temperate","lightning"},{"tropical","poison"}};b.mode("naturesWard","Nature's Ward",{{"conditionImmunity","poisoned"},{"resistance",resist.contains(land)?resist.at(land):"Unselected land"}},"Resistance follows current Circle land; Poisoned immunity always applies.");}
            if(l>=14)b.mode("naturesSanctuary","Nature's Sanctuary",{{"rangeFeet",120},{"cubeFeet",15},{"durationMinutes",1},{"coverACBonus",2},{"coverDexSaveBonus",2},{"moveBonusActionFeet",60},{"resourceCost",{{"druid:wild-shape",1}}}},"Magic action creates sanctuary; you and allies receive Half Cover, allies receive Nature's Ward resistance; ends on Incapacitated/death.");
        }
    }
    if(int l=level(x,"fighter")){
        Builder b(x,out,"fighter","47-49",true);b.resource("second-wind","Second Wind",table(x,"fighter","secondWind",l>=10?4:l>=4?3:2),"short-rest:1;long-rest:all");b.calc("secondWindHealing","Second Wind healing","1d10 + "+std::to_string(l),"Bonus Action; spend one Second Wind use.");out.attackCount=std::max(out.attackCount,table(x,"fighter","attacks",l>=20?4:l>=11?3:l>=5?2:1));
        if(l>=2){b.resource("action-surge","Action Surge",table(x,"fighter","actionSurge",l>=17?2:1),"short-rest:all;long-rest:all");b.mode("actionSurge","Action Surge",{{"additionalActions",1},{"magicActionAllowed",false},{"maximumPerTurn",1}},"Spend one use on your turn; cannot take the Magic action with it.");b.mode("tacticalMind","Tactical Mind",{{"abilityCheckDie","1d10"},{"resourceCost",{{"fighter:second-wind",1}}},{"refundOnFailure",true}},"After failed ability check, add1d10; no HP healing and use is spent only if successful.");}
        if(l>=5)b.mode("tacticalShift","Tactical Shift",{{"movementFraction",.5},{"provokeOpportunityAttacks",false}},"Part of activating Second Wind as a Bonus Action.");
        if(l>=9){b.resource("indomitable","Indomitable",table(x,"fighter","indomitable",l>=17?3:l>=13?2:1),"long-rest:all");b.calc("indomitableBonus","Indomitable saving-throw reroll bonus",l,"After a failed save, spend one use and reroll with Fighter level added; must use new roll.");b.calc("tacticalMaster","Tactical Master replacement options",Json::array({"Push","Sap","Slow"}),"Replace a usable weapon mastery for an individual attack.");}
        if(l>=13)b.mode("studiedAttacks","Studied Attacks",{{"advantage",true},{"expires","end-of-next-turn"}},"After missing a creature, your next attack against it before end of next turn has Advantage.");
        if(subclass(x,"fighter")){b.calc("criticalMinimum","Champion critical-hit minimum",l>=15?18:19,"Weapon and Unarmed Strike attack rolls crit on this natural number through20.");b.mode("remarkableAthlete","Remarkable Athlete",{{"initiativeAdvantage",true},{"athleticsAdvantage",true},{"criticalMoveSpeedFraction",.5},{"provokeOpportunityAttacks",false}},"Always advantage on Initiative/Athletics; movement occurs immediately after your Critical Hit.");if(l>=10)b.mode("heroicWarrior","Heroic Warrior",{{"trigger","start-of-turn-in-combat"},{"heroicInspirationMinimum",1}},"If you lack Heroic Inspiration at the start of your combat turn, gain it.");if(l>=18){b.calc("survivorHealing","Survivor healing at start of turn",5+constitution,"Requires at least1 HP and Bloodied; does not increase maximumHP.");b.mode("survivorDeathSaves","Survivor death saves",{{"advantage",true},{"criticalSuccessMinimum",18}},"Natural18-20 grants the benefit of natural20 on a Death Saving Throw.");}}
    }
    if(int l=level(x,"monk")){
        Builder b(x,out,"monk","50-52",true);const int die=tableDie(x,"monk","martialArtsDie",l>=17?12:l>=11?10:l>=5?8:6);const bool unarmored=!x.armored&&!x.shield;if(unarmored)b.armorFormula("Monk Unarmored Defense",10,{"dexterity","wisdom"},"50");
        b.calc("martialArtsDie","Martial Arts die",die,"d6 at1, d8 at5, d10 at11, d12 at17. Applies only unarmored, without Shield, and using Monk weapons/Unarmed Strikes.");b.mode("martialArts","Martial Arts",{{"enabled",unarmored},{"weaponScope","monk-weapons-or-unarmed"},{"damageDie",die},{"abilityOptions",{"strength","dexterity"}},{"attackBonus",x.proficiency+std::max(strength,dexterity)},{"damageBonus",std::max(strength,dexterity)},{"grappleShoveDC",8+x.proficiency+std::max(strength,dexterity)},{"bonusActionUnarmedAttacks",1}},"Monk weapons are Simple Melee or Light Martial Melee. No armor or Shield; Dexterity can replace Strength.");
        if(l>=2){b.resource("focus","Focus Points",table(x,"monk","focusPoints",l),"short-rest:all;long-rest:all");b.resource("uncanny-metabolism","Uncanny Metabolism",1,"long-rest:all");const int speed=table(x,"monk","unarmoredMovement",l>=18?30:l>=14?25:l>=10?20:l>=6?15:10);if(unarmored)b.speedBonus("Unarmored Movement at Monk level "+std::to_string(l),speed,"50-51");b.calc("focusDC","Monk Focus save DC",8+wisdom+x.proficiency,"8 + Wisdom modifier + proficiency bonus.");b.mode("flurryOfBlows","Flurry of Blows",{{"action","bonus-action"},{"unarmedAttacks",l>=10?3:2},{"resourceCost",{{"monk:focus",1}}}},"Spend1 Focus to make these Unarmed Strikes; Martial Arts armor restrictions apply to its damage/ability substitutions.");b.mode("patientDefense","Patient Defense",{{"action","bonus-action"},{"freeAction","disengage"},{"paidActions",{"disengage","dodge"}},{"focusCost",1},{"temporaryHpDice",l>=10?"2d"+std::to_string(die):"0"}},"Free Disengage; spend1 Focus to also Dodge. At10, paid option also grants2 Martial Arts dice temporaryHP.");b.mode("stepOfTheWind","Step of the Wind",{{"action","bonus-action"},{"freeAction","dash"},{"paidActions",{"dash","disengage"}},{"focusCost",1},{"jumpMultiplier",2},{"carryLargeOrSmallerAlly",l>=10}},"Free Dash; pay1 Focus to add Disengage and double jump distance. At10, paid option may carry a willing adjacent Large-or-smaller creature without Opportunity Attacks.");b.calc("metabolismHealing","Uncanny Metabolism healing",std::to_string(l)+" + 1d"+std::to_string(die),"At Initiative, spend once-per-Long-Rest permission to recover all Focus and this healing.");}
        if(l>=3)b.mode("deflectAttacks","Deflect Attacks",{{"action","reaction"},{"damageReduction","1d10 + "+std::to_string(dexterity+l)},{"allDamageTypes",l>=13},{"redirectFocusCost",1},{"redirectDamageDice","2d"+std::to_string(die)},{"redirectDamageBonus",dexterity},{"saveDC",8+wisdom+x.proficiency},{"saveAbility","dexterity"},{"meleeTargetRangeFeet",5},{"rangedTargetRangeFeet",60}},"Reduce attack damage; before13 attack must include Bludgeoning/Piercing/Slashing. Only if reduced to0 may redirect via1 Focus.");
        if(l>=4)b.calc("slowFall","Slow Fall damage reduction",5*l,"Reaction when falling; reduces falling damage by5 × Monk level.");
        if(l>=5){out.attackCount=std::max(out.attackCount,2);b.mode("stunningStrike","Stunning Strike",{{"focusCost",1},{"saveDC",8+wisdom+x.proficiency},{"saveAbility","constitution"},{"maximumPerTurn",1},{"failedSaveCondition","stunned"},{"successfulSaveSpeedMultiplier",.5},{"successfulSaveNextAttackAdvantage",true}},"On a Monk weapon/Unarmed hit; effects last until start of your next turn.");}
        if(l>=6)b.mode("empoweredStrikes","Empowered Strikes",{{"unarmedDamageOptions",{"force","normal"}}},"Choose Force or normal type each time your Unarmed Strike deals damage.");
        if(l>=7)b.mode("evasion","Monk Evasion",{{"successDamageFraction",0},{"failureDamageFraction",.5},{"enabled",!incapacitated(x)}},"Dexterity saves that normally halve damage; unavailable while Incapacitated.");
        if(l>=9)b.mode("acrobaticMovement","Acrobatic Movement",{{"enabled",unarmored},{"verticalSurfaceMovement",true},{"liquidSurfaceMovement",true}},"During your movement on your turn, without armor/Shield; does not let you end unsupported movement afloat.");
        if(l>=10)b.mode("selfRestoration","Self-Restoration",{{"removeOneCondition",{"charmed","frightened","poisoned"}},{"trigger","end-of-your-turn"},{"foodDrinkExhaustionImmune",true}},"Remove one listed condition at end of each turn; no Exhaustion from foregoing food/drink.");
        if(l>=14)b.mode("disciplinedSurvivor","Disciplined Survivor",{{"allSavingThrowProficiencies",true},{"failedSaveRerollFocusCost",1}},"Saving throw proficiencies are applied before derived saves. Failed-save reroll must use its new result.");
        if(l>=15)b.mode("perfectFocus","Perfect Focus",{{"trigger","initiative"},{"restoreToMinimum",4}},"When not using Uncanny Metabolism, recover expended Focus until4 if below4.");
        if(l>=18){b.effectSwitch("superior-defense","Superior Defense active");b.mode("superiorDefense","Superior Defense",{{"active",active(x,"monk:superior-defense")&&!incapacitated(x)},{"focusCost",3},{"durationMinutes",1},{"resistanceAllExcept","force"}},"Spend3 Focus at start of your turn; ends after1 minute or Incapacitated.");}
        if(subclass(x,"monk")){b.mode("openHandTechnique","Open Hand Technique",{{"options",{{"addle",{{"opportunityAttacks",false}}},{"push",{{"saveAbility","strength"},{"pushFeet",15}}},{"topple",{{"saveAbility","dexterity"},{"condition","prone"}}}}},{"saveDC",8+wisdom+x.proficiency}},"Choose one option each time a Flurry of Blows attack hits.");if(l>=6){b.resource("wholeness-of-body","Wholeness of Body",std::max(1,wisdom),"long-rest:all");b.calc("wholenessHealing","Wholeness of Body healing","1d"+std::to_string(die)+" + "+std::to_string(wisdom)+" (minimum1)","Bonus Action, spend one use.");}if(l>=11)b.mode("fleetStep","Fleet Step",{{"freeStepOfTheWind",true}},"Immediately after taking a Bonus Action other than Step of the Wind, may also use Step of the Wind.");if(l>=17)b.mode("quiveringPalm","Quivering Palm",{{"focusCost",4},{"damageDice","10d12"},{"damageType","force"},{"saveDC",8+wisdom+x.proficiency},{"saveAbility","constitution"},{"saveDamageFraction",.5},{"durationDays",l},{"maximumTargets",1}},"On Unarmed hit set vibrations; later end with an action or replace one Attack-action attack, while on same plane. May dismiss harmlessly.");}
    }
    if(int l=level(x,"paladin")){
        Builder b(x,out,"paladin","53-57",true);b.resource("lay-on-hands","Lay On Hands healing pool",5*l,"long-rest:all");b.mode("layOnHands","Lay On Hands",{{"action","bonus-action"},{"range","touch"},{"healingPool",5*l},{"poisonedRemovalCost",5}},"Restore chosen HP from pool, or spend5 poolHP to remove Poisoned; removal points do not also heal.");
        if(l>=2){b.grant("divine-smite","charisma","Paladin's Smite",1);b.resource("divine-smite","Paladin's Smite free casting",1,"long-rest:all");}
        if(l>=3){b.resource("channel-divinity","Paladin Channel Divinity",table(x,"paladin","channelDivinity",l>=11?3:2),"short-rest:1;long-rest:all");b.mode("divineSense","Divine Sense",{{"action","bonus-action"},{"radiusFeet",60},{"durationMinutes",10},{"creatureTypes",{"celestial","fiend","undead"}},{"resourceCost",{{"paladin:channel-divinity",1}}}},"Detect creature locations/types and consecrated/desecrated places or objects; ends on Incapacitated.");}
        if(l>=5){out.attackCount=std::max(out.attackCount,2);b.grant("find-steed","charisma","Faithful Steed",1);b.resource("faithful-steed","Faithful Steed free casting",1,"long-rest:all");}
        if(l>=6){const int bonus=std::max(1,charisma);if(!incapacitated(x))for(const auto& ability:abilityNames)out.saveBonuses[ability]+=bonus;b.mode("auraOfProtection","Aura of Protection",{{"active",!incapacitated(x)},{"savingThrowBonus",bonus},{"emanationFeet",l>=18?30:10},{"stackingGroup","aura-of-protection"}},"You and allies in aura add this bonus. Cannot stack multiple Paladin auras; unavailable while Incapacitated.");}
        if(l>=9)b.mode("abjureFoes","Abjure Foes",{{"action","magic"},{"maximumTargets",std::max(1,charisma)},{"rangeFeet",60},{"saveDC",8+charisma+x.proficiency},{"saveAbility","wisdom"},{"durationMinutes",1},{"condition","frightened"},{"resourceCost",{{"paladin:channel-divinity",1}}}},"Affected foes can only move, act, or take a Bonus Action on their turn; damage ends effect.");
        if(l>=10)b.mode("auraOfCourage","Aura of Courage",{{"conditionImmunity","frightened"},{"emanationFeet",l>=18?30:10},{"active",!incapacitated(x)}},"You and allies in Aura of Protection; suppress Frightened while present.");
        if(l>=11)b.mode("radiantStrikes","Radiant Strikes",{{"extraDamageDice","1d8"},{"damageType","radiant"},{"weaponScope","melee-weapons-or-unarmed"}},"Every qualifying attack hit, not once per turn. Ranged weapons are excluded.");
        if(l>=14)b.mode("restoringTouch","Restoring Touch",{{"conditions",{"blinded","charmed","deafened","frightened","paralyzed","stunned"}},{"layOnHandsCostPerCondition",5}},"During Lay On Hands, remove selected conditions by spending5 poolHP each; those points do not heal.");
        if(subclass(x,"paladin")){
            spellTable(b,"charisma",{{3,{"protection-from-evil-and-good","shield-of-faith"}},{5,{"aid","zone-of-truth"}},{9,{"beacon-of-hope","dispel-magic"}},{13,{"freedom-of-movement","guardian-of-faith"}},{17,{"commune","flame-strike"}}},"Oath of Devotion Spells");
            b.effectSwitch("sacred-weapon","Sacred Weapon active");b.mode("sacredWeapon","Sacred Weapon",{{"active",active(x,"paladin:sacred-weapon")},{"attackBonus",std::max(1,charisma)},{"weaponScope","one-held-melee-weapon"},{"damageTypeOptions",{"normal","radiant"}},{"durationMinutes",10},{"brightLightFeet",20},{"dimLightAdditionalFeet",20},{"resourceCost",{{"paladin:channel-divinity",1}}}},"Activate while taking Attack action; affects one held melee weapon until dropped, replaced, ended, or10 minutes.");
            if(l>=7)b.mode("auraOfDevotion","Aura of Devotion",{{"conditionImmunity","charmed"},{"active",!incapacitated(x)},{"emanationFeet",l>=18?30:10}},"You and allies in Aura of Protection; suppress Charmed while present.");
            if(l>=15)b.mode("smiteOfProtection","Smite of Protection",{{"coverACBonus",2},{"coverDexSaveBonus",2},{"emanationFeet",l>=18?30:10},{"expires","start-of-your-next-turn"}},"After you cast Divine Smite, you and allies in aura have Half Cover until next turn.");
            if(l==20){b.resource("holy-nimbus","Holy Nimbus",1,"long-rest:all","May restore use by expending one level5 spell slot.");b.mode("holyNimbus","Holy Nimbus",{{"action","bonus-action"},{"durationMinutes",10},{"radiantDamage",charisma+x.proficiency},{"saveAdvantageAgainstTypes",{"fiend","undead"}},{"sunlight",true},{"emanationFeet",30}},"When activated, enemy starting its turn in aura takes listed damage; aura shines sunlight.");}
        }
    }
    if(int l=level(x,"ranger")){
        Builder b(x,out,"ranger","58-61",true);const int uses=table(x,"ranger","favoredEnemy",2+(l-1)/4);b.grant("hunters-mark","wisdom","Favored Enemy",uses);b.resource("favored-enemy","Hunter's Mark free castings",uses,"long-rest:all");b.mode("favoredEnemy","Hunter's Mark feature damage",{{"damageDie",l==20?10:6},{"attackAdvantage",l>=17},{"damageCannotBreakConcentration",l>=13}},"Applies only to your Hunter's Mark target; damage still uses the spell's trigger and duration.");
        if(l>=5)out.attackCount=std::max(out.attackCount,2);if(l>=6){if(x.armorCategory!="heavy")b.speedBonus("Roving",10,"59");b.mode("roving","Roving",{{"speedBonusFeet",x.armorCategory!="heavy"?10:0},{"climbSpeedEqualsWalking",true},{"swimSpeedEqualsWalking",true}},"+10 walking Speed without Heavy armor; gain Climb and Swim Speeds equal to Speed.");}
        if(l>=10){b.resource("tireless","Tireless temporary HP",std::max(1,wisdom),"long-rest:all");b.calc("tirelessHp","Tireless temporary HP","1d8 + "+std::to_string(wisdom)+" (minimum1)","Magic action; Short Rests separately reduce Exhaustion by1.");}
        if(l>=14){b.resource("natures-veil","Nature's Veil",std::max(1,wisdom),"long-rest:all");b.mode("naturesVeil","Nature's Veil",{{"action","bonus-action"},{"condition","invisible"},{"expires","end-of-your-next-turn"}},"Spend one use to become Invisible until end of next turn.");}
        if(l>=18)b.calc("feralSenses","Feral Senses Blindsight (feet)",30,"Always-on Blindsight from the class feature.");
        if(subclass(x,"ranger")){
            b.mode("huntersLore","Hunter's Lore",{{"reveal",{"immunities","resistances","vulnerabilities"}}},"Reveals traits only of a creature currently marked by your Hunter's Mark.");
            const auto prey=text(x.document.choices,b.path("huntersPrey"));if(prey=="colossus-slayer")b.mode("colossusSlayer","Colossus Slayer",{{"extraDamageDice","1d8"},{"maximumPerTurn",1},{"weaponRequired",true}},"A weapon hits a target missing any HP.");else if(prey=="horde-breaker")b.mode("hordeBreaker","Horde Breaker",{{"extraAttacks",1},{"maximumPerTurn",1},{"secondaryWithinFeet",5},{"sameWeapon",true}},"After a weapon attack, attack another creature within5 feet of original target and weapon range, not attacked yet this turn.");
            if(l>=7)b.mode("defensiveTactics","Defensive Tactics",{{"selected",text(x.document.choices,b.path("defensiveTactics"))},{"opportunityAttackDisadvantage",text(x.document.choices,b.path("defensiveTactics"))=="escape-the-horde"},{"subsequentAttacksAfterHitDisadvantage",text(x.document.choices,b.path("defensiveTactics"))=="multiattack-defense"}},"Choice may change after a Short or Long Rest; Multiattack Defense lasts only the current turn and attacker.");
            if(l>=11)b.mode("superiorHuntersPrey","Superior Hunter's Prey",{{"damageDie",l==20?10:6},{"secondaryWithinFeet",30},{"maximumPerTurn",1}},"When you deal Hunter's Mark damage, also deal that spell's extra damage to one other visible creature within30 feet of first target.");
            if(l>=15)b.mode("superiorHuntersDefense","Superior Hunter's Defense",{{"action","reaction"},{"resistanceToTriggeringDamageType",true},{"expires","end-of-current-turn"}},"After taking damage, gain Resistance to that damage and further damage of same type this turn.");
        }
    }
    if(int l=level(x,"rogue")){
        Builder b(x,out,"rogue","61-64",true);const int dice=(l+1)/2;b.mode("sneakAttack","Sneak Attack",{{"extraDamageDice",std::to_string(dice)+"d6"},{"maximumPerTurn",1},{"weaponProperties",{"finesse","ranged"}}},"Requires attack Advantage, or ally within5 feet of target who is not Incapacitated and no Disadvantage.");
        if(l>=2)b.mode("cunningAction","Cunning Action",{{"action","bonus-action"},{"options",{"dash","disengage","hide"}}},"One of these actions on your turn.");
        if(l>=3)b.mode("steadyAim","Steady Aim",{{"action","bonus-action"},{"nextAttackAdvantage",true},{"speedForTurn",0}},"Requires not having moved this turn; speed becomes0 until turn ends.");
        if(l>=5){Json opts={{"poison",{{"dieCost",1},{"saveAbility","constitution"},{"condition","poisoned"},{"durationMinutes",1},{"requiredItem","poisoners-kit"}}},{"trip",{{"dieCost",1},{"saveAbility","dexterity"},{"condition","prone"},{"maximumSize","Large"}}},{"withdraw",{{"dieCost",1},{"moveSpeedFraction",.5},{"opportunityAttacks",false}}}};
            if(l>=14){opts["daze"]={{"dieCost",2},{"saveAbility","constitution"},{"nextTurnChooseOne",{"move","action","bonus-action"}}};opts["knock-out"]={{"dieCost",6},{"saveAbility","constitution"},{"condition","unconscious"},{"durationMinutes",1},{"repeatSave","end-of-target-turn"}};opts["obscure"]={{"dieCost",3},{"saveAbility","dexterity"},{"condition","blinded"},{"expires","end-of-target-next-turn"}};}
            if(subclass(x,"rogue")&&l>=9)opts["stealth-attack"]={{"dieCost",1},{"keepHideInvisibility",true},{"endTurnCover","three-quarters-or-total"}};
            b.mode("cunningStrike","Cunning Strike",{{"options",opts},{"maximumEffects",l>=11?2:1},{"availableSneakDice",dice},{"saveDC",8+dexterity+x.proficiency}},"Forgo the indicated Sneak Attack dice before rolling; effects occur after damage. Poison requires a Poisoner's Kit. Chosen costs cannot exceed available dice.");
            b.mode("uncannyDodge","Uncanny Dodge",{{"action","reaction"},{"damageMultiplier",.5},{"rounding","down"}},"An attacker you can see hits you with an attack roll; halve that attack's damage.");
        }
        if(l>=7){b.mode("evasion","Rogue Evasion",{{"successDamageFraction",0},{"failureDamageFraction",.5},{"enabled",!incapacitated(x)}},"Dexterity saves that normally halve damage; unavailable while Incapacitated.");out.statMinimums["proficient-skill-tool-d20"]=10;b.calc("reliableTalent","Reliable Talent minimum proficient skill/tool d20",10,"Treat natural1-9 as10 only on ability checks using a skill/tool proficiency.");}
        if(l>=18)b.mode("elusive","Elusive",{{"attacksAgainstAdvantageAllowed",incapacitated(x)}},"No attack can have Advantage against you unless you are Incapacitated.");
        if(l==20){b.resource("stroke-of-luck","Stroke of Luck",1,"short-rest:all;long-rest:all");b.mode("strokeOfLuck","Stroke of Luck",{{"replacementD20",20}},"After failing a D20 Test, spend one use to turn its roll into20.");}
        if(subclass(x,"rogue")){
            b.mode("fastHands","Fast Hands",{{"action","bonus-action"},{"options",{"sleight-of-hand","utilize","magic-item-magic-action"}}},"Sleight of Hand can pick locks/disarm traps with Thieves' Tools or pick pockets; item Magic action is supported, general spellcasting is not added.");
            b.mode("secondStoryWork","Second-Story Work",{{"climbSpeedEqualsWalking",true},{"jumpAbilityOptions",{"strength","dexterity"}},{"dexterityLongJumpFeet",score(x,"dexterity")},{"dexterityHighJumpFeet",3+dexterity}},"Use Dexterity instead of Strength for jump distances; normal approach and movement rules still apply.");
            if(l>=13)b.mode("useMagicDevice","Use Magic Device",{{"attunementMaximum",4},{"chargeConservationD6",6},{"scrollAbility","intelligence"},{"reliableScrollMaximumLevel",1},{"higherScrollArcanaDCBase",10}},"Any Spell Scroll: level2+ needs Intelligence(Arcana) DC10+spell level or scroll disintegrates. On d6=6 item charge use is free.");
            if(l>=17)b.mode("thiefsReflexes","Thief's Reflexes",{{"firstRoundTurns",2},{"secondTurnInitiativeOffset",-10}},"Only the first round of combat; second turn is at Initiative minus10.");
        }
    }
    if(int l=level(x,"sorcerer")){
        Builder b(x,out,"sorcerer","65-70",true);b.resource("innate-sorcery","Innate Sorcery",2,"long-rest:all");b.effectSwitch("innate-sorcery","Innate Sorcery active");const bool innate=active(x,"sorcerer:innate-sorcery");if(innate)out.spellSaveBonuses[selectedClassId(x,"sorcerer")]+=1;
        b.mode("innateSorcery","Innate Sorcery",{{"active",innate},{"classId",selectedClassId(x,"sorcerer")},{"saveDCBonus",1},{"spellAttackAdvantage",true},{"durationMinutes",1},{"action","bonus-action"}},"Affects Sorcerer spells only; spend one Innate Sorcery use.");
        if(l>=2){b.resource("sorcery-points","Sorcery Points",table(x,"sorcerer","sorceryPoints",l),"long-rest:all");Json conversion=Json::array();for(const auto& row:std::vector<std::array<int,3>>{{1,2,2},{2,3,3},{3,5,5},{4,6,7},{5,7,9}})conversion.push_back({{"slotLevel",row[0]},{"pointCost",row[1]},{"minimumSorcererLevel",row[2]},{"available",l>=row[2]}});b.mode("fontOfMagic","Font of Magic conversion",{{"createSlots",conversion},{"slotToPointsMultiplier",1},{"createAction","bonus-action"},{"consumeSlotAction","none"},{"createdSlotsExpire","long-rest"}},"Spend a slot for its level in Sorcery Points, capped at class maximum; creating a slot follows the costs and class-level gates shown.");
            const auto choices=strings(x.document.choices,b.path("metamagic"));for(const auto& id:choices){const auto key=slug(id);Json effect={{"pointCost",key=="heightened-spell"||key=="quickened-spell"?2:1}};
                if(key=="careful-spell")effect.update({{"maximumProtectedTargets",std::max(1,charisma)},{"automaticSaveSuccess",true},{"successfulSaveDamage",0}});
                else if(key=="distant-spell")effect.update({{"rangeMultiplier",2},{"minimumBaseRangeFeet",5},{"touchRangeFeet",30}});
                else if(key=="empowered-spell")effect.update({{"maximumRerollDice",std::max(1,charisma)},{"combineWithOtherMetamagic",true},{"mustUseNewRolls",true}});
                else if(key=="extended-spell")effect.update({{"durationMultiplier",2},{"minimumBaseDurationMinutes",1},{"maximumDurationHours",24},{"concentrationSaveAdvantage",true}});
                else if(key=="heightened-spell")effect.update({{"targetSaveDisadvantage",true},{"maximumTargets",1}});
                else if(key=="quickened-spell")effect.update({{"castingAction","bonus-action"},{"requiresOriginalAction",true},{"otherLeveledSpellThisTurnAllowed",false}});
                else if(key=="seeking-spell")effect.update({{"missedSpellAttackReroll",true},{"mustUseNewRoll",true},{"combineWithOtherMetamagic",true}});
                else if(key=="subtle-spell")effect.update({{"removeComponents",{"verbal","somatic","noncostly-nonconsumed-material"}}});
                else if(key=="transmuted-spell")effect.update({{"eligibleDamageTypes",{"acid","cold","fire","lightning","poison","thunder"}}});
                else if(key=="twinned-spell")effect.update({{"effectiveSlotLevelIncrease",1},{"requiresUpcastAddsTarget",true}});
                b.mode("metamagic-"+key,name(x,id),effect,"Spend the listed Sorcery Points; apply only to an eligible spell. One option per cast unless explicitly permitted otherwise.");
            }
        }
        if(l>=5){b.resource("sorcerous-restoration","Sorcerous Restoration",1,"long-rest:all");b.calc("restorationPoints","Sorcerous Restoration maximum recovery",l/2,"Once per Long Rest at a Short Rest, regain up to half Sorcerer level in expended points, rounded down.");}
        if(l>=7)b.mode("sorceryIncarnate","Sorcery Incarnate",{{"innateSorceryReplacementPointCost",2},{"requiresNoInnateUses",true},{"maximumMetamagicOptions",innate?2:1}},"If no Innate uses remain, spend2 points to activate; while active may use up to2 Metamagic options on a spell.");
        if(l==20)b.mode("arcaneApotheosis","Arcane Apotheosis",{{"active",innate},{"freeMetamagicOptionsPerTurn",1}},"During Innate Sorcery, one Metamagic option per your turn costs0 points.");
        if(subclass(x,"sorcerer")){
            out.hpBonus+=l;if(!x.armored)b.armorFormula("Draconic Resilience",10,{"dexterity","charisma"},"69");b.calc("draconicHp","Draconic Resilience HP bonus",l,"+3 at Sorcerer3, +1 per later Sorcerer level; class levels only.");
            spellTable(b,"charisma",{{3,{"alter-self","chromatic-orb","command","dragons-breath"}},{5,{"fear","fly"}},{7,{"arcane-eye","charm-monster"}},{9,{"legend-lore","summon-dragon"}}},"Draconic Spells");
            if(l>=6)b.mode("elementalAffinity","Elemental Affinity",{{"resistance",text(x.document.choices,b.path("elementalAffinity"))},{"spellDamageType",text(x.document.choices,b.path("elementalAffinity"))},{"damageBonus",charisma},{"maximumDamageRollsPerSpell",1}},"Permanent chosen resistance; add Charisma to one damage roll when casting any spell of the chosen damage type.");
            if(l>=14){b.resource("dragon-wings","Dragon Wings",1,"long-rest:all","May restore one use by spending3 Sorcery Points.");b.effectSwitch("dragon-wings","Dragon Wings active");b.mode("dragonWings","Dragon Wings",{{"active",active(x,"sorcerer:dragon-wings")},{"flySpeedFeet",60},{"durationMinutes",60},{"action","bonus-action"}},"Spend one use to manifest; may dismiss without an action.");}
            if(l>=18){b.grant("summon-dragon","charisma","Dragon Companion",1);b.resource("dragon-companion","Dragon Companion free casting",1,"long-rest:all");b.mode("dragonCompanion","Dragon Companion",{{"spellId","srd55:summon-dragon"},{"materialRequired",false},{"optionalNoConcentration",true},{"durationWithoutConcentrationMinutes",1}},"At the start of casting may remove Concentration and set duration to1 minute; normal slot castings also qualify.");}
        }
    }
    if(int l=level(x,"warlock")){
        Builder b(x,out,"warlock","71-76",true);b.calc("invocationCapacity","Eldritch Invocations known",table(x,"warlock","invocations",invocationCapacity(l)),"The class table sets legal invocation selection count; each retained selection separately validates prerequisites and repeatability.");const int pactSlots=l>=17?4:l>=11?3:l>=2?2:1,pactLevel=std::min(5,(l+1)/2);const auto invocations=strings(x.document.choices,b.path("invocations"));const auto ids=invocationIds(x);
        if(l>=2){b.resource("magical-cunning","Magical Cunning",1,"long-rest:all");b.calc("magicalCunningRecovery","Magical Cunning Pact slots recovered",l==20?pactSlots:(pactSlots+1)/2,"One-minute rite; at20 recover all expended Pact slots, otherwise half maximum rounded up.");}
        if(l>=9){b.grant("contact-other-plane","charisma","Contact Patron",1);b.resource("contact-patron","Contact Patron free casting",1,"long-rest:all");b.mode("contactPatron","Contact Patron",{{"automaticSavingThrowSuccess",true},{"spellId","srd55:contact-other-plane"}},"Free casting contacts your patron and automatically succeeds on the spell's saving throw.");}
        for(const auto& [threshold,sl]:std::vector<std::pair<int,int>>{{11,6},{13,7},{15,8},{17,9}})if(l>=threshold)b.resource("arcanum-"+std::to_string(sl),"Mystic Arcanum level "+std::to_string(sl),1,"long-rest:all");
        for(std::size_t i=0;i<invocations.size();++i){const auto id=invocations[i],key=slug(id);const auto target=text(x.document.choices,b.path("invocationTargets/"+std::to_string(i)));
            if(key=="agonizing-blast")b.mode(key+"-"+std::to_string(i),"Agonizing Blast",{{"spellId",target},{"damageBonus",charisma}},"Add Charisma modifier to selected known Warlock cantrip damage rolls.");
            else if(key=="eldritch-spear")b.mode(key+"-"+std::to_string(i),"Eldritch Spear",{{"spellId",target},{"rangeBonusFeet",30*l},{"minimumBaseRangeFeet",10}},"Selected damaging Warlock cantrip with base range10+ feet gains30 × Warlock level feet.");
            else if(key=="repelling-blast")b.mode(key+"-"+std::to_string(i),"Repelling Blast",{{"spellId",target},{"pushFeet",10},{"maximumTargetSize","Large"}},"On a hit with selected Warlock attack cantrip, optionally push straight away.");
            else if(key=="armor-of-shadows")b.grant("mage-armor","charisma","Armor of Shadows (self only)",-1);
            else if(key=="ascendant-step")b.grant("levitate","charisma","Ascendant Step (self only)",-1);
            else if(key=="mask-of-many-faces")b.grant("disguise-self","charisma","Mask of Many Faces",-1);
            else if(key=="master-of-myriad-forms")b.grant("alter-self","charisma","Master of Myriad Forms",-1);
            else if(key=="misty-visions")b.grant("silent-image","charisma","Misty Visions",-1);
            else if(key=="one-with-shadows")b.grant("invisibility","charisma","One with Shadows (self in Dim Light/Darkness)",-1);
            else if(key=="otherworldly-leap")b.grant("jump","charisma","Otherworldly Leap (self only)",-1);
            else if(key=="visions-of-distant-realms")b.grant("arcane-eye","charisma","Visions of Distant Realms",-1);
            else if(key=="whispers-of-the-grave")b.grant("speak-with-dead","charisma","Whispers of the Grave",-1);
            else if(key=="fiendish-vigor"){b.grant("false-life","charisma","Fiendish Vigor (self, maximum roll)",-1);b.calc("fiendishVigorTemporaryHp","Fiendish Vigor temporary HP",12,"False Life uses2d4+4 at base level; maximum dice yield12.");}
            else if(key=="devils-sight")b.mode(key,"Devil's Sight",{{"rangeFeet",120},{"magicalDarkness",true},{"dimLight",true}},"See normally in Dim Light and Darkness, including magical Darkness.");
            else if(key=="eldritch-mind")b.mode(key,"Eldritch Mind",{{"concentrationSavingThrowAdvantage",true}},"Only Constitution saving throws to maintain Concentration.");
            else if(key=="witch-sight")b.calc("witchSight","Witch Sight Truesight (feet)",30,"Permanent Truesight from the invocation.");
            else if(key=="eldritch-smite")b.mode(key,"Eldritch Smite",{{"damageDice",std::to_string(pactLevel+1)+"d8"},{"damageType","force"},{"pactSlotCost",1},{"maximumPerTurn",1},{"proneMaximumSize","Huge"}},"On pact-weapon hit spend a Pact slot; ordinary Spellcasting slots do not pay this cost.");
            else if(key=="gaze-of-two-minds")b.mode(key,"Gaze of Two Minds",{{"action","bonus-action"},{"initialRange","touch"},{"remoteCastMaximumDistanceFeet",60},{"samePlaneRequired",true},{"maintenance","bonus-action-each-turn"}},"Use willing creature's senses until next turn; casting may originate from either space while within60 feet.");
            else if(key=="gift-of-the-depths"){b.grant("water-breathing","charisma","Gift of the Depths",1);b.resource("gift-of-depths","Gift of the Depths Water Breathing",1,"long-rest:all");b.mode(key,"Gift of the Depths",{{"waterBreathing",true},{"swimSpeedEqualsWalking",true}},"Permanent Swim Speed and underwater breathing; separate one free Water Breathing cast.");}
            else if(key=="gift-of-the-protectors"){b.resource("gift-of-protectors","Gift of the Protectors",1,"long-rest:all");b.calc("protectorNames","Book of Shadows protected names",std::max(1,charisma),"Page can hold Charisma modifier names, minimum1. First named creature reduced to0 without dying instead reaches1 HP; then recharge.");}
            else if(key=="investment-of-the-chain-master")b.mode(key,"Investment of the Chain Master",{{"grantedFlyOrSwimSpeedFeet",40},{"familiarSaveDC",8+charisma+x.proficiency},{"commandAttackAction","bonus-action"},{"damageChoices",{"normal","necrotic","radiant"}},{"reactionResistance",true}},"Apply to familiar: choose Fly/Swim40; use your spellDC; optional damage changes and reaction Resistance to triggering damage.");
            else if(key=="lifedrinker")b.mode(key,"Lifedrinker",{{"extraDamageDice","1d6"},{"damageTypes",{"necrotic","psychic","radiant"}},{"maximumPerTurn",1},{"optionalHealingHitDiceCost",1},{"healingBonus",constitution},{"healingMinimum",1}},"Pact-weapon hit; may also spend one Hit Die to heal its roll plus Constitution, minimum1.");
            else if(key=="pact-of-the-blade")b.mode(key,"Pact of the Blade",{{"weaponId",text(x.document.choices,b.path("pactWeapon"))},{"ability","charisma"},{"attackBonus",charisma+x.proficiency},{"damageBonus",charisma},{"attackCount",ids.contains("devouring-blade")?3:ids.contains("thirsting-blade")?2:1},{"damageTypes",{"normal","necrotic","psychic","radiant"}}},"Only the bonded pact weapon receives this profile; extra attacks never add to another class's Extra Attack.");
            else if(key=="pact-of-the-chain"){b.grant("find-familiar","charisma","Pact of the Chain",-1);b.mode(key,"Pact of the Chain",{{"castingAction","magic"},{"specialForms",{"Imp","Pseudodragon","Quasit","Skeleton","Sphinx of Wonder","Sprite","Venomous Snake"}},{"forgoOwnAttackForFamiliarReactionAttack",true}},"Find Familiar without slot; choose a normal or listed special form. Familiar attacks with its Reaction when you forgo one Attack-action attack.");}
        }
        if(subclass(x,"warlock")){
            spellTable(b,"charisma",{{3,{"burning-hands","command","scorching-ray","suggestion"}},{5,{"fireball","stinking-cloud"}},{7,{"fire-shield","wall-of-fire"}},{9,{"geas","insect-plague"}}},"Fiend Spells");b.calc("darkOnesBlessing","Dark One's Blessing temporary HP",std::max(1,charisma+l),"Gain when you reduce an enemy to0, or someone else reduces an enemy within10 feet of you to0.");
            if(l>=6){b.resource("dark-ones-luck","Dark One's Own Luck",std::max(1,charisma),"long-rest:all");b.mode("darkOnesLuck","Dark One's Own Luck",{{"bonusDie","1d10"},{"maximumPerRoll",1}},"Add after seeing an ability check/save but before effects occur.");}
            if(l>=10)b.calc("fiendishResilience","Fiendish Resilience resistance",text(x.document.choices,b.path("fiendishResilience")),"Choose any damage type except Force after a Short or Long Rest.");
            if(l>=14){b.resource("hurl-through-hell","Hurl Through Hell",1,"long-rest:all","May restore a use by spending one Pact slot.");b.mode("hurlThroughHell","Hurl Through Hell",{{"saveDC",8+charisma+x.proficiency},{"saveAbility","charisma"},{"damageDice","8d10"},{"damageType","psychic"},{"fiendsTakeDamage",false},{"condition","incapacitated"},{"maximumPerTurn",1},{"returns","end-of-your-next-turn"}},"After an attack hits, failed save transports target until end of next turn; Fiends still suffer transport/incapacitation but no Psychic damage.");}
        }
    }
    if(int l=level(x,"wizard")){
        Builder b(x,out,"wizard","78-82",true);b.resource("arcane-recovery","Arcane Recovery",1,"long-rest:all");b.calc("arcaneRecoveryLevels","Arcane Recovery total slot levels",(l+1)/2,"At a Short Rest, recover combined slot levels up to half Wizard level rounded up, excluding level6+ slots.");b.mode("ritualAdept","Ritual Adept",{{"unpreparedBookRitualsAllowed",true}},"Ritual spell must be in your spellbook and have Ritual tag; read from the book while casting.");
        if(l>=5)b.mode("memorizeSpell","Memorize Spell",{{"replacementCount",1},{"trigger","short-rest"},{"source","spellbook"},{"minimumSpellLevel",1}},"Replace one prepared leveled Wizard spell with another eligible book spell at a Short Rest.");
        if(l>=18)b.calc("spellMastery","Spell Mastery spell levels",Json::array({1,2}),"Selected action-cast level1 and2 book spells are always prepared and cast at will at lowest level; replace one after Long Rest.");
        if(l==20)for(const auto& id:strings(x.document.choices,b.path("signatureSpells")))b.resource("signature-"+slug(id),"Signature Spell: "+name(x,id),1,"short-rest:all;long-rest:all");
        if(subclass(x,"wizard")){
            b.mode("potentCantrip","Potent Cantrip",{{"missDamageFraction",.5},{"successfulSaveDamageFraction",.5},{"extraEffectsOnMiss",false}},"Your damaging cantrip miss or target successful save still deals half damage, without additional effects.");
            if(l>=6){Json count=Json::object();for(int sl=0;sl<=9;++sl)count[std::to_string(sl)]=1+sl;b.mode("sculptSpells","Sculpt Spells",{{"protectedTargetsBySpellLevel",count},{"automaticSaveSuccess",true},{"successfulSaveDamage",0}},"For an Evocation spell affecting visible other creatures; choose up to1+spell level protected creatures.");}
            if(l>=10)b.mode("empoweredEvocation","Empowered Evocation",{{"classId",selectedClassId(x,"wizard")},{"school","evocation"},{"damageBonus",intelligence},{"maximumDamageRollsPerSpell",1}},"Add Intelligence to one damage roll of a Wizard Evocation spell.");
            if(l>=14){b.currentNumber("overchannel-uses","Overchannel uses since last Long Rest",100,"First use is safe; later uses increase self-damage. Reset to0 after a Long Rest.");b.resource("overchannel-safe","Overchannel safe use",1,"long-rest:all");const int uses=number(x.document.resources,"/wizard:overchannel-uses",0);b.mode("overchannel","Overchannel",{{"minimumSpellLevel",1},{"maximumSpellLevel",5},{"maximizeDamage",true},{"selfNecroticD12PerSlotLevel",uses==0?0:uses+1},{"selfDamageIgnoresResistanceImmunity",true}},"Wizard spell cast with a level1-5 slot: maximize damage on casting turn. First use safe; later uses inflict2d12 per slot level, increasing1d12 each further use until Long Rest.");}
        }
    }
    for(const auto& cls:{"druid","warlock"}) {
        const auto id=text(x.document.resources,"/familiars/"+std::string(cls)+"/form");const auto* creature=x.rules.find(id);if(id.empty()||!creature||creature->value("kind","")!="creature")continue;
        Builder b(x,out,cls,cls==std::string("druid")?"43":"73-74",true);auto profile=*creature;profile["ownerClass"]=selectedClassId(x,cls);profile["originalType"]=profile.value("type","");profile["type"]=cls==std::string("druid")?"Fey":text(x.document.resources,"/familiars/warlock/type","Fey");profile["normalAttackAllowed"]=false;
        if(cls==std::string("warlock")){profile["reactionAttackByForgoingOwnersAttack"]=true;if(invocationIds(x).contains("investment-of-the-chain-master")){const auto movement=text(x.document.choices,"/features/warlock/chainMovement","fly");profile["speed"][movement]=40;profile["featureSaveDC"]=8+charisma+x.proficiency;profile["bonusActionAttackCommand"]=true;profile["damageTypeOptions"]={"normal","necrotic","radiant"};profile["reactionResistanceToDamage"]=true;}}
        else profile["expires"]="owner-long-rest";b.calc("familiar","Current familiar profile",profile,"This creature remains separate from the character; class modifications are applied to its own stat block.");
    }
    for(const auto& [classId,l]:x.classLevels){const auto cls=classProfile(x.rules,classId);const auto* data=x.rules.find(classId);if(!data)continue;std::vector<std::string> notes;for(const auto& feature:data->value("features",Json::array()))if(feature.value("level",99)<=l)notes.push_back(feature.value("name","")+" [SRD "+feature.value("source",Json::object()).value("page","")+"]");std::vector<std::string> ids;for(const auto& calc:out.evaluation.calculations)if(calc.id.starts_with("feature."+cls+"."))ids.push_back(calc.id);out.evaluation.sections.push_back({data->value("name",cls)+" features",ids,notes});}
    return out;
}
} // namespace dnd::srd55v2
