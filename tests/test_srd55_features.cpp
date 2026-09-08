#include <catch2/catch_test_macros.hpp>
#include "../src/editions/srd55_v2_internal.hpp"
#include <fstream>
#include <cmath>

using namespace dnd;
namespace features=dnd::srd55v2;
namespace {
const ResolvedRuleset& featureRules(){
    // Feature unit tests use the checked-in source data; the integrated content
    // suite separately exercises validation and exact-version resolution.
    static const auto result=[](){ResolvedRuleset r;r.edition="srd55";r.moduleVersion="2.0.0";std::ifstream in(std::string(DND_DATA_DIR)+"/srd55-core-v2/content.json");Json entries;in>>entries;for(const auto& item:entries)r.content.emplace(item.at("id").get<std::string>(),item);return r;}();return result;
}
struct Fixture {
    CharacterDocument d;features::Context x;
    Fixture(const std::string& cls,int l):x{d,featureRules()}{
        d.edition="srd55";d.moduleVersion="2.0.0";x.classLevels={{"srd55:"+cls,l}};x.initialClass="srd55:"+cls;x.totalLevel=l;x.proficiency=2+(l-1)/4;
        x.scores={{"strength",14},{"dexterity",14},{"constitution",14},{"intelligence",16},{"wisdom",16},{"charisma",18}};
        for(const auto& [id,n]:x.scores)x.modifiers[id]=static_cast<int>(std::floor((n-10)/2.0));
        x.skillProficiencies={"srd55:arcana","srd55:perception","srd55:athletics","srd55:stealth"};x.weaponTraining={"simple","martial"};
        static const std::map<std::string,std::string> subs={{"barbarian","path-of-the-berserker"},{"bard","college-of-lore"},{"cleric","life-domain"},{"druid","circle-of-the-land"},{"fighter","champion"},{"monk","warrior-of-the-open-hand"},{"paladin","oath-of-devotion"},{"ranger","hunter"},{"rogue","thief"},{"sorcerer","draconic-sorcery"},{"warlock","fiend-patron"},{"wizard","evoker"}};
        if(l>=3)d.choices["subclasses"][cls]="srd55:"+subs.at(cls);
        d.choices["features"]["cleric"]={{"divineOrder","thaumaturge"},{"blessedStrikes","potent-spellcasting"}};
        d.choices["features"]["druid"]={{"primalOrder","magician"},{"elementalFury","potent-spellcasting"},{"land","arid"}};
        d.choices["features"]["ranger"]={{"huntersPrey","colossus-slayer"},{"defensiveTactics","escape-the-horde"}};
        d.choices["features"]["sorcerer"]["elementalAffinity"]="fire";
        d.choices["features"]["warlock"]["fiendishResilience"]="cold";
    }
    features::FeatureResult derived(){return features::evaluateClassFeatures(x);}
    features::FeatureResult choices(){return features::resolveClassChoices(x);}
};
Json calc(const features::FeatureResult& r,const std::string& id){const auto* found=r.evaluation.find(id);INFO(id);REQUIRE(found);return found->normal;}
int maximum(const features::FeatureResult& r,const std::string& id){auto it=std::find_if(r.evaluation.resources.begin(),r.evaluation.resources.end(),[&](const auto& resource){return resource.id==id;});INFO(id);REQUIRE(it!=r.evaluation.resources.end());return it->maximum;}
bool granted(const features::FeatureResult& r,const std::string& spell){return std::any_of(r.spellGrants.begin(),r.spellGrants.end(),[&](const auto& grant){return grant.value("spellId","")==spell;});}
void put(Json& target,const std::string& pointer,const Json& value){target[Json::json_pointer(pointer)]=value;}
}
TEST_CASE("SRD full class feature calculations are sourced and deterministic at all 240 levels", "[srd55-v2][features]") {
    for(const auto& cls:{"barbarian","bard","cleric","druid","fighter","monk","paladin","ranger","rogue","sorcerer","warlock","wizard"})for(int l=1;l<=20;++l){
        CAPTURE(cls,l);Fixture f(cls,l);const auto before=toJson(f.d);const auto r=f.derived();
        INFO(toJson(r.evaluation)["messages"].dump());CHECK(r.evaluation.complete());CHECK_FALSE(r.evaluation.calculations.empty());
        for(const auto& c:r.evaluation.calculations){CHECK_FALSE(c.sources.empty());CHECK_FALSE(c.steps.empty());CHECK(c.sources.front().publication=="System Reference Document 5.2.1");}
        CHECK(toJson(f.derived().evaluation)==toJson(r.evaluation));CHECK(toJson(f.d)==before);
        for(const auto& resource:r.evaluation.resources)CHECK(resource.maximum>=0);
    }
}
TEST_CASE("Barbarian rage progression unarmored defense and level20 capstone", "[srd55-v2][features]") {
    const std::array<int,20> rages={2,2,3,3,3,4,4,4,4,4,4,5,5,5,5,5,6,6,6,6};
    const std::array<int,20> damage={2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,4,4,4,4,4};
    for(int l=1;l<=20;++l){Fixture f("barbarian",l);auto r=f.derived();CHECK(maximum(r,"barbarian:rage")==rages[l-1]);CHECK(calc(r,"feature.barbarian.rageDamage")==damage[l-1]);}
    Fixture f("barbarian",20);f.x.modifiers["constitution"]=3;f.x.scores["constitution"]=16;f.x.shield=true;auto r=f.derived();CHECK(r.armorFormulas.at("Barbarian Unarmored Defense")==15); // Shield +2 is separately applied by shared equipment.
    f.x.armorCategory="heavy";f.x.armored=true;f.d.resources["effects"]["barbarian:rage"]=true;r=f.derived();CHECK(r.speedBonus==0);CHECK_FALSE(calc(r,"feature.barbarian.rage")["active"].get<bool>());
    f.x.armored=false;f.x.armorCategory.clear();r=f.derived();CHECK(r.speedBonus==10);CHECK(r.skillAbilityOverrides.at("srd55:perception")=="strength");CHECK(calc(r,"feature.barbarian.brutalStrike")["damageDice"]=="2d10");CHECK(calc(r,"feature.barbarian.brutalStrike")["maximumEffects"]==2);
    auto choices=f.choices();CHECK(choices.abilityBonuses.at("strength")==4);CHECK(choices.abilityBonuses.at("constitution")==4);CHECK(choices.abilityCaps.at("constitution")==25);
    f.d.resources["barbarian:relentless-rage-attempts"]=2;r=f.derived();CHECK(calc(r,"feature.barbarian.relentlessRageDC")==20);CHECK(calc(r,"feature.barbarian.relentlessRageHp")==40);
}
TEST_CASE("Bard expertise inspiration and half proficiency have distinct scopes", "[srd55-v2][features]") {
    for(const auto& [level,die]:std::vector<std::pair<int,int>>{{1,6},{4,6},{5,8},{9,8},{10,10},{14,10},{15,12},{20,12}}){Fixture f("bard",level);auto r=f.derived();CHECK(calc(r,"feature.bard.inspirationDie")==die);CHECK(maximum(r,"bard:inspiration")==4);}
    Fixture f("bard",5);auto r=f.derived();CHECK(r.halfProficiency==1);CHECK(2+r.halfProficiency==3);CHECK(2+2*f.x.proficiency==8); // Untrained ability+2 versus an expert at character5.
    f.x.modifiers["charisma"]=-1;CHECK(maximum(f.derived(),"bard:inspiration")==1);
    Fixture high("bard",20);r=high.derived();CHECK(granted(r,"srd55:power-word-heal"));CHECK(granted(r,"srd55:power-word-kill"));CHECK(calc(r,"feature.bard.wordsOfCreation")["maximumTargets"]==2);
    Fixture choice("bard",2);choice.d.choices["features"]["bard"]["expertise2"]={"srd55:arcana","srd55:athletics"};auto grants=choice.choices();CHECK(grants.expertise.contains("srd55:arcana"));CHECK(grants.expertise.contains("srd55:athletics"));
    choice.d.choices["features"]["bard"]["expertise2"]={"srd55:deception","srd55:athletics"};CHECK_FALSE(choice.choices().evaluation.complete());
}
TEST_CASE("Cleric orders channel capacities and Life healing use class and spell levels", "[srd55-v2][features]") {
    Fixture protector("cleric",1);protector.d.choices["features"]["cleric"]["divineOrder"]="protector";auto grants=protector.choices();CHECK(grants.armorTraining.contains("heavy"));CHECK(grants.weaponTraining.contains("martial"));
    Fixture thaumaturge("cleric",1);grants=thaumaturge.choices();CHECK(grants.cantripBonuses.at("srd55:cleric")==1);auto r=thaumaturge.derived();CHECK(r.skillBonuses.at("srd55:arcana")==3);CHECK(r.skillBonuses.at("srd55:religion")==3);
    for(const auto& [level,uses]:std::vector<std::pair<int,int>>{{2,2},{5,2},{6,3},{17,3},{18,4},{20,4}}){Fixture f("cleric",level);CHECK(maximum(f.derived(),"cleric:channel-divinity")==uses);}
    Fixture f("cleric",20);r=f.derived();CHECK(calc(r,"feature.cleric.divineSpark")["dice"]=="4d8");CHECK(calc(r,"feature.cleric.preserveLife")==100);CHECK(calc(r,"feature.cleric.discipleOfLife")["healingBonusBySlotLevel"]["1"]==3);CHECK(calc(r,"feature.cleric.discipleOfLife")["healingBonusBySlotLevel"]["9"]==11);CHECK(calc(r,"feature.cleric.potentTemporaryHp")==6);CHECK(calc(r,"feature.cleric.supremeHealing")["maximizeHealingDice"]==true);
    f.d.choices["features"]["cleric"]["blessedStrikes"]="divine-strike";CHECK(calc(f.derived(),"feature.cleric.divineStrike")["extraDamageDice"]=="2d8");
}
TEST_CASE("Druid Wild Shape preserves character HP and applies eligible physical forms", "[srd55-v2][features]") {
    Fixture f("druid",2);f.d.choices["features"]["druid"]["forms"]={"srd55:creature-cat","srd55:creature-rat","srd55:creature-spider","srd55:creature-wolf"};f.d.resources["wildShapeForm"]="srd55:creature-wolf";
    auto grants=f.choices();INFO(toJson(grants.evaluation)["messages"].dump());CHECK(grants.evaluation.complete());CHECK(grants.physicalAbilityOverrides.at("strength")==14);CHECK(grants.physicalAbilityOverrides.at("dexterity")==15);CHECK(grants.physicalAbilityOverrides.at("constitution")==12);CHECK_FALSE(grants.physicalAbilityOverrides.contains("wisdom"));CHECK(grants.transformedForm["ac"]==12); // Original SRD364 wolf table, visually checked.
    auto r=f.derived();CHECK(calc(r,"feature.druid.wildShape")["temporaryHP"]==2);CHECK(calc(r,"feature.druid.wildShape")["keepOriginalHP"]==true);CHECK(calc(r,"feature.druid.wildShape")["maximumCR"]==.25);CHECK(maximum(r,"druid:wild-shape")==2);
    f.d.choices["features"]["druid"]["forms"][0]="srd55:creature-hawk";CHECK_FALSE(f.choices().evaluation.complete());
    Fixture high("druid",20);r=high.derived();CHECK(maximum(r,"druid:wild-shape")==4);CHECK(calc(r,"feature.druid.wildShape")["knownForms"]==8);CHECK(calc(r,"feature.druid.wildShape")["flyAllowed"]==true);CHECK(calc(r,"feature.druid.naturalRecoveryLevels")==10);CHECK(calc(r,"feature.druid.landsAid")["healingDice"]=="4d6");CHECK(calc(r,"feature.druid.naturesWard")["resistance"]=="fire");CHECK(granted(r,"srd55:wall-of-stone"));
}
TEST_CASE("Fighter and Monk attack counts and resources do not use total level", "[srd55-v2][features]") {
    for(const auto& [level,count]:std::vector<std::pair<int,int>>{{1,1},{4,1},{5,2},{10,2},{11,3},{19,3},{20,4}}){Fixture f("fighter",level);CHECK(f.derived().attackCount==count);}
    Fixture fighter("fighter",20);auto r=fighter.derived();CHECK(maximum(r,"fighter:second-wind")==4);CHECK(maximum(r,"fighter:action-surge")==2);CHECK(maximum(r,"fighter:indomitable")==3);CHECK(calc(r,"feature.fighter.criticalMinimum")==18);CHECK(calc(r,"feature.fighter.survivorHealing")==7);
    Fixture monk("monk",20);r=monk.derived();CHECK(maximum(r,"monk:focus")==20);CHECK(calc(r,"feature.monk.martialArtsDie")==12);CHECK(r.speedBonus==30);CHECK(r.armorFormulas.at("Monk Unarmored Defense")==15);CHECK(calc(r,"feature.monk.slowFall")==100);CHECK(calc(r,"feature.monk.flurryOfBlows")["unarmedAttacks"]==3);CHECK(calc(r,"feature.monk.quiveringPalm")["damageDice"]=="10d12");
    auto grants=monk.choices();CHECK(grants.abilityBonuses.at("dexterity")==4);CHECK(grants.abilityBonuses.at("wisdom")==4);CHECK(grants.saveProficiencies.size()==6);
    monk.x.shield=true;r=monk.derived();CHECK(r.armorFormulas.empty());CHECK(r.speedBonus==0);CHECK(calc(r,"feature.monk.martialArts")["enabled"]==false);
    Fixture mixed("fighter",3);mixed.x.classLevels["srd55:ranger"]=2;mixed.x.totalLevel=5;mixed.x.proficiency=3;CHECK(mixed.derived().attackCount==1);mixed.x.classLevels["srd55:fighter"]=5;mixed.x.classLevels["srd55:ranger"]=5;CHECK(mixed.derived().attackCount==2);
}
TEST_CASE("Paladin and Ranger class resources permanent auras and Hunter effects", "[srd55-v2][features]") {
    Fixture paladin("paladin",20);auto r=paladin.derived();CHECK(maximum(r,"paladin:lay-on-hands")==100);CHECK(maximum(r,"paladin:channel-divinity")==3);CHECK(r.saveBonuses.at("constitution")==4);CHECK(calc(r,"feature.paladin.auraOfProtection")["emanationFeet"]==30);CHECK(calc(r,"feature.paladin.holyNimbus")["radiantDamage"]==10);CHECK(calc(r,"feature.paladin.radiantStrikes")["extraDamageDice"]=="1d8");
    paladin.d.resources["effects"]["incapacitated"]=true;CHECK(paladin.derived().saveBonuses.empty());
    Fixture ranger("ranger",20);r=ranger.derived();CHECK(maximum(r,"ranger:favored-enemy")==6);CHECK(maximum(r,"ranger:tireless")==3);CHECK(maximum(r,"ranger:natures-veil")==3);CHECK(r.speedBonus==10);CHECK(calc(r,"feature.ranger.favoredEnemy")["damageDie"]==10);CHECK(calc(r,"feature.ranger.superiorHuntersPrey")["damageDie"]==10);
    ranger.d.choices["features"]["ranger"]["huntersPrey"]="horde-breaker";CHECK(calc(ranger.derived(),"feature.ranger.hordeBreaker")["extraAttacks"]==1);
}
TEST_CASE("Rogue Thief feature scaling and save grants remain mechanical", "[srd55-v2][features]") {
    Fixture f("rogue",20);auto r=f.derived();CHECK(calc(r,"feature.rogue.sneakAttack")["extraDamageDice"]=="10d6");CHECK(calc(r,"feature.rogue.cunningStrike")["maximumEffects"]==2);CHECK(calc(r,"feature.rogue.cunningStrike")["saveDC"]==16);CHECK(calc(r,"feature.rogue.cunningStrike")["options"]["knock-out"]["dieCost"]==6);CHECK(r.statMinimums.at("proficient-skill-tool-d20")==10);CHECK(calc(r,"feature.rogue.useMagicDevice")["attunementMaximum"]==4);CHECK(maximum(r,"rogue:stroke-of-luck")==1);CHECK(calc(r,"feature.rogue.thiefsReflexes")["secondTurnInitiativeOffset"]==-10);
    auto grants=f.choices();CHECK(grants.saveProficiencies.contains("wisdom"));CHECK(grants.saveProficiencies.contains("charisma"));
}
TEST_CASE("Sorcerer Draconic HP AC affinity metamagic and source specific DC", "[srd55-v2][features]") {
    Fixture f("sorcerer",20);f.d.resources["effects"]["sorcerer:innate-sorcery"]=true;f.d.choices["features"]["sorcerer"]["metamagic"]={"srd55:careful-spell","srd55:empowered-spell","srd55:distant-spell","srd55:extended-spell","srd55:subtle-spell","srd55:quickened-spell"};auto r=f.derived();CHECK(r.hpBonus==20);CHECK(r.armorFormulas.at("Draconic Resilience")==16);CHECK(maximum(r,"sorcerer:sorcery-points")==20);CHECK(r.spellSaveBonuses.at("srd55:sorcerer")==1);CHECK_FALSE(r.spellSaveBonuses.contains("srd55:wizard"));CHECK(calc(r,"feature.sorcerer.metamagic-careful-spell")["maximumProtectedTargets"]==4);CHECK(calc(r,"feature.sorcerer.metamagic-quickened-spell")["pointCost"]==2);CHECK(calc(r,"feature.sorcerer.elementalAffinity")["damageBonus"]==4);CHECK(calc(r,"feature.sorcerer.dragonWings")["flySpeedFeet"]==60);CHECK(calc(r,"feature.sorcerer.restorationPoints")==10);
    Fixture mixed("monk",1);mixed.x.classLevels["srd55:sorcerer"]=3;mixed.d.choices["subclasses"]["sorcerer"]="srd55:draconic-sorcery";r=mixed.derived();CHECK(r.armorFormulas.at("Monk Unarmored Defense")==15);CHECK(r.armorFormulas.at("Draconic Resilience")==16);CHECK(r.hpBonus==3);
}
TEST_CASE("Warlock invocation prerequisites repeat targets pact effects and Fiend", "[srd55-v2][features]") {
    Fixture f("warlock",12);f.d.choices["features"]["warlock"]["invocations"]={"srd55:pact-of-the-blade","srd55:thirsting-blade","srd55:devouring-blade","srd55:lifedrinker","srd55:eldritch-smite","srd55:armor-of-shadows","srd55:eldritch-mind","srd55:devils-sight"};f.d.choices["features"]["warlock"]["pactWeapon"]="srd55:greatsword";f.d.choices["features"]["warlock"]["arcanum"]["6"]="srd55:circle-of-death";
    auto grants=f.choices();INFO(toJson(grants.evaluation)["messages"].dump());CHECK(grants.evaluation.complete());auto r=f.derived();CHECK(calc(r,"feature.warlock.pact-of-the-blade")["attackCount"]==3);CHECK(calc(r,"feature.warlock.pact-of-the-blade")["attackBonus"]==8);CHECK(calc(r,"feature.warlock.eldritch-smite")["damageDice"]=="6d8");CHECK(granted(r,"srd55:mage-armor"));
    f.x.classLevels["srd55:warlock"]=11;CHECK_FALSE(f.choices().evaluation.complete());f.x.classLevels["srd55:warlock"]=12;f.d.choices["features"]["warlock"]["invocations"][1]="srd55:fiendish-vigor";CHECK_FALSE(f.choices().evaluation.complete());
    Fixture high("warlock",20);r=high.derived();CHECK(calc(r,"feature.warlock.magicalCunningRecovery")==4);CHECK(calc(r,"feature.warlock.darkOnesBlessing")==24);CHECK(maximum(r,"warlock:dark-ones-luck")==4);CHECK(calc(r,"feature.warlock.hurlThroughHell")["damageDice"]=="8d10");
    Fixture familiar("warlock",5);familiar.d.choices["features"]["warlock"]["invocations"]={"srd55:pact-of-the-chain","srd55:investment-of-the-chain-master","srd55:armor-of-shadows","srd55:eldritch-mind","srd55:devils-sight"};familiar.d.choices["features"]["warlock"]["chainMovement"]="swim";familiar.d.resources["familiars"]["warlock"]["form"]="srd55:creature-imp";r=familiar.derived();CHECK(calc(r,"feature.warlock.familiar")["speed"]["swim"]==40);CHECK(calc(r,"feature.warlock.familiar")["featureSaveDC"]==15);
}
TEST_CASE("Wizard recovery and Evoker capstone calculations preserve spell scopes", "[srd55-v2][features]") {
    Fixture f("wizard",20);auto r=f.derived();CHECK(calc(r,"feature.wizard.arcaneRecoveryLevels")==10);CHECK(calc(r,"feature.wizard.empoweredEvocation")["damageBonus"]==3);CHECK(calc(r,"feature.wizard.sculptSpells")["protectedTargetsBySpellLevel"]["9"]==10);CHECK(calc(r,"feature.wizard.overchannel")["selfNecroticD12PerSlotLevel"]==0);
    f.d.resources["wizard:overchannel-uses"]=1;r=f.derived();CHECK(calc(r,"feature.wizard.overchannel")["selfNecroticD12PerSlotLevel"]==2);f.d.resources["wizard:overchannel-uses"]=2;CHECK(calc(f.derived(),"feature.wizard.overchannel")["selfNecroticD12PerSlotLevel"]==3);
    f.d.choices["features"]["wizard"]["scholar"]={"srd55:arcana"};f.d.choices["spellcasting"]["wizard"]["spellbook"]["1"]={"srd55:magic-missile","srd55:shield"};f.d.choices["spellcasting"]["wizard"]["spellbook"]["3"]={"srd55:mirror-image"};f.d.choices["spellcasting"]["wizard"]["spellbook"]["5"]={"srd55:fireball","srd55:fly"};f.d.choices["features"]["wizard"]["spellMastery"]["1"]="srd55:magic-missile";f.d.choices["features"]["wizard"]["spellMastery"]["2"]="srd55:mirror-image";f.d.choices["features"]["wizard"]["signatureSpells"]={"srd55:fireball","srd55:fly"};auto grants=f.choices();INFO(toJson(grants.evaluation)["messages"].dump());CHECK(grants.evaluation.complete());CHECK(granted(grants,"srd55:magic-missile"));CHECK(granted(grants,"srd55:fireball"));
    f.d.choices["features"]["wizard"]["spellMastery"]["1"]="srd55:shield";CHECK_FALSE(f.choices().evaluation.complete()); // Reaction spell, not an action.
}
TEST_CASE("Feature feat choices reject already owned nonrepeatable grants", "[srd55-v2][features]") {
    Fixture fighter("fighter",1);fighter.x.feats.insert("srd55:archery");fighter.d.choices["features"]["fighter"]["fightingStyle"]={"srd55:archery"};fighter.d.choices["features"]["fighter"]["weaponMasteries"]={"srd55:battleaxe","srd55:dagger","srd55:longbow"};CHECK_FALSE(fighter.choices().evaluation.complete());
    fighter.d.choices["features"]["fighter"]["fightingStyle"]={"srd55:defense"};CHECK(fighter.choices().evaluation.complete());
    Fixture mixed("fighter",1);mixed.x.classLevels["srd55:ranger"]=2;mixed.x.totalLevel=3;
    mixed.d.choices["features"]["fighter"]={{"fightingStyle",Json::array({"srd55:defense"})},{"weaponMasteries",Json::array({"srd55:battleaxe","srd55:dagger","srd55:longbow"})}};
    mixed.d.choices["features"]["ranger"]={{"fightingStyle",Json::array({"srd55:defense"})},{"expertise2",Json::array({"srd55:athletics"})},{"languages",Json::array({"srd55:elvish","srd55:dwarvish"})},{"weaponMasteries",Json::array({"srd55:longbow","srd55:dagger"})}};
    CHECK_FALSE(mixed.choices().evaluation.complete());mixed.d.choices["features"]["ranger"]["fightingStyle"][0]="srd55:archery";INFO(toJson(mixed.choices().evaluation)["messages"].dump());CHECK(mixed.choices().evaluation.complete());
    Fixture warlock("warlock",2);warlock.x.feats.insert("srd55:alert");warlock.d.choices["features"]["warlock"]["invocations"]={"srd55:armor-of-shadows","srd55:eldritch-mind","srd55:lessons-of-the-first-ones"};warlock.d.choices["features"]["warlock"]["invocationTargets"]={"","","srd55:alert"};CHECK_FALSE(warlock.choices().evaluation.complete());
    warlock.d.choices["features"]["warlock"]["invocationTargets"][2]="srd55:skilled";warlock.d.choices["features"]["warlock"]["invocationGrants"]["2"]["skilledChoices"]={"srd55:deception","srd55:history","srd55:insight"};CHECK(warlock.choices().evaluation.complete());
}
TEST_CASE("Wizard feature choices cannot use future grants or invalid copying records", "[srd55-v2][features]") {
    Fixture f("wizard",18);f.d.choices["features"]["wizard"]["scholar"]={"srd55:arcana"};
    f.d.choices["spellcasting"]["wizard"]["spellbook"]["3"]={"srd55:mirror-image"};
    f.d.choices["spellcasting"]["wizard"]["spellbook"]["19"]={"srd55:burning-hands"};
    f.d.choices["features"]["wizard"]["spellMastery"]["1"]="srd55:burning-hands";
    f.d.choices["features"]["wizard"]["spellMastery"]["2"]="srd55:mirror-image";
    CHECK_FALSE(f.choices().evaluation.complete());
    f.d.choices["spellcasting"]["wizard"]["savant"]["19"]={"srd55:burning-hands"};CHECK_FALSE(f.choices().evaluation.complete());
    f.d.choices["spellcasting"]["wizard"]["copiedSpells"]={{{"spellId","srd55:burning-hands"},{"paidCp",4999},{"minutes",120}}};CHECK_FALSE(f.choices().evaluation.complete());
    f.d.choices["spellcasting"]["wizard"]["copiedSpells"][0]["paidCp"]=5000;CHECK(f.choices().evaluation.complete());
    f.d.choices["spellcasting"]["wizard"]["copiedSpells"][0]["minutes"]=119;CHECK_FALSE(f.choices().evaluation.complete());
    f.d.choices["spellcasting"]["wizard"]["copiedSpells"]=Json::array();
    f.d.choices["spellcasting"]["wizard"]["spellbook"]["1"]={"srd55:mirror-image"};f.d.choices["spellcasting"]["wizard"]["spellbook"].erase("3");f.d.choices["spellcasting"]["wizard"]["spellbook"]["2"]={"srd55:burning-hands"};CHECK_FALSE(f.choices().evaluation.complete()); // Level1 book cannot grant a level2 spell.
}
TEST_CASE("Expertise prerequisites use proficiency at the feature acquisition event", "[srd55-v2][features]") {
    Fixture f("wizard",2);f.x.classLevels["srd55:bard"]=1;f.x.totalLevel=3;f.x.classLevelEvents={{"srd55:wizard",{1,2}},{"srd55:bard",{3}}};f.x.skillProficiencies.insert("srd55:religion");f.x.skillAcquisitionLevels["srd55:religion"]=3;f.d.choices["features"]["wizard"]["scholar"]={"srd55:religion"};
    auto r=f.choices();CHECK_FALSE(r.evaluation.complete());
    bool explained=false;for(const auto& stage:r.evaluation.stages)for(const auto& field:stage.fields)if(field.path=="/features/wizard/scholar")for(const auto& option:field.options)if(option.id=="srd55:religion"){CHECK_FALSE(option.available);CHECK(option.reason.find("character level 2")!=std::string::npos);CHECK(option.reason.find("character level 3")!=std::string::npos);explained=true;}CHECK(explained);
    f.x.skillAcquisitionLevels["srd55:religion"]=1;CHECK(f.choices().evaluation.complete());
    // A later Skilled feat cannot retroactively make the level2 selection legal.
    f.x.classLevels={{"srd55:wizard",4}};f.x.totalLevel=4;f.x.classLevelEvents={{"srd55:wizard",{1,2,3,4}}};f.x.feats.insert("srd55:skilled");f.x.skillAcquisitionLevels["srd55:religion"]=4;CHECK_FALSE(f.choices().evaluation.complete());
    f.x.skillAcquisitionLevels["srd55:religion"]=1;CHECK(f.choices().evaluation.complete());
    // Multiclass event time, rather than class-level number, also allows valid late acquisition.
    f.x.classLevels={{"srd55:wizard",2},{"srd55:bard",1}};f.x.classLevelEvents={{"srd55:bard",{1}},{"srd55:wizard",{2,3}}};f.x.totalLevel=3;f.x.skillAcquisitionLevels["srd55:religion"]=2;CHECK(f.choices().evaluation.complete());
}
TEST_CASE("Class skill grants publish their acquisition time for later Expertise", "[srd55-v2][features]") {
    Fixture f("barbarian",3);f.x.classLevels["srd55:wizard"]=2;f.x.totalLevel=5;f.x.classLevelEvents={{"srd55:barbarian",{1,4,5}},{"srd55:wizard",{2,3}}};
    f.d.choices["features"]["barbarian"]["primalKnowledge"]={"srd55:nature"};f.d.choices["features"]["barbarian"]["weaponMasteries"]={"srd55:battleaxe","srd55:dagger"};f.d.choices["features"]["wizard"]["scholar"]={"srd55:nature"};
    auto r=f.choices();CHECK(r.skillAcquisitionLevels.at("srd55:nature")==5);CHECK_FALSE(r.evaluation.complete());
    f.x.classLevelEvents={{"srd55:barbarian",{1,2,3}},{"srd55:wizard",{4,5}}};r=f.choices();CHECK(r.skillAcquisitionLevels.at("srd55:nature")==3);INFO(toJson(r.evaluation)["messages"].dump());CHECK(r.evaluation.complete());
}
TEST_CASE("Book of Shadows excludes spells already prepared from origins and class grants", "[srd55-v2][features]") {
    Fixture f("warlock",1);f.d.choices["features"]["warlock"]["invocations"]={"srd55:pact-of-the-tome"};f.d.choices["features"]["warlock"]["tomeCantrips"]={"srd55:guidance","srd55:light","srd55:mending"};f.d.choices["features"]["warlock"]["tomeRituals"]={"srd55:detect-magic","srd55:identify"};
    // Sage's Magic Initiate Wizard keeps Detect Magic always prepared.
    f.x.preparedSpells.insert("srd55:detect-magic");CHECK_FALSE(f.choices().evaluation.complete());
    f.d.choices["features"]["warlock"]["tomeRituals"]={"srd55:comprehend-languages","srd55:identify"};CHECK(f.choices().evaluation.complete());
    f.x.classLevels["srd55:druid"]=1;f.d.choices["features"]["warlock"]["tomeRituals"]={"srd55:speak-with-animals","srd55:identify"};CHECK_FALSE(f.choices().evaluation.complete());
    // A same-pass Blessed Warrior cantrip is already prepared too.
    f.x.classLevels.erase("srd55:druid");f.x.classLevels["srd55:paladin"]=2;f.d.choices["features"]["paladin"]["fightingStyle"]={"blessed-warrior"};f.d.choices["features"]["paladin"]["warriorCantrips"]={"srd55:guidance","srd55:sacred-flame"};f.d.choices["features"]["paladin"]["weaponMasteries"]={"srd55:battleaxe","srd55:dagger"};f.d.choices["features"]["warlock"]["tomeRituals"]={"srd55:comprehend-languages","srd55:identify"};CHECK_FALSE(f.choices().evaluation.complete());
    f.d.choices["features"]["warlock"]["tomeCantrips"][0]="srd55:message";CHECK(f.choices().evaluation.complete());
}
TEST_CASE("Lessons of First Ones cannot repeat an already owned Magic Initiate list", "[srd55-v2][features]") {
    Fixture f("warlock",2);f.x.feats.insert("srd55:magic-initiate-wizard");f.d.choices["features"]["warlock"]["invocations"]={"srd55:armor-of-shadows","srd55:eldritch-mind","srd55:lessons-of-the-first-ones"};f.d.choices["features"]["warlock"]["invocationTargets"]={"","","srd55:magic-initiate-wizard"};CHECK_FALSE(f.choices().evaluation.complete());f.d.choices["features"]["warlock"]["invocationTargets"][2]="srd55:magic-initiate-cleric";CHECK(f.choices().evaluation.complete());
}
TEST_CASE("Lessons Skilled preserves a distinct grant alongside an existing Human Skilled", "[srd55-v2][features]") {
    Fixture f("warlock",2);f.x.classLevelEvents={{"srd55:warlock",{1,2}}};f.x.feats.insert("srd55:skilled");
    const std::set<std::string> humanSkills={"srd55:arcana","srd55:history","srd55:religion"};f.x.skillProficiencies=humanSkills;for(const auto& skill:humanSkills)f.x.skillAcquisitionLevels[skill]=1;
    f.d.choices["skilledChoices"]={"srd55:arcana","srd55:history","srd55:religion"};
    f.d.choices["features"]["warlock"]["invocations"]={"srd55:armor-of-shadows","srd55:eldritch-mind","srd55:lessons-of-the-first-ones"};f.d.choices["features"]["warlock"]["invocationTargets"]={"","","srd55:skilled"};
    f.d.choices["features"]["warlock"]["invocationGrants"]["2"]["skilledChoices"]={"srd55:medicine","srd55:nature","srd55:survival"};
    const auto before=toJson(f.d);auto r=f.choices();INFO(toJson(r.evaluation)["messages"].dump());CHECK(r.evaluation.complete());REQUIRE(r.featGrants.size()==1);CHECK(r.featGrants[0]["id"]=="srd55:skilled");CHECK(r.featGrants[0]["path"]=="/features/warlock/invocationGrants/2");CHECK(r.featGrants[0]["level"]==2);CHECK_FALSE(r.feats.contains("srd55:skilled"));
    auto all=humanSkills;all.insert(r.skillProficiencies.begin(),r.skillProficiencies.end());CHECK(all.size()==6);CHECK(r.skillAcquisitionLevels.at("srd55:medicine")==2);CHECK(toJson(f.d)==before);
    f.d.choices["features"]["warlock"]["invocationGrants"]["2"]["skilledChoices"][0]="srd55:arcana";CHECK_FALSE(f.choices().evaluation.complete());
}
TEST_CASE("Lessons Skilled proficiencies are available to later Wizard Scholar", "[srd55-v2][features]") {
    Fixture f("warlock",2);f.x.classLevels["srd55:wizard"]=2;f.x.totalLevel=4;f.x.classLevelEvents={{"srd55:warlock",{1,2}},{"srd55:wizard",{3,4}}};f.x.skillProficiencies={"srd55:deception","srd55:intimidation"};
    f.d.choices["features"]["warlock"]["invocations"]={"srd55:armor-of-shadows","srd55:eldritch-mind","srd55:lessons-of-the-first-ones"};f.d.choices["features"]["warlock"]["invocationTargets"]={"","","srd55:skilled"};f.d.choices["features"]["warlock"]["invocationGrants"]["2"]["skilledChoices"]={"srd55:arcana","srd55:history","srd55:religion"};f.d.choices["features"]["wizard"]["scholar"]={"srd55:arcana"};
    auto r=f.choices();INFO(toJson(r.evaluation)["messages"].dump());CHECK(r.evaluation.complete());CHECK(r.skillAcquisitionLevels.at("srd55:arcana")==2);CHECK(r.expertise.contains("srd55:arcana"));
    f.x.classLevelEvents={{"srd55:wizard",{1,2}},{"srd55:warlock",{3,4}}};r=f.choices();CHECK(r.skillAcquisitionLevels.at("srd55:arcana")==4);CHECK_FALSE(r.evaluation.complete());
}
TEST_CASE("Lessons Skilled accepts tools and rejects prior tool proficiency", "[srd55-v2][features]") {
    Fixture f("warlock",2);f.x.toolProficiencies={"srd55:thieves-tools"};f.x.toolAcquisitionLevels["srd55:thieves-tools"]=1;f.d.choices["features"]["warlock"]["invocations"]={"srd55:armor-of-shadows","srd55:eldritch-mind","srd55:lessons-of-the-first-ones"};f.d.choices["features"]["warlock"]["invocationTargets"]={"","","srd55:skilled"};f.d.choices["features"]["warlock"]["invocationGrants"]["2"]["skilledChoices"]={"srd55:medicine","srd55:nature","srd55:thieves-tools"};CHECK_FALSE(f.choices().evaluation.complete());
    f.d.choices["features"]["warlock"]["invocationGrants"]["2"]["skilledChoices"][2]="srd55:smiths-tools";auto r=f.choices();CHECK(r.evaluation.complete());CHECK(r.toolProficiencies.contains("srd55:smiths-tools"));CHECK(r.skillProficiencies.size()==2);
    f.x.toolAcquisitionLevels["srd55:thieves-tools"]=4;f.d.choices["features"]["warlock"]["invocationGrants"]["2"]["skilledChoices"][2]="srd55:thieves-tools";CHECK(f.choices().evaluation.complete());
}
TEST_CASE("Early Lessons Skilled supports Expertise in class families evaluated before Warlock", "[srd55-v2][features]") {
    Fixture f("bard",2);f.x.classLevels["srd55:warlock"]=2;f.x.totalLevel=4;f.x.classLevelEvents={{"srd55:warlock",{1,2}},{"srd55:bard",{3,4}}};f.x.skillProficiencies={"srd55:performance","srd55:persuasion"};f.d.choices["features"]["bard"]["expertise2"]={"srd55:arcana","srd55:history"};
    f.d.choices["features"]["warlock"]["invocations"]={"srd55:armor-of-shadows","srd55:eldritch-mind","srd55:lessons-of-the-first-ones"};f.d.choices["features"]["warlock"]["invocationTargets"]={"","","srd55:skilled"};f.d.choices["features"]["warlock"]["invocationGrants"]["2"]["skilledChoices"]={"srd55:arcana","srd55:history","srd55:religion"};
    auto r=f.choices();INFO(toJson(r.evaluation)["messages"].dump());CHECK(r.evaluation.complete());CHECK(r.expertise.contains("srd55:arcana"));CHECK(r.expertise.contains("srd55:history"));CHECK(r.skillAcquisitionLevels.at("srd55:arcana")==2);
    f.x.classLevelEvents={{"srd55:bard",{1,2}},{"srd55:warlock",{3,4}}};CHECK_FALSE(f.choices().evaluation.complete());
}
