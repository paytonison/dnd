#include "dnd/engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>

namespace dnd {
namespace {
constexpr std::array<const char*, 6> abilities{"strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"};
SourceRef ref(const std::string& page) {
    return {"System Reference Document 5.2.1", page,
            "https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=" + page};
}
const Json* at(const Json& j, const std::string& pointer) {
    try { const Json::json_pointer p(pointer); return j.contains(p) ? &j.at(p) : nullptr; }
    catch (...) { return nullptr; }
}
std::string str(const Json& j, const std::string& pointer, const std::string& fallback = {}) {
    const auto* value = at(j, pointer);
    return value && value->is_string() ? value->get<std::string>() : fallback;
}
int num(const Json& j, const std::string& pointer, int fallback = 0) {
    const auto* value = at(j, pointer);
    if (!value || !value->is_number_integer()) return fallback;
    try { const auto n = value->get<std::int64_t>(); return n >= -1000000000 && n <= 1000000000 ? static_cast<int>(n) : fallback; }
    catch (...) { return fallback; }
}
bool flag(const Json& j, const std::string& pointer) {
    const auto* value = at(j, pointer); return value && value->is_boolean() && value->get<bool>();
}
std::vector<std::string> strings(const Json& j, const std::string& pointer) {
    std::vector<std::string> result;
    if (const auto* value = at(j, pointer); value && value->is_array())
        for (const auto& entry : *value) if (entry.is_string()) result.push_back(entry.get<std::string>());
    return result;
}
bool includes(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}
std::string title(std::string name) { if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))); return name; }
std::string signedNumber(int value) { return (value >= 0 ? "+" : "") + std::to_string(value); }
std::string joined(const std::vector<std::string>& list) {
    std::string result;
    for (const auto& item : list) { if (!result.empty()) result += ", "; result += item; }
    return result.empty() ? "None" : result;
}
SourceRef entryRef(const Json& entry) { return sourceFromJson(entry.at("source")); }
Choice literal(const std::string& id, const std::string& label, const std::string& page = "21") {
    return {id, label, true, {}, {ref(page)}};
}
std::vector<Choice> options(const ResolvedRuleset& rules, const std::string& kind,
                            const std::function<bool(const Json&)>& accept = {}) {
    std::vector<Choice> result;
    for (const auto& [id, entry] : rules.content)
        if (entry.value("kind", "") == kind && (!accept || accept(entry)))
            result.push_back({id, entry.at("name"), true, {}, {entryRef(entry)}});
    std::sort(result.begin(), result.end(), [](const Choice& a, const Choice& b) { return a.label < b.label; });
    return result;
}
Field select(const std::string& path, const std::string& label, std::vector<Choice> opts,
             bool multi = false, const std::string& help = {}) {
    return {path, label, multi ? "multiselect" : "select", 0, 999999, std::move(opts), false, help};
}
Field integer(const std::string& path, const std::string& label, int min, int max, const std::string& help = {}) {
    return {path, label, "integer", min, max, {}, false, help};
}
void issue(Evaluation& e, const std::string& code, const std::string& path, const std::string& text,
           const std::string& page = "19", const std::string& severity = "error") {
    e.messages.push_back({severity, "srd55." + code, path, text, {ref(page)}});
}
void numericInput(Evaluation& e, const Json& choices, const std::string& path, int min, int max, bool required = true) {
    const auto* value = at(choices, path);
    if ((!value || value->is_null()) && !required) return;
    if (!value || !value->is_number_integer() || num(choices, path, min - 1) < min || num(choices, path, max + 1) > max)
        issue(e, "number", path, "Enter a whole number from " + std::to_string(min) + " to " + std::to_string(max) + ".", "21");
}
std::string pick(Evaluation& e, const Json& choices, const Field& field, bool required = true, const std::string& fallback = {}) {
    auto value = str(choices, field.path, fallback);
    const auto* raw = at(choices, field.path);
    if (raw && !raw->is_string()) issue(e, "choice.type", field.path, "This selection must be a content identifier.");
    if (value.empty()) {
        if (required) issue(e, "choice.missing", field.path, "Choose " + field.label + ".");
        return {};
    }
    auto match = std::find_if(field.options.begin(), field.options.end(), [&](const auto& option) { return option.id == value; });
    if (match == field.options.end()) issue(e, "choice.unavailable", field.path, "The saved selection '" + value + "' is unavailable; it has been preserved.");
    else if (!match->available) issue(e, "choice.ineligible", field.path, match->reason);
    return value;
}
std::vector<std::string> picks(Evaluation& e, const Json& choices, const Field& field, int count, bool exact = true) {
    auto result = strings(choices, field.path);
    const auto* raw = at(choices, field.path);
    if (raw && (!raw->is_array() || !std::all_of(raw->begin(), raw->end(), [](const Json& j) { return j.is_string(); })))
        issue(e, "choice.type", field.path, "Selections must be an array of content identifiers.");
    if ((exact && static_cast<int>(result.size()) != count) || (!exact && static_cast<int>(result.size()) > count))
        issue(e, "choice.count", field.path, "Choose " + std::string(exact ? "exactly " : "at most ") + std::to_string(count) + " for " + field.label + ".");
    std::set<std::string> seen;
    for (const auto& id : result) {
        if (!seen.insert(id).second) issue(e, "choice.duplicate", field.path, "Each choice must be distinct: " + id + ".");
        auto match = std::find_if(field.options.begin(), field.options.end(), [&](const auto& o) { return o.id == id; });
        if (match == field.options.end()) issue(e, "choice.unavailable", field.path, "The saved selection '" + id + "' is unavailable or has the wrong kind; it is preserved.");
        else if (!match->available) issue(e, "choice.ineligible", field.path, match->reason);
    }
    return result;
}
const Json* selectedEntry(const ResolvedRuleset& rules, const std::string& id, const std::string& kind) {
    const auto* entry = rules.find(id);
    return entry && entry->value("kind", "") == kind ? entry : nullptr;
}
void record(Evaluation& e, SheetSection& section, const std::string& id, const std::string& label, Json value,
            std::vector<std::string> steps, std::vector<SourceRef> sources) {
    addCalculation(e, id, label, std::move(value), std::move(steps), std::move(sources));
    section.calculationIds.push_back(id);
}

std::vector<std::pair<std::string,std::string>> references(const Json& entry) {
    std::vector<std::pair<std::string,std::string>> result;
    for(const auto& [key,expected]:std::vector<std::pair<std::string,std::string>>{{"skills","skill"},{"feat","feat"},{"tool","tool"},{"classId","class"},{"speciesId","species"},{"cantrips","spell"},{"level1Spell","spell"},{"level3Spell","spell"}}){
        const auto* v=at(entry,"/"+key);if(!v)continue;
        if(v->is_string())result.emplace_back(v->get<std::string>(),expected);
        else if(v->is_array())for(const auto& id:*v)if(id.is_string())result.emplace_back(id.get<std::string>(),expected);
    }
    const auto* kits=at(entry,"/kits");if(kits&&kits->is_object())for(const auto& kit:*kits)for(const auto& id:strings(kit,"/items"))result.emplace_back(id,"equipment");
    return result;
}
bool matchingReference(const Json& entry,const std::string& expected) {
    const auto kind=str(entry,"/kind");
    return expected=="equipment" ? includes({"armor","shield","weapon","gear","tool"},kind):kind==expected;
}
std::vector<Message> validateResolved(const ResolvedRuleset& rules) {
    std::vector<Message> messages;
    for(const auto& [id,entry]:rules.content)for(const auto& [target,expected]:references(entry)){
        if(target=="srd55:gaming-set")continue;
        const auto* found=rules.find(target);
        if(!found||!matchingReference(*found,expected))
            messages.push_back({"error","srd55.content.reference",id,"Rule content requires unavailable "+expected+" reference "+target+". Restore or repair the content pack.",{ref("19")}});
    }
    return messages;
}
Evaluation run(const CharacterDocument& doc, const ResolvedRuleset& rules) {
    Evaluation e;
    const Json& c = doc.choices;
    issue(e, "experimental", "/edition", "Experimental SRD 5.2.1 coverage: single-class Fighter and Wizard, levels 1–3.", "19", "info");
    Stage identity{"identity", "Class and origins", {}};
    identity.fields.push_back(select("/classId", "Class", options(rules, "class")));
    identity.fields.push_back(integer("/level", "Level", 1, 3, "Levels 1–3 are implemented. Retain accepted HP and spell choices when advancing."));
    identity.fields.push_back(select("/speciesId", "Species", options(rules, "species")));
    identity.fields.push_back(select("/backgroundId", "Background", options(rules, "background")));
    const auto classId = pick(e, c, identity.fields[0]);
    const auto speciesId = pick(e, c, identity.fields[2]);
    const auto backgroundId = pick(e, c, identity.fields[3]);
    numericInput(e, c, "/level", 1, 3, false);
    const int level = std::clamp(num(c, "/level", 1), 1, 3);
    const auto* cls = selectedEntry(rules, classId, "class");
    const auto* species = selectedEntry(rules, speciesId, "species");
    const auto* background = selectedEntry(rules, backgroundId, "background");
    if (cls && classId != "srd55:fighter" && classId != "srd55:wizard") {
        issue(e, "class.unsupported", "/classId", "This class requires an edition-module extension; only Fighter and Wizard mechanics are implemented.");
        cls = nullptr;
    }
    const bool fighter = classId == "srd55:fighter";
    const bool wizard = classId == "srd55:wizard";
    std::vector<Choice> alignments;
    for (const auto* name : {"Lawful Good", "Neutral Good", "Chaotic Good", "Lawful Neutral", "Neutral", "Chaotic Neutral", "Lawful Evil", "Neutral Evil", "Chaotic Evil"}) alignments.push_back(literal(name, name));
    identity.fields.push_back(select("/alignment", "Alignment", alignments));
    const auto alignment = pick(e, c, identity.fields.back());
    if (alignment.find("Evil") != std::string::npos) issue(e, "alignment.gm", "/alignment", "The SRD asks you to check with your GM before creating an evil character.", "21", "info");
    identity.fields.push_back(select("/languages", "Two additional languages", options(rules, "language"), true, "Common is automatic; choose two Standard Languages."));
    auto languages = picks(e, c, identity.fields.back(), 2);
    if (species && species->at("sizes").size() > 1) {
        std::vector<Choice> sizes;
        for (const auto& size : species->at("sizes")) sizes.push_back(literal(size, size, "86"));
        identity.fields.push_back(select("/size", "Size", sizes));
        pick(e, c, identity.fields.back());
    }
    const auto lineageOptions = options(rules, "lineage", [&](const Json& entry) { return entry.value("speciesId", "") == speciesId; });
    const Json* lineage = nullptr;
    if (!lineageOptions.empty()) {
        identity.fields.push_back(select("/lineageId", "Ancestry or lineage", lineageOptions));
        lineage = selectedEntry(rules, pick(e, c, identity.fields.back()), "lineage");
        if (lineage && lineage->value("speciesId", "") != speciesId) lineage = nullptr;
    }
    const bool innateCaster = speciesId == "srd55:elf" || speciesId == "srd55:gnome" || speciesId == "srd55:tiefling";
    std::vector<Choice> castingAbilities;
    for (const auto* ability : {"intelligence", "wisdom", "charisma"}) castingAbilities.push_back(literal(ability, title(ability), "85"));
    if (innateCaster) {
        identity.fields.push_back(select("/speciesCastingAbility", "Species spellcasting ability", castingAbilities));
        pick(e, c, identity.fields.back());
    }
    e.stages.push_back(identity);

    Stage scores{"abilities", "Ability scores", {select("/abilityMethod", "Ability generation", {literal("standard-array", "Standard array"), literal("point-buy", "27-point buy"), literal("rolled", "Accepted 4d6-drop-lowest rolls")})}};
    const auto method = pick(e, c, scores.fields[0], true, "standard-array");
    std::map<std::string, int> mods, totals;
    std::vector<int> bases, boosts;
    int points = 0;
    bool allScores = true;
    SheetSection abilitySheet{"Abilities", {}, {}};
    const auto allowedBoosts = background ? strings(*background, "/abilities") : std::vector<std::string>{};
    for (const auto* ability : abilities) {
        const std::string path = "/abilities/" + std::string(ability);
        scores.fields.push_back(integer(path, title(ability) + " before background", 3, 18));
        numericInput(e, c, path, method == "point-buy" ? 8 : 3, method == "point-buy" ? 15 : 18);
        const int base = std::clamp(num(c, path, 10), 3, 18);
        if (!at(c, path)) allScores = false;
        bases.push_back(base);
        const std::string boostPath = "/backgroundBoosts/" + std::string(ability);
        if (includes(allowedBoosts, ability)) scores.fields.push_back(integer(boostPath, title(ability) + " background increase", 0, 2, "Use +2/+1 across two listed abilities, or +1 to all three."));
        numericInput(e, c, boostPath, 0, 2, false);
        int boost = std::clamp(num(c, boostPath), 0, 2);
        if (boost && !includes(allowedBoosts, ability)) issue(e, "background.ability", boostPath, "This background cannot increase " + std::string(ability) + ".", "83");
        boosts.push_back(boost);
        int total = base + boost;
        if (total > 20) issue(e, "ability.maximum", path, "Background increases cannot raise an ability score above 20.", "21");
        totals[ability] = total;
        mods[ability] = static_cast<int>(std::floor((total - 10) / 2.0));
        record(e, abilitySheet, "ability." + std::string(ability), title(ability), total,
               {"Base " + std::to_string(base) + "; background " + signedNumber(boost) + ".", "Modifier = floor((" + std::to_string(total) + " - 10) / 2) = " + signedNumber(mods[ability]) + "."}, {ref("21"), ref("83")});
        record(e, abilitySheet, "modifier." + std::string(ability), title(ability) + " modifier", mods[ability], {"floor((score - 10) / 2)."}, {ref("21")});
        if (base >= 8 && base <= 15) points += std::array<int,8>{0,1,2,3,4,5,7,9}[static_cast<std::size_t>(base-8)];
    }
    auto sorted = bases;
    std::sort(sorted.begin(), sorted.end());
    if (allScores && method == "standard-array" && sorted != std::vector<int>{8,10,12,13,14,15}) issue(e, "ability.array", "/abilities", "Assign each standard-array score once: 15, 14, 13, 12, 10, 8.", "21");
    if (allScores && method == "point-buy" && points != 27) issue(e, "ability.points", "/abilities", "Point-buy scores must spend exactly 27 points; current cost is " + std::to_string(points) + ".", "21");
    if (method == "rolled") abilitySheet.notes.push_back("Ability values are accepted 4d6-drop-lowest results. Evaluation never rolls dice.");
    std::sort(boosts.begin(), boosts.end());
    if (background && boosts != std::vector<int>{0,0,0,0,1,2} && boosts != std::vector<int>{0,0,0,1,1,1}) issue(e, "background.boosts", "/backgroundBoosts", "Apply +2 to one listed ability and +1 to another, or +1 to each of the three.", "83");
    e.stages.push_back(scores);
    e.sections.push_back(abilitySheet);
    if (!cls) return e;

    SheetSection summary{"Identity and progression", {}, {}};
    record(e, summary, "class", "Class", cls->at("name"), {"Selected class; no multiclassing in this experimental module."}, {entryRef(*cls)});
    record(e, summary, "level", "Level", level, {"Saved advancement level."}, {ref("23")});
    const int proficiency = cls->at("proficiency").at(static_cast<std::size_t>(level-1));
    record(e, summary, "proficiency", "Proficiency bonus", proficiency, {"Class progression at level " + std::to_string(level) + "."}, {entryRef(*cls), ref("23")});
    record(e, summary, "alignment", "Alignment", alignment, {"Selected alignment."}, {ref("21")});
    std::vector<std::string> languageNames{"Common"};
    for (const auto& id : languages) if (const auto* lang = selectedEntry(rules, id, "language")) languageNames.push_back(lang->at("name"));
    record(e, summary, "languages", "Languages", joined(languageNames), {"Common plus two selected Standard Languages."}, {ref("20")});
    if (species) record(e, summary, "species", "Species", species->at("name"), {"Species selected independently from class."}, {entryRef(*species)});
    if (background) record(e, summary, "background", "Background", background->at("name"), {"Background grants ability increases, skills, a tool, and an Origin feat."}, {entryRef(*background)});
    record(e, summary, "xp.minimum", "XP for this level", cls->at("xp").at(static_cast<std::size_t>(level-1)), {"Character Advancement table."}, {ref("23")});
    if (const auto* xp = at(c, "/xp"); xp && !xp->is_null()) {
        numericInput(e, c, "/xp", 0, 100000000);
        if (num(c, "/xp") < cls->at("xp").at(static_cast<std::size_t>(level-1)).get<int>()) issue(e, "xp.level", "/xp", "Saved XP is below this level's published threshold. Leave XP unset when using milestone advancement.", "23");
    }
    e.sections.push_back(summary);

    Stage skillsStage{"proficiencies", "Skills, feats, and class features", {}};
    std::set<std::string> trained;
    if (background) for (const auto& id : strings(*background, "/skills")) trained.insert(id);
    auto skillOptions = options(rules, "skill", [&](const Json& entry) { return includes(strings(*cls, "/skills"), entry.at("id")); });
    for (auto& choice : skillOptions) if (trained.contains(choice.id)) { choice.available = false; choice.reason = "Already granted by your background; choose a different class skill."; }
    skillsStage.fields.push_back(select("/classSkills", "Two class skills", skillOptions, true));
    auto classSkills = picks(e, c, skillsStage.fields.back(), 2);
    for (const auto& id : classSkills) if (selectedEntry(rules, id, "skill") && includes(strings(*cls, "/skills"), id)) trained.insert(id);
    std::set<std::string> toolProficiencies;
    if (background) {
        if (background->at("tool") == "srd55:gaming-set") {
            skillsStage.fields.push_back(select("/gamingSet", "Gaming set proficiency", options(rules, "tool", [](const Json& item) { return item.value("category", "") == "gaming-set"; })));
            auto id = pick(e, c, skillsStage.fields.back());
            if (selectedEntry(rules,id,"tool")) toolProficiencies.insert(id);
        } else toolProficiencies.insert(background->at("tool"));
    }
    if (speciesId == "srd55:human" || speciesId == "srd55:elf") {
        auto opts = options(rules, "skill", [&](const Json& entry) {
            return speciesId == "srd55:human" || includes({"srd55:insight","srd55:perception","srd55:survival"}, entry.at("id"));
        });
        for (auto& choice : opts) if (trained.contains(choice.id)) { choice.available = false; choice.reason = "Already proficient; select a different skill."; }
        skillsStage.fields.push_back(select("/speciesSkill", "Species skill proficiency", opts));
        auto id = pick(e,c,skillsStage.fields.back());
        if (selectedEntry(rules,id,"skill")) trained.insert(id);
    }
    std::vector<std::pair<std::string, std::string>> feats; // source path, identifier
    if (background) feats.emplace_back("background", background->at("feat"));
    if (speciesId == "srd55:human") {
        auto opts = options(rules,"feat",[](const Json& entry) { return entry.at("category") == "origin"; });
        for (auto& choice : opts) if (background && choice.id != "srd55:skilled" && choice.id == background->at("feat").get<std::string>()) { choice.available=false; choice.reason="That feat is already granted by the background; Magic Initiate requires a different spell list."; }
        skillsStage.fields.push_back(select("/humanFeat", "Human Origin feat", opts));
        feats.emplace_back("human",pick(e,c,skillsStage.fields.back()));
    }
    if (fighter) {
        skillsStage.fields.push_back(select("/fightingStyle", "Fighting Style feat", options(rules,"feat",[](const Json& item){ return item.at("category") == "fighting-style"; })));
        feats.emplace_back("fightingStyle",pick(e,c,skillsStage.fields.back()));
        skillsStage.fields.push_back(select("/weaponMasteries", "Three weapon masteries", options(rules,"weapon"), true, "Mastery changes require a Long Rest; only one chosen weapon can change per Long Rest."));
        picks(e,c,skillsStage.fields.back(),3);
    }
    SheetSection featureSheet{"Features and proficiencies", {}, {}};
    if (species) for (const auto& note : strings(*species,"/notes")) featureSheet.notes.push_back(note);
    if (lineage) for (const auto& note : strings(*lineage,"/notes")) featureSheet.notes.push_back(note);
    Stage spellsStage{"spellcasting", "Spellcasting", {}};
    std::set<std::string> extraPrepared, extraCantrips;
    auto spellOptions = [&](const std::string& list, int spellLevel, int maxLevel = -1, bool evocation = false) {
        return options(rules,"spell",[&](const Json& spell){
            return includes(strings(spell,"/lists"),list) && spell.at("level").get<int>()>=spellLevel && spell.at("level").get<int>()<=(maxLevel<0 ? spellLevel : maxLevel) && (!evocation || spell.at("school")=="Evocation");
        });
    };
    for (const auto& [origin,id] : feats) {
        const auto* feat = selectedEntry(rules,id,"feat");
        if (!feat) continue;
        record(e,featureSheet,"feat."+origin,"Feat: "+feat->at("name").get<std::string>(),feat->at("name"),strings(*feat,"/notes"),{entryRef(*feat)});
        if (id=="srd55:skilled") {
            auto opts=options(rules,"skill"); auto toolOpts=options(rules,"tool"); opts.insert(opts.end(),toolOpts.begin(),toolOpts.end());
            for(auto& opt:opts) if(trained.contains(opt.id)||toolProficiencies.contains(opt.id)) {opt.available=false;opt.reason="Already proficient; choose another skill or tool.";}
            skillsStage.fields.push_back(select("/skilledChoices/"+origin,"Three Skilled proficiencies ("+origin+")",opts,true));
            for(const auto& chosen:picks(e,c,skillsStage.fields.back(),3)) {
                if(selectedEntry(rules,chosen,"skill"))trained.insert(chosen);
                else if(selectedEntry(rules,chosen,"tool"))toolProficiencies.insert(chosen);
            }
        }
        if(feat->contains("spellList")) {
            const auto list=feat->at("spellList").get<std::string>(); const auto base="/magicInitiate/"+origin;
            spellsStage.fields.push_back(select(base+"/ability","Magic Initiate ("+list+") ability",castingAbilities));
            auto ability=pick(e,c,spellsStage.fields.back());
            spellsStage.fields.push_back(select(base+"/cantrips","Magic Initiate ("+list+") cantrips",spellOptions(list,0),true));
            for(const auto& spell:picks(e,c,spellsStage.fields.back(),2)) if(selectedEntry(rules,spell,"spell"))extraCantrips.insert(spell);
            spellsStage.fields.push_back(select(base+"/spell","Magic Initiate ("+list+") level 1 spell",spellOptions(list,1)));
            auto spell=pick(e,c,spellsStage.fields.back()); if(selectedEntry(rules,spell,"spell"))extraPrepared.insert(spell);
            record(e,featureSheet,"magicInitiate."+origin+".dc","Magic Initiate ("+list+") save DC",8+proficiency+mods[ability],{"8 + proficiency + "+ability+" modifier; separate from Wizard Intelligence casting."},{ref("23"),ref("87")});
            record(e,featureSheet,"magicInitiate."+origin+".attack","Magic Initiate ("+list+") attack",proficiency+mods[ability],{"Proficiency + "+ability+" modifier."},{ref("23"),ref("87")});
        }
    }
    const std::string style = fighter ? str(c,"/fightingStyle") : "";
    const bool alert=std::any_of(feats.begin(),feats.end(),[](const auto& feat){return feat.second=="srd55:alert";});
    std::string scholar;
    if(wizard && level>=2) {
        auto opts=options(rules,"skill",[](const Json& entry){return includes({"srd55:arcana","srd55:history","srd55:investigation","srd55:medicine","srd55:nature","srd55:religion"},entry.at("id"));});
        for(auto& opt:opts) if(!trained.contains(opt.id)){opt.available=false;opt.reason="Scholar requires proficiency in this skill.";}
        skillsStage.fields.push_back(select("/scholarSkill","Scholar expertise",opts));
        scholar=pick(e,c,skillsStage.fields.back());
    }
    const Json* subclass=nullptr;
    if(level==3) {
        skillsStage.fields.push_back(select("/subclassId","Subclass",options(rules,"subclass",[&](const Json& entry){return entry.at("classId")==classId;})));
        subclass=selectedEntry(rules,pick(e,c,skillsStage.fields.back()),"subclass");
        if(subclass && subclass->at("classId")!=classId)subclass=nullptr;
        if(subclass) record(e,featureSheet,"subclass","Subclass",subclass->at("name"),strings(*subclass,"/notes"),{entryRef(*subclass)});
    }
    const bool champion=subclass && subclass->at("id")=="srd55:champion";
    SheetSection skillSheet{"Skills and saving throws",{}, {}};
    for(const auto* ability:abilities) {
        bool proficient=includes(strings(*cls,"/saves"),ability);
        record(e,skillSheet,"save."+std::string(ability),title(ability)+" save",mods[ability]+(proficient?proficiency:0),
               {title(ability)+" modifier "+signedNumber(mods[ability])+(proficient?" + proficiency "+std::to_string(proficiency):"; no saving throw proficiency")+"."},{entryRef(*cls),ref("22")});
    }
    for(const auto& [id,skill]:rules.content) if(skill.at("kind")=="skill") {
        const auto ability=skill.at("ability").get<std::string>(); int multiplier=trained.contains(id)?(scholar==id?2:1):0;
        auto key="skill."+id.substr(id.find(':')+1);
        record(e,skillSheet,key,skill.at("name"),mods[ability]+multiplier*proficiency,
               {title(ability)+" modifier "+signedNumber(mods[ability])+" + proficiency "+std::to_string(proficiency)+" × "+std::to_string(multiplier)+(multiplier==2?" (Scholar expertise).":".")},{ref("8"),ref("22"),ref("78")});
        if(champion && id=="srd55:athletics")skillSheet.notes.push_back("Athletics: advantage from Remarkable Athlete.");
    }
    std::vector<std::string> toolNames;
    for(const auto& id:toolProficiencies)if(const auto* tool=selectedEntry(rules,id,"tool"))toolNames.push_back(tool->at("name"));
    record(e,featureSheet,"tools","Tool proficiencies",joined(toolNames),{"Background and selected feats; add proficiency when using these tools. A relevant skill proficiency also gives advantage."},{ref("83"),ref("87"),ref("93")});
    if(fighter) {
        record(e,featureSheet,"secondWind.uses","Second Wind uses",2,{"Bonus action heals 1d10 + "+std::to_string(level)+" HP; regain one use on a Short Rest and all on a Long Rest."},{ref("47"),ref("48")});
        record(e,featureSheet,"actionSurge.uses","Action Surge uses",level>=2?1:0,{level>=2?"One extra action, except the Magic action; recharge on a Short or Long Rest.":"Gained at Fighter level 2."},{ref("48")});
        if(level>=2)featureSheet.notes.push_back("Tactical Mind: after a failed ability check, spend Second Wind for +1d10; no use is spent if the check still fails.");
        record(e,featureSheet,"critical.minimum","Weapon critical roll",champion?19:20,{champion?"Champion Improved Critical applies to weapons and Unarmed Strikes.":"Critical hit on a natural 20."},{ref(champion?"49":"15")});
    }
    e.stages.push_back(skillsStage);
    e.sections.push_back(skillSheet);
    e.sections.push_back(featureSheet);
    Stage advancement{"advancement","Hit points and advancement",{select("/hpMethod","Hit points after first level",{literal("fixed","Fixed value","23"),literal("rolled","Accepted Hit Die rolls","23")})}};
    const auto hpMethod=pick(e,c,advancement.fields[0],true,"fixed");
    int hitDie=cls->at("hitDie"),hp=hitDie+mods["constitution"];
    std::vector<std::string> hpSteps{"Level 1: maximum d"+std::to_string(hitDie)+" ("+std::to_string(hitDie)+") + Constitution "+signedNumber(mods["constitution"])+"."};
    for(int l=2;l<=level;++l){
        int die=cls->at("fixedHp");
        if(hpMethod=="rolled"){
            auto path="/hp/"+std::to_string(l);advancement.fields.push_back(integer(path,"Accepted HP die at level "+std::to_string(l),1,hitDie));
            numericInput(e,c,path,1,hitDie);die=num(c,path,1);
        }
        int gained=std::max(1,die+mods["constitution"]);hp+=gained;
        hpSteps.push_back("Level "+std::to_string(l)+": "+std::to_string(die)+" + Constitution "+signedNumber(mods["constitution"])+", minimum 1 = "+std::to_string(gained)+".");
    }
    if(species && species->value("hpPerLevel",0)) {hp+=species->at("hpPerLevel").get<int>()*level;hpSteps.push_back("Dwarven Toughness +"+std::to_string(level)+" (1 per level).");}
    advancement.fields.push_back(integer("/xp","XP (optional; leave unset for milestones)",0,100000000));advancement.fields.back().advanced=true;
    e.stages.push_back(advancement);

    Stage gearStage{"equipment","Starting equipment and purchases",{}};
    auto kitField=[&](const Json& owner,const std::string& path,const std::string& name){
        std::vector<Choice> opts;
        for(auto it=owner.at("kits").begin();it!=owner.at("kits").end();++it){
            std::vector<std::string> names;for(const auto& id:strings(it.value(),"/items"))if(const auto* entry=rules.find(id))names.push_back(entry->at("name"));
            opts.push_back({it.key(),it.key()+": "+joined(names)+"; "+std::to_string(it.value().at("gold").get<int>())+" GP",true,{}, {entryRef(owner)}});
        }
        return select(path,name,opts);
    };
    std::vector<std::string> inventory;
    int goldCp=0;
    for(const auto& [owner,path,label]:std::vector<std::tuple<const Json*,std::string,std::string>>{{cls,"/classEquipment","Class starting equipment"},{background,"/backgroundEquipment","Background starting equipment"}}){
        if(!owner)continue;
        gearStage.fields.push_back(kitField(*owner,path,label)); auto kit=pick(e,c,gearStage.fields.back());
        if(owner->at("kits").contains(kit)){
            const auto& kitData=owner->at("kits").at(kit);goldCp+=100*kitData.at("gold").get<int>();
            auto items=strings(kitData,"/items");inventory.insert(inventory.end(),items.begin(),items.end());
            if(path=="/backgroundEquipment" && backgroundId=="srd55:soldier" && kit=="A" && selectedEntry(rules,str(c,"/gamingSet"),"tool"))inventory.push_back(str(c,"/gamingSet"));
        }
    }
    if(wizard && !includes(inventory,"srd55:spellbook"))inventory.push_back("srd55:spellbook"); // Initial book granted by Spellcasting (p. 78).
    std::vector<Choice> buyOptions;
    for(const auto& [id,item]:rules.content)if(item.contains("costCp") && !item.value("startingOnly",false)){
        int cp=item.at("costCp");buyOptions.push_back({id,item.at("name").get<std::string>()+" ("+std::to_string(cp/100.0)+" GP)",true,{}, {entryRef(item)}});
    }
    std::sort(buyOptions.begin(),buyOptions.end(),[](const auto& a,const auto& b){return a.label<b.label;});
    gearStage.fields.push_back(select("/purchases","Additional purchases (one of each)",buyOptions,true,"Starting kits are free; selected purchases reduce remaining starting money. For multiples, add quantities below after selecting the item."));
    auto purchases=picks(e,c,gearStage.fields.back(),1000,false);
    long long spent=0;
    for(const auto& id:purchases){
        const auto* item=rules.find(id);if(!item || !item->contains("costCp") || item->value("startingOnly",false))continue;
        auto path="/purchaseQuantities/"+id;gearStage.fields.push_back(integer(path,item->at("name").get<std::string>()+" quantity",1,1000));
        numericInput(e,c,path,1,1000,false);int count=std::clamp(num(c,path,1),1,1000);
        spent+=static_cast<long long>(item->at("costCp").get<int>())*count;
        for(int i=0;i<count;++i)inventory.push_back(id);
    }
    if(spent>goldCp)issue(e,"equipment.budget","/purchases","Purchases exceed the combined starting money by "+std::to_string((spent-goldCp)/100.0)+" GP.","20");
    auto armorOptions=options(rules,"armor");armorOptions.insert(armorOptions.begin(),literal("none","No armor","22"));
    auto weaponOptions=options(rules,"weapon");weaponOptions.insert(weaponOptions.begin(),literal("none","Unarmed Strike","189"));
    for(auto* opts:{&armorOptions,&weaponOptions})for(auto& opt:*opts)if(opt.id!="none" && !includes(inventory,opt.id)){opt.available=false;opt.reason="This item is not in the selected starting equipment or purchases.";}
    gearStage.fields.push_back(select("/armorId","Worn armor",armorOptions));const auto armorId=pick(e,c,gearStage.fields.back(),true,"none");
    gearStage.fields.push_back(select("/weaponId","Primary weapon",weaponOptions));const auto weaponId=pick(e,c,gearStage.fields.back(),true,"none");
    gearStage.fields.push_back({"/shield","Wield a shield","boolean",0,1,{},false,"Requires a shield in inventory; AC benefits require shield training."});
    const bool shield=flag(c,"/shield");
    if(shield && !includes(inventory,"srd55:shield-equipment"))issue(e,"shield.inventory","/shield","Add a Shield to your inventory before equipping it.","92");
    const auto* armor=selectedEntry(rules,armorId,"armor"); const auto* weapon=selectedEntry(rules,weaponId,"weapon");
    const bool trainedArmor=armor && includes(strings(*cls,"/armorTraining"),armor->at("category"));
    const bool trainedShield=includes(strings(*cls,"/armorTraining"),"shield");
    if(armor && !trainedArmor)issue(e,"armor.untrained","/armorId","You lack this armor training: disadvantage on Strength and Dexterity D20 Tests, and you cannot cast spells.","92","warning");
    if(shield && !trainedShield)issue(e,"shield.untrained","/shield","You lack Shield training and receive no AC benefit from a Shield.","92","warning");
    if(shield && weapon && weapon->value("twoHanded",false))issue(e,"hands","/weaponId","A two-handed weapon and a shield cannot be used together. A mounted lance is the exception; mounted combat is outside this experimental module.","89");
    e.stages.push_back(gearStage);
    SheetSection combat{"Combat and equipment",{}, {}};
    record(e,combat,"hp.maximum","Maximum hit points",hp,hpSteps,{ref("22"),ref("23"),entryRef(*cls),ref("84")});
    record(e,combat,"hitDice","Hit Dice",std::to_string(level)+"d"+std::to_string(hitDie),{"One Hit Die for each class level; spent dice are current resources."},{ref("23")});
    int ac=10+mods["dexterity"];
    std::vector<std::string> acSteps{"Unarmored: 10 + Dexterity "+signedNumber(mods["dexterity"])+"."};
    if(armor){
        const auto category=armor->at("category").get<std::string>(); int dex=category=="heavy"?0:std::min(mods["dexterity"],armor->at("dexCap").get<int>());
        ac=armor->at("ac").get<int>()+dex;acSteps={armor->at("name").get<std::string>()+": base "+std::to_string(armor->at("ac").get<int>())+" + applicable Dexterity "+signedNumber(dex)+"."};
        if(style=="srd55:defense"){++ac;acSteps.push_back("Defense fighting style +1 while wearing armor.");}
        if(armor->at("stealthDisadvantage").get<bool>())combat.notes.push_back("Worn armor gives disadvantage on Stealth checks.");
    }
    if(shield && trainedShield){ac+=2;acSteps.push_back("Shield training +2.");}
    record(e,combat,"armorClass","Armor Class (ascending)",ac,acSteps,{ref("22"),ref("92"),ref("88")});
    int speed=species?species->at("speed").get<int>():30;
    if(lineage && lineage->contains("speed"))speed=lineage->at("speed");
    std::vector<std::string> speedSteps{"Species/lineage movement: "+std::to_string(speed)+" feet."};
    if(armor && totals["strength"]<armor->at("strength").get<int>()){speed-=10;speedSteps.push_back("-10 feet: Strength below the armor requirement.");}
    record(e,combat,"speed","Speed (feet)",speed,speedSteps,{ref("84"),ref("85"),ref("92")});
    int darkvision=species?species->at("darkvision").get<int>():0;
    if(lineage && lineage->contains("darkvision"))darkvision=lineage->at("darkvision");
    record(e,combat,"darkvision","Darkvision (feet)",darkvision,{"Species/lineage trait."},{ref("84"),ref("85"),ref("86")});
    record(e,combat,"initiative","Initiative",mods["dexterity"]+(alert?proficiency:0),{"Dexterity modifier "+signedNumber(mods["dexterity"])+(alert?" + Alert proficiency "+std::to_string(proficiency):"")+"."},{ref("22"),ref("87")});
    if(champion)combat.notes.push_back("Advantage on Initiative from Remarkable Athlete.");
    const auto perception=e.find("skill.perception");
    record(e,combat,"passivePerception","Passive Perception",10+(perception?perception->normal.get<int>():mods["wisdom"]),{"10 + Wisdom (Perception) modifier."},{ref("22")});
    if(weapon){
        bool trainedWeapon=includes(strings(*cls,"/weaponTraining"),weapon->at("category"));
        const bool ranged=weapon->at("ranged");
        int mod=weapon->value("finesse",false)?std::max(mods["strength"],mods["dexterity"]):mods[ranged?"dexterity":"strength"];
        int archery=ranged && style=="srd55:archery"?2:0;
        record(e,combat,"attack.weapon","Primary weapon attack",mod+(trainedWeapon?proficiency:0)+archery,{weapon->at("name").get<std::string>()+": ability "+signedNumber(mod)+", proficiency "+std::to_string(trainedWeapon?proficiency:0)+", Archery "+std::to_string(archery)+"."},{ref("22"),entryRef(*weapon),ref("87")});
        record(e,combat,"damage.weapon","Primary weapon damage",weapon->at("damage").get<std::string>()+signedNumber(mod)+" "+weapon->at("damageType").get<std::string>(),{"Weapon damage dice + attack ability modifier."},{ref("22"),entryRef(*weapon)});
        combat.notes.push_back(weapon->at("name").get<std::string>()+" properties: "+weapon->at("properties").get<std::string>()+".");
        if(fighter && includes(strings(c,"/weaponMasteries"),weaponId))combat.notes.push_back("Active weapon mastery: "+weapon->at("mastery").get<std::string>()+" (SRD pages 90–91).");
        if(weapon->value("heavy",false) && totals[ranged?"dexterity":"strength"]<13)combat.notes.push_back("Heavy weapon: disadvantage on attacks because the relevant ability score is below 13.");
        if(!trainedWeapon)combat.notes.push_back("No proficiency bonus with this weapon category.");
    }else{
        record(e,combat,"attack.weapon","Unarmed Strike attack",mods["strength"]+proficiency,{"Strength modifier + proficiency."},{ref("189")});
        record(e,combat,"damage.weapon","Unarmed Strike damage",std::max(0,1+mods["strength"]),{"1 + Strength modifier, minimum 0, Bludgeoning."},{ref("189")});
    }
    if(lineage && lineage->contains("resistance"))record(e,combat,"resistance","Ancestry resistance",lineage->at("resistance"),{"Chosen lineage or ancestry."},{entryRef(*lineage)});
    if(speciesId=="srd55:dragonborn")record(e,combat,"breath.dc","Breath Weapon save DC",8+mods["constitution"]+proficiency,{"8 + Constitution modifier + proficiency; 1d10 damage, Dexterity save for half; two uses per Long Rest at levels 1–3."},{ref("84")});
    std::map<std::string,int> quantities;for(const auto& id:inventory)quantities[id]++;
    std::vector<std::string> inventoryNames;for(const auto& [id,count]:quantities)if(const auto* item=rules.find(id))inventoryNames.push_back(std::to_string(count)+" × "+item->at("name").get<std::string>());
    record(e,combat,"inventory","Inventory",joined(inventoryNames),{"Selected class and background kits plus purchased quantities. Equipped items must be present."},{ref("20"),entryRef(*cls),ref("83")});
    record(e,combat,"gold.remaining","Remaining starting money (GP)",(goldCp-spent)/100.0,{"Combined starting money "+std::to_string(goldCp/100.0)+" GP - purchases "+std::to_string(spent/100.0)+" GP."},{ref("20"),entryRef(*cls),ref("83")});
    e.sections.push_back(combat);

    SheetSection magic{"Spellcasting",{}, {}};
    if(innateCaster){
        auto ability=str(c,"/speciesCastingAbility");
        record(e,magic,"species.spellDc","Species spell save DC",8+proficiency+mods[ability],{"8 + proficiency + "+ability+" modifier."},{ref("23"),ref("85"),ref("86")});
        record(e,magic,"species.spellAttack","Species spell attack",proficiency+mods[ability],{"Proficiency + "+ability+" modifier."},{ref("23"),ref("85"),ref("86")});
        if(speciesId=="srd55:tiefling")extraCantrips.insert("srd55:thaumaturgy");
        if(lineage){
            if(lineage->value("chooseCantrip",false)){
                spellsStage.fields.push_back(select("/lineageCantrip","High Elf cantrip",spellOptions("wizard",0)));
                auto spell=pick(e,c,spellsStage.fields.back(),true,"srd55:prestidigitation");if(selectedEntry(rules,spell,"spell"))extraCantrips.insert(spell);
            }else for(const auto& id:strings(*lineage,"/cantrips"))extraCantrips.insert(id);
            if(lineage->contains("level1Spell")){
                extraPrepared.insert(lineage->at("level1Spell"));magic.notes.push_back("Forest Gnome Speak with Animals: "+std::to_string(proficiency)+" free castings per Long Rest; spell slots may also be used.");
            }
            if(level>=3 && lineage->contains("level3Spell")){
                auto id=lineage->at("level3Spell").get<std::string>();extraPrepared.insert(id);
                if(const auto* spell=selectedEntry(rules,id,"spell"))magic.notes.push_back(spell->at("name").get<std::string>()+": one free lineage casting per Long Rest; spell slots may also be used.");
            }
        }
    }
    if(wizard){
        const int highest=level==3?2:1;
        spellsStage.fields.push_back(select("/cantrips","Three Wizard cantrips",spellOptions("wizard",0),true));
        auto cantrips=picks(e,c,spellsStage.fields.back(),3);
        std::set<std::string> book;
        for(int l=1;l<=level;++l){
            auto opts=spellOptions("wizard",1,l==3?2:1);
            for(auto& opt:opts)if(book.contains(opt.id)){opt.available=false;opt.reason="Already in the spellbook from an earlier level; select a new spell.";}
            auto path="/spellbook/"+std::to_string(l);
            spellsStage.fields.push_back(select(path,"Wizard spells learned at level "+std::to_string(l),opts,true,l==1?"Choose six level 1 spells.":"Choose two new spells of an available slot level."));
            for(const auto& id:picks(e,c,spellsStage.fields.back(),l==1?6:2))if(const auto* spell=selectedEntry(rules,id,"spell");spell && includes(strings(*spell,"/lists"),"wizard") && spell->at("level").get<int>()>=1 && spell->at("level").get<int>()<=(l==3?2:1))book.insert(id);
        }
        if(level==3 && subclass && subclass->at("id")=="srd55:evoker"){
            auto opts=spellOptions("wizard",1,2,true);for(auto& opt:opts)if(book.contains(opt.id)){opt.available=false;opt.reason="Already in your spellbook; select a new Evocation spell.";}
            spellsStage.fields.push_back(select("/evocationSavant","Two Evocation Savant spells",opts,true,"Two additional level 1–2 Evocation spells, beyond normal level advancement."));
            for(const auto& id:picks(e,c,spellsStage.fields.back(),2))if(const auto* spell=selectedEntry(rules,id,"spell");spell && includes(strings(*spell,"/lists"),"wizard") && spell->at("school")=="Evocation" && spell->at("level").get<int>()>=1 && spell->at("level").get<int>()<=2)book.insert(id);
        }
        auto preparedOpts=spellOptions("wizard",1,highest);
        for(auto& opt:preparedOpts)if(!book.contains(opt.id)){opt.available=false;opt.reason="A prepared Wizard spell must be in your spellbook.";}
        const int prepared=cls->at("prepared").at(static_cast<std::size_t>(level-1));
        spellsStage.fields.push_back(select("/preparedSpells","Prepared Wizard spells",preparedOpts,true,"Always-prepared spells from species and feats do not count toward this limit."));
        auto selectedPrepared=picks(e,c,spellsStage.fields.back(),prepared);
        std::vector<std::string> names;for(const auto& id:cantrips)if(const auto* spell=selectedEntry(rules,id,"spell"))names.push_back(spell->at("name"));
        record(e,magic,"wizard.cantrips","Wizard cantrips",joined(names),{"Three cantrips at Wizard levels 1–3; one may change after a Long Rest."},{ref("77")});
        names.clear();for(const auto& id:book)if(const auto* spell=selectedEntry(rules,id,"spell"))names.push_back(spell->at("name"));
        record(e,magic,"wizard.spellbook","Spellbook",joined(names),{"Six spells at level 1, two per additional Wizard level, plus two Evocation Savant spells at level 3."},{ref("78"),ref("82")});
        names.clear();for(const auto& id:selectedPrepared)if(const auto* spell=selectedEntry(rules,id,"spell"))names.push_back(spell->at("name"));
        record(e,magic,"wizard.prepared","Prepared Wizard spells",joined(names),{std::to_string(prepared)+" selected spellbook spells; preparations can change after a Long Rest."},{ref("77"),ref("78")});
        record(e,magic,"spellDc","Wizard spell save DC",8+mods["intelligence"]+proficiency,{"8 + Intelligence modifier "+signedNumber(mods["intelligence"])+" + proficiency "+std::to_string(proficiency)+"."},{ref("23"),ref("78")});
        record(e,magic,"spellAttack","Wizard spell attack",mods["intelligence"]+proficiency,{"Intelligence modifier + proficiency."},{ref("23"),ref("78")});
        for(int s=1;s<=2;++s)record(e,magic,"spellSlots."+std::to_string(s),"Level "+std::to_string(s)+" spell slots",cls->at("slots").at(static_cast<std::size_t>(level-1)).at(static_cast<std::size_t>(s-1)),{"Wizard Features table at level "+std::to_string(level)+"; recharge on a Long Rest."},{ref("77")});
        record(e,magic,"arcaneRecovery","Arcane Recovery slot levels",(level+1)/2,{"Half Wizard level, rounded up; restore slots after a Short Rest, once per Long Rest."},{ref("78")});
        magic.notes.push_back("Ritual Adept: ritual spells in the spellbook can be cast as rituals without preparing them; read from the book.");
        magic.notes.push_back("Spellcasting focus: Arcane Focus or spellbook. Components and spell effects are referenced in SRD pages 104–175.");
    }
    if(!extraCantrips.empty()){
        std::vector<std::string> names;for(const auto& id:extraCantrips)if(const auto* spell=selectedEntry(rules,id,"spell"))names.push_back(spell->at("name"));
        record(e,magic,"extra.cantrips","Origin cantrips",joined(names),{"Granted by species or Magic Initiate; independent of the Wizard cantrip count."},{ref("85"),ref("86"),ref("87")});
    }
    if(!extraPrepared.empty()){
        std::vector<std::string> names;for(const auto& id:extraPrepared)if(const auto* spell=selectedEntry(rules,id,"spell"))names.push_back(spell->at("name"));
        record(e,magic,"extra.prepared","Always prepared from origins",joined(names),{"Species and Magic Initiate spells do not use a Wizard preparation slot."},{ref("85"),ref("86"),ref("87")});
    }
    if(armor && !trainedArmor && !magic.calculationIds.empty())magic.notes.push_back("SPELLCASTING BLOCKED while wearing armor without training (SRD page 92).");
    if(!spellsStage.fields.empty())e.stages.push_back(spellsStage);
    if(!magic.calculationIds.empty())e.sections.push_back(magic);
    for(const auto& stage:e.stages)for(const auto& field:stage.fields)if(field.kind=="boolean"){
        if(const auto* raw=at(c,field.path);raw&&!raw->is_boolean())issue(e,"choice.type",field.path,"This option must be true or false.");
    }
    // Retain historic inputs above current level, but report unsupported future advancement explicitly.
    if(doc.advancement.is_array())for(const auto& event:doc.advancement)if(event.is_object() && num(event,"/level")>3)
        issue(e,"advancement.unsupported","/advancement","This character records advancement beyond the implemented level 3 limit; events are preserved.","23");
    return e;
}
std::vector<Message> validatePack(const ContentPack& pack) {
    std::vector<Message> result;
    const std::set<std::string> kinds{"class","background","species","subclass","lineage","skill","language","feat","spell","weapon","armor","shield","tool","gear"};
    auto fail=[&](const std::string& id,const std::string& field,const std::string& message){result.push_back({"error","srd55.content.shape",id+"/"+field,message,{ref("19")}});};
    std::map<std::string,const Json*> local;
    std::set<std::string> namespaces;
    for(const auto& entry:pack.entries)if(entry.is_object()){
        const auto id=str(entry,"/id");local[id]=&entry;namespaces.insert(id.substr(0,id.find(':')));
    }
    for(const auto& entry:pack.entries){
        if(!entry.is_object())continue;
        for(const auto& [target,expected]:references(entry)){
            if(target=="srd55:gaming-set")continue;
            auto found=local.find(target);
            if(found!=local.end()&&!matchingReference(*found->second,expected))fail(str(entry,"/id"),"reference","Reference has the wrong mechanic kind: "+target);
            else if(found==local.end()&&namespaces.contains(target.substr(0,target.find(':')))&&pack.manifest.value("dependencies",Json::array()).empty())fail(str(entry,"/id"),"reference","Missing referenced content: "+target);
        }
        const auto id=entry.value("id",""); const auto kind=entry.value("kind",""); const auto mechanicId=entry.value("replaces",id);
        if(!kinds.contains(kind)){fail(id,"kind","Unsupported SRD mechanic kind: "+kind);continue;}
        std::set<std::string> allowed{"id","kind","name","source","replaces"};
        auto check=[&](const std::string& key,char type,bool required=true){
            allowed.insert(key);if(!entry.contains(key)){if(required)fail(id,key,"Required mechanic field is missing.");return;}
            const auto& v=entry.at(key);bool okay=type=='s'?v.is_string():type=='i'?v.is_number_integer():type=='b'?v.is_boolean():type=='a'?v.is_array():v.is_object();
            if(okay && type=='i')okay=num(entry,"/"+key,-1)>=0 && num(entry,"/"+key,-1)<=1000000;
            if(!okay)fail(id,key,"Mechanic field has an invalid type or numeric range.");
        };
        auto stringsCheck=[&](const std::string& key,bool required=true){
            check(key,'a',required);if(entry.contains(key) && entry.at(key).is_array())for(const auto& v:entry.at(key))if(!v.is_string())fail(id,key,"Array must contain strings only.");
        };
        auto kitsCheck=[&](){
            check("kits",'o');if(!entry.contains("kits")||!entry.at("kits").is_object())return;
            if(entry.at("kits").empty())fail(id,"kits","At least one starting-equipment choice is required.");
            for(const auto& kit:entry.at("kits")){
                if(!kit.is_object() || !kit.contains("gold") || !kit.at("gold").is_number_integer() || num(kit,"/gold",-1)<0 || num(kit,"/gold",-1)>10000 || !kit.contains("items") || !kit.at("items").is_array()){fail(id,"kits","Each kit requires nonnegative integer gold and an items array.");continue;}
                for(const auto& item:kit.at("items"))if(!item.is_string())fail(id,"kits/items","Kit items must be identifiers.");
            }
        };
        auto enumCheck=[&](const std::string& key,const std::vector<std::string>& values){if(entry.contains(key)&&entry.at(key).is_string()&&!includes(values,entry.at(key)))fail(id,key,"Unsupported value for this mechanic.");};
        if(kind=="class"){
            for(const auto* key:{"hitDie","fixedHp","maxLevel","skillCount"})check(key,'i');
            for(const auto* key:{"saves","skills","armorTraining","weaponTraining"})stringsCheck(key);
            for(const auto* key:{"proficiency","xp","prepared"}){
                check(key,'a');if(entry.contains(key)&&entry.at(key).is_array()){
                    if(entry.at(key).size()!=3)fail(id,key,"Progression requires exactly three level entries.");
                    for(const auto& value:entry.at(key))if(!value.is_number_integer() || value.get<double>()<0 || value.get<double>()>100000)fail(id,key,"Progression values must be nonnegative integers.");
                }
            }
            check("slots",'a');if(entry.contains("slots")&&entry.at("slots").is_array()){
                if(entry.at("slots").size()!=3)fail(id,"slots","Spell progression requires exactly three levels.");
                for(const auto& row:entry.at("slots")){
                    if(!row.is_array()||row.size()!=2){fail(id,"slots","Each slot row requires levels 1 and 2.");continue;}
                    for(const auto& value:row)if(!value.is_number_integer()||value.get<double>()<0||value.get<double>()>20)fail(id,"slots","Slot values must be nonnegative integers no greater than 20.");
                }
            }
            kitsCheck();
            if(mechanicId!="srd55:fighter"&&mechanicId!="srd55:wizard")fail(id,"id","New class mechanics require a C++ module extension.");
            if(num(entry,"/maxLevel")!=3)fail(id,"maxLevel","This experimental module implements levels 1–3.");
            if(!includes({"strength","dexterity","constitution","intelligence","wisdom","charisma"},str(entry,"/saves/0")) || !includes({"strength","dexterity","constitution","intelligence","wisdom","charisma"},str(entry,"/saves/1")))fail(id,"saves","Saving throw proficiencies must use ability names.");
        }else if(kind=="background"){
            stringsCheck("abilities");stringsCheck("skills");check("feat",'s');check("tool",'s');kitsCheck();
            if(entry.contains("abilities")&&entry.at("abilities").is_array()&&entry.at("abilities").size()!=3)fail(id,"abilities","A background requires three eligible abilities.");
            for(const auto& ability:strings(entry,"/abilities"))if(!includes({"strength","dexterity","constitution","intelligence","wisdom","charisma"},ability))fail(id,"abilities","Unknown ability name.");
        }else if(kind=="species"){
            check("speed",'i');check("darkvision",'i');check("hpPerLevel",'i');stringsCheck("sizes");stringsCheck("notes");
            if(entry.contains("sizes")&&entry.at("sizes").is_array()&&entry.at("sizes").empty())fail(id,"sizes","At least one size is required.");
            if(!includes({"srd55:dragonborn","srd55:dwarf","srd55:elf","srd55:gnome","srd55:goliath","srd55:halfling","srd55:human","srd55:orc","srd55:tiefling"},mechanicId))fail(id,"id","New species traits require a C++ module extension.");
        }else if(kind=="subclass"){
            check("classId",'s');check("minimumLevel",'i');stringsCheck("notes");
            if(mechanicId!="srd55:champion"&&mechanicId!="srd55:evoker")fail(id,"id","New subclass mechanics require a C++ module extension.");
        }else if(kind=="lineage"){
            check("speciesId",'s');
            for(const auto* key:{"darkvision","speed","freeUses"})check(key,'i',false);
            for(const auto* key:{"level1Spell","level3Spell","resistance"})check(key,'s',false);
            stringsCheck("cantrips",false);stringsCheck("notes",false);check("chooseCantrip",'b',false);
        }else if(kind=="skill"){
            check("ability",'s');enumCheck("ability",{"strength","dexterity","constitution","intelligence","wisdom","charisma"});
        }else if(kind=="feat"){
            check("category",'s');stringsCheck("notes");check("spellList",'s',false);
            enumCheck("category",{"origin","fighting-style"});enumCheck("spellList",{"cleric","druid","wizard"});
            if(!entry.contains("spellList")&&!includes({"srd55:alert","srd55:savage-attacker","srd55:skilled","srd55:archery","srd55:defense","srd55:great-weapon-fighting","srd55:two-weapon-fighting"},mechanicId))fail(id,"id","New feat effects require a C++ module extension; Magic Initiate spell-list definitions are supported.");
        }else if(kind=="spell"){
            check("level",'i');check("school",'s');stringsCheck("lists");for(const auto* key:{"ritual","concentration","materialCost"})check(key,'b');
            if(num(entry,"/level",-1)<0||num(entry,"/level")>2)fail(id,"level","Only spell levels 0–2 are supported in this pack.");
            enumCheck("school",{"Abjuration","Conjuration","Divination","Enchantment","Evocation","Illusion","Necromancy","Transmutation"});
        }else if(kind=="weapon"){
            for(const auto* key:{"category","damage","damageType","properties","mastery"})check(key,'s');
            for(const auto* key:{"ranged","finesse","twoHanded","heavy"})check(key,'b');check("costCp",'i');enumCheck("category",{"simple","martial"});
        }else if(kind=="armor"){
            check("category",'s');for(const auto* key:{"ac","dexCap","strength","costCp"})check(key,'i');check("stealthDisadvantage",'b');enumCheck("category",{"light","medium","heavy"});
        }else if(kind=="shield"){
            check("costCp",'i');check("ac",'i');
        }else if(kind=="tool"){
            check("ability",'s');check("costCp",'i');check("category",'s',false);enumCheck("ability",{"strength","dexterity","constitution","intelligence","wisdom","charisma"});
        }else if(kind=="gear"){
            check("costCp",'i');check("startingOnly",'b',false);
        }
        for(auto it=entry.begin();it!=entry.end();++it)if(!allowed.contains(it.key()))fail(id,it.key(),"Unknown mechanic field; the pack requires a supported schema or module extension.");
    }
    return result;
}
} // namespace
EditionModule srd55Module() { return {"srd55","5.5E / SRD 5.2.1 (experimental)","1.0.0",true,run,validatePack,validateResolved}; }
} // namespace dnd
