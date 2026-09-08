#include "dnd/content.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using namespace dnd;
namespace {
ContentPack srdPack() {
    auto loaded = loadPack(std::filesystem::path(DND_DATA_DIR) / "srd55-core");
    for (const auto& message : loaded.messages) INFO(message.path + ": " + message.text);
    REQUIRE(loaded.valid());
    return loaded.pack;
}
CharacterDocument fighterAt(int level = 1) {
    auto d = newCharacter("srd55"); d.name = "Bronn";
    d.choices = {{"classId","srd55:fighter"},{"level",level},{"speciesId","srd55:dwarf"},
                 {"backgroundId","srd55:soldier"},{"alignment","Neutral Good"},
                 {"languages",{"srd55:dwarvish","srd55:draconic"}},
                 {"abilities",{{"strength",15},{"dexterity",14},{"constitution",13},{"intelligence",8},{"wisdom",10},{"charisma",12}}},
                 {"backgroundBoosts",{{"strength",2},{"constitution",1}}},
                 {"classSkills",{"srd55:perception","srd55:survival"}},{"gamingSet","srd55:dice"},
                 {"fightingStyle","srd55:defense"},{"weaponMasteries",{"srd55:greatsword","srd55:flail","srd55:javelin"}},
                 {"classEquipment","A"},{"backgroundEquipment","B"},{"armorId","srd55:chain-mail"},{"weaponId","srd55:greatsword"}};
    if(level==3)d.choices["subclassId"]="srd55:champion";
    return d;
}
CharacterDocument wizardAt(int level = 1) {
    auto d = newCharacter("srd55"); d.name = "Nyra";
    d.choices = {{"classId","srd55:wizard"},{"level",level},{"speciesId","srd55:dwarf"},
                 {"backgroundId","srd55:sage"},{"alignment","Neutral"},
                 {"languages",{"srd55:elvish","srd55:draconic"}},
                 {"abilities",{{"strength",8},{"dexterity",12},{"constitution",13},{"intelligence",15},{"wisdom",14},{"charisma",10}}},
                 {"backgroundBoosts",{{"intelligence",2},{"constitution",1}}},
                 {"classSkills",{"srd55:insight","srd55:investigation"}},
                 {"classEquipment","A"},{"backgroundEquipment","A"},{"weaponId","srd55:dagger"},
                 {"cantrips",{"srd55:light","srd55:mage-hand","srd55:ray-of-frost"}},
                 {"spellbook",{{"1",{"srd55:detect-magic","srd55:feather-fall","srd55:mage-armor","srd55:magic-missile","srd55:sleep","srd55:thunderwave"}}}},
                 {"preparedSpells",{"srd55:mage-armor","srd55:magic-missile","srd55:sleep","srd55:thunderwave"}},
                 {"magicInitiate",{{"background",{{"ability","intelligence"},{"cantrips",{"srd55:fire-bolt","srd55:mending"}},{"spell","srd55:identify"}}}}}};
    if(level>=2){
        d.choices["scholarSkill"]="srd55:arcana";
        d.choices["spellbook"]["2"]={"srd55:shield","srd55:burning-hands"};
        d.choices["preparedSpells"].push_back("srd55:shield");
    }
    if(level==3){
        d.choices["subclassId"]="srd55:evoker";
        d.choices["spellbook"]["3"]={"srd55:misty-step","srd55:web"};
        d.choices["evocationSavant"]={"srd55:scorching-ray","srd55:shatter"};
        d.choices["preparedSpells"].push_back("srd55:web");
    }
    return d;
}
Evaluation result(const CharacterDocument& d) { return evaluate(d,resolveRuleset(d,{srdPack()})); }
void requireComplete(const Evaluation& e){for(const auto& message:e.messages)INFO(message.path+": "+message.text);REQUIRE(e.complete());}
int value(const Evaluation& e,const std::string& id){REQUIRE(e.find(id));return e.find(id)->normal.get<int>();}
bool has(const Evaluation& e,const std::string& code){return std::any_of(e.messages.begin(),e.messages.end(),[&](const Message& m){return m.code=="srd55."+code;});}
}

TEST_CASE("SRD 5.2.1 pack has independently verified tables and complete relevant spell lists", "[srd55][content]") {
    const auto pack=srdPack();
    REQUIRE(pack.manifest.at("license").at("id")=="CC-BY-4.0");
    REQUIRE(pack.manifest.at("license").at("text").get<std::string>().find("This work includes material")!=std::string::npos);
    std::array<int,3> count{};
    for(const auto& entry:pack.entries)if(entry.at("kind")=="spell"){
        auto lists=entry.at("lists").get<std::vector<std::string>>();
        if(std::find(lists.begin(),lists.end(),"wizard")!=lists.end())++count[entry.at("level").get<std::size_t>()];
    }
    REQUIRE(count==std::array<int,3>{15,30,35});
    REQUIRE(srd55Module().experimental);
}
TEST_CASE("Fighter reaches Champion level three with independent HP AC saves and combat examples", "[srd55]") {
    for(int level=1;level<=3;++level){
        const auto e=result(fighterAt(level));requireComplete(e);
        REQUIRE(value(e,"hp.maximum")==std::array<int,3>{13,22,31}[static_cast<std::size_t>(level-1)]);
        REQUIRE(value(e,"armorClass")==17);
        REQUIRE(value(e,"attack.weapon")==5);
        REQUIRE(value(e,"save.strength")==5);
        REQUIRE(value(e,"save.constitution")==4);
        REQUIRE(value(e,"proficiency")==2);
        REQUIRE(value(e,"actionSurge.uses")== (level>=2?1:0));
        REQUIRE(value(e,"critical.minimum")== (level==3?19:20));
        auto blankXp=fighterAt(level);blankXp.choices["xp"]=nullptr;requireComplete(result(blankXp));
    }
}
TEST_CASE("Wizard advancement counts spellbook study separately from Savant and origin spells", "[srd55]") {
    for(int level=1;level<=3;++level){
        const auto e=result(wizardAt(level));requireComplete(e);
        REQUIRE(value(e,"hp.maximum")==std::array<int,3>{9,16,23}[static_cast<std::size_t>(level-1)]);
        REQUIRE(value(e,"armorClass")==11);
        REQUIRE(value(e,"spellDc")==13);
        REQUIRE(value(e,"spellAttack")==5);
        REQUIRE(value(e,"spellSlots.1")==level+1);
        REQUIRE(value(e,"spellSlots.2")== (level==3?2:0));
        REQUIRE(value(e,"skill.arcana")== (level>=2?7:5));
        REQUIRE(value(e,"arcaneRecovery")== (level+1)/2);
    }
    auto d=wizardAt(3);d.choices["spellbook"]["2"][0]="srd55:misty-step";
    REQUIRE_FALSE(result(d).complete());
    d=wizardAt(3);d.choices["evocationSavant"]={"srd55:misty-step","srd55:web"};
    REQUIRE_FALSE(result(d).complete());
    d=wizardAt(1);d.choices["preparedSpells"][0]="srd55:identify";
    REQUIRE(has(result(d),"choice.ineligible")); // Magic Initiate does not add to the Wizard spellbook.
}
TEST_CASE("All nine species use the same character document and stages", "[srd55]") {
    const std::vector<std::pair<std::string,std::string>> cases{{"dragonborn","black-dragon"},{"dwarf",""},{"elf","wood-elf"},{"gnome","forest-gnome"},{"goliath","stone-giant"},{"halfling",""},{"human",""},{"orc",""},{"tiefling","infernal"}};
    for(const auto& [species,lineage]:cases){
        INFO(species);auto d=fighterAt(3);d.choices["speciesId"]="srd55:"+species;
        if(!lineage.empty())d.choices["lineageId"]="srd55:"+lineage;
        if(species=="elf"||species=="gnome"||species=="tiefling")d.choices["speciesCastingAbility"]="wisdom";
        if(species=="elf"||species=="human")d.choices["speciesSkill"]="srd55:insight";
        if(species=="human"||species=="tiefling")d.choices["size"]="Medium";
        if(species=="human"){
            d.choices["humanFeat"]="srd55:skilled";
            d.choices["skilledChoices"]={{"human",{"srd55:arcana","srd55:history","srd55:acrobatics"}}};
        }
        const auto e=result(d);requireComplete(e);
        REQUIRE(value(e,"speed")== ((species=="goliath"||species=="elf")?35:30));
        REQUIRE(value(e,"hp.maximum")== (species=="dwarf"?31:28));
    }
}
TEST_CASE("Accepted HP results never reroll and malformed inputs stay reviewable", "[srd55]") {
    auto d=fighterAt(3);d.choices["hpMethod"]="rolled";d.choices["hp"]={{"2",1},{"3",10}};
    auto e=result(d);requireComplete(e);REQUIRE(value(e,"hp.maximum")==30);
    REQUIRE(toJson(e)==toJson(result(d)));
    auto roundtrip=documentFromJson(toJson(d));REQUIRE(toJson(result(roundtrip))==toJson(e));
    d.choices["hp"]["2"]=11;REQUIRE(has(result(d),"number"));
    d=fighterAt();d.choices["abilities"]["strength"]=1000000000;REQUIRE(has(result(d),"number"));
    d.choices["classSkills"]="not an array";REQUIRE(has(result(d),"choice.type"));
    auto draft=newCharacter("srd55");REQUIRE_FALSE(result(draft).complete());REQUIRE_FALSE(result(draft).stages.empty());
}
TEST_CASE("Background choices, equipment ownership, and proficiency restrictions are enforced", "[srd55]") {
    auto d=fighterAt();d.choices["backgroundBoosts"]={{"intelligence",2},{"constitution",1}};REQUIRE(has(result(d),"background.ability"));
    d=fighterAt();d.choices["classSkills"]={"srd55:athletics","srd55:survival"};REQUIRE(has(result(d),"choice.ineligible"));
    d=fighterAt();d.choices["armorId"]="srd55:plate-armor";REQUIRE(has(result(d),"choice.ineligible"));
    d=fighterAt();d.choices["purchases"]={"srd55:plate-armor"};REQUIRE(has(result(d),"equipment.budget"));
    d=fighterAt();d.choices["purchases"]={"srd55:shield-equipment"};d.choices["shield"]=true;REQUIRE(has(result(d),"hands"));
    d=wizardAt();d.choices["backgroundEquipment"]="B";d.choices["purchases"]={"srd55:shield-equipment"};d.choices["shield"]=true;
    auto e=result(d);requireComplete(e);REQUIRE(value(e,"armorClass")==11);REQUIRE(has(e,"shield.untrained"));
}
TEST_CASE("SRD source removal and overrides preserve unresolved inputs", "[srd55]") {
    auto d=fighterAt();d.overrides=Json::array({{{"target","armorClass"},{"value",20},{"reason","Enchanted armor approved by GM"}}});
    auto e=result(d);requireComplete(e);REQUIRE(e.find("armorClass")->normal==17);REQUIRE(e.find("armorClass")->effective==20);
    auto missing=evaluate(d,resolveRuleset(d,{}));REQUIRE_FALSE(missing.complete());REQUIRE(d.choices.at("armorId")=="srd55:chain-mail");
    d.overrides[0]["reason"]="";REQUIRE_FALSE(result(d).complete());
}
TEST_CASE("SRD mechanics schema rejects broken progression and unknown effects", "[srd55][content]") {
    auto pack=srdPack();
    auto cls=std::find_if(pack.entries.begin(),pack.entries.end(),[](const Json& e){return e.at("id")=="srd55:wizard";});
    REQUIRE(cls!=pack.entries.end());(*cls)["slots"]={{2},{3,0},{4,2}};
    REQUIRE_FALSE(srd55Module().validateContent(pack).empty());
    pack=srdPack();pack.entries[0]["unimplementedMagic"]=true;REQUIRE_FALSE(srd55Module().validateContent(pack).empty());
}

TEST_CASE("SRD resolved references reject missing or wrongly typed content before evaluation", "[srd55][content]") {
    auto d=fighterAt();auto rules=resolveRuleset(d,{srdPack()});
    REQUIRE(rules.valid());
    REQUIRE(rules.find("srd55:minor-illusion"));
    REQUIRE(srd55Module().validateRuleset(rules).empty());
    rules.content.erase("srd55:minor-illusion");
    REQUIRE_FALSE(srd55Module().validateRuleset(rules).empty());
    rules=resolveRuleset(d,{srdPack()});rules.content["srd55:savage-attacker"]["kind"]="spell";
    REQUIRE_FALSE(srd55Module().validateRuleset(rules).empty());
}
TEST_CASE("Other SRD backgrounds and Origin feats supply their own skills and casting abilities", "[srd55]") {
    auto d=fighterAt();
    d.choices["speciesId"]="srd55:human";d.choices["size"]="Small";
    d.choices["speciesSkill"]="srd55:survival";d.choices["humanFeat"]="srd55:magic-initiate-druid";
    d.choices["backgroundId"]="srd55:criminal";d.choices["backgroundBoosts"]={{"dexterity",2},{"constitution",1}};
    d.choices["classSkills"]={"srd55:athletics","srd55:perception"};
    d.choices["classEquipment"]="B";d.choices["armorId"]="srd55:studded-leather-armor";d.choices["weaponId"]="srd55:longbow";
    d.choices["fightingStyle"]="srd55:archery";
    d.choices["magicInitiate"]={{"human",{{"ability","wisdom"},{"cantrips",{"srd55:druidcraft","srd55:guidance"}},{"spell","srd55:entangle"}}}};
    auto e=result(d);requireComplete(e);
    REQUIRE(value(e,"initiative")==5); // Dexterity +3, Alert +2.
    REQUIRE(value(e,"armorClass")==15); // Studded leather 12 + Dexterity 3.
    REQUIRE(value(e,"attack.weapon")==7); // Dexterity 3 + proficiency 2 + Archery 2.
    REQUIRE(value(e,"magicInitiate.human.dc")==10);

    d=wizardAt();d.choices["backgroundId"]="srd55:acolyte";
    d.choices["backgroundBoosts"]={{"intelligence",2},{"wisdom",1}};
    d.choices["classSkills"]={"srd55:arcana","srd55:investigation"};
    d.choices["magicInitiate"]={{"background",{{"ability","wisdom"},{"cantrips",{"srd55:sacred-flame","srd55:guidance"}},{"spell","srd55:bless"}}}};
    e=result(d);requireComplete(e);
    REQUIRE(value(e,"hp.maximum")==8);
    REQUIRE(value(e,"spellDc")==13);
    REQUIRE(value(e,"magicInitiate.background.dc")==12);
}
TEST_CASE("SRD point buy and low Constitution HP use the published thresholds", "[srd55]") {
    auto d=fighterAt();d.choices["abilityMethod"]="point-buy";
    d.choices["abilities"]={{"strength",15},{"dexterity",15},{"constitution",15},{"intelligence",8},{"wisdom",8},{"charisma",8}};
    auto e=result(d);requireComplete(e);REQUIRE(value(e,"hp.maximum")==14);
    d.choices["abilities"]["strength"]=14;REQUIRE(has(result(d),"ability.points"));
    d=fighterAt(3);d.choices["abilityMethod"]="rolled";d.choices["abilities"]["constitution"]=3;
    d.choices["backgroundBoosts"]={{"strength",2},{"dexterity",1}};
    d.choices["hpMethod"]="rolled";d.choices["hp"]={{"2",1},{"3",1}};
    e=result(d);requireComplete(e);REQUIRE(value(e,"hp.maximum")==11); // 10 - 4 + 1 dwarf, then 1 minimum + 1 dwarf each level.
}
