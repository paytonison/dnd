#include "dnd/engine.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <sstream>

namespace dnd {
namespace {
const std::array<std::string, 6> abilityKeys = {"str", "int", "wis", "dex", "con", "cha"};
const std::array<std::string, 6> abilityLabels = {"Strength", "Intelligence", "Wisdom", "Dexterity", "Constitution", "Charisma"};
SourceRef bxSource(const std::string& page) {
    const bool basic = page.front() == 'B';
    return {basic ? "D&D Basic Rulebook (Moldvay, 1981)" : "D&D Expert Rulebook (Cook/Marsh, 1981)", page,
            basic ? "https://www.americanroads.us/DandD/DnD_Basic_Rules_Moldvay.pdf" : "https://www.americanroads.us/DandD/DnD_Expert_Rules_Cook.pdf"};
}
std::vector<SourceRef> refs(const std::string& pages) {
    std::vector<SourceRef> result;
    std::istringstream input(pages);
    std::string page;
    while(std::getline(input,page,',')) {
        const auto first=page.find_first_not_of(" ");
        if(first!=std::string::npos)result.push_back(bxSource(page.substr(first)));
    }
    return result;
}
SourceRef entryRef(const Json& entry, const std::string& field = "source") { return sourceFromJson(entry.value(field, Json::object())); }
void message(Evaluation& e, const std::string& code, const std::string& path, const std::string& text,
             const std::string& page, const std::string& severity = "error") {
    e.messages.push_back({severity, code, path, text, refs(page)});
}
int modifier(int n) { return n <= 3 ? -3 : n <= 5 ? -2 : n <= 8 ? -1 : n <= 12 ? 0 : n <= 15 ? 1 : n <= 17 ? 2 : 3; }
int band(int n) { return n <= 3 ? 0 : n <= 5 ? 1 : n <= 8 ? 2 : n <= 12 ? 3 : n <= 15 ? 4 : n <= 17 ? 5 : 6; }
bool containsString(const Json& array, const std::string& value) {
    return array.is_array() && std::find(array.begin(), array.end(), Json(value)) != array.end();
}
std::vector<std::string> strings(const Json& value) {
    std::vector<std::string> result;
    if (value.is_array()) for (const auto& item : value) if (item.is_string()) result.push_back(item.get<std::string>());
    return result;
}
std::string join(const std::vector<std::string>& values, const std::string& separator = ", ") {
    std::string result;
    for (const auto& value : values) { if (!result.empty()) result += separator; result += value; }
    return result;
}
Field numberField(std::string path, std::string label, int minimum, int maximum, std::string help = {}, bool advanced = false) {
    return {std::move(path), std::move(label), "integer", minimum, maximum, {}, advanced, std::move(help)};
}
Field selectField(std::string path, std::string label, std::vector<Choice> choices, bool multiple = false) {
    return {std::move(path), std::move(label), multiple ? "multiselect" : "select", 0, 0, std::move(choices), false, {}};
}
Choice option(const std::string& id, const Json& entry, bool available = true, const std::string& reason = {}) {
    return {id, entry.value("name", id), available, reason, {entryRef(entry)}};
}
bool boolChoice(const Json& j, const std::string& key, bool fallback = false) {
    return j.is_object() && j.contains(key) && j[key].is_boolean() ? j[key].get<bool>() : fallback;
}
int readNumber(Evaluation& e, const Json& j, const std::string& key, int minimum, int maximum,
               int fallback, const std::string& path, bool required, const std::string& page) {
    if (!j.is_object() || !j.contains(key)) {
        if (required) message(e, "bx.input.missing", path, "Record " + key + " before completing this stage.", page);
        return fallback;
    }
    if (!j[key].is_number_integer()) { message(e, "bx.input.type", path, "Expected a whole number.", page); return fallback; }
    const auto n = j[key].get<std::int64_t>();
    if (n < minimum || n > maximum) { message(e, "bx.input.range", path, "Value must be between " + std::to_string(minimum) + " and " + std::to_string(maximum) + ".", page); return fallback; }
    return static_cast<int>(n);
}
std::string classRestriction(const Json& cls, const std::map<std::string,int>& raw) {
    std::vector<std::string> reasons;
    for (auto it = cls.at("minimumAbilities").begin(); it != cls.at("minimumAbilities").end(); ++it) {
        if (!raw.contains(it.key()) || raw.at(it.key()) < it.value().get<int>())
            reasons.push_back(it.key() + " must be at least " + std::to_string(it.value().get<int>()) + " before class adjustments");
    }
    return join(reasons, "; ");
}
std::string equipmentRestriction(const std::string& profile, const Json& item) {
    const auto kind = item.value("kind", "");
    if (kind == "armor") {
        if (profile == "bx:magic-user") return "Magic-users cannot wear armor (B10).";
        if (profile == "bx:thief" && item.value("armorClass", 9) < 7) return "Thieves may wear leather armor only (B10).";
    }
    if (kind == "weapon") {
        if (profile == "bx:magic-user" && !item.value("magicUserAllowed", false)) return "Magic-users may use daggers only (B10).";
        if (profile == "bx:cleric" && !item.value("clericAllowed", false)) return "Clerics may use only their permitted blunt weapons (B9, X9).";
        if ((profile == "bx:dwarf" || profile == "bx:halfling") && item.value("demihumanForbidden", false)) return "Dwarves and halflings cannot use long bows or two-handed swords (B9-B10).";
    }
    return {};
}
Evaluation evaluateBx(const CharacterDocument& d, const ResolvedRuleset& rules) {
    Evaluation e;
    const auto& c = d.choices;
    for(const auto& key:{"abilities","adjustments","options","quantities","prepared"})
        if(c.contains(key) && !c[key].is_object())message(e,"bx.input.type","/choices/"+std::string(key),"This input must be an object.","B5");
    for(const auto& key:{"equipment","languages","spellbook"})if(c.contains(key)) {
        if(!c[key].is_array() || !std::all_of(c[key].begin(),c[key].end(),[](const auto& item){return item.is_string();}))
            message(e,"bx.input.type","/choices/"+std::string(key),"Selections must be an array of identifiers.","B5");
    }
    for(const auto& key:{"class","alignment","armor","weapon","wealthReason","weightReason"})
        if(c.contains(key) && !c[key].is_string())message(e,"bx.input.type","/choices/"+std::string(key),"This choice must be a string.","B5");
    if(c.contains("shield") && !c["shield"].is_boolean())message(e,"bx.input.type","/choices/shield","Shield selection must be a boolean.","B12");
    Stage abilityStage{"abilities", "Ability scores", {}};
    const auto abilities = c.value("abilities", Json::object());
    std::map<std::string,int> raw, scores;
    for (std::size_t i = 0; i < abilityKeys.size(); ++i) {
        const auto& key = abilityKeys[i];
        abilityStage.fields.push_back(numberField("/abilities/" + key, abilityLabels[i] + " (3d6 total)", 3, 18, "Accepted ability roll; recalculation never rerolls it."));
        raw[key] = readNumber(e, abilities, key, 3, 18, 10, "/choices/abilities/" + key, true, "B5");
    }
    e.stages.push_back(abilityStage);
    std::vector<Choice> classOptions;
    for (const auto& [id, entry] : rules.content) if (entry.value("kind", "") == "class") {
        const auto reason = classRestriction(entry, raw);
        classOptions.push_back(option(id, entry, reason.empty(), reason));
    }
    const auto classId = stringChoice(c, "class");
    Stage classStage{"class", "Class and level", {selectField("/class", "Class", classOptions)}};
    const auto* cls = rules.find(classId);
    if (!cls || cls->value("kind", "") != "class") {
        e.stages.push_back(classStage);
        message(e, "bx.class.missing", "/choices/class", classId.empty() ? "Select a class." : "The selected class is unavailable in the enabled sources; its selection is preserved.", "B9-B10");
        for (std::size_t i=0; i<abilityKeys.size(); ++i) addCalculation(e,"ability."+abilityKeys[i],abilityLabels[i],raw[abilityKeys[i]],{"Accepted 3d6 total."},{bxSource("B5")});
        e.sections.push_back({"Abilities", {"ability.str","ability.int","ability.wis","ability.dex","ability.con","ability.cha"}, {}});
        return e;
    }
    const auto restriction = classRestriction(*cls, raw);
    if (!restriction.empty()) message(e, "bx.class.eligibility", "/choices/class", restriction, "B9-B10");
    const std::string profile = "bx:" + cls->at("rulesProfile").get<std::string>();
    const int maxLevel = cls->at("maxLevel").get<int>();
    const int level = readNumber(e,c,"level",1,maxLevel,1,"/choices/level",false,"X5-X8");
    const int hitDie = cls->at("hitDie").get<int>();
    const auto& xpTable = cls->at("xp");
    const int xpMinimum = xpTable.at(static_cast<std::size_t>(level-1)).get<int>();
    const int xp = readNumber(e,c,"xp",0,2000000000,xpMinimum,"/choices/xp",false,"X5-X6");
    classStage.fields.push_back(numberField("/level","Level",1,maxLevel));
    classStage.fields.push_back(numberField("/xp","Experience points (after prior awards)",0,2000000000,"For higher-level creation, leaving this unset uses the class minimum. Earned experience is recorded after its prime-requisite adjustment."));
    e.stages.push_back(classStage);
    addCalculation(e,"class","Class",cls->at("name"),{"Original 1981 B/X class, including race-as-class where applicable."},{entryRef(*cls)});
    addCalculation(e,"level","Level",level,{"Supported class limit: "+std::to_string(maxLevel)+"."},{entryRef(*cls,"progressionSource")});
    addCalculation(e,"xp","Experience points",xp,{"Saved adjusted experience; never retroactively rescaled when abilities change."},{bxSource("B7"),entryRef(*cls,"progressionSource")});
    addCalculation(e,"xp.minimum","Minimum XP for level",xpMinimum,{"Class progression table, level "+std::to_string(level)+"."},{entryRef(*cls,"progressionSource")});
    if (xp < xpMinimum) message(e,"bx.xp.level","/choices/xp","The selected level requires at least "+std::to_string(xpMinimum)+" experience points.","X5-X6");
    if (level<maxLevel) {
        int next=xpTable.at(static_cast<std::size_t>(level)).get<int>();
        addCalculation(e,"xp.next","Next level at XP",next,{"Class progression table, next level."},{entryRef(*cls,"progressionSource")});
        if (xp>=next) message(e,"bx.advance.ready","/choices/level","Experience is sufficient for the next level. Record its hit-point result and advancement choices before increasing the level.","B22","info");
    } else addCalculation(e,"xp.next","Next level at XP","Class limit",{"No advancement beyond the supported original class table."},{entryRef(*cls,"progressionSource")});

    const auto adjustments = c.value("adjustments", Json::object());
    int spent=0,gained=0;
    Stage adjustmentsStage{"adjustments","Permitted ability adjustments",{}};
    for (std::size_t i=0; i<abilityKeys.size(); ++i) {
        const auto& key=abilityKeys[i];
        const int delta=readNumber(e,adjustments,key,-9,9,0,"/choices/adjustments/"+key,false,"B6");
        adjustmentsStage.fields.push_back(numberField("/adjustments/"+key,abilityLabels[i]+" adjustment",-9,9,"Lower eligible scores by 2 for each 1 added to a prime requisite. No lowered score may finish below 9."));
        if (delta<0) {
            spent-=delta;
            if (!containsString(cls->at("abilityDonors"),key) || raw[key]+delta<9 || delta%2!=0)
                message(e,"bx.adjustment.donor","/choices/adjustments/"+key,"This reduction is not permitted: only eligible donor abilities may fall, in pairs of points, and never below 9.","B6");
        } else if (delta>0) {
            gained+=delta;
            if (!containsString(cls->at("primeRequisites"),key)) message(e,"bx.adjustment.prime","/choices/adjustments/"+key,"Only a prime requisite may be increased.","B6");
        }
        scores[key]=raw[key]+delta;
        if(scores[key]<3 || scores[key]>18) message(e,"bx.adjustment.range","/choices/adjustments/"+key,"The adjusted score must remain between 3 and 18.","B6");
        // Invalid drafts remain inspectable, but out-of-range input does not index tables.
        scores[key]=std::clamp(scores[key],3,18);
        addCalculation(e,"ability."+key,abilityLabels[i],scores[key],{"Accepted 3d6: "+std::to_string(raw[key])+".","Permitted adjustment requested: "+std::to_string(delta)+"."},{bxSource("B5-B7")});
    }
    if(spent != 2*gained) message(e,"bx.adjustment.exchange","/choices/adjustments","Ability adjustments must spend exactly 2 eligible points for every 1 prime-requisite point gained.","B6");
    e.stages.push_back(adjustmentsStage);
    const int str=modifier(scores["str"]), dex=modifier(scores["dex"]), con=modifier(scores["con"]), wis=modifier(scores["wis"]);
    int xpPercent=0;
    if(profile=="bx:elf") xpPercent=scores["str"]>=13 && scores["int"]>=16?10:scores["str"]>=13 && scores["int"]>=13?5:0;
    else if(profile=="bx:halfling") xpPercent=scores["str"]>=13 && scores["dex"]>=13?10:scores["str"]>=13 || scores["dex"]>=13?5:0;
    else {const int prime=scores[cls->at("primeRequisites").at(0).get<std::string>()];xpPercent=prime<=5?-20:prime<=8?-10:prime<=12?0:prime<=15?5:10;}
    addCalculation(e,"xp.adjustment","XP award adjustment (%)",xpPercent,{"Class-specific prime-requisite rule applied to newly awarded experience."},{bxSource(profile=="bx:elf"?"B9":profile=="bx:halfling"?"B10":"B7")});
    Stage advancementStage{"advancement","Hit points and advancement",{}};
    const int rolledLevels=std::min(level,cls->at("rolledHitDiceLimit").get<int>());
    const auto hpRolls=c.value("hp",Json::array());
    int hp=0;
    std::vector<std::string> hpSteps;
    Json progression=Json::array();
    for (int i=0;i<rolledLevels;++i) {
        const auto path="/hp/"+std::to_string(i);
        advancementStage.fields.push_back(numberField(path,"Level "+std::to_string(i+1)+" hit die (d"+std::to_string(hitDie)+")",1,hitDie,"Record the accepted unmodified die result; Constitution applies separately."));
        int roll=0;
        if(hpRolls.is_array() && static_cast<std::size_t>(i)<hpRolls.size() && hpRolls[i].is_number_integer()) {
            auto n=hpRolls[i].get<std::int64_t>();
            if(n>=1 && n<=hitDie) roll=static_cast<int>(n);
        }
        if(roll==0) message(e,"bx.hp.roll","/choices"+path,"Record an accepted d"+std::to_string(hitDie)+" result for level "+std::to_string(i+1)+".","B6, X5-X6");
        const int gainedHp=roll>0?std::max(1,roll+con):0;
        hp+=gainedHp;
        hpSteps.push_back("Level "+std::to_string(i+1)+": "+(roll?std::to_string(roll)+" + Constitution "+std::to_string(con)+", minimum 1 = "+std::to_string(gainedHp):"hit die missing")+".");
        progression.push_back({{"level",i+1},{"hitDie",roll?Json(roll):Json(nullptr)},{"constitution",con},{"hpAdded",gainedHp}});
    }
    const int fixed=cls->at("fixedHpPerLevel").get<int>();
    for(int l=rolledLevels+1;l<=level;++l){hp+=fixed;hpSteps.push_back("Level "+std::to_string(l)+": fixed "+std::to_string(fixed)+"; Constitution no longer applies.");progression.push_back({{"level",l},{"hpAdded",fixed},{"constitution",0}});}
    addCalculation(e,"hp.max","Maximum hit points",hp,hpSteps,{bxSource("B6-B7"),entryRef(*cls,"progressionSource")});
    addCalculation(e,"advancement","Advancement inputs",progression,{"Each rolled hit die is saved; fixed gains after ninth level do not roll or add Constitution."},{entryRef(*cls,"progressionSource")});
    if(d.resources.contains("hp")) {
        const int current=readNumber(e,d.resources,"hp",0,2000000000,0,"/resources/hp",true,"B6");
        addCalculation(e,"hp.current","Current hit points",current,{"Editable current resource, kept separate from maximum HP and accepted advancement rolls."},{bxSource("B6")});
        if(current>hp) message(e,"bx.hp.current","/resources/hp","Current hit points exceed the calculated maximum; check a healing entry or maximum-HP override.","B6","warning");
    }
    advancementStage.fields.push_back(numberField("/xpAward","Proposed adventure XP award (before adjustment)",0,2000000000,"Preview only: accept the calculated total into Experience points after the adventure."));
    const int award=readNumber(e,c,"xpAward",0,2000000000,0,"/choices/xpAward",false,"B22");
    if(award>0) {
        const auto adjusted=static_cast<long long>(award)*(100+xpPercent)/100;
        auto after=static_cast<long long>(xp)+adjusted;
        if(level+1<maxLevel) after=std::min(after,std::max<long long>(xp,xpTable.at(static_cast<std::size_t>(level+1)).get<int>()-1));
        addCalculation(e,"xp.awardAdjusted","Proposed adjusted XP award",adjusted,{"Award × (100 + prime-requisite percentage) / 100; fractional XP discarded as an explicit implementation convention."},{bxSource("B7, B22")});
        addCalculation(e,"xp.afterAward","XP after proposed award and level cap",after,{"An adventure may advance at most one level; excess stops one XP short of a second level. Existing XP is never reduced."},{bxSource("B22")});
    }
    e.stages.push_back(advancementStage);

    const auto alignment=stringChoice(c,"alignment");
    Stage identityStage{"identity","Alignment and languages",{selectField("/alignment","Alignment",{{"lawful","Lawful",true,{}, {bxSource("B11")}},{"neutral","Neutral",true,{}, {bxSource("B11")}},{"chaotic","Chaotic",true,{}, {bxSource("B11")}}})}};
    if(alignment!="lawful" && alignment!="neutral" && alignment!="chaotic") message(e,"bx.alignment","/choices/alignment","Choose lawful, neutral, or chaotic alignment.","B11");
    addCalculation(e,"alignment","Alignment",alignment.empty()?"Unselected":alignment,{"Character alignment also determines the alignment language."},{bxSource("B11")});
    std::vector<std::string> languages={"Common"};
    if(!alignment.empty()) languages.push_back(alignment+" alignment language");
    for(const auto& s:strings(cls->at("nativeLanguages"))) languages.push_back(s);
    const int bonusLanguages=std::max(0,modifier(scores["int"]));
    std::vector<Choice> languageOptions;
    for(const auto& [id,item]:rules.content) if(item.value("kind","")=="language") {
        const bool native=std::find(languages.begin(),languages.end(),item.value("name",""))!=languages.end();
        languageOptions.push_back(option(id,item,!native,native?"Already a native language.":""));
    }
    identityStage.fields.push_back(selectField("/languages","Additional languages ("+std::to_string(bonusLanguages)+")",languageOptions,true));
    const auto chosenLanguages=strings(c.value("languages",Json::array()));
    std::set<std::string> languageIds;
    for(const auto& id:chosenLanguages) {
        const auto* item=rules.find(id);
        if(!item || item->value("kind","")!="language") message(e,"bx.language.unavailable","/choices/languages","Selected language '"+id+"' is unavailable; selection retained.","B13");
        else if(!languageIds.insert(id).second || std::find(languages.begin(),languages.end(),item->value("name",""))!=languages.end()) message(e,"bx.language.duplicate","/choices/languages","Languages must be distinct and cannot duplicate native languages.","B13");
        else languages.push_back(item->value("name",id));
    }
    if(static_cast<int>(chosenLanguages.size())!=bonusLanguages) message(e,"bx.language.count","/choices/languages","Choose exactly "+std::to_string(bonusLanguages)+" additional language(s) from Intelligence.","B7, B13");
    addCalculation(e,"languages","Languages",join(languages),{"Native class languages plus alignment language and "+std::to_string(bonusLanguages)+" from Intelligence."},{bxSource("B7, B9-B10, B13")});
    addCalculation(e,"literacy","Literacy",scores["int"]==3?"Trouble speaking; cannot read or write":scores["int"]<=5?"Cannot read or write Common":scores["int"]<=8?"Can write simple Common words":"Reads and writes native languages",{"Intelligence language table."},{bxSource("B7")});
    const std::array<int,7> reactions={-2,-1,-1,0,1,1,2};
    addCalculation(e,"reaction","Reaction adjustment",reactions[band(scores["cha"])],{"Charisma reaction table (distinct from the general ability modifier)."},{bxSource("B7")});
    addCalculation(e,"retainers.max","Maximum retainers",band(scores["cha"])+1,{"Charisma retainer table."},{bxSource("B7")});
    addCalculation(e,"retainers.morale","Retainer morale",band(scores["cha"])+4,{"Charisma retainer morale table."},{bxSource("B7")});
    e.stages.push_back(identityStage);

    const auto options=c.value("options",Json::object());
    auto enabled=[&](const std::string& key,bool fallback){return options.contains(key)?boolChoice(options,key,fallback):boolChoice(d.campaign,key,fallback);};
    const bool variableDamage=enabled("variableWeaponDamage",false);
    const bool individualInitiative=enabled("individualInitiative",false);
    const bool expertWeapons=enabled("expertWeaponRules",false);
    const bool rerollFirst=enabled("rerollLowFirstHp",false);
    const std::string encumbrance=stringChoice(options,"encumbrance",stringChoice(d.campaign,"encumbrance","basic"));
    Stage optionalStage{"options","Optional rules",{}};
    for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"variableWeaponDamage","Variable weapon damage (B27, X25)"},{"expertWeaponRules","Two-handed weapon / crossbow timing (X4)"},{"individualInitiative","Individual initiative (B7, B23)"},{"rerollLowFirstHp","DM permits rerolling first-level HP of 1 or 2 (B6)"}})
        optionalStage.fields.push_back({"/options/"+item.first,item.second,"boolean",0,1,{},true,"Published optional rule; accepted rolls remain unchanged."});
    auto encField=selectField("/options/encumbrance","Encumbrance method",{{"basic","Armor and treasure categories",true,{}, {bxSource("B20")}},{"detailed","Detailed coin weights",true,{}, {bxSource("B20")}}});encField.advanced=true;optionalStage.fields.push_back(encField);
    if(encumbrance!="basic" && encumbrance!="detailed") message(e,"bx.option.encumbrance","/campaign/encumbrance","Encumbrance method must be basic or detailed.","B20");
    if(rerollFirst && hpRolls.is_array() && !hpRolls.empty() && hpRolls[0].is_number_integer() && hpRolls[0].get<int>()<=2)
        message(e,"bx.hp.reroll.available","/choices/hp/0","The campaign permits rerolling this first-level result. Roll explicitly and record the accepted result if desired.","B6","info");
    e.stages.push_back(optionalStage);

    Stage equipmentStage{"equipment","Money and equipment",{numberField("/moneyRoll","Starting money roll (3d6 total)",3,18,"Starting money is this accepted result multiplied by 10 gp. Higher-level extra wealth is a documented DM grant.")}};
    const int moneyRoll=readNumber(e,c,"moneyRoll",3,18,0,"/choices/moneyRoll",true,"B5");
    equipmentStage.fields.push_back(numberField("/wealthGrant","Additional wealth granted by DM (gp)",0,1000000000,"House ruling for higher-level creation or treasure; record a reason below.",true));
    equipmentStage.fields.push_back({"/wealthReason","Reason for additional wealth","text",0,0,{},true,"Required when a DM wealth grant is used."});
    const int grant=readNumber(e,c,"wealthGrant",0,1000000000,0,"/choices/wealthGrant",false,"X9");
    if(grant && stringChoice(c,"wealthReason").find_first_not_of(" \t\r\n")==std::string::npos) message(e,"bx.wealth.reason","/choices/wealthReason","A DM wealth grant requires a reason and is identified as a house ruling.","X9");
    if(grant) message(e,"bx.wealth.house_rule","/choices/wealthGrant","Additional wealth is a DM grant: "+stringChoice(c,"wealthReason"),"X9","info");
    const int money=moneyRoll*10+grant;
    std::vector<Choice> armorOptions={{"","Clothing only",true,{}, {bxSource("B12")}}},weaponOptions={{"","Unarmed",true,{}, {bxSource("X25")}}},gearOptions;
    for(const auto& [id,item]:rules.content) {
        const auto kind=item.value("kind","");
        const auto reason=equipmentRestriction(profile,item);
        if(kind=="armor") armorOptions.push_back(option(id,item,reason.empty(),reason));
        if(kind=="weapon") {weaponOptions.push_back(option(id,item,reason.empty(),reason));gearOptions.push_back(option(id,item,reason.empty(),reason));}
        if(kind=="equipment") gearOptions.push_back(option(id,item));
    }
    equipmentStage.fields.push_back(selectField("/armor","Worn armor",armorOptions));
    equipmentStage.fields.push_back({"/shield","Shield","boolean",0,1,{},false,"Costs 10 gp; subtract 1 from armor class. Cannot combine with a two-handed weapon in use."});
    equipmentStage.fields.push_back(selectField("/weapon","Weapon in use",weaponOptions));
    equipmentStage.fields.push_back(selectField("/equipment","Additional equipment / carried weapons",gearOptions,true));
    equipmentStage.fields.push_back(numberField("/treasureCoins","Other coins carried (treasure)",0,2000000000,"Coin weight in addition to the remaining starting gold."));
    const auto armorId=stringChoice(c,"armor"), weaponId=stringChoice(c,"weapon");
    const auto* armor=armorId.empty()?nullptr:rules.find(armorId);
    const auto* weapon=weaponId.empty()?nullptr:rules.find(weaponId);
    auto checkEquipment=[&](const Json*& item,const std::string& id,const std::string& kind,const std::string& path){
        if(id.empty()) return;
        if(!item || item->value("kind","")!=kind){message(e,"bx.equipment.unavailable",path,"Selected item '"+id+"' is unavailable; selection retained.","B12, X9");item=nullptr;}
        else if(const auto reason=equipmentRestriction(profile,*item); !reason.empty()) message(e,"bx.equipment.restriction",path,reason,"B9-B10");
    };
    checkEquipment(armor,armorId,"armor","/choices/armor");checkEquipment(weapon,weaponId,"weapon","/choices/weapon");
    const bool shield=boolChoice(c,"shield");
    if(shield && (profile=="bx:magic-user" || profile=="bx:thief")) message(e,"bx.shield.restriction","/choices/shield","This class cannot use a shield.","B10");
    if(shield && weapon && weapon->value("twoHanded",false)) message(e,"bx.shield.hands","/choices/shield","A two-handed weapon cannot be used while holding a shield.","B12, X25");
    int spentGp=shield?10:0,carriedWeight=shield?100:0;
    std::vector<std::string> equipmentNames;
    bool unknownWeight=false;
    auto carry=[&](const Json& item,int quantity=1){
        spentGp+=item.value("cost",0)*quantity;
        if(item.contains("weightCoins")) {
            if(item["weightCoins"].is_number_integer()) carriedWeight+=item["weightCoins"].get<int>()*quantity;
            else unknownWeight=true;
        }
        equipmentNames.push_back(item.value("name","")+(quantity>1?" × "+std::to_string(quantity):""));
    };
    if(armor)carry(*armor);if(weapon)carry(*weapon);if(shield)equipmentNames.push_back("Shield");
    const auto gear=strings(c.value("equipment",Json::array()));
    const auto quantities=c.value("quantities",Json::object());
    std::set<std::string> gearIds;
    for(const auto& id:gear){
        if(!gearIds.insert(id).second){message(e,"bx.equipment.duplicate","/choices/equipment","Use item quantity to carry multiple copies.","B12");continue;}
        const auto* item=rules.find(id);
        if(!item || (item->value("kind","")!="equipment" && item->value("kind","")!="weapon")){message(e,"bx.equipment.unavailable","/choices/equipment","Selected item '"+id+"' is unavailable; selection retained.","B12, X9");continue;}
        const int quantity=readNumber(e,quantities,id,1,1000,1,"/choices/quantities/"+id,false,"B12");
        equipmentStage.fields.push_back(numberField("/quantities/"+id,item->value("name",id)+" quantity",1,1000));
        if(const auto reason=equipmentRestriction(profile,*item);!reason.empty())message(e,"bx.equipment.restriction","/choices/equipment",reason,"B9-B10");
        carry(*item,quantity);
    }
    const int remaining=money-spentGp;
    if(remaining<0)message(e,"bx.money.budget","/choices/equipment","Equipment costs "+std::to_string(spentGp)+" gp but available creation funds are "+std::to_string(money)+" gp.","B5, B12");
    if(profile=="bx:cleric" && !gearIds.contains("bx:holy-symbol")) message(e,"bx.cleric.symbol","/choices/equipment","A cleric must have a holy symbol.","X10");
    if(profile=="bx:thief" && !gearIds.contains("bx:thieves-tools"))message(e,"bx.thief.tools","/choices/equipment","Open Locks requires thieves' tools; the ability is unavailable without them.","X10","warning");
    addCalculation(e,"money.starting","Starting gold (gp)",money,{"Accepted 3d6 total "+std::to_string(moneyRoll)+" × 10 gp.","Additional documented DM grant: "+std::to_string(grant)+" gp."},{bxSource("B5, B12")});
    addCalculation(e,"money.spent","Equipment cost (gp)",spentGp,{"Sum of armor, shield, wielded weapon, and additional equipment quantities."},refs("B12, X9"));
    addCalculation(e,"money.remaining","Gold after creation purchases (gp)",remaining,{std::to_string(money)+" - "+std::to_string(spentGp)+"."},{bxSource("B12")});
    addCalculation(e,"equipment","Equipment",join(equipmentNames),{"Selected and purchased equipment; quantities are retained as choices."},refs("B12, X9"));
    int ac=(armor?armor->value("armorClass",9):9)-(shield?1:0)-dex;
    addCalculation(e,"ac","Armor class (descending)",ac,{"Armor base "+std::to_string(armor?armor->value("armorClass",9):9)+".","Shield: "+std::to_string(shield?-1:0)+".","Subtract Dexterity adjustment "+std::to_string(dex)+"."},{bxSource("B7, B12")});
    if(profile=="bx:halfling")addCalculation(e,"ac.largeOpponents","AC versus larger-than-man-sized opponents",ac-2,{"Normal AC "+std::to_string(ac)+"; halfling size benefit subtracts 2 against these attackers only."},{bxSource("B10")});
    const int treasure=readNumber(e,c,"treasureCoins",0,2000000000,0,"/choices/treasureCoins",false,"B20");
    carriedWeight+=80+std::max(0,remaining)+treasure;
    // B20 lumps all ordinary miscellaneous gear into a flat 80 coins.
    int movement=120;
    if(encumbrance=="detailed") {
        equipmentStage.fields.push_back(numberField("/otherWeightCoins","DM-assigned weight for unlisted items (coins)",0,1000000,"Record weights not provided by the original encumbrance table; use the reason field.",true));
        equipmentStage.fields.push_back({"/weightReason","Reason for assigned item weights","text",0,0,{},true,"Required for an assigned weight, including weapons without listed B20 weight."});
        const int extraWeight=readNumber(e,c,"otherWeightCoins",0,1000000,0,"/choices/otherWeightCoins",false,"B20");
        if((unknownWeight||extraWeight>0) && stringChoice(c,"weightReason").find_first_not_of(" \t\r\n")==std::string::npos)message(e,"bx.weight.adjudication","/choices/weightReason","The original table does not assign every item a weight. Record a DM weight and reason for unlisted carried items.","B20");
        carriedWeight+=extraWeight;
        movement=carriedWeight<=400?120:carriedWeight<=600?90:carriedWeight<=800?60:carriedWeight<=1600?30:0;
    }else{
        movement=!armor?120:armor->value("armorClass",9)>=7?90:60;
        if(treasure>0)movement=movement==120?90:movement==90?60:30;
    }
    addCalculation(e,"encumbrance","Carried weight (coins)",carriedWeight,{"Armor + weapons + shield + 80 coins miscellaneous equipment + remaining starting gold + other treasure + assigned unlisted weights.","Detailed weight determines speed only when detailed encumbrance is selected."},{bxSource("B20")});
    addCalculation(e,"movement","Normal movement (feet / turn)",movement,{encumbrance=="detailed"?"Detailed coin-weight brackets: 400 / 600 / 800 / 1600.":"Armor category; carrying treasure lowers movement by one category."},{bxSource("B20")});
    addCalculation(e,"movement.encounter","Encounter movement (feet / round)",movement/3,{"One third of normal movement."},{bxSource("B20")});
    if(!movement)message(e,"bx.encumbrance.overloaded","/choices/equipment","The character is overloaded and cannot move.","B20","warning");
    e.stages.push_back(equipmentStage);

    const auto* combat=rules.find("bx:combat-tables");
    if(!combat){message(e,"bx.rules.combat","/packs","The original character attack matrix is unavailable; a DM override cannot supply it.","X26");return e;}
    const int attackRow=(level-1)/cls->at("attackStep").get<int>();
    const auto& attack=combat->at("attackRows").at(static_cast<std::size_t>(attackRow));
    Json meleeTable=Json::object(),missileTable=Json::object(),baseTable=Json::object();
    const int missile=dex+(profile=="bx:halfling"?1:0);
    for(std::size_t i=0;i<combat->at("armorClasses").size();++i){
        const auto armorClass=std::to_string(combat->at("armorClasses")[i].get<int>());
        const int needed=attack.at(i).get<int>();
        baseTable[armorClass]=needed;meleeTable[armorClass]=needed-str;missileTable[armorClass]=needed-missile;
    }
    addCalculation(e,"attack.base","Attack matrix: armor class → roll",baseTable,{"Printed character matrix selected by class and level; repeated 20s are preserved."},{bxSource("X26")});
    addCalculation(e,"attack.melee","Melee: armor class → roll",meleeTable,{"Subtract Strength to-hit adjustment "+std::to_string(str)+" from each printed target. Natural 1 misses and natural 20 hits."},refs("B7, X26"));
    addCalculation(e,"attack.missile","Missile: armor class → roll",missileTable,{"Subtract Dexterity "+std::to_string(dex)+" and halfling missile bonus "+std::to_string(profile=="bx:halfling"?1:0)+". Range applies separately (+1 short / 0 medium / -1 long). Natural 1 misses and natural 20 hits."},refs("B7, B10, B27, X26"));
    addCalculation(e,"attack.meleeBonus","Strength to-hit / melee damage adjustment",str,{"Strength table; damage on a successful attack cannot fall below 1."},{bxSource("B7")});
    addCalculation(e,"attack.missileBonus","Missile to-hit adjustment",missile,{"Dexterity "+std::to_string(dex)+" plus halfling bonus "+std::to_string(profile=="bx:halfling"?1:0)+"; this does not increase missile damage."},{bxSource("B7, B10")});
    const int damageDie=weapon?(variableDamage?weapon->value("damageDie",6):6):2;
    const bool missileOnly=weapon && weapon->value("missileOnly",false);
    const int damageModifier=missileOnly?0:str;
    std::string damage="1d"+std::to_string(damageDie);
    if(damageModifier)damage+=(damageModifier>0?" + ":" - ")+std::to_string(std::abs(damageModifier));
    addCalculation(e,"weapon.damage","Weapon damage",damage,{weapon?(variableDamage?"Published optional variable weapon die.":"Standard weapon damage is 1d6."):"Unarmed combat inflicts 1-2 plus Strength.",missileOnly?"Missile damage receives no Dexterity bonus.":"Strength applies in hand-to-hand combat; minimum 1 damage on a hit. Thrown mode uses the die without Strength."},refs("B7, B27, X25"));
    if(weapon && weapon->value("missile",false) && !weapon->value("missileRange",Json::array()).empty())addCalculation(e,"weapon.range","Missile ranges: short / medium / long (feet)",weapon->at("missileRange"),{"Short range grants +1 to hit; medium 0; long -1. No attack beyond long range."},{bxSource("B27")});
    if(expertWeapons && weapon && weapon->value("twoHanded",false) && !missileOnly)message(e,"bx.weapon.twoHanded","/choices/weapon","A heavy two-handed weapon strikes last under the published optional weapon rule.","X4","info");
    if(expertWeapons && weaponId=="bx:crossbow")message(e,"bx.weapon.crossbow","/choices/weapon","The published optional crossbow rule fires once every two rounds (load, then fire).","X4","info");
    if(individualInitiative){const std::array<int,7> init={-2,-1,-1,0,1,1,2};addCalculation(e,"initiative","Individual initiative adjustment",init[band(scores["dex"])]+(profile=="bx:halfling"?1:0),{"Optional Dexterity initiative table; halflings add another +1."},{bxSource("B7, B10")});}
    else addCalculation(e,"initiative","Initiative","Party d6",{"Default party initiative receives no Dexterity adjustment."},{bxSource("B7, B23")});
    const int saveRow=(level-1)/cls->at("savingThrowStep").get<int>();
    const auto& saves=cls->at("savingThrows").at(static_cast<std::size_t>(saveRow));
    const std::array<std::string,5> saveIds={"death","wands","paralysis","breath","spells"};
    const std::array<std::string,5> saveLabels={"Death ray or poison","Magic wands","Paralysis or turn to stone","Dragon breath","Rods, staves, or spells"};
    for(std::size_t i=0;i<saveIds.size();++i)addCalculation(e,"save."+saveIds[i],"Save: "+saveLabels[i],saves.at(i),{"Printed class saving-throw target before conditional magical-attack adjustments."},{entryRef(*cls,"savesSource")});
    addCalculation(e,"save.magicAdjustment","Wisdom adjustment to magical-attack saves",wis,{"Bonus is added to the saving throw roll. Applies to wands, rods/staves/spells, and magical turn-to-stone. Does not apply to breath; death/poison application depends on whether the attack is magical."},{bxSource("B7")});
    addCalculation(e,"save.spellsMagic","Magical spell saving-throw target",saves.at(4).get<int>()-wis,{"Printed spell target minus Wisdom adjustment "+std::to_string(wis)+"."},refs("B7, X24"));
    std::vector<std::string> classNotes;
    for(const auto& ability:cls->at("abilities"))if(ability.at("level").get<int>()<=level)classNotes.push_back(ability.at("text").get<std::string>()+" ["+ability.at("source").value("page","")+"]");
    if(profile=="bx:cleric") {
        const auto* turn=rules.find("bx:turn-undead");
        if(!turn)message(e,"bx.rules.turning","/packs","Turning progression is unavailable.","X5");
        else {Json targets=Json::object();const auto& row=turn->at("rows").at(static_cast<std::size_t>(std::min(level,11)-1));for(std::size_t i=0;i<8;++i)targets[turn->at("undead")[i].get<std::string>()]=row.at(i);addCalculation(e,"turnUndead","Turn undead (2d6 target)",targets,{"T automatically turns; D destroys; dash means no effect. A successful attempt affects 2d6 hit dice, at least one creature."},{entryRef(*turn),bxSource("X7")});}
    }
    if(profile=="bx:thief") {
        const auto* table=rules.find("bx:thief-skills");
        if(!table)message(e,"bx.rules.thief","/packs","Thief progression is unavailable.","X6");
        else {Json skills=Json::object();for(std::size_t i=0;i<7;++i)skills[table->at("skills")[i].get<std::string>()]=table->at("rows").at(static_cast<std::size_t>(level-1)).at(i);addCalculation(e,"thief.skills","Thief abilities (%; hear noise out of 6)",skills,{"Printed X6 table; trap discovery and removal each use the listed percentage. Pick pockets subtracts 5 percentage points per victim level above 5; even high-level success retains at least 1% failure."},{entryRef(*table),bxSource("B8")});}
        addCalculation(e,"thief.backstab","Backstab to-hit bonus",4,{"Requires striking unnoticed from behind; successful damage is doubled."},{bxSource("B10")});
    }
    const std::string tradition=cls->at("spellTradition").get<std::string>();
    if(!tradition.empty()) {
        const auto& slots=cls->at("spellSlots").at(static_cast<std::size_t>(level-1));
        addCalculation(e,"spells.slots","Daily spells by spell level",slots,{"Printed class progression; array begins at first-level spells. High Intelligence grants no extra spells."},{entryRef(*cls,"progressionSource")});
        Stage spellStage{"spells","Spellbook and memorization",{}};
        std::vector<Choice> spellOptions;
        const auto known=strings(c.value("spellbook",Json::array()));
        std::set<std::string> knownIds;
        std::vector<int> knownCounts(slots.size(),0);
        for(const auto& [id,item]:rules.content)if(item.value("kind","")=="spell" && item.value("tradition","")==tradition) {
            const int spellLevel=item.at("spellLevel").get<int>();
            const bool available=spellLevel>0 && static_cast<std::size_t>(spellLevel)<=slots.size() && slots.at(static_cast<std::size_t>(spellLevel-1)).get<int>()>0;
            auto opt=option(id,item,available,available?"":"The class has no slot of this spell level.");opt.label="Level "+std::to_string(spellLevel)+": "+opt.label;spellOptions.push_back(opt);
        }
        if(tradition=="magic-user") {
            spellStage.fields.push_back(selectField("/spellbook","Spells learned (spellbook)",spellOptions,true));
            for(const auto& id:known){
                const auto* spell=rules.find(id);
                if(!spell || spell->value("kind","")!="spell" || spell->value("tradition","")!=tradition){message(e,"bx.spellbook.unavailable","/choices/spellbook","Spell '"+id+"' is unavailable in this class list or the enabled sources; selection retained.","B16, X11");continue;}
                const int sl=spell->at("spellLevel").get<int>();
                if(!knownIds.insert(id).second)message(e,"bx.spellbook.duplicate","/choices/spellbook","A spell is learned once; multiple daily copies belong in memorization slots.","X11");
                else if(sl<1 || static_cast<std::size_t>(sl)>slots.size() || slots.at(static_cast<std::size_t>(sl-1)).get<int>()==0)message(e,"bx.spellbook.level","/choices/spellbook","Cannot learn a spell above the class's available spell levels.","X11");
                else ++knownCounts[static_cast<std::size_t>(sl-1)];
            }
            for(std::size_t i=0;i<slots.size();++i)if(knownCounts[i]!=slots.at(i).get<int>())message(e,"bx.spellbook.count","/choices/spellbook","The original B/X spellbook holds exactly "+std::to_string(slots.at(i).get<int>())+" spell(s) of spell level "+std::to_string(i+1)+" at this class level.","B16, X11");
            std::vector<std::string> names;for(const auto& id:known){const auto* item=rules.find(id);names.push_back(item?item->value("name",id):id+" (unavailable)");}
            addCalculation(e,"spells.spellbook","Spellbook",join(names),{"Learned spells are limited by the daily spell counts at each spell level. Read Magic is chosen normally and is not a free extra spell. New learning requires the training described on X11."},refs("B16, X11"));
        }
        const auto prepared=c.value("prepared",Json::object());
        Json memorized=Json::object();
        for(std::size_t i=0;i<slots.size();++i) {
            const int count=slots.at(i).get<int>();
            const auto sl=std::to_string(i+1);
            std::vector<Choice> preparedOptions={{"","Leave unmemorized",true,{}, {bxSource("B15")}}};
            for(const auto& [id,spell]:rules.content)if(spell.value("kind","")=="spell" && spell.value("tradition","")==tradition && spell.at("spellLevel").get<int>()==static_cast<int>(i+1)) {
                const bool learned=tradition=="cleric" || knownIds.contains(id);
                preparedOptions.push_back(option(id,spell,learned,learned?"":"This spell is not in the spellbook."));
                if(spell.value("reversible",false)) {
                    auto reversed=option(id+":reverse",spell,learned,learned?"":"The normal spell is not in the spellbook.");
                    reversed.label=spell.value("reverseName","")+" (reversed "+spell.value("name","")+")";
                    preparedOptions.push_back(reversed);
                }
            }
            const auto chosen=prepared.is_object()?prepared.value(sl,Json::array()):Json::array();
            if(!chosen.is_array())message(e,"bx.spells.preparedType","/choices/prepared/"+sl,"Memorized slots must be an array of spell identifiers.","B15");
            else if(static_cast<int>(chosen.size())>count)message(e,"bx.spells.count","/choices/prepared/"+sl,"More spells are memorized than the class permits at this spell level.","X5-X6");
            std::vector<std::string> names;
            for(int slot=0;slot<count;++slot) {
                const auto slotPath="/prepared/"+sl+"/"+std::to_string(slot);
                auto field=selectField(slotPath,"Spell level "+sl+", daily slot "+std::to_string(slot+1),preparedOptions);
                field.help="Duplicate memorization is allowed. Casting and recovery are tracked separately as current resources.";spellStage.fields.push_back(field);
                const std::string selected=chosen.is_array() && static_cast<std::size_t>(slot)<chosen.size() && chosen[slot].is_string()?chosen[slot].get<std::string>():"";
                if(selected.empty()){names.push_back("Unmemorized");continue;}
                const bool reverse=selected.size()>8 && selected.ends_with(":reverse");
                const auto baseId=reverse?selected.substr(0,selected.size()-8):selected;
                const auto* spell=rules.find(baseId);
                if(!spell || spell->value("kind","")!="spell" || spell->value("tradition","")!=tradition || spell->at("spellLevel").get<int>()!=static_cast<int>(i+1) || (tradition=="magic-user" && !knownIds.contains(baseId)) || (reverse && !spell->value("reversible",false))) {
                    message(e,"bx.spells.choice","/choices"+slotPath,"This spell is unavailable in this memorization slot; selection retained.","B15-B16, X11");names.push_back(selected+" (unavailable)");
                } else {
                    names.push_back(reverse?spell->value("reverseName",""):spell->value("name",selected));
                    if(reverse && tradition=="cleric")message(e,"bx.spell.reversal","/choices"+slotPath,"A reversed clerical spell requires the alignment and deity judgment described in X11; clerics can reverse a spell when casting.","X11","info");
                }
            }
            if(count)memorized[sl]=names;
        }
        // Preserve and diagnose slots outside every supported spell level as well.
        if(prepared.is_object())for(auto it=prepared.begin();it!=prepared.end();++it){
            bool recognized=false;for(std::size_t i=0;i<slots.size();++i)if(it.key()==std::to_string(i+1))recognized=true;
            if(!recognized && !it.value().empty())message(e,"bx.spells.extraLevel","/choices/prepared/"+it.key(),"No supported spell level corresponds to these retained memorization choices.","X5-X6");
        }
        addCalculation(e,"spells.prepared","Memorized spells",memorized,{"Each slot is selected independently; multiple copies of a known spell are allowed. An unused slot remains unmemorized. Rest and one hour of study or prayer restore expended spells.","Reversed magic-user/elf forms are chosen while memorizing; clerical reversals are available when casting and subject to deity/alignment judgment."},refs("B15-B16, X11"));
        e.stages.push_back(spellStage);
    } else if(!c.value("spellbook",Json::array()).empty() || !c.value("prepared",Json::object()).empty())message(e,"bx.spells.class","/choices/spellbook","The selected class has no spellcasting progression. Retained spell choices are unavailable.","X5-X6");

    e.sections.push_back({"Character",{"class","level","alignment","xp","xp.minimum","xp.next","xp.adjustment"},{}});
    e.sections.push_back({"Abilities",{"ability.str","ability.int","ability.wis","ability.dex","ability.con","ability.cha","languages","literacy","reaction","retainers.max","retainers.morale"},{}});
    e.sections.push_back({"Combat",{"hp.max","hp.current","ac","ac.largeOpponents","movement","movement.encounter","initiative","weapon.damage","weapon.range","attack.meleeBonus","attack.missileBonus","attack.base","attack.melee","attack.missile"},{"Attack matrices preserve B/X descending armor class. Natural 1 misses; natural 20 hits."}});
    e.sections.push_back({"Saving throws",{"save.death","save.wands","save.paralysis","save.breath","save.spells","save.magicAdjustment","save.spellsMagic"},{}});
    e.sections.push_back({"Class abilities",{"turnUndead","thief.skills","thief.backstab"},classNotes});
    e.sections.push_back({"Spells",{"spells.slots","spells.spellbook","spells.prepared"},{}});
    e.sections.push_back({"Equipment and money",{"equipment","money.starting","money.spent","money.remaining","encumbrance"},{}});
    e.sections.push_back({"Advancement",{"advancement","xp.awardAdjusted","xp.afterAward"},{}});
    return e;
}

std::vector<Message> validateBx(const ContentPack& pack) {
    std::vector<Message> errors;
    auto fail=[&](const Json& entry,const std::string& why){errors.push_back({"error","pack.bx.mechanics","/packs/"+pack.manifest.value("id","")+"/"+entry.value("id",""),why,{entryRef(entry)}});};
    const std::set<std::string> kinds={"class","table","weapon","armor","equipment","language","spell","coverage"};
    auto integer=[](const Json& j,long long low,long long high){if(!j.is_number_integer())return false;try{auto n=j.get<long long>();return n>=low && n<=high;}catch(...){return false;}};
    auto ints=[&](const Json& j,std::size_t count,int low,int high){if(!j.is_array() || j.size()!=count)return false;return std::all_of(j.begin(),j.end(),[&](const auto& n){return integer(n,low,high);});};
    auto rows=[&](const Json& j,std::size_t count,std::size_t columns,int low,int high){if(!j.is_array() || j.size()!=count)return false;return std::all_of(j.begin(),j.end(),[&](const auto& r){return ints(r,columns,low,high);});};
    auto stringArray=[](const Json& j){return j.is_array() && std::all_of(j.begin(),j.end(),[](const auto& s){return s.is_string();});};
    for(const auto& item:pack.entries) {
        const auto kind=item.value("kind","");
        if(!kinds.contains(kind)){fail(item,"B/X does not support content kind '"+kind+"'. A new mechanic needs a module extension.");continue;}
        if(item.contains("modifiers") || item.contains("effects") || item.contains("mechanics")){fail(item,"Unrecognized executable modifier/effect declarations are unsupported by this B/X module.");continue;}
        if(kind=="class") {
            const std::set<std::string> profiles={"cleric","dwarf","elf","fighter","halfling","magic-user","thief"};
            if(!item.contains("rulesProfile") || !item["rulesProfile"].is_string() || !profiles.contains(item["rulesProfile"].get<std::string>()))fail(item,"Class requires a supported rulesProfile to interpret its special mechanics.");
            bool good=true;
            for(const auto& key:{"hitDie","maxLevel","rolledHitDiceLimit","fixedHpPerLevel","attackStep","savingThrowStep"}) {
                if(!item.contains(key) || !integer(item[key],key==std::string("fixedHpPerLevel")?0:1,20)){fail(item,std::string("Class requires bounded integer ")+key+".");good=false;}
            }
            if(!good)continue;
            const int cap=item["maxLevel"].get<int>(),astep=item["attackStep"].get<int>(),sstep=item["savingThrowStep"].get<int>();
            if(cap>14 || item["rolledHitDiceLimit"].get<int>()>9 || (cap-1)/astep>=5)fail(item,"Class exceeds this module's supported original B/X progression limits.");
            if(!item.contains("xp") || !ints(item["xp"],static_cast<std::size_t>(cap),0,2000000000) || item["xp"][0]!=0 || !std::is_sorted(item["xp"].begin(),item["xp"].end()) || std::adjacent_find(item["xp"].begin(),item["xp"].end())!=item["xp"].end())fail(item,"XP progression must start at zero and have one strictly increasing integer threshold per level.");
            if(!item.contains("savingThrows") || !rows(item["savingThrows"],static_cast<std::size_t>((cap+sstep-1)/sstep),5,1,20))fail(item,"Saving throws must contain every level band and exactly five targets per row.");
            if(!item.contains("primeRequisites") || !stringArray(item["primeRequisites"]) || item["primeRequisites"].empty())fail(item,"Class requires at least one named prime requisite.");
            if(!item.contains("abilityDonors") || !stringArray(item["abilityDonors"]))fail(item,"Class requires an ability donor list.");
            for(const auto& key:{"primeRequisites","abilityDonors"})if(item.contains(key) && stringArray(item[key]))for(const auto& value:item[key])if(std::find(abilityKeys.begin(),abilityKeys.end(),value.get<std::string>())==abilityKeys.end())fail(item,"Unknown ability identifier in class.");
            if(!item.contains("minimumAbilities") || !item["minimumAbilities"].is_object())fail(item,"Class requires minimumAbilities object.");
            else for(auto it=item["minimumAbilities"].begin();it!=item["minimumAbilities"].end();++it)if(std::find(abilityKeys.begin(),abilityKeys.end(),it.key())==abilityKeys.end() || !integer(it.value(),3,18))fail(item,"Invalid class ability prerequisite.");
            if(item.contains("primeRequisites") && stringArray(item["primeRequisites"])) {
                const auto primes=strings(item["primeRequisites"]);
                const std::set<std::string> unique(primes.begin(),primes.end());
                const auto profile=item.value("rulesProfile","");
                if(unique.size()!=primes.size())fail(item,"Prime requisites must be distinct.");
                if(profile=="elf" && unique!=std::set<std::string>{"str","int"})fail(item,"Elf profile requires Strength and Intelligence prime requisites.");
                else if(profile=="halfling" && unique!=std::set<std::string>{"str","dex"})fail(item,"Halfling profile requires Strength and Dexterity prime requisites.");
                else if(profile!="elf" && profile!="halfling" && primes.size()!=1)fail(item,"This supported class profile requires exactly one prime requisite.");
            }
            if(!item.contains("nativeLanguages") || !stringArray(item["nativeLanguages"]))fail(item,"Class requires nativeLanguages array.");
            for(const auto& key:{"progressionSource","savesSource","attackSource"})if(!item.contains(key) || !item[key].is_object() || !item[key].contains("publication") || !item[key]["publication"].is_string() || !item[key].contains("page") || !item[key]["page"].is_string())fail(item,std::string("Class requires ")+key+" publication and printed page.");
            if(!item.contains("spellTradition") || !item["spellTradition"].is_string()){fail(item,"Class requires a supported spellTradition.");continue;}
            const auto tradition=item["spellTradition"].get<std::string>();
            if(tradition!="" && tradition!="cleric" && tradition!="magic-user")fail(item,"Unknown B/X spell tradition.");
            if(!item.contains("spellSlots") || !item["spellSlots"].is_array())fail(item,"Class requires spellSlots array.");
            else if(tradition.empty()?!item["spellSlots"].empty():!rows(item["spellSlots"],static_cast<std::size_t>(cap),tradition=="cleric"?5:6,0,20))fail(item,"Spell slots must contain one correctly sized row per class level, or be empty for noncasters.");
            if(!item.contains("abilities") || !item["abilities"].is_array())fail(item,"Class requires an abilities list.");
            else for(const auto& a:item["abilities"])if(!a.is_object() || !a.contains("level") || !integer(a["level"],1,cap) || !a.contains("text") || !a["text"].is_string() || !a.contains("source") || !a["source"].is_object() || !a["source"].contains("publication") || !a["source"]["publication"].is_string() || !a["source"].contains("page") || !a["source"]["page"].is_string())fail(item,"Class ability requires a supported level, original descriptive text, and source.");
        }
        if(kind=="table") {
            const auto id=item.value("replaces",item.value("id",""));
            if(id=="bx:combat-tables") {
                if(!item.contains("armorClasses") || !ints(item["armorClasses"],13,-3,9) || !item.contains("attackRows") || !rows(item["attackRows"],5,13,2,20))fail(item,"Attack matrix requires five rows of thirteen targets and thirteen armor classes.");
                else {std::set<int> ac;for(const auto& n:item["armorClasses"])ac.insert(n.get<int>());if(ac.size()!=13)fail(item,"Attack armor classes must be unique.");}
            }else if(id=="bx:thief-skills") {
                if(!item.contains("skills") || !stringArray(item["skills"]) || item["skills"].size()!=7 || !item.contains("rows") || !rows(item["rows"],14,7,0,200))fail(item,"Thief table requires seven skill labels and fourteen seven-value rows.");
                else for(const auto& row:item["rows"])if(!integer(row[6],1,6))fail(item,"Hear-noise target must be a d6 result.");
            }else if(id=="bx:turn-undead") {
                if(!item.contains("undead") || !stringArray(item["undead"]) || item["undead"].size()!=8 || !item.contains("rows") || !item["rows"].is_array() || item["rows"].size()!=11)fail(item,"Turning table requires eight labels and eleven rows.");
                else for(const auto& row:item["rows"])if(!stringArray(row) || row.size()!=8)fail(item,"Turning row must contain eight results.");else for(const auto& value:row)if(value!="-" && value!="T" && value!="D" && value!="7" && value!="9" && value!="11")fail(item,"Unsupported turning result.");
            }else fail(item,"No evaluator is registered for table '"+id+"'.");
        }
        if(kind=="equipment" || kind=="armor" || kind=="weapon")if(!item.contains("cost") || !integer(item["cost"],0,1000000))fail(item,"Equipment requires a nonnegative integer gp price.");
        if(kind=="armor") {
            if(!item.contains("armorClass") || !integer(item["armorClass"],-10,9) || !item.contains("weightCoins") || !integer(item["weightCoins"],0,1000000))fail(item,"Armor requires armorClass and coin weight.");
        }
        if(kind=="weapon") {
            if(!item.contains("damageDie") || !integer(item["damageDie"],2,20))fail(item,"Weapon requires a supported damage die.");
            for(const auto& key:{"twoHanded","missile","missileOnly","clericAllowed","magicUserAllowed","demihumanForbidden"})if(!item.contains(key) || !item[key].is_boolean())fail(item,std::string("Weapon requires boolean ")+key+".");
            if(!item.contains("weightCoins") || (!item["weightCoins"].is_null() && !integer(item["weightCoins"],0,1000000)))fail(item,"Weapon coin weight must be an integer or explicit null for an unlisted original weight.");
            if(!item.contains("missileRange") || !item["missileRange"].is_array() || (!item["missileRange"].empty() && !ints(item["missileRange"],3,1,10000)))fail(item,"Missile ranges must be empty or a three-integer range.");
            else if(!item["missileRange"].empty() && (!(item["missileRange"][0]<item["missileRange"][1]) || !(item["missileRange"][1]<item["missileRange"][2])))fail(item,"Short, medium, and long ranges must increase strictly.");
        }
        if(kind=="spell") {
            if(!item.contains("tradition") || !item["tradition"].is_string() || (item["tradition"]!="cleric" && item["tradition"]!="magic-user"))fail(item,"Spell requires cleric or magic-user tradition.");
            if(!item.contains("spellLevel") || !integer(item["spellLevel"],1,item.value("tradition","")=="cleric"?5:6))fail(item,"Invalid B/X spell level.");
            if(!item.contains("reversible") || !item["reversible"].is_boolean() || !item.contains("reverseName") || !item["reverseName"].is_string())fail(item,"Spell requires reversibility metadata.");
            else if(item["reversible"].get<bool>() && item["reverseName"].get<std::string>().empty())fail(item,"A reversible spell needs its reversed name.");
        }
    }
    return errors;
}
} // namespace
EditionModule bxModule(){return {"bx","Original B/X (1981)","1.0.0",false,evaluateBx,validateBx,[](const ResolvedRuleset& rules){
    std::vector<Message> messages;
    auto required=[&](const std::string& id,const std::string& purpose){
        if(!rules.find(id))messages.push_back({"error","content.bx.missing_reference","/packs","Required B/X table '"+id+"' is unavailable for "+purpose+". Overrides cannot supply missing rules.",{bxSource("X5-X6, X26")}});
    };
    bool anyClass=false;
    for(const auto& [id,item]:rules.content)if(item.value("kind","")=="class") {
        anyClass=true;
        const auto profile=item.value("rulesProfile","");
        if(profile=="cleric")required("bx:turn-undead",id);
        if(profile=="thief")required("bx:thief-skills",id);
    }
    if(anyClass)required("bx:combat-tables","character classes");
    return messages;
}};}
} // namespace dnd
