#include "dnd/persistence.hpp"
#include "srd55_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
#include <tuple>

using namespace dnd;
namespace {
const ResolvedRuleset& rules() {
    static const auto result=srd55fixtures::rules();
    return result;
}
CharacterDocument finished(CharacterDocument document,const ResolvedRuleset& ruleset) {
    auto result=srd55fixtures::finish(std::move(document),ruleset);
    INFO(srd55fixtures::errors(result.evaluation));
    REQUIRE(result.evaluation.complete());
    return result.document;
}
Evaluation checked(const CharacterDocument& document,const ResolvedRuleset& ruleset) {
    auto result=evaluate(document,ruleset);
    INFO(srd55fixtures::errors(result));
    REQUIRE(result.complete());
    return result;
}
const Calculation& calculation(const Evaluation& result,const std::string& id) {
    const auto* value=result.find(id);
    INFO(id);
    REQUIRE(value);
    return *value;
}
void step(const Calculation& value,const std::string& fragment) {
    INFO(value.id+": expected explanation fragment: "+fragment);
    INFO(Json(value.steps).dump(2));
    CHECK(std::any_of(value.steps.begin(),value.steps.end(),[&](const auto& text){return text.find(fragment)!=std::string::npos;}));
}
void source(const Calculation& value,const std::string& publication,const std::string& page) {
    INFO(value.id+": expected source "+publication+", "+page);
    CHECK(std::any_of(value.sources.begin(),value.sources.end(),[&](const auto& ref){return ref.publication==publication && ref.page==page;}));
}
void srdSource(const Calculation& value,const std::string& page) {
    source(value,"System Reference Document 5.2.1",page);
}
void score(CharacterDocument& document,const std::string& ability,std::array<int,4> dice) {
    auto ordered=dice;std::sort(ordered.begin(),ordered.end());
    const int total=ordered[1]+ordered[2]+ordered[3];
    document.choices["abilities"][ability]=total;
    document.rolls["abilities"][ability]={{"dice",dice},{"dropLowest",1},{"total",total}};
}
CharacterDocument inventoryAction(const CharacterDocument& document,const std::string& id,Json inputs=Json::object()) {
    auto result=executeCommand(document,rules(),{"srd55.inventory."+id,std::move(inputs)});
    for(const auto& message:result.messages)INFO(message.path+": "+message.text);
    REQUIRE(result.valid());
    return result.document;
}
}

// Independent expectations: official SRD 5.2.1 printed pp. 8, 50, 62, 69 and 92
// were visually checked against the PDF tables; pp. 87-88 and 248 supply the
// Alert, Defense and Luckstone text. Expected operands below are literal worked
// examples, not values obtained from the implementation's feature helpers.
TEST_CASE("Draconic armor explains both modifiers and the selected profile sources", "[srd55-v2][explanations]") {
    for(const bool alternate:{false,true}) {
        CAPTURE(alternate);
        auto document=srd55fixtures::base("sorcerer",rules(),3);
        score(document,"dexterity",{6,6,4,1}); // 16, modifier +3; Soldier does not boost it here.
        score(document,"charisma",{6,6,6,1}); // 18, modifier +4.
        auto resolved=rules();
        if(alternate) {
            ContentPack supplement;
            supplement.manifest=rules().packs.front().manifest;
            supplement.manifest["id"]="explanation-profiles";
            supplement.manifest["version"]="1.0.0";
            supplement.manifest["publisher"]="Explanation regression campaign";
            supplement.manifest["origin"]="homebrew";
            supplement.manifest["dependencies"]={{{"id","srd55-core"},{"version","2.0.0"}}};
            auto cls=*rules().find("srd55:sorcerer");
            cls["id"]="house:scale-sorcerer";cls["name"]="Scale Sorcerer";
            cls["source"]={{"publication","Campaign Sorcerer Profile"},{"page","C1"}};
            auto subclass=*rules().find("srd55:draconic-sorcery");
            subclass["id"]="house:scale-sorcery";subclass["classId"]="house:scale-sorcerer";
            subclass["source"]={{"publication","Campaign Draconic Profile"},{"page","S1"}};
            supplement.entries={cls,subclass};
            document.packs.push_back({"explanation-profiles","1.0.0"});
            document.choices["classId"]="house:scale-sorcerer";
            document.choices["subclasses"]["sorcerer"]="house:scale-sorcery";
            auto installed=rules().packs;installed.push_back(supplement);
            resolved=resolveRuleset(document,installed);
            for(const auto& message:resolved.messages)INFO(message.text);
            REQUIRE(resolved.valid());
        }
        document=finished(std::move(document),resolved);
        const auto result=checked(document,resolved);
        const auto& armor=calculation(result,"armorClass");
        CHECK(armor.normal==17); // 10 + 3 Dexterity + 4 Charisma.
        step(armor,"10 + dexterity modifier (3) + charisma modifier (4) = 17");
        step(armor,"Selected base AC 17 + Shield 0 + Defense 0 + item bonuses 0 = 17");
        srdSource(armor,"69");
        if(alternate) {
            source(armor,"Campaign Sorcerer Profile","C1");
            source(armor,"Campaign Draconic Profile","S1");
            CHECK(document.choices["classId"]=="house:scale-sorcerer");
            CHECK(document.choices["subclasses"]["sorcerer"]=="house:scale-sorcery");
        }
        // A reasoned override changes the effective value while retaining the complete normal trace.
        document.overrides={{{"target","armorClass"},{"value",19},{"reason","DM blessing for the campaign prologue"}}};
        const auto overridden=checked(document,resolved);
        const auto& effective=calculation(overridden,"armorClass");
        CHECK(effective.normal==17);CHECK(effective.effective==19);
        CHECK(effective.overrideReason=="DM blessing for the campaign prologue");
        CHECK(effective.steps==armor.steps);
        const auto path=std::filesystem::temp_directory_path()/("dnd-explanation-"+document.id+".json");
        saveCharacter(path,document);
        const auto reopened=loadCharacter(path);
        std::filesystem::remove(path);
        REQUIRE_FALSE(reopened.inspectOnly);
        CHECK(toJson(checked(reopened.document,resolved))==toJson(overridden));
        CHECK(reopened.document.rolls==document.rolls);
    }
}

TEST_CASE("Expertise shows ability proficiency multiplication and carried item contribution", "[srd55-v2][explanations]") {
    auto document=finished(srd55fixtures::base("rogue",rules(),5),rules());
    auto result=checked(document,rules());
    const auto& ordinary=calculation(result,"skill.stealth");
    CHECK(ordinary.normal==9); // DEX 15 + level-4 ASI 2 => 17 (+3); twice level-5 PB 3 => +6.
    step(ordinary,"Dexterity modifier = 3");
    step(ordinary,"Expertise: 2 x proficiency bonus 3 = 6");
    step(ordinary,"Ability 3 + proficiency contribution 6 + other bonuses 0 = 9");
    srdSource(ordinary,"8");srdSource(ordinary,"61");

    document=inventoryAction(document,"initialize");
    document=inventoryAction(document,"acquire",{{"itemId","srd55:magic-stone-of-good-luck-luckstone"},{"quantity",1},{"source","gift"}});
    const std::string instance=document.resources["inventory"]["instances"].back().at("id");
    document=inventoryAction(document,"attune",{{"instanceId",instance},{"completedShortRest",true}});
    document=inventoryAction(document,"equip",{{"instanceId",instance},{"slot","auto"}});
    result=checked(document,rules());
    const auto& lucky=calculation(result,"skill.stealth");
    CHECK(lucky.normal==10); // 3 + 2*3 + Luckstone 1.
    step(lucky,"Expertise: 2 x proficiency bonus 3 = 6");
    step(lucky,"Stone of Good Luck (Luckstone) ["+instance+"]");
    step(lucky,"Ability 3 + proficiency contribution 6 + other bonuses 1 = 10");
    srdSource(lucky,"248");
    const auto reopened=documentFromJson(toJson(document));
    CHECK(toJson(checked(reopened,rules()))==toJson(result));
}

TEST_CASE("Monk speed explanations identify the actual level bonus and species base", "[srd55-v2][explanations]") {
    // The printed Monk table changes movement from +10 at 5 to +15 at 6.
    for(const auto& [level,bonus,total]:std::vector<std::tuple<int,int,int>>{{5,10,40},{6,15,45}}) {
        CAPTURE(level);
        const auto document=finished(srd55fixtures::base("monk",rules(),level),rules());
        const auto result=checked(document,rules());
        const auto& speed=calculation(result,"speed");
        CHECK(speed.normal==total);
        step(speed,"Dwarf base Speed: 30 feet");
        step(speed,"Unarmored Movement at Monk level "+std::to_string(level)+": +"+std::to_string(bonus)+" feet");
        step(speed,"Base 30 + class bonuses "+std::to_string(bonus)+" + item bonuses 0 = "+std::to_string(total));
        srdSource(speed,"50-51");srdSource(speed,"84");
    }
}

TEST_CASE("Equipped armor Shield and Defense contributions reconstruct final AC and speed", "[srd55-v2][explanations]") {
    auto document=srd55fixtures::base("fighter",rules(),1);
    score(document,"strength",{4,3,3,1}); // 10 + Soldier 2 = 12, below Chain Mail's requirement 13.
    document.choices["classEquipment"]="C";
    document.choices["features"]["fighter"]["fightingStyle"][0]="srd55:defense";
    document.choices["purchases"]={"srd55:chain-mail","srd55:shield-equipment"};
    document.choices["armorId"]="srd55:chain-mail";document.choices["shield"]=true;
    document=finished(std::move(document),rules());
    const auto result=checked(document,rules());
    const auto& armor=calculation(result,"armorClass");
    CHECK(armor.normal==19); // Chain Mail 16 + shield 2 + Defense 1; Dexterity contributes 0.
    step(armor,"base 16 + applied Dexterity (0) = 16");
    step(armor,"Selected base AC 16 + Shield 2 + Defense 1 + item bonuses 0 = 19");
    srdSource(armor,"92");srdSource(armor,"88");
    const auto& speed=calculation(result,"speed");
    CHECK(speed.normal==20); // Dwarf speed 30 - armor penalty 10.
    step(speed,"Strength 12 < required 13; Speed penalty 10 feet");
    step(speed,"Final Speed: max(0, 20) = 20 feet");
    srdSource(speed,"92");srdSource(speed,"84");
}

TEST_CASE("Alert initiative explains Dexterity proficiency and Luckstone separately", "[srd55-v2][explanations]") {
    auto document=srd55fixtures::base("fighter",rules(),5);
    document.choices["backgroundId"]="srd55:criminal";
    document.choices["backgroundBoosts"]={{"dexterity",2},{"constitution",1}};
    document=finished(std::move(document),rules());
    document=inventoryAction(document,"initialize");
    document=inventoryAction(document,"acquire",{{"itemId","srd55:magic-stone-of-good-luck-luckstone"},{"quantity",1},{"source","gift"}});
    const std::string instance=document.resources["inventory"]["instances"].back().at("id");
    document=inventoryAction(document,"attune",{{"instanceId",instance},{"completedShortRest",true}});
    document=inventoryAction(document,"equip",{{"instanceId",instance},{"slot","auto"}});
    const auto result=checked(document,rules());
    const auto& initiative=calculation(result,"initiative");
    CHECK(initiative.normal==8); // DEX 15 + background2 + ASI2 =19 (+4), Alert PB3, Luckstone1.
    step(initiative,"Dexterity modifier 4 + Alert 3 + class bonuses 0 + item bonuses 1 = 8");
    step(initiative,"Alert adds the proficiency bonus: 3");
    step(initiative,"Stone of Good Luck (Luckstone) ["+instance+"]");
    srdSource(initiative,"13");srdSource(initiative,"87");srdSource(initiative,"248");
}

TEST_CASE("Compatible homebrew armor and Shield values contribute without stock substitutions", "[srd55-v2][explanations][replacement]") {
    for(const bool negativeArmor:{false,true}) {
        CAPTURE(negativeArmor);
        auto document=srd55fixtures::base("fighter",rules(),1);
        document.choices["classEquipment"]="C";
        ContentPack supplement;
        supplement.manifest=rules().packs.front().manifest;
        supplement.manifest["id"]="explanation-equipment";
        supplement.manifest["version"]="1.0.0";
        supplement.manifest["origin"]="homebrew";
        supplement.manifest["dependencies"]={{{"id","srd55-core"},{"version","2.0.0"}}};
        const std::string target=negativeArmor?"srd55:leather-armor":"srd55:shield-equipment";
        auto replacement=*rules().find(target);
        replacement["id"]=negativeArmor?"house:fragile-armor":"house:broad-shield";
        replacement["name"]=negativeArmor?"Fragile Armor":"Broad Shield";
        replacement["replaces"]=target;
        replacement["ac"]=negativeArmor?0:3;
        replacement["source"]={{"publication","Campaign Equipment Rulings"},{"page","E1"}};
        supplement.entries={replacement};
        document.packs.push_back({"explanation-equipment","1.0.0"});
        auto installed=rules().packs;installed.push_back(supplement);
        const auto resolved=resolveRuleset(document,installed);
        for(const auto& message:resolved.messages)INFO(message.text);
        REQUIRE(resolved.valid());
        if(negativeArmor) {
            score(document,"dexterity",{3,3,2,1}); // DEX 8, modifier -1.
            document.choices["purchases"]={target};document.choices["armorId"]=target;
        } else {
            document.choices["purchases"]={"srd55:chain-mail",target};
            document.choices["armorId"]="srd55:chain-mail";document.choices["shield"]=true;
        }
        document=finished(std::move(document),resolved);
        const auto result=checked(document,resolved);
        const auto& armor=calculation(result,"armorClass");
        CHECK(armor.normal==(negativeArmor?-1:19));
        source(armor,"Campaign Equipment Rulings","E1");
        if(negativeArmor) {
            step(armor,"Fragile Armor: base 0 + applied Dexterity (-1) = -1");
            step(armor,"Selected base AC -1 + Shield 0 + Defense 0 + item bonuses 0 = -1");
        } else step(armor,"Selected base AC 16 + Shield 3 + Defense 0 + item bonuses 0 = 19");
    }
}

TEST_CASE("Walking Speed explains only the item movement modes that contribute", "[srd55-v2][explanations]") {
    // SRD pp. 214 and 239: Boots set a walking minimum of 30, whereas the Ring
    // grants a Swim Speed of 40. The Ring must not explain the walking result.
    auto document=finished(srd55fixtures::base("fighter",rules(),1),rules());
    document=inventoryAction(document,"initialize");
    document=inventoryAction(document,"acquire",{{"itemId","srd55:magic-boots-of-striding-and-springing"},{"quantity",1},{"source","gift"}});
    const std::string boots=document.resources["inventory"]["instances"].back().at("id");
    document=inventoryAction(document,"attune",{{"instanceId",boots},{"completedShortRest",true}});
    document=inventoryAction(document,"equip",{{"instanceId",boots},{"slot","auto"}});
    document=inventoryAction(document,"acquire",{{"itemId","srd55:magic-ring-of-swimming"},{"quantity",1},{"source","gift"}});
    const std::string ring=document.resources["inventory"]["instances"].back().at("id");
    document=inventoryAction(document,"equip",{{"instanceId",ring},{"slot","auto"}});
    const auto result=checked(document,rules());
    const auto& speed=calculation(result,"speed");
    CHECK(speed.normal==30);
    step(speed,"Item walking minimum: max(30, 30) = 30");
    step(speed,"Boots of Striding and Springing ["+boots+"]");
    CHECK(std::none_of(speed.steps.begin(),speed.steps.end(),[](const auto& text){return text.find("Ring of Swimming")!=std::string::npos;}));
    srdSource(speed,"214");
    CHECK(std::none_of(speed.sources.begin(),speed.sources.end(),[](const auto& ref){return ref.publication=="System Reference Document 5.2.1" && ref.page=="239";}));
}

TEST_CASE("Gauntlets Strength provenance follows its modifier into Athletics and disappears when inactive", "[srd55-v2][explanations]") {
    // SRD p. 223 sets Strength to 19 while these gauntlets are worn; p. 8 adds
    // ordinary proficiency once. Soldier grants Athletics at character creation.
    auto document=finished(srd55fixtures::base("fighter",rules(),1),rules());
    auto result=checked(document,rules());
    CHECK(calculation(result,"skill.athletics").normal==5); // STR17 => +3; PB2 => 5.
    document=inventoryAction(document,"initialize");
    document=inventoryAction(document,"acquire",{{"itemId","srd55:magic-gauntlets-of-ogre-power"},{"quantity",1},{"source","gift"}});
    const std::string gloves=document.resources["inventory"]["instances"].back().at("id");
    document=inventoryAction(document,"attune",{{"instanceId",gloves},{"completedShortRest",true}});
    document=inventoryAction(document,"equip",{{"instanceId",gloves},{"slot","auto"}});
    result=checked(document,rules());
    CHECK(calculation(result,"ability.strength").normal==19);
    const auto& athletics=calculation(result,"skill.athletics");
    CHECK(athletics.normal==6); // Gauntlets STR19 => +4; PB2 => 6.
    step(athletics,"Gauntlets of Ogre Power");
    step(athletics,"floor((19 - 10) / 2) = 4");
    step(athletics,"Ability 4 + proficiency contribution 2 + other bonuses 0 = 6");
    srdSource(athletics,"223");
    const auto& modifier=calculation(result,"modifier.strength");
    CHECK(modifier.normal==4);step(modifier,"Gauntlets of Ogre Power");srdSource(modifier,"223");

    document=inventoryAction(document,"unequip",{{"instanceId",gloves}});
    result=checked(document,rules());
    const auto& inactive=calculation(result,"skill.athletics");
    CHECK(calculation(result,"ability.strength").normal==17);
    CHECK(inactive.normal==5);
    CHECK(std::none_of(inactive.steps.begin(),inactive.steps.end(),[](const auto& text){return text.find("Gauntlets of Ogre Power")!=std::string::npos;}));
    CHECK(std::none_of(inactive.sources.begin(),inactive.sources.end(),[](const auto& ref){return ref.publication=="System Reference Document 5.2.1" && ref.page=="223";}));
}

TEST_CASE("An Agility Ioun Stone contributes its Dexterity arithmetic and source to AC and Initiative", "[srd55-v2][explanations]") {
    // SRD p. 227: Agility grants Dexterity +2, capped at 20. This character's
    // Dexterity goes from 15 (+2) to 17 (+3), below that cap.
    auto document=finished(srd55fixtures::base("fighter",rules(),1),rules());
    auto result=checked(document,rules());
    CHECK(calculation(result,"armorClass").normal==12);
    CHECK(calculation(result,"initiative").normal==2);
    document=inventoryAction(document,"initialize");
    document=inventoryAction(document,"acquire",{{"itemId","srd55:magic-ioun-stone"},{"variantId","agility"},{"quantity",1},{"source","gift"}});
    const std::string stone=document.resources["inventory"]["instances"].back().at("id");
    document=inventoryAction(document,"attune",{{"instanceId",stone},{"completedShortRest",true}});
    document=inventoryAction(document,"equip",{{"instanceId",stone},{"slot","auto"}});
    result=checked(document,rules());
    CHECK(calculation(result,"ability.dexterity").normal==17);
    const auto& armor=calculation(result,"armorClass");
    const auto& initiative=calculation(result,"initiative");
    CHECK(armor.normal==13);CHECK(initiative.normal==3);
    for(const auto* value:{&armor,&initiative}) {
        step(*value,"Ioun Stone");
        step(*value,"floor((17 - 10) / 2) = 3");
        srdSource(*value,"227");
    }
    step(armor,"Standard unarmored: 10 + Dexterity modifier (3) = 13");
    step(initiative,"Dexterity modifier 3 + Alert 0 + class bonuses 0 + item bonuses 0 = 3");
}
