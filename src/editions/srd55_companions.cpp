#include "dnd/companions.hpp"
#include "dnd/lifecycle.hpp"
#include "srd55_v2_internal.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace dnd::srd55v2 {
namespace {
constexpr const char* spellId="srd55:find-familiar";
constexpr const char* actionPrefix="srd55.companions.";
struct Route {
    std::string id,label,owner,ability,profileId,resourceId,mode,page;
    int minimumMinutes=60;
    int materialCp=1000;
    bool slot=false,druid=false;
};
struct Familiar {std::string owner;Json record;};
void error(std::vector<Message>& messages,const std::string& code,const std::string& text,const std::string& path="/resources/familiars",const std::string& page="130") {messages.push_back({"error","srd55.companions."+code,path,text,{srd55v2::ref(page)}});}
std::string encoded(const std::string& value){static const char* hex="0123456789abcdef";std::string result;for(unsigned char c:value){result+=hex[c>>4];result+=hex[c&15];}return result;}
int clsLevel(const CharacterDocument& d,const std::string& cls){int result=0;const int total=std::clamp(number(d.choices,"/level",1),1,20);const auto initial=text(d.choices,"/classId");const auto* multi=at(d.choices,"/multiclass");for(int n=1;n<=total;++n){const auto id=n==1||!multi||*multi!=true?initial:text(d.choices,"/advancement/"+std::to_string(n)+"/classId");if(id=="srd55:"+cls)++result;}return result;}
bool invocation(const CharacterDocument& d,const std::string& id){if(clsLevel(d,"warlock")==0)return false;const auto known=strings(d.choices,"/features/warlock/invocations");return std::find(known.begin(),known.end(),"srd55:"+id)!=known.end();}
int statistic(const Evaluation& e,const std::string& id,int fallback=0){const auto* value=e.find(id);return value&&value->effective.is_number_integer()?value->effective.get<int>():fallback;}
bool effect(const CharacterDocument& d,const std::string& key){const auto* v=at(d.resources,"/effects/"+key);return v&&(v->is_boolean()?v->get<bool>():v->is_object()&&v->value("active",false));}
bool canAct(const CharacterDocument& d,const Evaluation& e){return !effect(d,"incapacitated")&&!effect(d,"unconscious")&&number(d.resources,"/hp",statistic(e,"hp.maximum",1))>0;}
bool canCast(const CharacterDocument& d,const Evaluation& e,bool noMaterials){
    if(!canAct(d,e))return false;
    if(std::any_of(e.messages.begin(),e.messages.end(),[](const auto& message){return message.code.ends_with("armor.untrained");}))return false;
    if(const auto* rage=e.find("feature.barbarian.rage");rage&&rage->normal.is_object()&&rage->normal.value("active",false))return false;
    if(!text(d.resources,"/wildShapeForm").empty()&&(clsLevel(d,"druid")<18||!noMaterials))return false;
    return true;
}
std::vector<Familiar> familiars(const CharacterDocument& d){std::vector<Familiar> result;const auto* all=at(d.resources,"/familiars");if(!all||!all->is_object())return result;for(auto it=all->begin();it!=all->end();++it)if(it.value().is_object()&&it.value().contains("form")&&it.value()["form"].is_string()&&!it.value()["form"].get<std::string>().empty())result.push_back({it.key(),it.value()});return result;}
long long money(const CharacterDocument& d,const Evaluation& e){
    if(d.resources.contains("currencyCp")){const auto& v=d.resources.at("currencyCp");if(!v.is_number_integer()||v<0||v>1000000000000LL)throw std::runtime_error("Current currencyCp must be a nonnegative supported whole-number balance.");return v.get<long long>();}
    const auto* remaining=e.find("money.remainingCp");if(!remaining||!remaining->effective.is_number_integer()||remaining->effective<0)throw std::runtime_error("Current material funds are unavailable.");return remaining->effective.get<long long>();
}
const ResourceDefinition* resource(const Evaluation& e,const std::string& id){const auto it=std::find_if(e.resources.begin(),e.resources.end(),[&](const auto& r){return r.id==id;});return it==e.resources.end()?nullptr:&*it;}
int available(const CharacterDocument& d,const ResourceDefinition& r){const auto it=d.resources.find(r.id);if(it==d.resources.end())return r.maximum;if(!it->is_number_integer()||*it<0||*it>r.maximum)throw std::runtime_error("Correct the saved current value of "+r.label+" before casting.");return it->get<int>();}
void spend(CharacterDocument& d,const Evaluation& e,const std::string& id){const auto* r=resource(e,id);if(!r)throw std::runtime_error("The selected resource is unavailable.");const int current=available(d,*r);if(current<1)throw std::runtime_error("No uses remain in "+r->label+".");d.resources[id]=current-1;}
std::vector<Choice> slotOptions(const CharacterDocument& d,const Evaluation& e){std::vector<Choice> out;for(const auto& r:e.resources)if(r.id.starts_with("spellSlots.")||r.id=="pactMagic.slots"){int remaining=0;try{remaining=available(d,r);}catch(...){}out.push_back({r.id,r.label+" ("+std::to_string(remaining)+")",r.maximum>0&&remaining>0,remaining>0?"":"No slots remain.",r.sources});}return out;}
bool legalForm(const Json& creature,bool chain){return creature.value("kind","")=="creature"&&((creature.value("type","")=="Beast"&&creature.value("cr",99.0)==0)||(chain&&creature.value("chainFamiliar",false)));}
std::vector<Choice> forms(const ResolvedRuleset& rules,bool chain){std::vector<Choice> out;for(const auto& [id,c]:rules.content)if(legalForm(c,chain))out.push_back({id,c.value("name",id),true,{}, {sourceFromJson(c.at("source"))}});std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.label<b.label;});return out;}
std::vector<Route> routes(const CharacterDocument& d,const Evaluation& e){
    std::vector<Route> out;std::set<std::string> emitted;
    const auto add=[&](Route route){if(emitted.insert(route.id).second)out.push_back(std::move(route));};
    const auto* book=at(e.moduleData,"/spellbooks/wizard");if(clsLevel(d,"wizard")>0&&book&&book->is_array()&&std::find(book->begin(),book->end(),Json(spellId))!=book->end())add({"wizard-book-ritual","Find Familiar — Wizard spellbook ritual","wizard","intelligence","wizard-book","","ritual","78, 104, 130",70,1000,false,false});
    if(invocation(d,"pact-of-the-chain"))add({"pact-chain","Find Familiar — Pact of the Chain","warlock","charisma","pact-of-the-chain","","magic-action","74, 130",0,1000,false,false});
    if(clsLevel(d,"druid")>=2){add({"wild-companion-shape","Find Familiar — spend Wild Shape","druid","wisdom","wild-companion","druid:wild-shape","magic-action","43",0,0,false,true});add({"wild-companion-slot","Find Familiar — Wild Companion with a slot","druid","wisdom","wild-companion","","magic-action","43",0,0,true,true});}
    const auto* profiles=at(e.moduleData,"/castingProfiles");if(profiles&&profiles->is_array())for(std::size_t index=0;index<profiles->size();++index){const auto& p=(*profiles)[index];if(!p.is_object()||p.value("spellId","")!=spellId||p.value("sourceType","")=="item")continue;
        const auto key=p.value("profileId","");if(key.empty())continue;const auto ability=p.value("ability","");if(ability.empty())continue;const auto reason=p.value("reason",p.value("classId","Spell grant"));const auto owner=p.value("classId","")=="srd55:wizard"?"wizard":p.value("classId","")=="srd55:warlock"?"warlock":"spell";
        // The Chain feature already exposes its faster unlimited casting route.
        if(reason=="Pact of the Chain")continue;
        const auto id=encoded(key);
        if(owner!=std::string("wizard")||!emitted.contains("wizard-book-ritual"))add({"ritual-"+id,"Find Familiar — ritual via "+reason,owner,ability,key,"","ritual","104, 130",70,1000,false,false});
        add({"slot-"+id,"Find Familiar — spell slot via "+reason,owner,ability,key,"","spell-slot","104, 130",60,1000,true,false});
        const auto free=p.value("freeUses",0);const auto resourceId=p.value("resourceId","");if(free>0&&!resourceId.empty())add({"free-"+id,"Find Familiar — free use via "+reason,owner,ability,key,resourceId,"feature-use","87, 130",60,1000,false,false});
    }
    return out;
}
bool otherwiseValid(const Evaluation& e){return std::none_of(e.messages.begin(),e.messages.end(),[](const auto& m){return m.severity=="error"&&!m.code.starts_with("srd55.companions.");});}
std::string routeFailure(const CharacterDocument& d,const Evaluation& e,const Route& route){
    if(!otherwiseValid(e))return "Resolve the character's rule errors before casting.";
    if(!canCast(d,e,route.materialCp==0))return "The character cannot currently cast this spell (HP, condition, untrained armor, Rage, or Wild Shape restriction).";
    try{if(route.materialCp>0&&money(d,e)<route.materialCp)return "Find Familiar requires10 GP of consumed incense; current funds are insufficient.";if(!route.resourceId.empty()){const auto* r=resource(e,route.resourceId);if(!r||available(d,*r)<1)return "No uses remain for this casting feature.";}if(route.slot){const auto opts=slotOptions(d,e);if(std::none_of(opts.begin(),opts.end(),[](const auto& o){return o.available;}))return "No eligible spell slots remain.";}}catch(const std::exception& ex){return ex.what();}
    return {};
}
Field boolean(const std::string& path,const std::string& label){return {path,label,"boolean",0,1,{},false,{}};}
Json spiritProfile(const CharacterDocument& d,const Evaluation& e,const Familiar& f,const Json& creature){
    auto profile=creature;profile["owner"]=f.owner;profile["familiarId"]=f.record.value("familiarId",d.id+":familiar");profile["type"]=f.owner=="druid"?"Fey":f.record.value("type","Fey");profile["maximumHP"]=creature.value("hp",1);profile["currentHP"]=f.record.contains("hp")?f.record["hp"]:Json(creature.value("hp",1));profile["status"]=f.record.value("status","active");if(profile["currentHP"].is_number_integer()&&profile["currentHP"]==0)profile["status"]="vanished";
    profile["spellId"]=spellId;profile["sourceProfileId"]=f.record.value("sourceProfileId","legacy-unrecorded");profile["telepathyRangeFeet"]=100;profile["touchSpellDeliveryRangeFeet"]=100;profile["touchDeliveryAction"]="familiar-reaction";profile["normalAttackAllowed"]=false;profile["reactionAttackByForgoingOwnerAttack"]=invocation(d,"pact-of-the-chain");profile["expiry"]=f.owner=="druid"||f.record.value("expiresLongRest",false)?"owner-long-rest":"persistent-until-dismissed-or-0-HP";
    if(invocation(d,"investment-of-the-chain-master")&&invocation(d,"pact-of-the-chain")){
        const auto movement=text(d.choices,"/features/warlock/chainMovement","fly");profile["speed"][movement=="swim"?"swim":"fly"]=40;profile["featureSaveDC"]=statistic(e,"warlock.spellDc",8+statistic(e,"modifier.charisma")+statistic(e,"proficiency",2));profile["bonusActionAttackCommand"]=true;profile["damageTypeOptions"]={"normal","necrotic","radiant"};profile["reactionResistanceToTriggeringDamage"]=true;
    }
    return profile;
}
std::string eventKey(const CharacterDocument& d){return "familiar-cast-"+std::to_string(d.advancement.size()+1);}
void clearForms(CharacterDocument& d){auto* all=&d.resources["familiars"];if(!all->is_object())*all=Json::object();for(auto it=all->begin();it!=all->end();++it)if(it.value().is_object()&&it.value().contains("form")){it.value()["lastForm"]=it.value()["form"];it.value()["form"]="";it.value()["status"]="replaced";}}
void requireCompleted(const Json& inputs){if(!inputs.contains("completed")||!inputs["completed"].is_boolean()||!inputs["completed"].get<bool>())throw std::runtime_error("Record a completed casting/action.");if(inputs.contains("interrupted")&&(!inputs["interrupted"].is_boolean()||inputs["interrupted"].get<bool>()))throw std::runtime_error("An interrupted casting does not produce a familiar.");}
int intInput(const Json& inputs,const std::string& key,int low,int high){if(!inputs.contains(key)||!inputs[key].is_number_integer()||inputs[key]<low||inputs[key]>high)throw std::runtime_error(key+" must be a whole number from "+std::to_string(low)+" to "+std::to_string(high)+".");return inputs[key].get<int>();}
}

void appendSrd55CompanionState(const CharacterDocument& d,const ResolvedRuleset& rules,Evaluation& e){
    const auto current=familiars(d);if(current.size()>1)error(e.messages,"multiple","Find Familiar permits only one familiar across all casting sources. A valid new casting replaces every prior owner-form record.");
    Json profiles=Json::array();
    for(const auto& f:current){const auto form=f.record.value("form","");const auto* creature=rules.find(form);if(!creature||creature->value("kind","")!="creature"){error(e.messages,"source-missing","The saved familiar form is unavailable; its selection is retained.","/resources/familiars/"+f.owner+"/form");continue;}
        const bool wasChain=f.record.value("chainAtCast",f.owner=="warlock");if(!legalForm(*creature,wasChain))error(e.messages,"form","This stat block is not a legal Find Familiar form for the recorded casting.","/resources/familiars/"+f.owner+"/form");
        const auto type=f.owner=="druid"?"Fey":f.record.value("type","Fey");if(type!="Fey"&&type!="Celestial"&&type!="Fiend")error(e.messages,"type","A Find Familiar spirit must be Celestial, Fey, or Fiend.","/resources/familiars/"+f.owner+"/type");
        if(f.record.contains("hp")&&(!f.record["hp"].is_number_integer()||f.record["hp"]<0||f.record["hp"]>creature->value("hp",1)))error(e.messages,"hp","Saved familiar HP is outside its form's range; the value is preserved.","/resources/familiars/"+f.owner+"/hp");
        profiles.push_back(spiritProfile(d,e,f,*creature));
    }
    if(!profiles.empty()){addCalculation(e,"companions.familiar","Find Familiar companion",profiles,{"One familiar across all casting sources. It acts independently, cannot attack normally, and retains separate HP and state.","Current class features modify its own profile; current character statistics are unchanged."},{srd55v2::ref("130"),srd55v2::ref("73-74")});e.sections.push_back({"Familiar",{"companions.familiar"},{}});}
    e.moduleData["companions.findFamiliar"]=profiles;
    for(auto& stage:e.stages)for(auto& field:stage.fields)if(field.scope=="resources"&&field.path.starts_with("/familiars/")){field.editable=false;field.readOnlyReason="Use familiar casting and state actions so costs, expiry, and the one-familiar limit remain consistent.";}
    for(auto& section:e.sections)if(section.title!="Familiar")section.calculationIds.erase(std::remove_if(section.calculationIds.begin(),section.calculationIds.end(),[](const auto& id){return id=="feature.warlock.familiar"||id=="feature.druid.familiar";}),section.calculationIds.end());
}

void appendSrd55CompanionActions(const CharacterDocument& d,const ResolvedRuleset& rules,Evaluation& e){
    // Actions are prepared after common statistic overrides. Refresh dependent
    // familiar DCs here while preserving both the normal calculation and any
    // explicit override of the companion profile itself.
    Json effectiveProfiles=Json::array();for(const auto& familiar:familiars(d))if(const auto* creature=rules.find(familiar.record.value("form",""));creature&&creature->value("kind","")=="creature")effectiveProfiles.push_back(spiritProfile(d,e,familiar,*creature));
    for(auto& calculation:e.calculations)if(calculation.id=="companions.familiar"&&calculation.overrideReason.empty()){calculation.effective=effectiveProfiles;e.moduleData["companions.findFamiliar"]=effectiveProfiles;}
    const bool chain=invocation(d,"pact-of-the-chain");const auto formChoices=forms(rules,chain);const auto spiritChoices=std::vector<Choice>{{"Celestial","Celestial",true,{}, {srd55v2::ref("130")}},{"Fey","Fey",true,{}, {srd55v2::ref("130")}},{"Fiend","Fiend",true,{}, {srd55v2::ref("130")}}};
    for(const auto& route:routes(d,e)){
        auto reason=routeFailure(d,e,route);std::vector<Field> fields={select("/form","Familiar form",formChoices)};if(!route.druid)fields.push_back(select("/type","Spirit type",spiritChoices));if(route.slot)fields.push_back(select("/slotResource","Spell slot to expend",slotOptions(d,e)));fields.push_back(integer("/minutes","Accepted casting time (minutes)",route.minimumMinutes,1000000,route.minimumMinutes==0?"This feature casts with a Magic action; zero completed minutes is valid.":"Rituals take the normal hour plus10 minutes."));fields.push_back(boolean("/completed","Casting completed"));fields.push_back(boolean("/interrupted","Casting was interrupted"));
        ActionDefinition action={std::string(actionPrefix)+"cast."+route.id,route.label,route.materialCp?"Complete the casting, consume10 GP of incense from current currency, and adopt one eligible familiar form.":"Complete Wild Companion, expend its selected resource, and summon a Fey familiar until the next Long Rest.",fields,reason.empty()&&!formChoices.empty(),reason,{srd55v2::ref(route.page)}};action.initialInputs={{"minutes",route.minimumMinutes},{"completed",false},{"interrupted",false},{"type","Fey"}};if(route.druid)action.initialInputs.erase("type");e.actions.push_back(std::move(action));
    }
    const auto current=familiars(d);if(current.size()!=1)return;const auto& f=current.front();const auto* c=rules.find(f.record.value("form",""));if(!c)return;const int hp=number(f.record,"/hp",c->value("hp",1));const auto status=f.record.value("status","active");const bool active=status=="active"&&hp>0,acting=canAct(d,e);
    e.actions.push_back({std::string(actionPrefix)+"dismiss","Dismiss familiar temporarily","Use a Magic action to send the familiar to its pocket dimension; its HP and identity are preserved.",{},active&&acting,active?acting?"":"The character cannot act.":"The familiar is not currently active.",{srd55v2::ref("130")}});
    e.actions.push_back({std::string(actionPrefix)+"return","Return dismissed familiar","Use a Magic action to return the same familiar to an unoccupied space within30 feet.",{integer("/distanceFeet","Return distance (feet)",0,30)},status=="dismissed"&&hp>0&&acting,"A living, temporarily dismissed familiar and an acting character are required.",{srd55v2::ref("130")}});
    e.actions.push_back({std::string(actionPrefix)+"dismiss-permanently","Dismiss familiar permanently","End this familiar's service. Calling it again requires a new casting.",{},acting,acting?"":"The character cannot act.",{srd55v2::ref("130")}});
    e.actions.push_back({std::string(actionPrefix)+"damage","Record familiar damage","Apply already resolved damage to the familiar's own HP. At0 HP it disappears and requires another casting.",{integer("/amount","Resolved damage",1,1000000)},active,active?"":"The familiar is not currently active.",{srd55v2::ref("130")}});
    e.actions.push_back({std::string(actionPrefix)+"heal","Record familiar healing","Apply already resolved healing to the active familiar, up to its form's HP maximum.",{integer("/amount","Resolved healing",1,1000000)},active&&hp<c->value("hp",1),"An active injured familiar is required.",{srd55v2::ref("130")}});
}

TransitionResult applySrd55CompanionCommand(const CharacterDocument& before,const ResolvedRuleset& rules,const CharacterCommand& command){
    TransitionResult result{before,{}};
    try{
        const auto e=evaluate(before,rules);const auto castPrefix=std::string(actionPrefix)+"cast.";
        if(command.id.starts_with(castPrefix)){
            const auto all=routes(before,e);const auto id=command.id.substr(castPrefix.size());const auto found=std::find_if(all.begin(),all.end(),[&](const auto& route){return route.id==id;});if(found==all.end())throw std::runtime_error("This Find Familiar casting permission is unavailable.");const auto reason=routeFailure(before,e,*found);if(!reason.empty())throw std::runtime_error(reason);requireCompleted(command.inputs);const int minutes=intInput(command.inputs,"minutes",found->minimumMinutes,1000000);
            const auto form=text(command.inputs,"/form");const auto* creature=rules.find(form);const bool chain=invocation(before,"pact-of-the-chain");if(!creature||!legalForm(*creature,chain))throw std::runtime_error("Choose a legal familiar form for this character's current features.");const auto type=found->druid?"Fey":text(command.inputs,"/type");if(type!="Fey"&&type!="Celestial"&&type!="Fiend")throw std::runtime_error("Choose Celestial, Fey, or Fiend spirit type.");
            auto& d=result.document;const long long balance=found->materialCp>0?money(before,e):0;if(found->materialCp>balance)throw std::runtime_error("Insufficient funds for the consumed incense.");if(found->materialCp)d.resources["currencyCp"]=balance-found->materialCp;
            int slotLevel=0;std::string spentResource=found->resourceId;
            if(found->slot){spentResource=text(command.inputs,"/slotResource");const auto options=slotOptions(before,e);if(std::none_of(options.begin(),options.end(),[&](const auto& choice){return choice.available&&choice.id==spentResource;}))throw std::runtime_error("Choose an available Spellcasting or Pact Magic slot.");if(spentResource=="pactMagic.slots")slotLevel=statistic(e,"pactMagic.level",1);else slotLevel=std::stoi(spentResource.substr(std::string("spellSlots.").size()));}
            if(!spentResource.empty())spend(d,e,spentResource);
            std::string identity=text(before.resources,"/findFamiliarIdentity",before.id+":familiar");const auto previous=familiars(before);if(previous.size()==1)identity=previous.front().record.value("familiarId",identity);else if(const auto* archived=at(before.resources,"/familiars");archived&&archived->is_object())for(const auto& record:*archived)if(record.is_object()&&record.value("status","")=="vanished"&&!text(record,"/familiarId").empty()){identity=text(record,"/familiarId");break;}clearForms(d);d.resources["findFamiliarIdentity"]=identity;
            d.resources["familiars"][found->owner]={{"familiarId",identity},{"form",form},{"type",type},{"status","active"},{"hp",creature->value("hp",1)},{"spellId",spellId},{"sourceProfileId",found->profileId},{"castingAbility",found->ability},{"castingMode",found->mode},{"castingMinutes",minutes},{"materialConsumedCp",found->materialCp},{"slotLevel",slotLevel},{"spentResource",spentResource},{"expiresLongRest",found->druid},{"chainAtCast",chain}};
            d.rolls["companionCastings"][eventKey(before)]={{"source",found->profileId},{"completedMinutes",minutes},{"form",form},{"materialConsumedCp",found->materialCp},{"spentResource",spentResource},{"slotLevel",slotLevel}};
            d.resources["lifecycle"]["restWindow"]="";recordSrd55HistoryTrigger(before,d,"cast-find-familiar");
        }else{
            const auto current=familiars(before);if(current.size()!=1)throw std::runtime_error("A single existing familiar is required for this state action.");const auto& f=current.front();const auto* creature=rules.find(f.record.value("form",""));if(!creature)throw std::runtime_error("The familiar's exact form source is unavailable.");auto& record=result.document.resources["familiars"][f.owner];const int hp=number(record,"/hp",creature->value("hp",1));const auto status=record.value("status","active");result.document.resources["findFamiliarIdentity"]=record.value("familiarId",before.id+":familiar");
            if(command.id==std::string(actionPrefix)+"dismiss"){if(status!="active"||hp<1||!canAct(before,e))throw std::runtime_error("A living active familiar and an acting character are required.");record["status"]="dismissed";}
            else if(command.id==std::string(actionPrefix)+"return"){if(status!="dismissed"||hp<1||!canAct(before,e))throw std::runtime_error("Only a living temporarily dismissed familiar can return without another casting.");intInput(command.inputs,"distanceFeet",0,30);record["status"]="active";}
            else if(command.id==std::string(actionPrefix)+"dismiss-permanently"){if(!canAct(before,e))throw std::runtime_error("The character cannot act.");record["lastForm"]=record["form"];record["form"]="";record["status"]="ended";}
            else if(command.id==std::string(actionPrefix)+"damage"){if(status!="active"||hp<1)throw std::runtime_error("The familiar is not active.");record["hp"]=std::max(0,hp-intInput(command.inputs,"amount",1,1000000));if(record["hp"]==0){record["lastForm"]=record["form"];record["form"]="";record["status"]="vanished";}}
            else if(command.id==std::string(actionPrefix)+"heal"){if(status!="active"||hp<1)throw std::runtime_error("Only an active living familiar can receive this healing.");record["hp"]=std::min(creature->value("hp",1),hp+intInput(command.inputs,"amount",1,1000000));}
            else throw std::runtime_error("Unknown companion command.");
        }
        const auto checked=evaluate(result.document,rules);for(const auto& m:checked.messages)if(m.severity=="error")result.messages.push_back(m);
    }catch(const std::exception& ex){error(result.messages,"command",ex.what());}
    if(!result.valid())result.document=before;return result;
}

void expireSrd55CompanionsOnLongRest(CharacterDocument& d){
    auto it=d.resources.find("familiars");if(it==d.resources.end()||!it->is_object())return;
    for(auto owner=it->begin();owner!=it->end();++owner)if(owner.value().is_object()&&(owner.key()=="druid"||owner.value().value("expiresLongRest",false))){auto& record=owner.value();if(record.contains("form")){record["lastForm"]=record["form"];record["form"]="";}record["status"]="expired";}
}
} // namespace dnd::srd55v2
