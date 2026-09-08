#include "srd55_v2_internal.hpp"
#include "dnd/srd55_inventory.hpp"
#include <algorithm>
#include <set>
#include <sstream>

namespace dnd::srd55v2 {
namespace {
const std::set<std::string> classProfiles={"barbarian","bard","cleric","druid","fighter","monk","paladin","ranger","rogue","sorcerer","warlock","wizard"};
const std::set<std::string> subclassProfiles={"path-of-the-berserker","college-of-lore","life-domain","circle-of-the-land","champion","warrior-of-the-open-hand","oath-of-devotion","hunter","thief","draconic-sorcery","fiend-patron","evoker"};
// These are the feature schedules the compiled handlers execute. Renaming a
// definition or changing its provenance does not change this contract. Moving,
// removing, or inventing a binding requires an implemented profile extension.
const std::map<std::string,std::string> featureSchedules={
 {"barbarian","1:rage 1:unarmored-defense 1:weapon-mastery 2:danger-sense 2:reckless-attack 3:barbarian-subclass 3:primal-knowledge 4:ability-score-improvement 5:extra-attack 5:fast-movement 7:feral-instinct 7:instinctive-pounce 8:ability-score-improvement 9:brutal-strike 11:relentless-rage 12:ability-score-improvement 13:improved-brutal-strike 15:persistent-rage 16:ability-score-improvement 17:improved-brutal-strike 18:indomitable-might 19:epic-boon 20:primal-champion"},
 {"bard","1:bardic-inspiration 1:spellcasting 2:expertise 2:jack-of-all-trades 3:bard-subclass 4:ability-score-improvement 5:font-of-inspiration 7:countercharm 8:ability-score-improvement 9:expertise 10:magical-secrets 12:ability-score-improvement 16:ability-score-improvement 18:superior-inspiration 19:epic-boon 20:words-of-creation"},
 {"cleric","1:divine-order 1:spellcasting 2:channel-divinity 3:cleric-subclass 4:ability-score-improvement 5:sear-undead 7:blessed-strikes 8:ability-score-improvement 10:divine-intervention 12:ability-score-improvement 14:improved-blessed-strikes 16:ability-score-improvement 19:epic-boon 20:greater-divine-intervention"},
 {"druid","1:druidic 1:primal-order 1:spellcasting 2:wild-companion 2:wild-shape 3:druid-subclass 4:ability-score-improvement 5:wild-resurgence 7:elemental-fury 8:ability-score-improvement 12:ability-score-improvement 15:improved-elemental-fury 16:ability-score-improvement 18:beast-spells 19:epic-boon 20:archdruid"},
 {"fighter","1:fighting-style 1:second-wind 1:weapon-mastery 2:action-surge 2:tactical-mind 3:fighter-subclass 4:ability-score-improvement 5:extra-attack 5:tactical-shift 6:ability-score-improvement 8:ability-score-improvement 9:indomitable 9:tactical-master 11:two-extra-attacks 12:ability-score-improvement 13:studied-attacks 14:ability-score-improvement 16:ability-score-improvement 19:epic-boon 20:three-extra-attacks"},
 {"monk","1:martial-arts 1:unarmored-defense 2:monks-focus 2:unarmored-movement 2:uncanny-metabolism 3:deflect-attacks 3:monk-subclass 4:ability-score-improvement 4:slow-fall 5:extra-attack 5:stunning-strike 6:empowered-strikes 7:evasion 8:ability-score-improvement 9:acrobatic-movement 10:heightened-focus 10:self-restoration 12:ability-score-improvement 13:deflect-energy 14:disciplined-survivor 15:perfect-focus 16:ability-score-improvement 18:superior-defense 19:epic-boon 20:body-and-mind"},
 {"paladin","1:lay-on-hands 1:spellcasting 1:weapon-mastery 2:fighting-style 2:paladins-smite 3:channel-divinity 3:paladin-subclass 4:ability-score-improvement 5:extra-attack 5:faithful-steed 6:aura-of-protection 8:ability-score-improvement 9:abjure-foes 10:aura-of-courage 11:radiant-strikes 12:ability-score-improvement 14:restoring-touch 16:ability-score-improvement 18:aura-expansion 19:epic-boon"},
 {"ranger","1:favored-enemy 1:spellcasting 1:weapon-mastery 2:deft-explorer 2:fighting-style 3:ranger-subclass 4:ability-score-improvement 5:extra-attack 6:roving 8:ability-score-improvement 9:expertise 10:tireless 12:ability-score-improvement 13:relentless-hunter 14:natures-veil 16:ability-score-improvement 17:precise-hunter 18:feral-senses 19:epic-boon 20:foe-slayer"},
 {"rogue","1:expertise 1:sneak-attack 1:thieves-cant 1:weapon-mastery 2:cunning-action 3:rogue-subclass 3:steady-aim 4:ability-score-improvement 5:cunning-strike 5:uncanny-dodge 6:expertise 7:evasion 7:reliable-talent 8:ability-score-improvement 10:ability-score-improvement 11:improved-cunning-strike 12:ability-score-improvement 14:devious-strikes 15:slippery-mind 16:ability-score-improvement 18:elusive 19:epic-boon 20:stroke-of-luck"},
 {"sorcerer","1:innate-sorcery 1:spellcasting 2:font-of-magic 2:metamagic 3:sorcerer-subclass 4:ability-score-improvement 5:sorcerous-restoration 7:sorcery-incarnate 8:ability-score-improvement 10:metamagic 12:ability-score-improvement 16:ability-score-improvement 17:metamagic 19:epic-boon 20:arcane-apotheosis"},
 {"warlock","1:eldritch-invocations 1:pact-magic 2:magical-cunning 3:warlock-subclass 4:ability-score-improvement 8:ability-score-improvement 9:contact-patron 11:mystic-arcanum 12:ability-score-improvement 13:mystic-arcanum 15:mystic-arcanum 16:ability-score-improvement 17:mystic-arcanum 19:epic-boon 20:eldritch-master"},
 {"wizard","1:arcane-recovery 1:ritual-adept 1:spellcasting 2:scholar 3:wizard-subclass 4:ability-score-improvement 5:memorize-spell 8:ability-score-improvement 12:ability-score-improvement 16:ability-score-improvement 18:spell-mastery 19:epic-boon 20:signature-spells"},
 {"champion","3:improved-critical 3:remarkable-athlete 7:additional-fighting-style 10:heroic-warrior 15:superior-critical 18:survivor"},
 {"circle-of-the-land","3:circle-of-the-land-spells 3:lands-aid 6:natural-recovery 10:natures-ward 14:natures-sanctuary"},
 {"college-of-lore","3:bonus-proficiencies 3:cutting-words 6:magical-discoveries 14:peerless-skill"},
 {"draconic-sorcery","3:draconic-resilience 3:draconic-spells 6:elemental-affinity 14:dragon-wings 18:dragon-companion"},
 {"evoker","3:evocation-savant 3:potent-cantrip 6:sculpt-spells 10:empowered-evocation 14:overchannel"},
 {"fiend-patron","3:dark-ones-blessing 3:fiend-spells 6:dark-ones-own-luck 10:fiendish-resilience 14:hurl-through-hell"},
 {"hunter","3:hunters-lore 3:hunters-prey 7:defensive-tactics 11:superior-hunters-prey 15:superior-hunters-defense"},
 {"life-domain","3:disciple-of-life 3:life-domain-spells 3:preserve-life 6:blessed-healer 17:supreme-healing"},
 {"oath-of-devotion","3:oath-of-devotion-spells 3:sacred-weapon 7:aura-of-devotion 15:smite-of-protection 20:holy-nimbus"},
 {"path-of-the-berserker","3:frenzy 6:mindless-rage 10:retaliation 14:intimidating-presence"},
 {"thief","3:fast-hands 3:second-story-work 9:supreme-sneak 13:use-magic-device 17:thiefs-reflexes"},
 {"warrior-of-the-open-hand","3:open-hand-technique 6:wholeness-of-body 11:fleet-step 17:quivering-palm"},
};
const std::map<std::string,std::string> subclassOwners={
 {"champion","fighter"},
 {"circle-of-the-land","druid"},
 {"college-of-lore","bard"},
 {"draconic-sorcery","sorcerer"},
 {"evoker","wizard"},
 {"fiend-patron","warlock"},
 {"hunter","ranger"},
 {"life-domain","cleric"},
 {"oath-of-devotion","paladin"},
 {"path-of-the-berserker","barbarian"},
 {"thief","rogue"},
 {"warrior-of-the-open-hand","monk"},
};
const std::map<std::string,std::set<std::string>> progressionColumns={
 {"barbarian",{"rages","rageDamage","weaponMastery"}},
 {"bard",{"bardicDie"}},
 {"cleric",{"channelDivinity"}},
 {"druid",{"wildShape","knownForms","maxFormCr"}},
 {"fighter",{"secondWind","weaponMastery","actionSurge","indomitable","attacks"}},
 {"monk",{"focusPoints","martialArtsDie","unarmoredMovement"}},
 {"paladin",{"channelDivinity","weaponMastery","layOnHands"}},
 {"ranger",{"favoredEnemy","weaponMastery"}},
 {"rogue",{"sneakAttack","weaponMastery"}},
 {"sorcerer",{"sorceryPoints","metamagicCount","innateSorcery"}},
 {"warlock",{"invocations","pactSlotLevel","pactSlotCount"}},
 {"wizard",{}},
};
// Columns still coupled to a compiled feature/lifecycle schedule cannot vary
// independently yet. Reject variations instead of accepting ignored effects.
const Json fixedProgressions=Json::parse(R"json({"druid":{"knownForms":[0,4,4,6,6,6,6,8,8,8,8,8,8,8,8,8,8,8,8,8],"maxFormCr":[0,0.25,0.25,0.5,0.5,0.5,0.5,1,1,1,1,1,1,1,1,1,1,1,1,1]},"paladin":{"layOnHands":[5,10,15,20,25,30,35,40,45,50,55,60,65,70,75,80,85,90,95,100]},"rogue":{"sneakAttack":["1d6","1d6","2d6","2d6","3d6","3d6","4d6","4d6","5d6","5d6","6d6","6d6","7d6","7d6","8d6","8d6","9d6","9d6","10d6","10d6"]},"sorcerer":{"metamagicCount":[0,2,2,2,2,2,2,2,2,4,4,4,4,4,4,4,6,6,6,6],"innateSorcery":[2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2]},"warlock":{"invocations":[1,3,3,3,5,5,6,6,7,7,7,8,8,8,9,9,9,10,10,10],"pactSlotLevel":[1,1,2,2,3,3,4,4,5,5,5,5,5,5,5,5,5,5,5,5],"pactSlotCount":[1,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,4,4,4,4]}})json");
const Json subclassSpellBindings=Json::parse(R"json({"champion":{},"circle-of-the-land":{"landSpells":{"arid":[{"level":3,"spells":["srd55:blur","srd55:burning-hands","srd55:fire-bolt"]},{"level":5,"spells":["srd55:fireball"]},{"level":7,"spells":["srd55:blight"]},{"level":9,"spells":["srd55:wall-of-stone"]}],"polar":[{"level":3,"spells":["srd55:fog-cloud","srd55:hold-person","srd55:ray-of-frost"]},{"level":5,"spells":["srd55:sleet-storm"]},{"level":7,"spells":["srd55:ice-storm"]},{"level":9,"spells":["srd55:cone-of-cold"]}],"temperate":[{"level":3,"spells":["srd55:misty-step","srd55:shocking-grasp","srd55:sleep"]},{"level":5,"spells":["srd55:lightning-bolt"]},{"level":7,"spells":["srd55:freedom-of-movement"]},{"level":9,"spells":["srd55:tree-stride"]}],"tropical":[{"level":3,"spells":["srd55:acid-splash","srd55:ray-of-sickness","srd55:web"]},{"level":5,"spells":["srd55:stinking-cloud"]},{"level":7,"spells":["srd55:polymorph"]},{"level":9,"spells":["srd55:insect-plague"]}]}},"college-of-lore":{},"draconic-sorcery":{"spellGrants":[{"level":3,"spells":["srd55:alter-self","srd55:chromatic-orb","srd55:command","srd55:dragons-breath"]},{"level":5,"spells":["srd55:fear","srd55:fly"]},{"level":7,"spells":["srd55:arcane-eye","srd55:charm-monster"]},{"level":9,"spells":["srd55:legend-lore","srd55:summon-dragon"]}]},"evoker":{},"fiend-patron":{"spellGrants":[{"level":3,"spells":["srd55:burning-hands","srd55:command","srd55:scorching-ray","srd55:suggestion"]},{"level":5,"spells":["srd55:fireball","srd55:stinking-cloud"]},{"level":7,"spells":["srd55:fire-shield","srd55:wall-of-fire"]},{"level":9,"spells":["srd55:geas","srd55:insect-plague"]}]},"hunter":{},"life-domain":{"spellGrants":[{"level":3,"spells":["srd55:aid","srd55:bless","srd55:cure-wounds","srd55:lesser-restoration"]},{"level":5,"spells":["srd55:mass-healing-word","srd55:revivify"]},{"level":7,"spells":["srd55:aura-of-life","srd55:death-ward"]},{"level":9,"spells":["srd55:greater-restoration","srd55:mass-cure-wounds"]}]},"oath-of-devotion":{"spellGrants":[{"level":3,"spells":["srd55:protection-from-evil-and-good","srd55:shield-of-faith"]},{"level":5,"spells":["srd55:aid","srd55:zone-of-truth"]},{"level":9,"spells":["srd55:beacon-of-hope","srd55:dispel-magic"]},{"level":13,"spells":["srd55:freedom-of-movement","srd55:guardian-of-faith"]},{"level":17,"spells":["srd55:commune","srd55:flame-strike"]}]},"path-of-the-berserker":{},"thief":{},"warrior-of-the-open-hand":{}})json");
const Json castingBindings=Json::parse(R"json({"barbarian":{"ability":"","list":"","kind":"none","learnMode":"prepared","preparationChange":"long-rest-any","cantripChange":"class-level-one"},"bard":{"ability":"charisma","list":"bard","kind":"full","learnMode":"known","preparationChange":"class-level-one","cantripChange":"class-level-one"},"cleric":{"ability":"wisdom","list":"cleric","kind":"full","learnMode":"prepared","preparationChange":"long-rest-any","cantripChange":"long-rest-one"},"druid":{"ability":"wisdom","list":"druid","kind":"full","learnMode":"prepared","preparationChange":"long-rest-any","cantripChange":"class-level-one"},"fighter":{"ability":"","list":"","kind":"none","learnMode":"prepared","preparationChange":"long-rest-any","cantripChange":"class-level-one"},"monk":{"ability":"","list":"","kind":"none","learnMode":"prepared","preparationChange":"long-rest-any","cantripChange":"class-level-one"},"paladin":{"ability":"charisma","list":"paladin","kind":"half","learnMode":"prepared","preparationChange":"long-rest-one","cantripChange":"class-level-one"},"ranger":{"ability":"wisdom","list":"ranger","kind":"half","learnMode":"prepared","preparationChange":"long-rest-one","cantripChange":"class-level-one"},"rogue":{"ability":"","list":"","kind":"none","learnMode":"prepared","preparationChange":"long-rest-any","cantripChange":"class-level-one"},"sorcerer":{"ability":"charisma","list":"sorcerer","kind":"full","learnMode":"known","preparationChange":"class-level-one","cantripChange":"class-level-one"},"warlock":{"ability":"charisma","list":"warlock","kind":"pact","learnMode":"known","preparationChange":"class-level-one","cantripChange":"class-level-one"},"wizard":{"ability":"intelligence","list":"wizard","kind":"full","learnMode":"book","preparationChange":"long-rest-any","cantripChange":"long-rest-one"}})json");
const std::map<std::string,std::set<std::string>> namedBindings={
 {"feat",{"srd55:ability-score-improvement","srd55:alert","srd55:archery","srd55:boon-of-combat-prowess","srd55:boon-of-dimensional-travel","srd55:boon-of-fate","srd55:boon-of-irresistible-offense","srd55:boon-of-spell-recall","srd55:boon-of-truesight","srd55:boon-of-the-night-spirit","srd55:defense","srd55:grappler","srd55:great-weapon-fighting","srd55:magic-initiate-cleric","srd55:magic-initiate-druid","srd55:magic-initiate-wizard","srd55:savage-attacker","srd55:skilled","srd55:two-weapon-fighting"}},
 {"invocation",{"srd55:agonizing-blast","srd55:armor-of-shadows","srd55:ascendant-step","srd55:devils-sight","srd55:devouring-blade","srd55:eldritch-mind","srd55:eldritch-smite","srd55:eldritch-spear","srd55:fiendish-vigor","srd55:gaze-of-two-minds","srd55:gift-of-the-depths","srd55:gift-of-the-protectors","srd55:investment-of-the-chain-master","srd55:lessons-of-the-first-ones","srd55:lifedrinker","srd55:mask-of-many-faces","srd55:master-of-myriad-forms","srd55:misty-visions","srd55:one-with-shadows","srd55:otherworldly-leap","srd55:pact-of-the-blade","srd55:pact-of-the-chain","srd55:pact-of-the-tome","srd55:repelling-blast","srd55:thirsting-blade","srd55:visions-of-distant-realms","srd55:whispers-of-the-grave","srd55:witch-sight"}},
 {"metamagic",{"srd55:careful-spell","srd55:distant-spell","srd55:empowered-spell","srd55:extended-spell","srd55:heightened-spell","srd55:quickened-spell","srd55:seeking-spell","srd55:subtle-spell","srd55:transmuted-spell","srd55:twinned-spell"}},
};
const std::set<std::string> abilities={"strength","dexterity","constitution","intelligence","wisdom","charisma"};
bool integer(const Json& j,int minimum,int maximum){if(!j.is_number_integer())return false;try{const auto n=j.get<long long>();return n>=minimum&&n<=maximum;}catch(...){return false;}}
bool array(const Json& j,std::size_t size,int minimum,int maximum){return j.is_array()&&j.size()==size&&std::all_of(j.begin(),j.end(),[&](const auto& n){return integer(n,minimum,maximum);});}
bool texts(const Json& j){return j.is_array()&&std::all_of(j.begin(),j.end(),[](const auto& n){return n.is_string();});}
Json spellMechanics(Json value){
    if(value.is_object()){value.erase("source");for(auto& child:value)child=spellMechanics(child);}
    else if(value.is_array())for(auto& child:value)child=spellMechanics(child);
    return value;
}
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
            textField("rulesProfile");if(!classProfiles.contains(text(j,"/rulesProfile")))fail(j,"rulesProfile","Unsupported class mechanics profile.");
            intField("hitDie",4,12);intField("fixedHp",3,7);intField("maxLevel",20,20);intField("skillCount",0,4);intField("multiclassSkillCount",0,4);
            for(const auto* key:{"saves","skills","armorTraining","weaponTraining","primaryAbilities","multiclassSkills","toolProficiencies"})stringArray(key);
            if(j.contains("primaryAbilities")&&texts(j["primaryAbilities"]))for(const auto& a:j["primaryAbilities"])if(!abilities.contains(a.get<std::string>()))fail(j,"primaryAbilities","Unknown ability prerequisite.");
            if(text(j,"/primaryAbilityMode")!="all"&&text(j,"/primaryAbilityMode")!="any")fail(j,"primaryAbilityMode","Expected all or any prerequisites.");
            for(const auto* key:{"xp","proficiency"})if(!j.contains(key)||!array(j[key],20,0,1000000000))fail(j,key,"Progression must have exactly twenty bounded integer rows.");
            if(j.contains("proficiency")&&j["proficiency"]!=Json::array({2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,6,6,6,6}))fail(j,"proficiency","The supported total-character proficiency schedule cannot vary by class.");
            if(!j.contains("featLevels")||!j["featLevels"].is_array()||!std::all_of(j["featLevels"].begin(),j["featLevels"].end(),[](const auto& n){return integer(n,1,20);}))fail(j,"featLevels","Feat levels must be character class levels 1–20.");
            if(!j.contains("casting")||!j["casting"].is_object())fail(j,"casting","Class requires a casting profile, including noncasters.");
            else{
                const auto& cast=j["casting"];const auto mode=text(cast,"/kind");
                const auto profile=text(j,"/rulesProfile");
                const std::set<std::string> castingFields={"ability","list","kind","learnMode","preparationChange","cantripChange","cantrips","prepared","pactSlots","pactSlotLevel","slots"};
                for(auto it=cast.begin();it!=cast.end();++it)if(!castingFields.contains(it.key()))fail(j,"casting/"+it.key(),"Unsupported casting property.");
                if(castingBindings.contains(profile))for(auto it=castingBindings[profile].begin();it!=castingBindings[profile].end();++it)
                    if(!cast.contains(it.key())||cast[it.key()]!=it.value())fail(j,"casting/"+it.key(),"Unsupported casting binding for this class profile.");
                if(mode!="none"&&mode!="full"&&mode!="half"&&mode!="pact")fail(j,"casting/kind","Unknown casting interpretation.");
                for(const auto* key:{"cantrips","prepared","pactSlots","pactSlotLevel"})if(!cast.contains(key)||!array(cast[key],20,0,30))fail(j,std::string("casting/")+key,"Casting progression requires twenty bounded entries.");
                if(!cast.contains("slots")||!cast["slots"].is_array()||cast["slots"].size()!=20)fail(j,"casting/slots","Spell slots require twenty rows.");
                else for(const auto& row:cast["slots"])if(!array(row,9,0,20))fail(j,"casting/slots","Each spell-slot row requires nine bounded counts.");
                if(mode!="none"&&!abilities.contains(text(cast,"/ability")))fail(j,"casting/ability","Unsupported spellcasting ability.");
            }
            if(!j.contains("multiclassTraining")||!j["multiclassTraining"].is_object())fail(j,"multiclassTraining","Class requires explicit multiclass training.");
            else for(const auto* key:{"armor","weapons","tools"})if(!j["multiclassTraining"].contains(key)||!texts(j["multiclassTraining"][key]))fail(j,"multiclassTraining","Training requires armor/weapons/tools arrays.");
            if(!j.contains("progression")||!j["progression"].is_object())fail(j,"progression","Class resource progression must be an object.");
            else {
                const auto profile=text(j,"/rulesProfile");
                const auto supported=progressionColumns.find(profile);
                if(supported!=progressionColumns.end())for(const auto& key:supported->second)
                    if(!j["progression"].contains(key))fail(j,"progression/"+key,"Required profile progression is missing.");
                if(fixedProgressions.contains(profile))for(auto it=fixedProgressions[profile].begin();it!=fixedProgressions[profile].end();++it)
                    if(j["progression"].contains(it.key())&&j["progression"][it.key()]!=it.value())fail(j,"progression/"+it.key(),"Variation of this compiled profile progression is not supported.");
                for(auto it=j["progression"].begin();it!=j["progression"].end();++it){
                    const auto path="progression/"+it.key();
                    if(supported==progressionColumns.end()||!supported->second.contains(it.key())){fail(j,path,"No interpreter supports this progression column for the selected profile.");continue;}
                    if(!it.value().is_array()||it.value().size()!=20){fail(j,path,"Each class-specific progression requires twenty rows.");continue;}
                    for(std::size_t row=0;row<20;++row){
                        const auto& value=it.value()[row];bool valid=false;
                        if(it.key()=="bardicDie"||it.key()=="martialArtsDie")valid=value=="1d4"||value=="1d6"||value=="1d8"||value=="1d10"||value=="1d12";
                        else if(it.key()=="sneakAttack") {if(value.is_string())for(int dice=1;dice<=20;++dice)valid=valid||value==std::to_string(dice)+"d6";}
                        else if(it.key()=="maxFormCr")valid=value.is_number()&&(value==0||value==0.125||value==0.25||value==0.5||value==1);
                        else {const int maximum=it.key()=="unarmoredMovement"?200:it.key()=="layOnHands"?1000:it.key()=="attacks"?20:100;valid=integer(value,it.key()=="attacks"?1:0,maximum);}
                        if(!valid)fail(j,path+"/"+std::to_string(row),"Invalid value for this executable progression column.");
                    }
                }
            }
        }
        if(kind=="class"||kind=="subclass"){
            if(kind=="subclass"){
                textField("classId");textField("rulesProfile");intField("minimumLevel",3,3);const auto profile=text(j,"/rulesProfile");
                if(!subclassProfiles.contains(profile))fail(j,"rulesProfile","Unsupported subclass profile.");
                else for(const auto* key:{"spellGrants","landSpells"}){
                    const auto& supported=subclassSpellBindings.at(profile);
                    if((supported.contains(key)&&(!j.contains(key)||spellMechanics(j[key])!=supported[key]))||(!supported.contains(key)&&j.contains(key)))fail(j,key,"Unsupported subclass spell-grant variation for this compiled profile.");
                }
            }
            if(!j.contains("features")||!j["features"].is_array())fail(j,"features","Features must be an array.");
            else {
                const auto profile=text(j,"/rulesProfile");
                std::set<std::pair<int,std::string>> expected,seen;
                if(const auto schedule=featureSchedules.find(profile);schedule!=featureSchedules.end()){
                    std::istringstream rows(schedule->second);std::string token;
                    while(rows>>token){const auto colon=token.find(':');expected.emplace(std::stoi(token.substr(0,colon)),profile+":"+token.substr(colon+1));}
                }
                for(std::size_t index=0;index<j["features"].size();++index){
                    const auto& f=j["features"][index];const auto path="features/"+std::to_string(index);
                    if(!f.is_object()||!f.contains("level")||!integer(f["level"],1,20)||!f.contains("name")||!f["name"].is_string()||!f.contains("description")||!f["description"].is_string()||!f.contains("source")||!source(f["source"])) {fail(j,path,"Every feature requires level, name, description and printed source.");continue;}
                    const auto* mechanics=at(f,"/mechanics");
                    if(!mechanics||!mechanics->is_object()||text(*mechanics,"/handler")!="srd55-v2"||text(*mechanics,"/feature").empty()||mechanics->size()!=2){fail(j,path+"/mechanics","Unknown feature mechanics handler or unsupported executable property.");continue;}
                    const auto binding=std::make_pair(f["level"].get<int>(),text(*mechanics,"/feature"));
                    if(!expected.contains(binding))fail(j,path+"/mechanics/feature","Unsupported feature binding or acquisition level for profile '"+profile+"'.");
                    if(!seen.insert(binding).second)fail(j,path+"/mechanics/feature","Duplicate executable feature binding.");
                }
                for(const auto& binding:expected)if(!seen.contains(binding))fail(j,"features","Profile '"+profile+"' requires binding '"+binding.second+"' at level "+std::to_string(binding.first)+"; feature removal or relocation is not supported.");
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
            if(!namedBindings.at(kind).contains(text(j,"/replaces",text(j,"/id"))))fail(j,"id","Alternate feat identities have no supported executable profile.");
            textField("category");boolField("repeatable",false);
            if(j.contains("abilityIncrease")){const auto& inc=j["abilityIncrease"];if(!inc.is_object()||!inc.contains("points")||!integer(inc["points"],1,2)||!inc.contains("maxPerAbility")||!integer(inc["maxPerAbility"],1,2)||!inc.contains("maxScore")||!integer(inc["maxScore"],20,30)||!inc.contains("abilities")||!texts(inc["abilities"]))fail(j,"abilityIncrease","Invalid ability-increase definition.");}
        }
        if(kind=="weapon"){textField("category");textField("damage");textField("properties");textField("mastery");for(const auto* key:{"ranged","finesse","heavy","twoHanded"})boolField(key);intField("costCp",0,1000000000);}
        if(kind=="armor"){textField("category");intField("ac",0,30);intField("dexCap",0,99);intField("strength",0,30);boolField("stealthDisadvantage");}
        if(kind=="shield")intField("ac",0,10);
        if(kind=="tool")textField("category",false);
        if(kind=="invocation"||kind=="metamagic"){
            const auto id=text(j,"/replaces",text(j,"/id"));
            if(!namedBindings.at(kind).contains(id))fail(j,"id","Alternate identities for this executable binding are not supported; they require a mechanics-profile extension.");
            const auto* mechanics=at(j,"/mechanics");const auto colon=id.find(':');
            if(!mechanics||!mechanics->is_object()||mechanics->size()!=2||text(*mechanics,"/handler")!="srd55-v2"||text(*mechanics,"/feature")!=kind+":"+id.substr(colon==std::string::npos?0:colon+1))fail(j,"mechanics/feature","Unsupported mechanic handler or feature binding.");
        }else if(j.contains("mechanics"))fail(j,"mechanics","This record kind has no top-level executable mechanics binding.");
        if(kind=="creature"){
            for(const auto* key:{"type","size","description"})textField(key);intField("ac",0,40);intField("hp",1,10000);
            if(!j.contains("cr")||!j["cr"].is_number()||j["cr"]<0||j["cr"]>30)fail(j,"cr","Challenge must be between zero and thirty.");
            for(const auto* key:{"abilities","saves","skills","speed"})if(!j.contains(key)||!j[key].is_object())fail(j,key,"Creature stat group must be an object.");
            if(j.contains("abilities")&&j["abilities"].is_object())for(const auto& a:abilities)if(!j["abilities"].contains(a)||!integer(j["abilities"][a],1,30))fail(j,"abilities/"+a,"Creature requires all six bounded ability scores.");
            const std::set<std::string> movements={"walk","burrow","climb","fly","swim"};
            const std::set<std::string> skills={"acrobatics","animal-handling","arcana","athletics","deception","history","insight","intimidation","investigation","medicine","nature","perception","performance","persuasion","religion","sleight-of-hand","stealth","survival"};
            for(const auto* group:{"speed","saves","skills"})if(j.contains(group)&&j[group].is_object())for(auto it=j[group].begin();it!=j[group].end();++it){
                const bool speed=std::string(group)=="speed",save=std::string(group)=="saves";
                const bool known=speed?movements.contains(it.key()):save?abilities.contains(it.key()):skills.contains(it.key());
                if(!known||!integer(it.value(),speed?0:-10,speed?1000:50))fail(j,std::string(group)+"/"+it.key(),"Unsupported stat name or unbounded/noninteger creature statistic.");
            }
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
        if(kind=="subclass"){
            require(j,text(j,"/classId"),"class");
            const auto expected=subclassOwners.find(text(j,"/rulesProfile"));
            if(expected!=subclassOwners.end()&&classProfile(rules,text(j,"/classId"))!=expected->second)
                out.push_back({"error","content.srd55.v2.profile",j.value("id","")+"/classId","Subclass mechanics profile requires a class using rulesProfile '"+expected->second+"'.",{sourceFromJson(j.at("source"))}});
        }
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
