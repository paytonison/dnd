#include "dnd/lifecycle.hpp"
#include "srd55_v2_internal.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <limits>

namespace dnd::srd55v2 {
namespace {
constexpr const char* prefix="srd55.history.";
thread_local int historyReplayDepth=0;
struct ReplayGuard{ReplayGuard(){++historyReplayDepth;}~ReplayGuard(){--historyReplayDepth;}};
struct Policy {
    std::string id,path,budget,cls,page;
    int classLevelLimit=0,longRestLimit=0,shortRestLimit=0,characterLevelLimit=0;
    bool atWill=false;
};
struct Parsed {
    bool accepted=false;
    Json stable=Json::object();
    Json pending=Json::object();
    std::vector<Json> events;
    int sequence=-1;
    int triggerSequence=-1;
    std::string trigger,triggerClass;
    std::map<std::string,int> used;
    Json acquisitions=Json::object();
    std::vector<Message> messages;
};
void problem(std::vector<Message>& out,const std::string& code,const std::string& text,const std::string& path="/advancement",const std::string& page="23-26"){out.push_back({"error","srd55.history."+code,path,text,{srd55v2::ref(page)}});}
std::string shortId(const std::string& id){const auto colon=id.find(':');return colon==std::string::npos?id:id.substr(colon+1);}
bool starts(const std::string& s,const std::string& parent){return s==parent||s.starts_with(parent+"/");}
std::string escaped(const std::string& key){std::string out;for(char c:key){if(c=='~')out+="~0";else if(c=='/')out+="~1";else out+=c;}return out;}
std::string safeId(std::string s){for(char& c:s)if(!std::isalnum(static_cast<unsigned char>(c)))c='_';return s;}
void set(Json& target,const std::string& path,const Json& value){target[Json::json_pointer(path)]=value;}
Json get(const Json& value,const std::string& path){const auto* p=at(value,path);return p?*p:Json();}
int total(const Json& c){return std::clamp(number(c,"/level",1),1,20);}
std::vector<std::string> classOrder(const Json& c){std::vector<std::string> result;const auto initial=text(c,"/classId");for(int n=1;n<=total(c);++n){const bool multi=get(c,"/multiclass")==true;result.push_back(n==1||!multi?initial:text(c,"/advancement/"+std::to_string(n)+"/classId"));}return result;}
std::map<std::string,int> levels(const Json& c){std::map<std::string,int> result;for(const auto& id:classOrder(c))++result[id];return result;}
const Json* profileClass(const Json& c,const ResolvedRuleset& rules,const std::string& profile){for(const auto& id:classOrder(c))if(classProfile(rules,id)==profile)return rules.find(id);return nullptr;}
int characterAt(const Json& c,const ResolvedRuleset& rules,const std::string& cls,int classRank){int rank=0;const auto order=classOrder(c);for(std::size_t i=0;i<order.size();++i)if(classProfile(rules,order[i])==cls&&++rank==classRank)return static_cast<int>(i+1);return 0;}
Json snapshot(const CharacterDocument& d){return {{"choices",d.choices},{"rolls",d.rolls},{"campaign",d.campaign},{"resources",d.resources},{"overrides",d.overrides}};}
Json lockedChoices(Json c){
    // Wielded/worn equipment, XP bookkeeping, and current resource choices are
    // deliberately separate from accepted creation/progression selections.
    for(const auto* key:{"armorId","weaponId","shield","xp","xpAward","inventorySelection","activeBookId"})c.erase(key);
    return c;
}
bool sameLocked(const Json& a,const Json& b){return a.is_object()&&b.is_object()&&lockedChoices(a.value("choices",Json::object()))==lockedChoices(b.value("choices",Json::object()));}
bool preserves(const Json& old,const Json& now){
    if(old.is_object()){if(!now.is_object())return false;for(auto it=old.begin();it!=old.end();++it)if(!now.contains(it.key())||!preserves(it.value(),now[it.key()]))return false;return true;}
    if(old.is_array()){if(!now.is_array()||now.size()<old.size())return false;for(std::size_t i=0;i<old.size();++i)if(!preserves(old[i],now[i]))return false;return true;}
    return old==now;
}
CharacterDocument fromSnapshot(const CharacterDocument& prototype,const Json& s,const std::vector<Json>& history){auto d=prototype;d.choices=s.at("choices");d.rolls=s.value("rolls",Json::object());d.campaign=s.value("campaign",Json::object());d.resources=s.value("resources",Json::object());d.overrides=s.value("overrides",Json::array());d.advancement=Json::array();for(const auto& event:history)d.advancement.push_back(event);return d;}
void diff(const Json& a,const Json& b,const std::string& path,std::vector<std::string>& result){
    if(a==b)return;
    if((a.is_object()||a.is_null())&&(b.is_object()||b.is_null())&&(a.is_object()||b.is_object())){std::set<std::string> keys;if(a.is_object())for(auto it=a.begin();it!=a.end();++it)keys.insert(it.key());if(b.is_object())for(auto it=b.begin();it!=b.end();++it)keys.insert(it.key());for(const auto& key:keys)diff(a.is_object()?a.value(key,Json()):Json(),b.is_object()?b.value(key,Json()):Json(),path+"/"+escaped(key),result);return;}
    const bool indexed=path=="/features/warlock/invocations"||path=="/features/warlock/invocationTargets"||path=="/features/fighter/fightingStyle"||path=="/features/wizard/spellMastery"||path=="/features/warlock/arcanum";
    const bool records=(a.is_array()&&std::any_of(a.begin(),a.end(),[](const auto& v){return v.is_object()||v.is_array();}))||(b.is_array()&&std::any_of(b.begin(),b.end(),[](const auto& v){return v.is_object()||v.is_array();}));
    if((indexed||records)&&(a.is_array()||a.is_null())&&(b.is_array()||b.is_null())){const auto count=std::max(a.is_array()?a.size():0,b.is_array()?b.size():0);for(std::size_t i=0;i<count;++i)diff(a.is_array()&&i<a.size()?a[i]:Json(),b.is_array()&&i<b.size()?b[i]:Json(),path+"/"+std::to_string(i),result);return;}
    result.push_back(path);
}
int removals(const Json& before,const Json& after){
    if(before==after)return 0;
    if(before.is_array()&&after.is_array()){std::multiset<std::string> remaining;for(const auto& item:after)remaining.insert(item.dump());int removed=0;for(const auto& item:before){auto found=remaining.find(item.dump());if(found==remaining.end())++removed;else remaining.erase(found);}return removed;}
    return before.is_null()?0:1;
}
int additions(const Json& before,const Json& after){return removals(after,before);}
int listCount(const Json& j){return j.is_array()?static_cast<int>(j.size()):0;}
std::vector<std::string> featRoots(const Json& c,const ResolvedRuleset& rules){
    std::vector<std::string> result;
    auto add=[&](const std::string& feat,const std::string& path){const auto* e=rules.find(feat);if(e&&e->value("kind","")=="feat"&&e->contains("spellList"))result.push_back(path);};
    if(const auto* b=rules.find(text(c,"/backgroundId")))add(b->value("feat",""),"/magicInitiate/background");add(text(c,"/humanFeat"),"/magicInitiate/human");
    const auto* feats=at(c,"/feats");if(feats&&(feats->is_object()||feats->is_array()))for(const auto& [key,f]:feats->items())if(f.is_object())add(f.value("id",""),"/feats/"+key);
    const auto invocations=strings(c,"/features/warlock/invocations");for(std::size_t i=0;i<invocations.size();++i)if(invocations[i]=="srd55:lessons-of-the-first-ones")add(text(c,"/features/warlock/invocationTargets/"+std::to_string(i)),"/features/warlock/invocationGrants/"+std::to_string(i));
    return result;
}
std::vector<Policy> policies(const Json& c,const ResolvedRuleset& rules){
    std::vector<Policy> result;
    auto add=[&](const std::string& path,const std::string& cls,const std::string& page,int level,int longRest,int shortRest=0,int character=0,const std::string& budget="",bool any=false){result.push_back({safeId(path),path,budget.empty()?path:budget,cls,page,level,longRest,shortRest,character,any});};
    for(const auto& [id,l]:levels(c)){const auto* data=rules.find(id);if(!data)continue;const auto cls=classProfile(rules,id),base="/spellcasting/"+cls;const auto casting=data->value("casting",Json::object());if(casting.value("kind","none")!="none"){
        const auto cantrip=casting.value("cantripChange","");add(base+"/cantrips",cls,data->at("source").value("page","19"),cantrip=="class-level-one"?1:0,cantrip=="long-rest-one"?1:0);
        const auto prepared=casting.value("preparationChange","");add(base+"/preparedSpells",cls,data->at("source").value("page","19"),prepared=="class-level-one"?1:0,prepared=="long-rest-any"?999:prepared=="long-rest-one"?1:0,cls=="wizard"&&l>=5?1:0);
    }
    if(cls=="barbarian"||cls=="fighter"||cls=="paladin"||cls=="ranger"||cls=="rogue")add("/features/"+cls+"/weaponMasteries",cls,cls=="fighter"?"48":cls=="barbarian"?"29":cls=="rogue"?"62":"54-59",0,cls=="fighter"||cls=="barbarian"?1:999);
    if(cls=="fighter")add("/features/fighter/fightingStyle/0",cls,"47",1,0);
    if((cls=="paladin"||cls=="ranger")&&l>=2)add("/features/"+cls+"/warriorCantrips",cls,cls=="paladin"?"54":"59",1,0);
    if(cls=="bard"&&l>=6)add("/features/bard/magicalDiscoveries",cls,"35",1,0);
    if(cls=="druid"){if(l>=2)add("/features/druid/forms",cls,"42",0,1);if(l>=3)add("/features/druid/land",cls,"46",0,1);}
    if(cls=="ranger"){if(l>=3)add("/features/ranger/huntersPrey",cls,"61",0,1,1);if(l>=7)add("/features/ranger/defensiveTactics",cls,"61",0,1,1);}
    if(cls=="sorcerer"&&l>=2)add("/features/sorcerer/metamagic",cls,"66",1,0);
    if(cls=="warlock"){
        const auto inv=strings(c,"/features/warlock/invocations");for(std::size_t n=0;n<inv.size();++n){add("/features/warlock/invocations/"+std::to_string(n),cls,"71",1,0,0,0,"warlock-invocation");if(inv[n]=="srd55:agonizing-blast"||inv[n]=="srd55:eldritch-spear"||inv[n]=="srd55:repelling-blast"||inv[n]=="srd55:lessons-of-the-first-ones")add("/features/warlock/invocationTargets/"+std::to_string(n),cls,"72-74",1,0,0,0,"warlock-invocation");}
        add("/features/warlock/tomeCantrips",cls,"74",0,999,999);add("/features/warlock/tomeRituals",cls,"74",0,999,999);
        if(l>=10)add("/features/warlock/fiendishResilience",cls,"76",0,1,1);
        for(int sl=6;sl<=9;++sl)if(l>=2*sl-1)add("/features/warlock/arcanum/"+std::to_string(sl),cls,"72",1,0,0,0,"warlock-arcanum");
        add("/features/warlock/pactWeapon",cls,"74",0,0,0,0,"",true);add("/features/warlock/chainMovement",cls,"73",0,0);
    }
    if(cls=="wizard"&&l>=18)for(int sl=1;sl<=2;++sl)add("/features/wizard/spellMastery/"+std::to_string(sl),cls,"79",0,1,0,0,"wizard-spell-mastery");
    }
    if(text(c,"/lineageId")=="srd55:high-elf")add("/lineageCantrip","","84",0,1);
    for(const auto& root:featRoots(c,rules)){add(root+"/cantrips","","87",0,0,0,1,"magic-initiate:"+root);add(root+"/spell","","87",0,0,0,1,"magic-initiate:"+root);}
    return result;
}
int allowance(const Policy& p,const Parsed& history){if(p.atWill)return 999;if(history.trigger=="cast-find-familiar"&&p.path=="/features/warlock/chainMovement")return 1;if(history.trigger=="long-rest")return p.longRestLimit;if(history.trigger=="short-rest")return p.shortRestLimit;if(history.trigger=="advance")return p.characterLevelLimit>0?p.characterLevelLimit:history.triggerClass==p.cls?p.classLevelLimit:0;return 0;}
const Policy* matching(const std::vector<Policy>& choices,const std::string& path){const Policy* result=nullptr;for(const auto& p:choices)if(starts(path,p.path)&&(!result||p.path.size()>result->path.size()))result=&p;return result;}
Json invocationAcquisitions(const Json& c,const ResolvedRuleset& rules,const Json& input,std::vector<Message>& messages){
    Json result=Json::object();const auto inv=strings(c,"/features/warlock/invocations");const int warlock=profileLevel(c,rules,"warlock");
    for(std::size_t i=0;i<inv.size();++i){const auto* entry=rules.find(inv[i]);if(!entry)continue;const auto key=std::to_string(i);const int rank=number(input,"/invocationLevels/"+key,0);const int minimum=number(*entry,"/prerequisites/minWarlockLevel",1);
        int slotMinimum=1;const auto* cls=profileClass(c,rules,"warlock");if(cls)for(int l=1;l<=20;++l)if(number(*cls,"/progression/invocations/"+std::to_string(l-1),0)>static_cast<int>(i)){slotMinimum=l;break;}
        if(rank<std::max(minimum,slotMinimum)||rank>warlock){problem(messages,"baseline-acquisition","Declare a legal acquisition class level for invocation "+key+" (minimum "+std::to_string(std::max(minimum,slotMinimum))+", maximum "+std::to_string(warlock)+").","/invocationLevels/"+key,"71-74");continue;}
        result["/features/warlock/invocations/"+key]={{"classLevel",rank},{"characterLevel",characterAt(c,rules,"warlock",rank)},{"id",inv[i]},{"origin","declared-baseline"}};
    }
    for(std::size_t i=0;i<inv.size();++i){const auto* item=rules.find(inv[i]);if(!item)continue;const auto* dependencies=at(*item,"/prerequisites/invocations");if(!dependencies||!dependencies->is_array())continue;for(const auto& dependency:*dependencies)if(dependency.is_string()){
        const auto found=std::find(inv.begin(),inv.end(),dependency.get<std::string>());if(found==inv.end())continue;const auto parent="/features/warlock/invocations/"+std::to_string(found-inv.begin()),child="/features/warlock/invocations/"+std::to_string(i);
        if(result.contains(parent)&&result.contains(child)&&result[parent]["characterLevel"]>result[child]["characterLevel"])problem(messages,"baseline-prerequisite-time","An invocation cannot predate its prerequisite invocation.",child,"71-74");
    }}return result;
}
void updateAcquisitions(Parsed& history,const Json& before,const Json& after,const ResolvedRuleset* rules=nullptr){
    const auto old=strings(before,"/features/warlock/invocations"),now=strings(after,"/features/warlock/invocations");const int rank=rules?profileLevel(after,*rules,"warlock"):0;
    for(std::size_t i=0;i<now.size();++i){const auto root="/features/warlock/invocations/"+std::to_string(i),target="/features/warlock/invocationTargets/"+std::to_string(i);if(i>=old.size()||old[i]!=now[i]||get(before,target)!=get(after,target))history.acquisitions[root]={{"id",now[i]},{"classLevel",rank},{"characterLevel",total(after)},{"origin","recorded-event"}};}
    for(auto it=history.acquisitions.begin();it!=history.acquisitions.end();){const auto key=it.key();bool present=false;for(std::size_t i=0;i<now.size();++i)present=present||key=="/features/warlock/invocations/"+std::to_string(i);if(!present)it=history.acquisitions.erase(it);else ++it;}
}
std::vector<std::string> invocationScopes(const Json& before,const Json& after){
    std::vector<std::string> result;const auto old=strings(before,"/features/warlock/invocations"),now=strings(after,"/features/warlock/invocations");
    for(std::size_t i=0;i<std::max(old.size(),now.size());++i){const auto key=std::to_string(i);if(i<old.size()&&i<now.size()&&old[i]==now[i]&&get(before,"/features/warlock/invocationTargets/"+key)==get(after,"/features/warlock/invocationTargets/"+key))continue;
        result.push_back("/features/warlock/invocations/"+key);result.push_back("/features/warlock/invocationTargets/"+key);result.push_back("/features/warlock/invocationGrants/"+key);
        if(i<now.size()){if(now[i]=="srd55:pact-of-the-tome"){result.push_back("/features/warlock/tomeCantrips");result.push_back("/features/warlock/tomeRituals");}if(now[i]=="srd55:pact-of-the-blade")result.push_back("/features/warlock/pactWeapon");if(now[i]=="srd55:investment-of-the-chain-master")result.push_back("/features/warlock/chainMovement");}
    }return result;
}
// Changeable group lists are compared by selected values; owning invocation slots
// and their nested feat choices retain their stable indices.
std::map<std::string,int> changedBudgets(const Json& before,const Json& after,const std::vector<Policy>& policy){
    std::map<std::string,int> result;const auto owned=invocationScopes(before,after);for(const auto& p:policy){const auto a=get(before,p.path),b=get(after,p.path);if(a==b)continue;bool associated=false;for(const auto& scope:owned)associated=associated||starts(p.path,scope);if(associated&&p.budget!="warlock-invocation")continue;result[p.budget]+=std::max(1,removals(a,b));}
    const auto old=strings(before,"/features/warlock/invocations"),now=strings(after,"/features/warlock/invocations");int inv=0;for(std::size_t i=0;i<std::max(old.size(),now.size());++i)if(i>=old.size()||i>=now.size()||old[i]!=now[i]||get(before,"/features/warlock/invocationTargets/"+std::to_string(i))!=get(after,"/features/warlock/invocationTargets/"+std::to_string(i)))++inv;
    if(inv)result["warlock-invocation"]=inv;return result;
}
void validateInvocationDependencies(const Json& before,const Json& after,const ResolvedRuleset& rules,std::vector<Message>& errors){
    const auto old=strings(before,"/features/warlock/invocations"),now=strings(after,"/features/warlock/invocations");
    for(const auto& previous:old)if(std::find(now.begin(),now.end(),previous)==now.end())for(const auto& retained:now){const auto* item=rules.find(retained);if(!item)continue;for(const auto& requirement:strings(*item,"/prerequisites/invocations"))if(requirement==previous)problem(errors,"invocation-prerequisite","An invocation used as a prerequisite by a retained invocation cannot be replaced.","/choices/features/warlock/invocations","71");}
}
void validateReplacement(const Json& before,const Json& after,const ResolvedRuleset& rules,const Parsed& state,std::vector<Message>& errors){
    validateInvocationDependencies(before,after,rules,errors);
    const auto policy=policies(before,rules);std::vector<std::string> paths;diff(lockedChoices(before),lockedChoices(after),"",paths);const auto owned=invocationScopes(before,after);const auto budgets=changedBudgets(before,after,policy);
    for(const auto& path:paths){if(path=="/features/warlock/invocations")continue;bool associated=false;for(const auto& scope:owned)associated=associated||starts(path,scope);if(associated)continue;
        const auto* p=matching(policy,path);if(!p){problem(errors,"frozen-choice","This accepted selection has no replacement permission at this event.","/choices"+path);continue;}if(get(before,p->path).is_array()&&get(after,p->path).is_array()&&listCount(get(before,p->path))!=listCount(get(after,p->path)))problem(errors,"replacement-count","Replacement must preserve the number of selected options.","/choices"+p->path,p->page);
    }
    if(listCount(get(before,"/features/warlock/invocations"))!=listCount(get(after,"/features/warlock/invocations")))problem(errors,"invocation-count","An ordinary replacement cannot add or remove invocation slots.","/choices/features/warlock/invocations","71");
    for(const auto& [budget,used]:budgets){const auto found=std::find_if(policy.begin(),policy.end(),[&](const auto& p){return p.budget==budget;});if(found==policy.end()){problem(errors,"permission","No published replacement permission exists for this change.");continue;}const int limit=allowance(*found,state);const auto prior=state.used.find(budget);if(limit==0||used+(prior==state.used.end()?0:prior->second)>limit)problem(errors,"cadence","Replacement allowance is unavailable or already used for the latest recorded trigger.","/choices"+found->path,found->page);}
}
Evaluation core(const CharacterDocument& d,const ResolvedRuleset& rules){ReplayGuard guard;return evaluate(d,rules);}
std::map<std::string,Field> fieldMap(const Evaluation& e){std::map<std::string,Field> result;for(const auto& stage:e.stages)for(const auto& field:stage.fields)if(field.scope=="choices")result[field.path]=field;return result;}
std::set<std::string> newFields(const CharacterDocument& d,const Json& before,const Json& after,const ResolvedRuleset& rules,const std::vector<Json>& history,std::map<std::string,Field>* resultingFields=nullptr){
    auto prior=history;
    for(std::size_t i=history.size();i>0;--i){const auto& event=history[i-1];const auto kind=event.value("kind","");if(kind.find("begin-")==std::string::npos&&event.contains("after")&&sameLocked(event["after"],before)){prior.resize(i);break;}}
    const auto old=fieldMap(core(fromSnapshot(d,before,prior),rules)),now=fieldMap(core(fromSnapshot(d,after,history),rules));if(resultingFields)*resultingFields=now;std::set<std::string> result;for(const auto& [path,field]:now)if(!old.contains(path))result.insert(path);return result;
}
bool isNew(const std::set<std::string>& newPaths,const std::string& path){for(const auto& p:newPaths)if(starts(path,p))return true;return false;}
void validateAdvance(const CharacterDocument& d,const Json& before,const Json& after,const ResolvedRuleset& rules,const std::vector<Json>& history,std::vector<Message>& errors,std::map<std::string,int>* used=nullptr){
    const auto& a=before.at("choices");const auto& b=after.at("choices");const auto oldOrder=classOrder(a),newOrder=classOrder(b);
    if(total(b)!=total(a)+1||total(a)>=20||newOrder.size()!=oldOrder.size()+1||!std::equal(oldOrder.begin(),oldOrder.end(),newOrder.begin())){problem(errors,"advance-level","Advancement must add exactly one class level while preserving every earlier class event.");return;}
    validateInvocationDependencies(a,b,rules,errors);
    const auto target=classProfile(rules,newOrder.back());const auto available=policies(a,rules);std::map<std::string,Field> resultingFields;const auto added=newFields(d,before,after,rules,history,&resultingFields);std::vector<std::string> paths;diff(lockedChoices(a),lockedChoices(b),"",paths);
    const auto changedInvocation=invocationScopes(a,b);std::set<std::string> groupPaths;
    for(const auto& path:paths){
        if(path=="/level"||path=="/multiclass"||starts(path,"/advancement"))continue;
        if(isNew(added,path))continue;
        bool associated=false;for(const auto& scope:changedInvocation)associated=associated||starts(path,scope);if(associated&&target=="warlock")continue;
        const auto* p=matching(available,path);if(p){groupPaths.insert(p->path);continue;}
        problem(errors,"advance-frozen","This earlier selection cannot be edited during advancement.","/choices"+path);
    }
    std::map<std::string,int> consumed;
    for(const auto& path:groupPaths){const auto* p=matching(available,path);if(!p)continue;int removed=removals(get(a,path),get(b,path));const int extra=additions(get(a,path),get(b,path));
        // The shared evaluator exposes an old normal preparation as unavailable
        // when a newly gained class feature keeps it always prepared. Removing
        // that redundant counted entry does not unlearn or replace the spell.
        if(path.ends_with("/preparedSpells")&&resultingFields.contains(path)){
            const auto previous=get(a,path),current=get(b,path);
            if(previous.is_array()&&current.is_array())for(const auto& spell:previous)if(spell.is_string()&&std::find(current.begin(),current.end(),spell)==current.end()){
                const auto& options=resultingFields.at(path).options;const auto option=std::find_if(options.begin(),options.end(),[&](const auto& o){return Json(o.id)==spell;});
                if(option!=options.end()&&!option->available&&option->reason.find("always prepared")!=std::string::npos)--removed;
            }
        }
        const bool classGrowth=p->cls==target&&(path.starts_with("/spellcasting/")||path.ends_with("/weaponMasteries")||path=="/features/sorcerer/metamagic"||path=="/features/druid/forms");
        const int limit=p->characterLevelLimit?p->characterLevelLimit:p->cls==target?p->classLevelLimit:0;
        if(removed>limit||(!classGrowth&&extra>removed))problem(errors,"advance-replacement","This change exceeds the class-level replacement permission; new choices must be granted by the advancing class.","/choices"+path,p->page);
        if(removed)consumed[p->budget]+=removed;
    }
    if(target=="warlock"){
        const auto oldInv=strings(a,"/features/warlock/invocations"),nowInv=strings(b,"/features/warlock/invocations");int replaced=0;
        for(std::size_t i=0;i<oldInv.size();++i)if(i>=nowInv.size()||oldInv[i]!=nowInv[i]||get(a,"/features/warlock/invocationTargets/"+std::to_string(i))!=get(b,"/features/warlock/invocationTargets/"+std::to_string(i)))++replaced;
        if(replaced>1)problem(errors,"advance-invocations","A Warlock level permits replacing at most one existing invocation.","/choices/features/warlock/invocations","71");
        if(replaced)consumed["warlock-invocation"]=replaced;
    }
    for(const auto& [budget,count]:consumed){const auto p=std::find_if(available.begin(),available.end(),[&](const auto& item){return item.budget==budget;});if(p!=available.end()){const int limit=p->characterLevelLimit?p->characterLevelLimit:p->cls==target?p->classLevelLimit:0;if(count>limit)problem(errors,"advance-budget","The same per-level replacement allowance was spent more than once.","/choices"+p->path,p->page);}}
    if(used)*used=std::move(consumed);
}
bool validSnapshot(const Json& s){return s.is_object()&&s.contains("choices")&&s["choices"].is_object()&&s.contains("rolls")&&s["rolls"].is_object()&&s.contains("campaign")&&s["campaign"].is_object()&&s.contains("resources")&&s["resources"].is_object()&&s.contains("overrides")&&s["overrides"].is_array();}
void checkCore(const CharacterDocument& d,const Json& s,const ResolvedRuleset& r,const std::vector<Json>& history,std::vector<Message>& errors,const std::string& context){
    const auto result=core(fromSnapshot(d,s,history),r);for(const auto& m:result.messages)if(m.severity=="error")errors.push_back({"error","srd55.history.replay-rule",m.path,context+": "+m.text,m.sources});
}
Parsed parse(const CharacterDocument& d,const ResolvedRuleset* rules,bool checkRules){
    Parsed state;
    if(!d.advancement.is_array()){problem(state.messages,"format","Advancement/history must remain an array.");return state;}
    for(const auto& event:d.advancement){
        if(!event.is_object())continue;const auto kind=event.value("kind","");if(!kind.starts_with(prefix))continue;
        if(integerChoice(event,"version",-1)!=1){problem(state.messages,"version","Unsupported SRD history format version.");break;}
        const int seq=integerChoice(event,"sequence",-1);if(seq!=state.sequence+1){problem(state.messages,"sequence","History sequence is missing, duplicated, or out of order.");break;}
        state.sequence=seq;state.events.push_back(event);
        if(kind==std::string(prefix)+"baseline"){
            if(state.accepted||seq!=0||!validSnapshot(event.value("after",Json()))){problem(state.messages,"baseline","A single complete baseline must be the first history record.");break;}
            if(event.value("characterId","")!=d.id||event.value("edition","")!=d.edition||event.value("moduleVersion","")!=d.moduleVersion){problem(state.messages,"identity","The accepted baseline belongs to a different character or module version.");break;}
            state.accepted=true;state.stable=event["after"];state.acquisitions=event.value("acquisitions",Json::object());
            if(rules){auto verified=invocationAcquisitions(state.stable["choices"],*rules,event.value("inputs",Json::object()),state.messages);if(verified!=state.acquisitions)problem(state.messages,"acquisition-tampered","Baseline invocation acquisition records do not match their declared inputs.");}
            if(checkRules&&rules)checkCore(d,state.stable,*rules,state.events,state.messages,"Accepted baseline is not a valid character");
            continue;
        }
        if(!state.accepted){problem(state.messages,"no-baseline","A history event exists without an accepted baseline.");break;}
        if(!validSnapshot(event.value("before",Json()))||!validSnapshot(event.value("after",Json()))){problem(state.messages,"snapshot","History event has an invalid before/after snapshot.");break;}
        const auto& before=event["before"];const auto& after=event["after"];
        if(!sameLocked(state.stable,before)||!preserves(state.stable["rolls"],before["rolls"])||!preserves(before["rolls"],after["rolls"])){problem(state.messages,"continuity","A history event changes an earlier accepted snapshot or removes/changes accepted dice evidence.");break;}
        if(kind==std::string(prefix)+"begin-advance"||kind==std::string(prefix)+"begin-change"){
            if(!state.pending.empty()){problem(state.messages,"nested-pending","Finish or cancel the existing pending change first.");break;}
            if(kind==std::string(prefix)+"begin-advance"){
                const auto order=classOrder(after["choices"]),old=classOrder(before["choices"]);if(order.size()!=old.size()+1||!std::equal(old.begin(),old.end(),order.begin())||event.value("classId","")!=order.back())problem(state.messages,"advance-start","Pending advancement changes more than the selected next class level.");
            }else if(rules)validateReplacement(before["choices"],after["choices"],*rules,state,state.messages);
            state.pending=event;continue;
        }
        if(kind==std::string(prefix)+"commit-advance"||kind==std::string(prefix)+"commit-change"){
            if(state.pending.empty()||integerChoice(event,"beginSequence",-1)!=integerChoice(state.pending,"sequence",-2)){problem(state.messages,"commit","Commit has no matching pending operation.");break;}
            const bool advance=kind==std::string(prefix)+"commit-advance";
            if(state.pending.value("kind","")!=std::string(prefix)+(advance?"begin-advance":"begin-change")){problem(state.messages,"commit-kind","Commit kind does not match the pending operation.");break;}
            if(advance){if(classOrder(after["choices"]).back()!=state.pending.value("classId",""))problem(state.messages,"advance-class","The committed class differs from the class selected by Begin Advancement.");std::map<std::string,int> spent;if(rules)validateAdvance(d,before,after,*rules,state.events,state.messages,&spent);state.trigger="advance";state.triggerClass=rules?classProfile(*rules,classOrder(after["choices"]).back()):shortId(classOrder(after["choices"]).back());state.triggerSequence=seq;state.used=std::move(spent);}
            else if(rules){validateReplacement(before["choices"],after["choices"],*rules,state,state.messages);const auto available=policies(before["choices"],*rules);for(const auto& [budget,n]:changedBudgets(before["choices"],after["choices"],available)){const auto permission=std::find_if(available.begin(),available.end(),[&](const auto& p){return p.budget==budget;});if(permission!=available.end()&&permission->atWill)continue;state.used[budget]+=permission!=available.end()&&allowance(*permission,state)>=999?999:n;}}
            // A rules-free history read cannot infer a content ID's mechanics
            // profile. Preserve recorded acquisition metadata here; the public
            // evaluator validates its independent derivation with resolved rules.
            if(rules){
                updateAcquisitions(state,before["choices"],after["choices"],rules);
                if(event.value("acquisitions",Json::object())!=state.acquisitions)problem(state.messages,"acquisition-tampered","Recorded choice acquisition levels do not match the committed change.");
            }else state.acquisitions=event.value("acquisitions",Json::object());
            state.stable=after;state.pending=Json::object();
            if(checkRules&&rules)checkCore(d,after,*rules,state.events,state.messages,"Committed history snapshot is not a valid character");
        }else if(kind==std::string(prefix)+"cancel"){
            if(state.pending.empty()||integerChoice(event,"beginSequence",-1)!=integerChoice(state.pending,"sequence",-2)||!sameLocked(before,after)){problem(state.messages,"cancel","Cancellation must restore its pending operation's accepted choices.");break;}
            state.stable=after;state.pending=Json::object();
        }else if(kind==std::string(prefix)+"trigger"){
            if(!state.pending.empty()||!sameLocked(before,after)){problem(state.messages,"trigger-choice","A rest/trigger event cannot alter accepted choices or occur during a pending change.");break;}
            const auto trigger=event.value("trigger","");if(trigger!="short-rest"&&trigger!="long-rest"&&trigger!="cast-find-familiar"){problem(state.messages,"trigger","Unsupported history trigger.");break;}
            state.trigger=trigger;state.triggerClass.clear();state.triggerSequence=seq;state.used.clear();state.stable=after;
        }else if(kind==std::string(prefix)+"external-change"){
            if(!state.pending.empty()||event.value("operation","")!="copy-spell"){problem(state.messages,"external","Unsupported external change or change during pending work.");break;}
            auto a=lockedChoices(before["choices"]),b=lockedChoices(after["choices"]);const auto old=get(a,"/spellcasting/wizard/copiedSpells"),now=get(b,"/spellcasting/wizard/copiedSpells");
            if(!now.is_array()||now.size()!=(old.is_array()?old.size():0)+1||(!old.is_null()&&!preserves(old,now))){problem(state.messages,"copy-history","A spell-copy event must append one copying record without changing earlier records.");break;}
            set(a,"/spellcasting/wizard/copiedSpells",Json::array());set(b,"/spellcasting/wizard/copiedSpells",Json::array());if(a!=b)problem(state.messages,"copy-scope","Spell copying changed an unrelated accepted selection.");state.stable=after;
            if(checkRules&&rules)checkCore(d,after,*rules,state.events,state.messages,"Copied spell snapshot is not a valid character");
        }else {problem(state.messages,"event-kind","Unsupported SRD history event: "+kind);break;}
    }
    if(!state.accepted&&d.campaign.contains("srd55History"))problem(state.messages,"missing-ledger","This character is marked as tracked but its accepted baseline is missing.");
    if(state.accepted){
        if(!d.campaign.contains("srd55History"))problem(state.messages,"missing-marker","The accepted history marker has been removed.");
        const auto current=snapshot(d);
        if(state.pending.empty()){if(!sameLocked(state.stable,current))problem(state.messages,"unrecorded-edit","Accepted choices were edited outside a validated change or advancement command.","/choices");}
        else if(rules){if(state.pending.value("kind","")==std::string(prefix)+"begin-advance"){if(classOrder(current["choices"]).back()!=state.pending.value("classId",""))problem(state.messages,"advance-class","The pending class differs from the class selected by Begin Advancement.");validateAdvance(d,state.stable,current,*rules,state.events,state.messages);}else validateReplacement(state.stable["choices"],current["choices"],*rules,state,state.messages);}
        if(!preserves(state.stable.value("rolls",Json::object()),d.rolls))problem(state.messages,"roll-evidence","Previously accepted dice evidence was removed or changed.","/rolls");
    }
    return state;
}
void appendEvent(CharacterDocument& d,const Parsed& state,const std::string& kind,const Json& before,const Json& after,const Json& extra=Json::object()){
    Json event={{"kind",std::string(prefix)+kind},{"version",1},{"sequence",state.sequence+1},{"before",before},{"after",after},{"acquisitions",state.acquisitions}};
    for(auto it=extra.begin();it!=extra.end();++it)event[it.key()]=it.value();d.advancement.push_back(event);
}
void appendErrors(std::vector<Message>& target,const Evaluation& e){for(const auto& m:e.messages)if(m.severity=="error")target.push_back(m);}
TransitionResult rejected(const CharacterDocument& original,const std::string& code,const std::string& message){TransitionResult r;r.document=original;problem(r.messages,code,message);return r;}
std::string cadence(const Policy& p){if(p.atWill)return "As an explicit feature action";if(p.path=="/features/warlock/chainMovement")return "Cast Find Familiar through Pact of the Chain";std::string s;if(p.longRestLimit)s="Long Rest";if(p.shortRestLimit)s+=(s.empty()?"":" or ")+std::string("Short Rest");if(p.classLevelLimit)s="Gain a "+p.cls+" level";if(p.characterLevelLimit)s="Gain a character level";return s;}
std::vector<std::string> editablePendingPaths(const CharacterDocument& d,const ResolvedRuleset& rules,const Parsed& state,const Evaluation& e){
    std::vector<std::string> result;if(state.pending.empty())return result;const auto after=snapshot(d);const auto& before=state.stable;
    if(state.pending.value("kind","")==std::string(prefix)+"begin-advance"){
        const auto paths=newFields(d,before,after,rules,state.events);result.insert(result.end(),paths.begin(),paths.end());const auto target=classProfile(rules,classOrder(d.choices).back());for(const auto& p:policies(before["choices"],rules))if(p.cls==target||p.characterLevelLimit>0)result.push_back(p.path);
    }else for(const auto& p:policies(before["choices"],rules)){const auto used=state.used.find(p.budget);if(allowance(p,state)>(used==state.used.end()?0:used->second))result.push_back(p.path);}
    const auto owned=invocationScopes(before["choices"],d.choices);result.insert(result.end(),owned.begin(),owned.end());
    // New dependent feat/Tome fields are offered only inside their owning invocation.
    for(const auto& [path,field]:fieldMap(e))for(const auto& owner:owned)if(starts(path,owner))result.push_back(path);
    return result;
}
} // namespace

std::vector<Message> validateSrd55History(const CharacterDocument& d,const ResolvedRuleset& r){if(historyReplayDepth)return {};try{return parse(d,&r,true).messages;}catch(const std::exception& error){std::vector<Message> messages;problem(messages,"invalid-format",std::string("History cannot be read: ")+error.what());return messages;}}
std::map<std::string,int> srd55HistoryAcquisitionLevels(const CharacterDocument& d){
    std::map<std::string,int> result;Parsed state;try{state=parse(d,nullptr,false);}catch(...){return result;}if(!state.accepted)return result;
    if(!state.pending.empty())updateAcquisitions(state,state.stable.value("choices",Json::object()),d.choices);
    if(state.acquisitions.is_object())for(auto it=state.acquisitions.begin();it!=state.acquisitions.end();++it){const int n=number(it.value(),"/characterLevel",0);if(n>=1&&n<=20)result[it.key()]=n;}return result;
}
void appendHistoryActionsInternal(const CharacterDocument& d,const ResolvedRuleset& r,Evaluation& e){
    if(historyReplayDepth)return;const auto state=parse(d,&r,false);const auto fields=fieldMap(e);
    if(!state.accepted){ActionDefinition accept;accept.id=std::string(prefix)+"accept-baseline";accept.label="Accept character and start history";accept.description="Lock the completed creation choices. Later changes use their published advancement or rest permissions.";accept.available=e.complete()&&state.messages.empty();accept.reason=accept.available?"":"Complete and validate the character before accepting its baseline.";accept.sources={srd55v2::ref("23-26")};
        const auto invocations=strings(d.choices,"/features/warlock/invocations");const int warlock=profileLevel(d.choices,r,"warlock");for(std::size_t i=0;i<invocations.size();++i){const auto* item=r.find(invocations[i]);int minimum=item?number(*item,"/prerequisites/minWarlockLevel",1):1;if(const auto* cls=profileClass(d.choices,r,"warlock"))for(int l=1;l<=20;++l)if(number(*cls,"/progression/invocations/"+std::to_string(l-1),0)>static_cast<int>(i)){minimum=std::max(minimum,l);break;}accept.fields.push_back(integer("/invocationLevels/"+std::to_string(i),"Warlock class level when "+(item?item->value("name",invocations[i]):invocations[i])+" was acquired",minimum,warlock,"Declare the actual acquisition level; a later replacement uses its new acquisition level."));}e.actions.push_back(accept);return;
    }
    const auto editable=editablePendingPaths(d,r,state,e);
    for(auto& stage:e.stages)for(auto& f:stage.fields)if(f.scope=="choices"){
        if(f.path=="/armorId"||f.path=="/weaponId"||f.path=="/shield"||f.path=="/xp")continue;
        bool permitted=false;for(const auto& p:editable)permitted=permitted||starts(f.path,p);if(f.path=="/level"||f.path=="/classId"||f.path=="/multiclass"||starts(f.path,"/advancement"))permitted=false;
        f.editable=permitted;f.readOnlyReason=permitted?"":"Accepted selection. Use a permitted history change or advancement command.";
    }
    if(!state.pending.empty()){
        for(auto& action:e.actions)if(!action.id.starts_with(prefix)){action.available=false;action.reason="Commit or cancel the pending character choices before using lifecycle actions.";}
        e.actions.push_back({std::string(prefix)+"commit","Commit pending changes","Validate and accept the completed change, preserving every earlier history event.",{},e.complete()&&state.messages.empty(),e.complete()?"":"Complete the pending choices and resolve validation errors first.",{srd55v2::ref("23-26")}});
        e.actions.push_back({std::string(prefix)+"cancel","Cancel pending changes","Restore the last accepted choices; newly recorded dice evidence remains saved.",{},true,{}, {srd55v2::ref("23-26")}});return;
    }
    e.actions.push_back({std::string(prefix)+"begin-advance","Advance one character level","Choose the class gaining the next level, complete its new choices, then commit.",{select("/classId","Class gaining the level",options(r,"class"))},e.complete()&&total(d.choices)<20,total(d.choices)>=20?"The SRD character-level limit is20.":e.complete()?"":"Resolve current validation errors first.",{srd55v2::ref("23-26")}});
    for(const auto& p:policies(d.choices,r)){const auto f=fields.find(p.path);if(f==fields.end())continue;auto input=f->second;input.path="/value";input.scope="choices";input.editable=true;input.readOnlyReason.clear();const auto used=state.used.find(p.budget);const bool permitted=allowance(p,state)>(used==state.used.end()?0:used->second);const std::string reason=permitted?"":cadence(p)+" is required, or its replacement allowance is already used.";
        e.actions.push_back({std::string(prefix)+"replace."+p.id,"Change "+f->second.label,cadence(p)+". Retain unchanged selections; limited replacements share their owning feature's allowance.",{input},permitted&&e.complete(),reason,{srd55v2::ref(p.page)}});
        e.actions.back().initialInputs={{"value",get(d.choices,p.path)}};
        const auto books=resolveWizardSpellbooks(d,r);
        if(!books.hasAccessibleBook&&(p.path=="/spellcasting/wizard/preparedSpells"||p.path.starts_with("/features/wizard/spellMastery"))){e.actions.back().available=false;e.actions.back().reason="Carry an owned spellbook to study before changing these Wizard preparations.";}
    }
}

TransitionResult applyHistoryCommandInternal(const CharacterDocument& original,const ResolvedRuleset& rules,const CharacterCommand& command){
    TransitionResult result;result.document=original;
    if(original.edition!="srd55"||original.moduleVersion!="2.0.0")return rejected(original,"edition","History commands apply only to the expanded SRD 5.2.1 module.");
    if(!command.inputs.is_object())return rejected(original,"inputs","Command inputs must be an object.");
    if(!rules.valid()){result.messages=rules.messages;return result;}
    const auto state=parse(original,&rules,true);
    const auto id=command.id;
    if(id==std::string(prefix)+"cancel"){
        if(state.pending.empty())return rejected(original,"pending","There is no pending change to cancel.");
        auto priorEvents=state.events;priorEvents.pop_back();const auto prior=fromSnapshot(original,state.stable,priorEvents);const auto previous=parse(prior,&rules,true);if(!previous.messages.empty()){result.messages=previous.messages;return result;}
        result.document.choices=state.stable.at("choices");
        if(!preserves(state.stable.value("rolls",Json::object()),result.document.rolls))return rejected(original,"roll-evidence","Restore earlier accepted dice evidence before canceling.");
        appendEvent(result.document,state,"cancel",state.stable,snapshot(result.document),{{"beginSequence",integerChoice(state.pending,"sequence",-1)}});return result;
    }
    if(!state.messages.empty()){result.messages=state.messages;return result;}
    if(id==std::string(prefix)+"accept-baseline"){
        if(state.accepted)return rejected(original,"baseline-exists","This character already has an accepted baseline.");
        const auto before=core(original,rules);if(!before.complete()){appendErrors(result.messages,before);return result;}
        const auto acquisitions=invocationAcquisitions(original.choices,rules,command.inputs,result.messages);if(!result.messages.empty())return result;
        auto candidate=original;candidate.campaign["srd55History"]={{"version",1},{"characterId",candidate.id}};
        candidate.advancement.push_back({{"kind",std::string(prefix)+"baseline"},{"version",1},{"sequence",0},{"characterId",candidate.id},{"edition",candidate.edition},{"moduleVersion",candidate.moduleVersion},{"after",snapshot(candidate)},{"inputs",command.inputs},{"acquisitions",acquisitions}});
        const auto checked=evaluate(candidate,rules);if(!checked.complete()){appendErrors(result.messages,checked);return result;}result.document=std::move(candidate);return result;
    }
    if(!state.accepted)return rejected(original,"baseline-required","Accept the completed character baseline before recording lifecycle changes.");
    if(id==std::string(prefix)+"commit"){
        if(state.pending.empty())return rejected(original,"pending","There is no pending change to commit.");
        const auto checked=core(original,rules);if(!checked.complete()){appendErrors(result.messages,checked);return result;}
        auto candidate=original;const bool advancement=state.pending.value("kind","")==std::string(prefix)+"begin-advance";auto next=state;updateAcquisitions(next,state.stable.at("choices"),candidate.choices,&rules);
        if(advancement){auto accepted=original;accepted.choices=state.stable.at("choices");recordWizardBookAdvancement(accepted,candidate,rules);}
        appendEvent(candidate,state,advancement?"commit-advance":"commit-change",state.stable,snapshot(candidate),{{"beginSequence",integerChoice(state.pending,"sequence",-1)},{"inputs",command.inputs},{"acquisitions",next.acquisitions}});
        const auto verified=evaluate(candidate,rules);if(!verified.complete()){appendErrors(result.messages,verified);return result;}result.document=std::move(candidate);return result;
    }
    if(!state.pending.empty())return rejected(original,"pending","Complete or cancel the existing pending operation first.");
    const auto current=core(original,rules);if(!current.complete()){appendErrors(result.messages,current);return result;}
    if(id==std::string(prefix)+"begin-advance"){
        const auto target=text(command.inputs,"/classId");const auto* cls=rules.find(target);if(!cls||cls->value("kind","")!="class")return rejected(original,"class","Choose an available class for advancement.");
        const auto books=resolveWizardSpellbooks(original,rules);
        if(profileLevel(original.choices,rules,"wizard")>0&&classProfile(rules,target)=="wizard"&&(!books.hasAccessibleBook||(books.tracked&&!books.destinationAccessible)))return rejected(original,"spellbook-destination","Carry and choose a registered destination book before recording new Wizard research.");
        const int nextLevel=total(original.choices)+1;if(nextLevel>20)return rejected(original,"level-cap","Character level cannot exceed20.");
        auto candidate=original;const auto order=classOrder(original.choices);candidate.choices["level"]=nextLevel;
        if(get(original.choices,"/multiclass")==true||target!=text(original.choices,"/classId")){
            candidate.choices["multiclass"]=true;for(std::size_t i=1;i<order.size();++i)set(candidate.choices,"/advancement/"+std::to_string(i+1)+"/classId",order[i]);set(candidate.choices,"/advancement/"+std::to_string(nextLevel)+"/classId",target);
        }
        appendEvent(candidate,state,"begin-advance",state.stable,snapshot(candidate),{{"classId",target},{"inputs",command.inputs}});
        const auto history=parse(candidate,&rules,false);if(!history.messages.empty()){result.messages=history.messages;return result;}
        const auto preview=core(candidate,rules);for(const auto& m:preview.messages)if(m.severity=="error"&&(m.code.find("multiclass.prerequisite")!=std::string::npos||m.code.find("module.")!=std::string::npos)){result.messages.push_back(m);}
        if(!result.messages.empty())return result;result.document=std::move(candidate);return result;
    }
    const std::string replacePrefix=std::string(prefix)+"replace.";
    if(id.starts_with(replacePrefix)){
        const auto available=policies(original.choices,rules);const auto key=id.substr(replacePrefix.size());const auto found=std::find_if(available.begin(),available.end(),[&](const auto& p){return p.id==key;});if(found==available.end())return rejected(original,"choice","Unknown replacement group.");
        const auto books=resolveWizardSpellbooks(original,rules);
        if(!books.hasAccessibleBook&&(found->path=="/spellcasting/wizard/preparedSpells"||found->path.starts_with("/features/wizard/spellMastery")))return rejected(original,"spellbook-study","Carry an owned spellbook to study before changing these Wizard preparations.");
        const auto fields=fieldMap(current);const auto f=fields.find(found->path);if(f==fields.end())return rejected(original,"inactive-choice","This choice is not currently granted by the character.");
        const auto used=state.used.find(found->budget);if(allowance(*found,state)<=(used==state.used.end()?0:used->second))return rejected(original,"cadence","The latest recorded trigger does not allow this replacement, or its allowance is already used.");
        if(!command.inputs.contains("value"))return rejected(original,"value","Provide the replacement value or complete selection list.");
        const auto value=command.inputs["value"];std::vector<std::string> selected;
        if(f->second.kind=="select"){if(!value.is_string())return rejected(original,"value-type","A scalar choice requires one option identifier.");selected.push_back(value.get<std::string>());}
        else if(f->second.kind=="multiselect"){if(!value.is_array()||!std::all_of(value.begin(),value.end(),[](const auto& v){return v.is_string();}))return rejected(original,"value-type","This choice requires a list of option identifiers.");for(const auto& v:value)selected.push_back(v.get<std::string>());}
        else return rejected(original,"value-type","This accepted field does not support replacement commands.");
        std::set<std::string> distinctSelections;
        for(const auto& selectedId:selected){if(!distinctSelections.insert(selectedId).second)return rejected(original,"duplicate-value","Choose each option only once.");const auto option=std::find_if(f->second.options.begin(),f->second.options.end(),[&](const auto& o){return o.id==selectedId;});if(option==f->second.options.end()||!option->available)return rejected(original,"ineligible",option==f->second.options.end()?"Replacement content is unavailable.":option->reason);}
        if(get(original.choices,found->path)==value)return result;
        auto candidate=original;set(candidate.choices,found->path,value);std::vector<Message> permissions;validateReplacement(original.choices,candidate.choices,rules,state,permissions);if(!permissions.empty()){result.messages=std::move(permissions);return result;}
        appendEvent(candidate,state,"begin-change",state.stable,snapshot(candidate),{{"policyId",found->id},{"inputs",command.inputs}});
        const auto preview=core(candidate,rules);auto pending=parse(candidate,&rules,false);const auto editable=editablePendingPaths(candidate,rules,pending,preview);
        if(preview.complete()){
            auto next=pending;updateAcquisitions(next,state.stable["choices"],candidate.choices,&rules);appendEvent(candidate,pending,"commit-change",state.stable,snapshot(candidate),{{"beginSequence",pending.sequence},{"inputs",command.inputs},{"acquisitions",next.acquisitions}});
            const auto verified=evaluate(candidate,rules);if(!verified.complete()){appendErrors(result.messages,verified);return result;}
        }else{
            for(const auto& message:preview.messages)if(message.severity=="error"){
                std::string path=message.path;if(path.starts_with("/choices/"))path=path.substr(8);bool canRepair=false;for(const auto& p:editable)canRepair=canRepair||starts(path,p);
                if(!canRepair)result.messages.push_back(message);
            }
            if(!result.messages.empty())return result;
        }
        result.document=std::move(candidate);return result;
    }
    return rejected(original,"command","Unknown SRD history command.");
}

void appendSrd55HistoryActions(const CharacterDocument& d,const ResolvedRuleset& r,Evaluation& e){
    try{appendHistoryActionsInternal(d,r,e);}catch(const std::exception& error){problem(e.messages,"invalid-format",std::string("History actions are unavailable: ")+error.what());for(auto& stage:e.stages)for(auto& field:stage.fields)if(field.scope=="choices"){field.editable=false;field.readOnlyReason="Restore a valid history record before editing accepted selections.";}}
}
TransitionResult applySrd55HistoryCommand(const CharacterDocument& d,const ResolvedRuleset& r,const CharacterCommand& command){
    try{return applyHistoryCommandInternal(d,r,command);}catch(const std::exception& error){return rejected(d,"invalid-command",error.what());}
}

void recordSrd55HistoryTrigger(const CharacterDocument& before,CharacterDocument& after,const std::string& kind){
    const auto state=parse(before,nullptr,false);if(!state.accepted)return;if(!state.messages.empty())throw std::runtime_error(state.messages.front().text);if(!state.pending.empty())throw std::runtime_error("Finish or cancel pending history work before resting or opening another replacement window.");
    if(kind!="short-rest"&&kind!="long-rest"&&kind!="cast-find-familiar")throw std::runtime_error("Unsupported SRD history trigger.");
    if(!sameLocked(state.stable,snapshot(after))||!preserves(before.rolls,after.rolls))throw std::runtime_error("The trigger cannot alter accepted choices or earlier dice evidence.");
    appendEvent(after,state,"trigger",snapshot(before),snapshot(after),{{"trigger",kind}});
}
void recordSrd55HistoryChange(const CharacterDocument& before,CharacterDocument& after,const std::string& kind,const Json& inputs){
    const auto state=parse(before,nullptr,false);if(!state.accepted)return;if(!state.messages.empty())throw std::runtime_error(state.messages.front().text);if(!state.pending.empty())throw std::runtime_error("Finish or cancel pending history work before copying spells.");
    if(kind!="copy-spell")throw std::runtime_error("Unsupported external change to accepted SRD choices.");
    appendEvent(after,state,"external-change",snapshot(before),snapshot(after),{{"operation",kind},{"inputs",inputs}});
}
} // namespace dnd::srd55v2
