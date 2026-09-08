#include "dnd/companions.hpp"
#include "dnd/lifecycle.hpp"
#include "dnd/srd55_inventory.hpp"
#include "srd55_v2_internal.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <sstream>

namespace dnd::srd55v2 {
SourceRef ref(const std::string &page) {
    return {"System Reference Document 5.2.1", page,
            "https://media.dndbeyond.com/compendium-images/srd/5.2/SRD_CC_v5.2.1.pdf#page=" + page};
}
const Json *at(const Json &value, const std::string &pointer) {
    try {
        const Json::json_pointer p(pointer);
        return value.contains(p) ? &value.at(p) : nullptr;
    } catch (...) {
        return nullptr;
    }
}
int number(const Json &value, const std::string &pointer, int fallback) {
    const auto *found = at(value, pointer);
    if (!found || !found->is_number_integer())
        return fallback;
    try {
        const auto n = found->get<long long>();
        return n >= -1000000000 && n <= 1000000000 ? static_cast<int>(n) : fallback;
    } catch (...) {
        return fallback;
    }
}
std::string text(const Json &value, const std::string &pointer, const std::string &fallback) {
    const auto *found = at(value, pointer);
    return found && found->is_string() ? found->get<std::string>() : fallback;
}
std::vector<std::string> strings(const Json &value, const std::string &pointer) {
    std::vector<std::string> result;
    const auto *found = at(value, pointer);
    if (found && found->is_array())
        for (const auto &item : *found)
            if (item.is_string())
                result.push_back(item.get<std::string>());
    return result;
}
void issue(Evaluation &result, const std::string &code, const std::string &path,
           const std::string &message, const std::string &page, const std::string &severity) {
    result.messages.push_back({severity, "srd55.v2." + code, path, message, {ref(page)}});
}
Field integer(const std::string &path, const std::string &label, int minimum, int maximum,
              const std::string &help) {
    return {path, label, "integer", minimum, maximum, {}, false, help};
}
Field select(const std::string &path, const std::string &label, std::vector<Choice> opts,
             bool multiple, const std::string &help) {
    return {path, label, multiple ? "multiselect" : "select", 0, 0, std::move(opts), false, help};
}
std::vector<Choice> options(const ResolvedRuleset &rules, const std::string &kind) {
    std::vector<Choice> result;
    for (const auto &[id, entry] : rules.content)
        if (entry.value("kind", "") == kind)
            result.push_back(
                {id, entry.at("name"), true, {}, {sourceFromJson(entry.at("source"))}});
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.label < b.label; });
    return result;
}
std::string pick(Evaluation &result, const Json &choices, const Field &field, bool required) {
    const auto selected = text(choices, field.path);
    if (selected.empty()) {
        if (required)
            issue(result, "choice.missing", field.path, "Choose " + field.label + ".");
        return {};
    }
    const auto it = std::find_if(field.options.begin(), field.options.end(),
                                 [&](const auto &o) { return o.id == selected; });
    if (it == field.options.end())
        issue(result, "choice.unavailable", field.path,
              "Saved selection '" + selected + "' is unavailable and is preserved.");
    else if (!it->available)
        issue(result, "choice.ineligible", field.path, it->reason);
    return selected;
}
std::vector<std::string> picks(Evaluation &result, const Json &choices, const Field &field,
                               int count, bool exact) {
    auto resultIds = strings(choices, field.path);
    const auto *raw = at(choices, field.path);
    if (raw && (!raw->is_array() || !std::all_of(raw->begin(), raw->end(),
                                                 [](const Json &j) { return j.is_string(); })))
        issue(result, "choice.type", field.path, "Selections must be a list of identifiers.");
    if ((exact && static_cast<int>(resultIds.size()) != count) ||
        (!exact && static_cast<int>(resultIds.size()) > count))
        issue(result, "choice.count", field.path,
              "Choose " + std::string(exact ? "exactly " : "at most ") + std::to_string(count) +
                  " for " + field.label + ".");
    std::set<std::string> seen;
    for (const auto &id : resultIds) {
        if (!seen.insert(id).second)
            issue(result, "choice.duplicate", field.path, "Select each option once: " + id + ".");
        const auto found = std::find_if(field.options.begin(), field.options.end(),
                                        [&](const auto &o) { return o.id == id; });
        if (found == field.options.end())
            issue(result, "choice.unavailable", field.path,
                  "Saved selection '" + id + "' is unavailable and is preserved.");
        else if (!found->available)
            issue(result, "choice.ineligible", field.path, found->reason);
    }
    return resultIds;
}
void merge(Evaluation &target, const Evaluation &source) {
    target.stages.insert(target.stages.end(), source.stages.begin(), source.stages.end());
    target.calculations.insert(target.calculations.end(), source.calculations.begin(),
                               source.calculations.end());
    target.sections.insert(target.sections.end(), source.sections.begin(), source.sections.end());
    target.messages.insert(target.messages.end(), source.messages.begin(), source.messages.end());
    target.rollRequests.insert(target.rollRequests.end(), source.rollRequests.begin(),
                               source.rollRequests.end());
    target.resources.insert(target.resources.end(), source.resources.begin(),
                            source.resources.end());
    target.actions.insert(target.actions.end(), source.actions.begin(), source.actions.end());
    for (auto it = source.moduleData.begin(); it != source.moduleData.end(); ++it)
        target.moduleData[it.key()] = it.value();
}
namespace {
const std::array<std::string, 6> abilities = {"strength",     "dexterity", "constitution",
                                              "intelligence", "wisdom",    "charisma"};
const std::array<int, 20> xp = {0,      300,    900,    2700,   6500,   14000,  23000,
                                34000,  48000,  64000,  85000,  100000, 120000, 140000,
                                165000, 195000, 225000, 265000, 305000, 355000};
std::string slug(const std::string &id) {
    const auto colon = id.find(':');
    return colon == std::string::npos ? id : id.substr(colon + 1);
}
std::string title(std::string value) {
    if (!value.empty())
        value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    return value;
}
bool contains(const std::vector<std::string> &a, const std::string &b) {
    return std::find(a.begin(), a.end(), b) != a.end();
}
bool flag(const Json &c, const std::string &path) {
    const auto *v = at(c, path);
    return v && v->is_boolean() && v->get<bool>();
}
const Json *entry(const ResolvedRuleset &r, const std::string &id, const std::string &kind) {
    const auto *found = r.find(id);
    return found && found->value("kind", "") == kind ? found : nullptr;
}
std::string names(const ResolvedRuleset &rules, const std::vector<std::string> &ids) {
    std::string out;
    for (const auto &id : ids) {
        if (!out.empty())
            out += ", ";
        const auto *e = rules.find(id);
        out += e ? e->value("name", id) : id;
    }
    return out.empty() ? "None" : out;
}
std::string names(const ResolvedRuleset &rules, const std::set<std::string> &ids) {
    return names(rules, std::vector<std::string>(ids.begin(), ids.end()));
}
void numeric(Evaluation &e, const Json &c, const std::string &path, int low, int high,
             bool required = true) {
    const auto *value = at(c, path);
    if (!value) {
        if (required)
            issue(e, "number.missing", path,
                  "Enter a whole number from " + std::to_string(low) + " to " +
                      std::to_string(high) + ".");
        return;
    }
    if (!value->is_number_integer() || number(c, path, low - 1) < low ||
        number(c, path, high + 1) > high)
        issue(e, "number.range", path,
              "Enter a whole number from " + std::to_string(low) + " to " + std::to_string(high) +
                  ".");
}
void record(Evaluation &e, SheetSection &s, const std::string &id, const std::string &label,
            Json value, std::vector<std::string> steps, const std::string &page) {
    addCalculation(e, id, label, std::move(value), std::move(steps), {ref(page)});
    s.calculationIds.push_back(id);
}
std::vector<Choice> literal(const std::vector<std::string> &ids, const std::string &page = "19") {
    std::vector<Choice> out;
    for (const auto &id : ids)
        out.push_back({id, title(id), true, {}, {ref(page)}});
    return out;
}
void addSet(std::set<std::string> &target, const std::vector<std::string> &values) {
    target.insert(values.begin(), values.end());
}
void addSet(std::set<std::string> &target, const std::set<std::string> &values) {
    target.insert(values.begin(), values.end());
}
std::vector<Choice> filter(std::vector<Choice> opts,
                           const std::function<bool(const Choice &)> &predicate) {
    opts.erase(
        std::remove_if(opts.begin(), opts.end(), [&](const auto &o) { return !predicate(o); }),
        opts.end());
    return opts;
}
std::string defaultedPick(Evaluation &e, const Json &choices, const Field &field,
                          const std::string &fallback) {
    return at(choices, field.path) ? pick(e, choices, field) : fallback;
}
struct LevelEvent {
    int characterLevel;
    std::string classId;
    int classLevel;
};
struct FeatSelection {
    std::string id;
    std::string path;
    int level;
    std::string owner;
};
Evaluation run(const CharacterDocument &d, const ResolvedRuleset &rules) {
    Evaluation e;
    const auto &c = d.choices;
    Context ctx{d, rules};
    const auto historyMessages = validateSrd55History(d, rules);
    e.messages.insert(e.messages.end(), historyMessages.begin(), historyMessages.end());
    ctx.choiceAcquisitionLevels = srd55HistoryAcquisitionLevels(d);
    issue(e, "coverage.experimental", "/edition",
          "Expanded SRD 5.2.1 module; feature coverage and source verification are recorded per "
          "publication.",
          "19", "info");
    Stage identity{"identity",
                   "Class and origins",
                   {select("/classId", "Initial class", options(rules, "class")),
                    integer("/level", "Character level", 1, 20),
                    {"/multiclass",
                     "Use ordered multiclass advancement",
                     "boolean",
                     0,
                     1,
                     {},
                     true,
                     "Each character level records which class gained that level."},
                    select("/speciesId", "Species", options(rules, "species")),
                    select("/backgroundId", "Background", options(rules, "background"))}};
    ctx.initialClass = pick(e, c, identity.fields[0]);
    numeric(e, c, "/level", 1, 20, false);
    ctx.totalLevel = std::clamp(number(c, "/level", 1), 1, 20);
    ctx.proficiency = 2 + (ctx.totalLevel - 1) / 4;
    const auto speciesId = pick(e, c, identity.fields[3]),
               backgroundId = pick(e, c, identity.fields[4]);
    const auto *initial = entry(rules, ctx.initialClass, "class");
    const auto *species = entry(rules, speciesId, "species");
    const auto *background = entry(rules, backgroundId, "background");
    identity.fields.push_back(
        select("/alignment", "Alignment",
               literal({"Lawful Good", "Neutral Good", "Chaotic Good", "Lawful Neutral", "Neutral",
                        "Chaotic Neutral", "Lawful Evil", "Neutral Evil", "Chaotic Evil"})));
    const auto alignment = pick(e, c, identity.fields.back());
    identity.fields.push_back(select("/languages", "Two additional Standard languages",
                                     filter(options(rules, "language"),
                                            [&](const auto &o) {
                                                return rules.find(o.id)->value(
                                                           "category", "standard") == "standard";
                                            }),
                                     true));
    auto languages = picks(e, c, identity.fields.back(), 2);
    if (species && species->at("sizes").size() > 1) {
        identity.fields.push_back(select("/size", "Size", literal(strings(*species, "/sizes"))));
        pick(e, c, identity.fields.back());
    }
    const auto lineageOptions = filter(options(rules, "lineage"), [&](const auto &o) {
        return rules.find(o.id)->value("speciesId", "") == speciesId;
    });
    const Json *lineage = nullptr;
    if (!lineageOptions.empty()) {
        identity.fields.push_back(select("/lineageId", "Ancestry or lineage", lineageOptions));
        lineage = entry(rules, pick(e, c, identity.fields.back()), "lineage");
    }
    if (speciesId == "srd55:elf" || speciesId == "srd55:gnome" || speciesId == "srd55:tiefling") {
        identity.fields.push_back(select("/speciesCastingAbility", "Species spellcasting ability",
                                         literal({"intelligence", "wisdom", "charisma"}, "85")));
        pick(e, c, identity.fields.back());
    }
    e.stages.push_back(identity);
    Stage scoreStage{"abilities",
                     "Ability scores",
                     {select("/abilityMethod", "Ability generation",
                             literal({"standard-array", "point-buy", "rolled"}, "21"))}};
    const auto method = defaultedPick(e, c, scoreStage.fields[0], "standard-array");
    std::vector<int> bases, boosts;
    int points = 0;
    for (const auto &ability : abilities) {
        const auto path = "/abilities/" + ability;
        scoreStage.fields.push_back(integer(path, title(ability) + " before background",
                                            method == "point-buy" ? 8 : 3,
                                            method == "point-buy" ? 15 : 18));
        numeric(e, c, path, method == "point-buy" ? 8 : 3, method == "point-buy" ? 15 : 18);
        const int base = std::clamp(number(c, path, 10), 3, 18);
        bases.push_back(base);
        const auto boostPath = "/backgroundBoosts/" + ability;
        const auto eligible = background && contains(strings(*background, "/abilities"), ability);
        if (eligible)
            scoreStage.fields.push_back(
                integer(boostPath, title(ability) + " background increase", 0, 2));
        numeric(e, c, boostPath, 0, 2, false);
        int boost = std::clamp(number(c, boostPath), 0, 2);
        boosts.push_back(boost);
        if (boost && !eligible)
            issue(e, "background.ability", boostPath,
                  "The selected background cannot increase " + ability + ".", "83");
        ctx.scores[ability] = base + boost;
        ctx.modifiers[ability] = static_cast<int>(std::floor((base + boost - 10) / 2.0));
        if (base >= 8 && base <= 15)
            points +=
                std::array<int, 8>{0, 1, 2, 3, 4, 5, 7, 9}[static_cast<std::size_t>(base - 8)];
        if (method == "rolled")
            e.rollRequests.push_back(
                {"ability." + ability, title(ability), path, "abilities", 6, 4, 1});
    }
    std::sort(bases.begin(), bases.end());
    std::sort(boosts.begin(), boosts.end());
    if (method == "standard-array" && bases != std::vector<int>{8, 10, 12, 13, 14, 15})
        issue(e, "ability.array", "/abilities", "Assign 15, 14, 13, 12, 10, and 8 exactly once.",
              "21");
    if (method == "point-buy" && points != 27)
        issue(e, "ability.points", "/abilities",
              "Point-buy must spend 27 points; current cost " + std::to_string(points) + ".", "21");
    if (background && boosts != std::vector<int>{0, 0, 0, 0, 1, 2} &&
        boosts != std::vector<int>{0, 0, 0, 1, 1, 1})
        issue(e, "background.boosts", "/backgroundBoosts",
              "Choose +2/+1 across two permitted abilities or +1 to each of three.", "83");
    e.stages.push_back(scoreStage);
    if (!initial)
        return e;
    const auto creationScores = ctx.scores;
    std::vector<LevelEvent> events;
    Stage history{"levels", "Class levels and advancement", {}};
    for (int level = 1; level <= ctx.totalLevel; ++level) {
        std::string classId = ctx.initialClass;
        if (flag(c, "/multiclass") && level > 1) {
            const auto path = "/advancement/" + std::to_string(level) + "/classId";
            history.fields.push_back(
                select(path, "Class gained at character level " + std::to_string(level),
                       options(rules, "class")));
            classId = pick(e, c, history.fields.back());
        }
        if (!entry(rules, classId, "class"))
            continue;
        const int classLevel = ++ctx.classLevels[classId];
        events.push_back({level, classId, classLevel});
        ctx.classLevelEvents[classId].push_back(level);
    }
    for (const auto &[id, level] : ctx.classLevels)
        if (level >= 3) {
            const auto opts = filter(options(rules, "subclass"), [&](const auto &o) {
                return rules.find(o.id)->value("classId", "") == id;
            });
            history.fields.push_back(
                select("/subclasses/" + slug(id), title(slug(id)) + " subclass", opts));
            pick(e, c, history.fields.back());
        }
    if (!history.fields.empty())
        e.stages.push_back(history);
    for (const auto &[id, level] : ctx.classLevels) {
        (void)level;
        const auto ability = text(*rules.find(id), "/casting/ability");
        if (ability == "intelligence" || ability == "wisdom" || ability == "charisma")
            ctx.castingAbilities.insert(ability);
    }
    const auto innateAbility = text(c, "/speciesCastingAbility");
    if ((speciesId == "srd55:elf" || speciesId == "srd55:gnome" || speciesId == "srd55:tiefling") &&
        (innateAbility == "intelligence" || innateAbility == "wisdom" ||
         innateAbility == "charisma"))
        ctx.castingAbilities.insert(innateAbility);
    auto featCasting = [&](const std::string &id, const std::string &path) {
        const auto *feat = entry(rules, id, "feat");
        const auto ability = text(c, path + "/ability");
        if (feat && feat->contains("spellList") &&
            (ability == "intelligence" || ability == "wisdom" || ability == "charisma"))
            ctx.castingAbilities.insert(ability);
    };
    if (background)
        featCasting(background->value("feat", ""), "/magicInitiate/background");
    if (speciesId == "srd55:human")
        featCasting(text(c, "/humanFeat"), "/magicInitiate/human");
    for (const auto &event : events) {
        const auto &grants = rules.find(event.classId)->at("featLevels");
        if (std::find(grants.begin(), grants.end(), Json(event.classLevel)) != grants.end())
            featCasting(text(c, "/feats/" + std::to_string(event.characterLevel) + "/id"),
                        "/feats/" + std::to_string(event.characterLevel));
    }
    if (ctx.classLevels.contains("srd55:warlock")) {
        const int count = number(*rules.find("srd55:warlock"),
                                 "/progression/invocations/" +
                                     std::to_string(ctx.classLevels.at("srd55:warlock") - 1));
        for (int i = 0; i < count; ++i)
            if (text(c, "/features/warlock/invocations/" + std::to_string(i)) ==
                "srd55:lessons-of-the-first-ones")
                featCasting(text(c, "/features/warlock/invocationTargets/" + std::to_string(i)),
                            "/features/warlock/invocationGrants/" + std::to_string(i));
    }
    addSet(ctx.armorTraining, strings(*initial, "/armorTraining"));
    for (const auto &[id, level] : ctx.classLevels)
        if (id != ctx.initialClass) {
            (void)level;
            addSet(ctx.armorTraining, strings(*rules.find(id), "/multiclassTraining/armor"));
        }
    const auto itemState = resolveInventory(ctx);
    merge(e, itemState.evaluation);
    ctx.proficiency += itemState.proficiencyBonus;
    addSet(ctx.skillProficiencies,
           background ? strings(*background, "/skills") : std::vector<std::string>{});
    for (const auto &skill : ctx.skillProficiencies)
        ctx.skillAcquisitionLevels[skill] = 1;
    addSet(ctx.armorTraining, strings(*initial, "/armorTraining"));
    addSet(ctx.weaponTraining, strings(*initial, "/weaponTraining"));
    addSet(ctx.weaponTraining, itemState.weaponTraining);
    std::set<std::string> saves;
    addSet(saves, strings(*initial, "/saves"));
    Stage training{"training", "Skills and training", {}};
    auto chooseSkills = [&](const std::string &path, const std::string &label,
                            std::vector<Choice> opts, int count, int acquiredAt = 1) {
        for (auto &option : opts)
            if (ctx.skillProficiencies.contains(option.id)) {
                option.available = false;
                option.reason = "Already proficient; choose another skill.";
            }
        training.fields.push_back(select(path, label, opts, true));
        const auto selected = picks(e, c, training.fields.back(), count);
        addSet(ctx.skillProficiencies, selected);
        for (const auto &skill : selected)
            if (!ctx.skillAcquisitionLevels.contains(skill))
                ctx.skillAcquisitionLevels[skill] = acquiredAt;
    };
    chooseSkills(
        "/classSkills", "Initial class skills",
        filter(options(rules, "skill"),
               [&](const auto &o) { return contains(strings(*initial, "/skills"), o.id); }),
        initial->value("skillCount", 2));
    for (const auto &[id, level] : ctx.classLevels)
        if (id != ctx.initialClass) {
            (void)level;
            const auto &cls = *rules.find(id);
            addSet(ctx.armorTraining, strings(cls, "/multiclassTraining/armor"));
            addSet(ctx.weaponTraining, strings(cls, "/multiclassTraining/weapons"));
            const int count = cls.value("multiclassSkillCount", 0);
            if (count)
                chooseSkills(
                    "/multiclassSkills/" + slug(id), title(slug(id)) + " multiclass skill",
                    filter(options(rules, "skill"),
                           [&](const auto &o) { return contains(strings(cls, "/skills"), o.id); }),
                    count, ctx.classLevelEvents.at(id).front());
        }
    if (speciesId == "srd55:elf" || speciesId == "srd55:human") {
        auto opts = options(rules, "skill");
        if (speciesId == "srd55:elf")
            opts = filter(opts, [](const auto &o) {
                return o.id == "srd55:insight" || o.id == "srd55:perception" ||
                       o.id == "srd55:survival";
            });
        for (auto &o : opts)
            if (ctx.skillProficiencies.contains(o.id)) {
                o.available = false;
                o.reason = "Already proficient; choose another skill.";
            }
        training.fields.push_back(select("/speciesSkill", "Species skill", opts));
        const auto selected = pick(e, c, training.fields.back());
        if (!selected.empty()) {
            ctx.skillProficiencies.insert(selected);
            ctx.skillAcquisitionLevels[selected] = 1;
        }
    }
    std::set<std::string> tools;
    std::map<std::string, int> toolLevels;
    addSet(tools, strings(*initial, "/toolProficiencies"));
    for (const auto &tool : tools)
        toolLevels[tool] = 1;
    for (const auto &[id, level] : ctx.classLevels)
        if (id != ctx.initialClass) {
            (void)level;
            for (const auto &tool : strings(*rules.find(id), "/multiclassTraining/tools")) {
                tools.insert(tool);
                if (!toolLevels.contains(tool))
                    toolLevels[tool] = ctx.classLevelEvents.at(id).front();
            }
        }
    if (background) {
        const auto tool = background->value("tool", "");
        if (tool == "srd55:gaming-set") {
            training.fields.push_back(select(
                "/gamingSet", "Gaming set", filter(options(rules, "tool"), [&](const auto &o) {
                    return rules.find(o.id)->value("category", "") == "gaming-set";
                })));
            const auto selected = pick(e, c, training.fields.back());
            tools.insert(selected);
            toolLevels[selected] = 1;
        } else if (!tool.empty()) {
            tools.insert(tool);
            toolLevels[tool] = 1;
        }
    }
    auto toolChoices = [&](const Json &owner, const std::string &key, const std::string &base,
                           int acquiredAt = 1) {
        int index = 0;
        for (const auto &grant : owner.value(key, Json::array())) {
            const auto category = grant.value("category", "");
            const auto categories = strings(grant, "/categories");
            const int count = grant.value("count", 1);
            const auto path =
                base + "/" + (category.empty() ? "choice-" + std::to_string(index) : category);
            ++index;
            auto opts = filter(options(rules, "tool"), [&](const auto &o) {
                const auto found = rules.find(o.id)->value("category", "");
                return !categories.empty() ? contains(categories, found)
                                           : category.empty() || found == category;
            });
            for (auto &o : opts)
                if (tools.contains(o.id)) {
                    o.available = false;
                    o.reason = "Already proficient with this tool.";
                }
            training.fields.push_back(select(path, title(category) + " proficiencies", opts, true));
            const auto selected = picks(e, c, training.fields.back(), count);
            addSet(tools, selected);
            for (const auto &tool : selected)
                if (!toolLevels.contains(tool))
                    toolLevels[tool] = acquiredAt;
        }
    };
    toolChoices(*initial, "initialToolChoices", "/tools/" + slug(ctx.initialClass));
    for (const auto &[id, level] : ctx.classLevels)
        if (id != ctx.initialClass) {
            (void)level;
            toolChoices(*rules.find(id), "multiclassToolChoices", "/tools/" + slug(id),
                        ctx.classLevelEvents.at(id).front());
        }
    e.stages.push_back(training);
    std::vector<FeatSelection> feats;
    if (background && !background->value("feat", "").empty())
        feats.push_back({background->at("feat"), "/magicInitiate/background", 1, "background"});
    Stage featStage{"feats", "Feats and ability advancement", {}};
    if (speciesId == "srd55:human") {
        auto opts = filter(options(rules, "feat"), [&](const auto &o) {
            return rules.find(o.id)->value("category", "") == "origin";
        });
        for (auto &o : opts)
            if (background && background->value("feat", "") == o.id &&
                !rules.find(o.id)->value("repeatable", false)) {
                o.available = false;
                o.reason = "This nonrepeatable feat is already granted by your background.";
            }
        for (auto &o : opts)
            if (background && background->value("feat", "") == o.id &&
                rules.find(o.id)->value("repeatGroup", "") == "magic-initiate") {
                o.available = false;
                o.reason = "Repeated Magic Initiate must use a different spell list.";
            }
        featStage.fields.push_back(select("/humanFeat", "Human Origin feat", opts));
        feats.push_back({pick(e, c, featStage.fields.back()), "/magicInitiate/human", 1, "human"});
    }
    std::vector<std::map<std::string, int>> scoreHistory(
        static_cast<std::size_t>(ctx.totalLevel + 1), creationScores);
    std::set<std::string> ownedFeats;
    for (const auto &f : feats)
        ownedFeats.insert(f.id);
    auto abilityIncrease = [&](const Json &feat, const std::string &path) {
        if (!feat.contains("abilityIncrease"))
            return;
        const auto &inc = feat.at("abilityIncrease");
        const int count = inc.value("points", 0), perAbility = inc.value("maxPerAbility", 1),
                  cap = inc.value("maxScore", 20);
        int spent = 0;
        for (const auto &ability : abilities) {
            const auto fieldPath = path + "/boosts/" + ability;
            const bool allowed = contains(strings(inc, "/abilities"), ability);
            if (allowed)
                featStage.fields.push_back(
                    integer(fieldPath, feat.value("name", "") + ": " + title(ability) + " increase",
                            0, perAbility));
            numeric(e, c, fieldPath, 0, perAbility, false);
            const int delta = std::clamp(number(c, fieldPath), 0, perAbility);
            spent += delta;
            if (delta && !allowed)
                issue(e, "feat.ability", fieldPath, "This feat cannot increase " + ability + ".",
                      "87");
            if (delta > 0 && ctx.scores[ability] + delta > cap)
                issue(e, "feat.cap", fieldPath,
                      "This increase cannot raise the ability above " + std::to_string(cap) + ".",
                      "87");
            ctx.scores[ability] += delta;
        }
        if (spent != count)
            issue(e, "feat.points", path,
                  "Assign exactly " + std::to_string(count) +
                      " ability-score increase points for " + feat.value("name", "") + ".",
                  "87");
    };
    std::set<std::string> appliedPermanent;
    auto applyPermanent = [&](int atLevel) {
        for (const auto &effect : itemState.permanentEffects) {
            if (!effect.is_object() || effect.value("acquiredCharacterLevel", 0) != atLevel)
                continue;
            const auto id = effect.value("eventId", "");
            const auto ability = effect.value("ability", "");
            if (id.empty() || !appliedPermanent.insert(id).second ||
                !ctx.scores.contains(ability) || effect.value("op", "") != "ability-increase")
                continue;
            ctx.scores[ability] = std::min(effect.value("maxScore", 30),
                                           ctx.scores[ability] + effect.value("value", 0));
        }
    };
    for (const auto &event : events) {
        const auto &cls = *rules.find(event.classId);
        const auto levels = cls.value("featLevels", Json::array());
        if (std::find(levels.begin(), levels.end(), Json(event.classLevel)) != levels.end()) {
            const auto path = "/feats/" + std::to_string(event.characterLevel);
            auto opts = options(rules, "feat");
            for (auto &option : opts) {
                const auto &feat = *rules.find(option.id);
                const auto requirements = feat.value("prerequisites", Json::object());
                if (event.characterLevel < requirements.value("minLevel", 0)) {
                    option.available = false;
                    option.reason = "Requires character level " +
                                    std::to_string(requirements.value("minLevel", 0)) + ".";
                }
                if (ownedFeats.contains(option.id) && !feat.value("repeatable", false)) {
                    option.available = false;
                    option.reason = "This feat is already owned and is not repeatable.";
                }
                if (ownedFeats.contains(option.id) &&
                    feat.value("repeatGroup", "") == "magic-initiate") {
                    option.available = false;
                    option.reason = "Repeated Magic Initiate must use a different spell list.";
                }
                if (requirements.contains("abilityAny")) {
                    bool met = false;
                    for (auto it = requirements["abilityAny"].begin();
                         it != requirements["abilityAny"].end(); ++it)
                        met = met || ctx.scores[it.key()] >= it.value().get<int>();
                    if (!met) {
                        option.available = false;
                        option.reason =
                            "The feat's ability prerequisite is not met before its increase.";
                    }
                }
                if (requirements.value("requiresFeature", "") == "spellcasting") {
                    bool casts = false;
                    for (const auto &prior : events)
                        if (prior.characterLevel <= event.characterLevel) {
                            const auto kind = text(*rules.find(prior.classId), "/casting/kind");
                            casts = casts || kind == "full" || kind == "half";
                        }
                    if (!casts) {
                        option.available = false;
                        option.reason = "Requires the Spellcasting feature.";
                    }
                }
                if (feat.value("category", "") == "fighting-style") {
                    bool style = false;
                    for (const auto &prior : events)
                        if (prior.characterLevel <= event.characterLevel)
                            style = style || prior.classId == "srd55:fighter" ||
                                    ((prior.classId == "srd55:paladin" ||
                                      prior.classId == "srd55:ranger") &&
                                     prior.classLevel >= 2);
                    if (!style) {
                        option.available = false;
                        option.reason = "Requires a Fighting Style feature.";
                    }
                }
            }
            featStage.fields.push_back(
                select(path + "/id",
                       "Feat at character level " + std::to_string(event.characterLevel), opts));
            const auto selected = pick(e, c, featStage.fields.back());
            if (const auto *feat = entry(rules, selected, "feat")) {
                abilityIncrease(*feat, path);
                feats.push_back({selected, path, event.characterLevel, event.classId});
                ownedFeats.insert(selected);
            }
        }
        const auto abilityGrant = advancementAbilityGrant(event.classId, event.classLevel);
        for (const auto &[ability, bonus] : abilityGrant.abilityBonuses)
            ctx.scores[ability] = std::min(abilityGrant.abilityCaps.contains(ability)
                                               ? abilityGrant.abilityCaps.at(ability)
                                               : 20,
                                           ctx.scores[ability] + bonus);
        applyPermanent(event.characterLevel);
        scoreHistory[static_cast<std::size_t>(event.characterLevel)] = ctx.scores;
    }
    if (ctx.classLevels.size() > 1) {
        std::set<std::string> acquired;
        for (const auto &event : events) {
            if (acquired.contains(event.classId)) {
                continue;
            }
            if (!acquired.empty()) {
                auto check = [&](const std::string &id) {
                    const auto &cls = *rules.find(id);
                    const auto requirements = cls.value("primaryAbilities", Json::object());
                    bool any = false, all = true;
                    if (requirements.is_object())
                        for (auto it = requirements.begin(); it != requirements.end(); ++it) {
                            const bool met =
                                scoreHistory[static_cast<std::size_t>(event.characterLevel - 1)].at(
                                    it.key()) >= it.value().get<int>();
                            any = any || met;
                            all = all && met;
                        }
                    else if (requirements.is_array())
                        for (const auto &ability : requirements) {
                            const bool met =
                                scoreHistory[static_cast<std::size_t>(event.characterLevel - 1)].at(
                                    ability.get<std::string>()) >= 13;
                            any = any || met;
                            all = all && met;
                        }
                    if (!(cls.value("primaryAbilityMode", "all") == "any" ? any : all))
                        issue(e, "multiclass.prerequisite",
                              "/advancement/" + std::to_string(event.characterLevel) + "/classId",
                              "Primary-ability requirements for " + cls.value("name", id) +
                                  " must be met before entering a new class.",
                              "25");
                };
                for (const auto &id : acquired)
                    check(id);
                check(event.classId);
            }
            acquired.insert(event.classId);
        }
    }
    for (const auto &ability : abilities)
        ctx.modifiers[ability] = static_cast<int>(std::floor((ctx.scores[ability] - 10) / 2.0));
    const auto armorId =
        itemState.initialized ? itemState.armorProfile : text(c, "/armorId", "none");
    const auto *armor = entry(rules, armorId, "armor");
    const auto *weapon =
        entry(rules, itemState.initialized ? itemState.weaponProfile : text(c, "/weaponId", "none"),
              "weapon");
    ctx.armored = armor != nullptr;
    ctx.armorCategory = armor ? armor->value("category", "") : "";
    ctx.shield = itemState.initialized ? itemState.shield : flag(c, "/shield");
    for (const auto &[ability, bonus] : itemState.abilityBonuses)
        if (ctx.scores.contains(ability)) {
            const int cap = itemState.abilityBonusCaps.contains(ability)
                                ? itemState.abilityBonusCaps.at(ability)
                                : 20;
            if (bonus > 0 && ctx.scores[ability] < cap)
                ctx.scores[ability] = std::min(cap, ctx.scores[ability] + bonus);
        }
    for (const auto &[ability, minimum] : itemState.abilityMinimums)
        if (ctx.scores.contains(ability))
            ctx.scores[ability] = std::max(ctx.scores[ability], minimum);
    for (const auto &ability : abilities)
        ctx.modifiers[ability] = static_cast<int>(std::floor((ctx.scores[ability] - 10) / 2.0));
    const auto skillTimesBeforeFeats = ctx.skillAcquisitionLevels;
    const auto toolTimesBeforeFeats = toolLevels;
    // Level-one and advancement feat proficiencies can be targets of later Expertise.
    for (const auto &f : feats)
        if (f.id == "srd55:skilled") {
            const auto path = f.owner == "human" ? "/skilledChoices" : f.path + "/skilledChoices";
            for (const auto &skill : strings(c, path))
                if (entry(rules, skill, "skill")) {
                    ctx.skillProficiencies.insert(skill);
                    if (!ctx.skillAcquisitionLevels.contains(skill))
                        ctx.skillAcquisitionLevels[skill] = f.level;
                }
        }
    ctx.feats = ownedFeats;
    ctx.toolProficiencies = tools;
    ctx.toolAcquisitionLevels = toolLevels;
    for (const auto &[id, level] : ctx.classLevels) {
        addSet(ctx.preparedSpells, strings(c, "/spellcasting/" + slug(id) + "/preparedSpells"));
        const auto *sub = entry(rules, text(c, "/subclasses/" + slug(id)), "subclass");
        if (sub)
            for (const auto &grant : sub->value("spellGrants", Json::array()))
                if (grant.value("level", 99) <= level)
                    addSet(ctx.preparedSpells, strings(grant, "/spells"));
    }
    for (const auto &f : feats)
        if (const auto *feat = entry(rules, f.id, "feat"); feat && feat->contains("spellList")) {
            const auto prepared = text(c, f.path + "/spell");
            if (!prepared.empty())
                ctx.preparedSpells.insert(prepared);
        }
    if (lineage) {
        for (const int level : {1, 3, 5})
            if (ctx.totalLevel >= level) {
                const auto key = "level" + std::to_string(level) + "Spell";
                if (lineage->contains(key))
                    ctx.preparedSpells.insert(lineage->at(key).get<std::string>());
            }
    }
    const auto grants = resolveClassChoices(ctx);
    merge(e, grants.evaluation);
    // Unconditional class ability increases were applied at their actual level event above.
    addSet(ctx.skillProficiencies, grants.skillProficiencies);
    addSet(ctx.expertise, grants.expertise);
    addSet(ctx.armorTraining, grants.armorTraining);
    addSet(ctx.weaponTraining, grants.weaponTraining);
    addSet(saves, grants.saveProficiencies);
    addSet(tools, grants.toolProficiencies);
    for (const auto &[skill, level] : grants.skillAcquisitionLevels)
        if (!ctx.skillAcquisitionLevels.contains(skill) ||
            level < ctx.skillAcquisitionLevels.at(skill))
            ctx.skillAcquisitionLevels[skill] = level;
    for (const auto &id : grants.feats)
        if (!ownedFeats.contains(id)) {
            feats.push_back({id, "/featureFeats/" + slug(id), ctx.totalLevel, "class feature"});
            ownedFeats.insert(id);
        }
    for (const auto &grant : grants.featGrants) {
        const auto id = grant.value("id", "");
        const auto path = grant.value("path", "");
        if (id.empty() || path.empty())
            continue;
        std::string owner = path;
        std::replace(owner.begin(), owner.end(), '/', '.');
        feats.push_back({id, path, grant.value("level", ctx.totalLevel), owner});
        ownedFeats.insert(id);
    }
    for (const auto &id : grants.languages)
        if (!contains(languages, id))
            languages.push_back(id);
    for (const auto &id : itemState.languages)
        if (!contains(languages, id))
            languages.push_back(id);
    const auto normalScores = ctx.scores;
    const int normalCon = static_cast<int>(std::floor((ctx.scores["constitution"] - 10) / 2.0));
    if (!grants.transformedForm.empty()) {
        for (const auto &[ability, value] : grants.physicalAbilityOverrides)
            ctx.scores[ability] = value;
        if (text(d.resources, "/formEquipment", "merged") != "worn") {
            ctx.armored = false;
            ctx.shield = false;
            ctx.armorCategory.clear();
            armor = nullptr;
            weapon = nullptr;
        }
    }
    for (const auto &ability : abilities)
        ctx.modifiers[ability] = static_cast<int>(std::floor((ctx.scores[ability] - 10) / 2.0));
    const auto features = evaluateClassFeatures(ctx);
    merge(e, features.evaluation);
    auto mode = [&](const std::string &id) -> const Json * {
        for (const auto &item : features.attackModes)
            if (item.value("id", "") == id)
                return &item;
        return nullptr;
    };
    addSet(ctx.expertise, features.expertise);
    addSet(ctx.skillProficiencies, features.skillProficiencies);
    addSet(saves, features.saveProficiencies);
    addSet(ctx.armorTraining, features.armorTraining);
    addSet(ctx.weaponTraining, features.weaponTraining);
    SheetSection featSheet{"Feats", {}, {}}, magic{"Spellcasting", {}, {}},
        combat{"Combat", {}, {}}, abilitySheet{"Abilities", {}, {}},
        summary{"Identity and progression", {}, {}}, gearSheet{"Equipment and money", {}, {}},
        skillSheet{"Skills, saves and proficiencies", {}, {}};
    Json extraSpells = Json::array();
    int featInitiative = 0, featAc = 0, featRanged = 0;
    auto spellOptions = [&](const std::string &list, int low, int high) {
        return filter(options(rules, "spell"), [&](const auto &o) {
            const auto &spell = *rules.find(o.id);
            return contains(strings(spell, "/lists"), list) && spell.value("level", -1) >= low &&
                   spell.value("level", 99) <= high;
        });
    };
    auto skilledSeen = skillTimesBeforeFeats;
    auto skilledToolsSeen = toolTimesBeforeFeats;
    std::stable_sort(feats.begin(), feats.end(),
                     [](const auto &a, const auto &b) { return a.level < b.level; });
    for (const auto &selection : feats) {
        const auto *feat = entry(rules, selection.id, "feat");
        if (!feat)
            continue;
        const auto page = feat->at("source").value("page", "87");
        featSheet.notes.push_back(
            feat->value("name", selection.id) + ": " +
            feat->value("description", names(rules, strings(*feat, "/notes"))));
        if (selection.id == "srd55:alert")
            featInitiative = ctx.proficiency;
        if (selection.id == "srd55:defense" && ctx.armored)
            featAc = 1;
        if (selection.id == "srd55:archery")
            featRanged = 2;
        if (selection.id == "srd55:skilled") {
            auto opts = options(rules, "skill");
            const auto toolOpts = options(rules, "tool");
            opts.insert(opts.end(), toolOpts.begin(), toolOpts.end());
            const auto path =
                selection.owner == "human" ? "/skilledChoices" : selection.path + "/skilledChoices";
            for (auto &o : opts)
                if ((skilledSeen.contains(o.id) && skilledSeen.at(o.id) <= selection.level) ||
                    (skilledToolsSeen.contains(o.id) &&
                     skilledToolsSeen.at(o.id) <= selection.level)) {
                    o.available = false;
                    o.reason = "Already proficient; choose another skill or tool.";
                }
            const auto field = select(path, "Skilled: three new proficiencies", opts, true);
            bool exposed = false;
            for (const auto &stage : e.stages)
                for (const auto &existing : stage.fields)
                    exposed = exposed || existing.path == path;
            if (!exposed)
                featStage.fields.push_back(field);
            for (const auto &id : picks(e, c, field, 3)) {
                if (entry(rules, id, "skill")) {
                    ctx.skillProficiencies.insert(id);
                    skilledSeen[id] = selection.level;
                } else if (entry(rules, id, "tool")) {
                    tools.insert(id);
                    skilledToolsSeen[id] = selection.level;
                }
            }
        }
        if (feat->contains("spellList")) {
            const auto list = feat->at("spellList").get<std::string>();
            const auto path = selection.path;
            const auto freeResource =
                "magicInitiate." + selection.owner + "." + std::to_string(selection.level);
            featStage.fields.push_back(
                select(path + "/ability", "Magic Initiate casting ability",
                       literal({"intelligence", "wisdom", "charisma"}, "87")));
            const auto ability = pick(e, c, featStage.fields.back());
            featStage.fields.push_back(select(path + "/cantrips", "Magic Initiate: two cantrips",
                                              spellOptions(list, 0, 0), true));
            for (const auto &id : picks(e, c, featStage.fields.back(), 2))
                extraSpells.push_back({{"spellId", id},
                                       {"ability", ability},
                                       {"reason", feat->value("name", "")},
                                       {"source", feat->at("source")},
                                       {"freeUses", -1}});
            featStage.fields.push_back(
                select(path + "/spell", "Magic Initiate: level 1 spell", spellOptions(list, 1, 1)));
            const auto selected = pick(e, c, featStage.fields.back());
            if (!selected.empty())
                extraSpells.push_back({{"spellId", selected},
                                       {"ability", ability},
                                       {"reason", feat->value("name", "")},
                                       {"source", feat->at("source")},
                                       {"freeUses", 1},
                                       {"resourceId", freeResource}});
            e.resources.push_back({freeResource,
                                   feat->value("name", "") + " free casting",
                                   1,
                                   "Long Rest",
                                   {sourceFromJson(feat->at("source"))}});
        }
        if (selection.id == "srd55:boon-of-combat-prowess")
            e.resources.push_back(
                {"boon.combatProwess", "Peerless Aim", 1, "Start of your turn", {ref("88")}});
        if (selection.id == "srd55:boon-of-fate")
            e.resources.push_back({"boon.fate",
                                   "Improve Fate",
                                   1,
                                   "Initiative, Short Rest, or Long Rest",
                                   {ref("88")}});
        if (selection.id == "srd55:boon-of-truesight")
            record(e, featSheet, "senses.truesight", "Truesight (feet)", 60, {"Boon of Truesight."},
                   "88");
        if (selection.id == "srd55:boon-of-irresistible-offense") {
            std::string increased;
            for (const auto &ability : abilities)
                if (number(c, selection.path + "/boosts/" + ability) > 0)
                    increased = ability;
            if (!increased.empty())
                record(e, featSheet, "boon.overwhelmingStrike", "Overwhelming Strike extra damage",
                       ctx.scores[increased],
                       {"On an attack roll's natural 20; equal to the " + increased +
                        " score increased by this feat. Same damage type as the attack."},
                       "88");
        }
    }
    if (!featStage.fields.empty())
        e.stages.push_back(featStage);
    if (!featSheet.notes.empty() || !featSheet.calculationIds.empty())
        e.sections.push_back(featSheet);
    for (const auto &grant : grants.spellGrants)
        extraSpells.push_back(grant);
    for (const auto &grant : features.spellGrants)
        extraSpells.push_back(grant);
    for (const auto &grant : itemState.spellGrants)
        extraSpells.push_back(grant);
    for (const auto &ability : abilities) {
        record(e, abilitySheet, "ability." + ability, title(ability), ctx.scores[ability],
               {"Accepted starting score, background and ordered ability increases.",
                "Normal untransformed score " + std::to_string(normalScores.at(ability)) +
                    "; effective form score " + std::to_string(ctx.scores[ability]) + "."},
               "21");
        record(e, abilitySheet, "modifier." + ability, title(ability) + " modifier",
               ctx.modifiers[ability], {"floor((score - 10) / 2)."}, "21");
    }
    record(e, summary, "level", "Character level", ctx.totalLevel,
           {"Total number of ordered class-level gains; class limits are separate."}, "23");
    Json split = Json::object();
    for (const auto &[id, level] : ctx.classLevels)
        split[rules.find(id)->value("name", id)] = level;
    record(e, summary, "class", "Class levels", split,
           {"Initial class " + initial->value("name", ctx.initialClass) +
            " determines initial training, saving throws, starting equipment and first-level "
            "maximum Hit Die."},
           "25");
    record(e, summary, "proficiency", "Proficiency bonus", ctx.proficiency,
           {"2 + floor((total character level - 1) / 4), plus equipped-item modifier " +
            std::to_string(itemState.proficiencyBonus) + "."},
           "23");
    record(e, summary, "alignment", "Alignment", alignment, {"Chosen alignment."}, "21");
    record(e, summary, "xp.minimum", "Minimum XP for this level",
           xp[static_cast<std::size_t>(ctx.totalLevel - 1)],
           {"Character advancement table; milestone campaigns may leave current XP unset."}, "23");
    if (const auto *current = at(c, "/xp"); current && !current->is_null()) {
        numeric(e, c, "/xp", 0, 1000000000);
        const int amount = number(c, "/xp");
        if (amount < xp[static_cast<std::size_t>(ctx.totalLevel - 1)])
            issue(e, "xp.level", "/xp", "The selected character level exceeds the recorded XP.",
                  "23");
        record(e, summary, "xp", "Experience points", amount,
               {"Recorded XP; changing abilities never rewrites prior awards."}, "23");
    }
    Stage hpStage{
        "hp",
        "Hit points and accepted advancement",
        {select("/hpMethod", "HP after first character level", literal({"fixed", "rolled"}, "23")),
         integer("/xp", "Experience points (optional)", 0, 1000000000)}};
    const auto hpMethod = defaultedPick(e, c, hpStage.fields[0], "fixed");
    int maximumHp = 0;
    Json hpRows = Json::array(), hitDice = Json::object();
    for (const auto &event : events) {
        const auto &cls = *rules.find(event.classId);
        const int die = cls.at("hitDie");
        int accepted = event.characterLevel == 1 ? die : cls.at("fixedHp").get<int>();
        const auto path = "/hp/" + std::to_string(event.characterLevel);
        if (event.characterLevel > 1 && hpMethod == "rolled") {
            hpStage.fields.push_back(integer(
                path,
                "Accepted d" + std::to_string(die) + " at character level " +
                    std::to_string(event.characterLevel) + " (" + cls.value("name", "") + ")",
                1, die));
            numeric(e, c, path, 1, die);
            accepted = std::clamp(number(c, path, cls.at("fixedHp").get<int>()), 1, die);
            e.rollRequests.push_back(
                {"hp." + std::to_string(event.characterLevel),
                 "Hit points at character level " + std::to_string(event.characterLevel), path,
                 "hitPoints", die, 1, 0});
        }
        const int gain = std::max(1, accepted + normalCon);
        maximumHp += gain;
        const auto dieName = "d" + std::to_string(die);
        hitDice[dieName] = hitDice.value(dieName, 0) + 1;
        hpRows.push_back({{"level", event.characterLevel},
                          {"class", cls.value("name", "")},
                          {"dieResult", accepted},
                          {"constitution", normalCon},
                          {"hpGain", gain}});
    }
    const int speciesHp = species ? species->value("hpPerLevel", 0) * ctx.totalLevel : 0;
    maximumHp +=
        speciesHp + grants.hpBonus + features.hpBonus + itemState.hpBonusPerLevel * ctx.totalLevel;
    const int unreducedHp = maximumHp;
    const int hpReduction = number(d.resources, "/hpMaximumReduction");
    if (hpReduction < 0 || hpReduction > maximumHp)
        issue(e, "hp.reduction", "/resources/hpMaximumReduction",
              "HP maximum reduction must lie between zero and the unreduced maximum.", "185");
    maximumHp = std::max(0, maximumHp - hpReduction);
    record(e, combat, "hp.maximum", "Maximum hit points", maximumHp,
           {"Sum of accepted/max/fixed class Hit Dice plus Constitution at every level, minimum 1 "
            "per level.",
            "Untransformed Constitution modifier " + std::to_string(normalCon) +
                " applies retroactively; species bonus " + std::to_string(speciesHp) +
                "; class-feature bonus " + std::to_string(grants.hpBonus + features.hpBonus) + "."},
           "23");
    record(e, combat, "hitDice", "Hit Dice by class die", hitDice,
           {"Initial maximum die does not repeat when entering a new class."}, "25");
    record(e, summary, "advancement.hp", "HP advancement", hpRows,
           {"Accepted dice are saved inputs. Refreshing or reopening never rolls dice."}, "23");
    e.resources.push_back({"hp",
                           "Current hit points",
                           maximumHp,
                           "Healing and rests; entered explicitly",
                           {ref("23")}});
    for (auto it = hitDice.begin(); it != hitDice.end(); ++it)
        e.resources.push_back({"hitDice." + it.key(),
                               "Unspent Hit Dice " + it.key(),
                               it.value().get<int>(),
                               "Long Rest restores spent Hit Dice",
                               {ref("25")}});
    e.stages.push_back(hpStage);
    Stage gearStage{"equipment", "Equipment and ownership", {}};
    std::vector<std::string> inventory;
    long long goldCp = 0, spent = 0;
    for (const auto &owner : std::vector<std::tuple<const Json *, std::string, std::string>>{
             {initial, "/classEquipment", "Initial class equipment"},
             {background, "/backgroundEquipment", "Background equipment"}}) {
        const auto *item = std::get<0>(owner);
        if (!item)
            continue;
        const auto path = std::get<1>(owner);
        std::vector<Choice> opts;
        for (auto it = item->at("kits").begin(); it != item->at("kits").end(); ++it)
            opts.push_back({it.key(),
                            it.key() + ": " + names(rules, strings(it.value(), "/items")) + "; " +
                                std::to_string(it.value().value("gold", 0)) + " GP",
                            true,
                            {},
                            {sourceFromJson(item->at("source"))}});
        gearStage.fields.push_back(select(path, std::get<2>(owner), opts));
        const auto selected = pick(e, c, gearStage.fields.back());
        if (!item->at("kits").contains(selected))
            continue;
        const auto &kit = item->at("kits").at(selected);
        goldCp += 100LL * kit.value("gold", 0);
        const auto items = strings(kit, "/items");
        inventory.insert(inventory.end(), items.begin(), items.end());
        for (const auto &choice : kit.value("choices", Json::array())) {
            const auto category = choice.value("category", "");
            const auto categories = strings(choice, "/categories");
            const auto key = path + "Choices/" + choice.value("id", category);
            auto choices = filter(options(rules, choice.value("kind", "tool")), [&](const auto &o) {
                const auto found = rules.find(o.id)->value("category", "");
                return !categories.empty() ? contains(categories, found)
                                           : category.empty() || found == category;
            });
            if (choice.value("matchesProficiency", false))
                for (auto &option : choices)
                    if (!tools.contains(option.id)) {
                        option.available = false;
                        option.reason =
                            "This starting item must match one of your tool proficiencies.";
                    }
            gearStage.fields.push_back(select(key, "Equipment: " + category, choices, true));
            auto chosen = picks(e, c, gearStage.fields.back(), choice.value("count", 1));
            inventory.insert(inventory.end(), chosen.begin(), chosen.end());
        }
        if (path == "/backgroundEquipment" && backgroundId == "srd55:soldier" && selected == "A" &&
            entry(rules, text(c, "/gamingSet"), "tool"))
            inventory.push_back(text(c, "/gamingSet"));
    }
    if (ctx.classLevels.contains("srd55:wizard") && !contains(inventory, "srd55:spellbook"))
        inventory.push_back("srd55:spellbook");
    std::vector<Choice> buy;
    for (const auto &[id, item] : rules.content)
        if (item.contains("costCp") && !item.value("startingOnly", false) &&
            item.value("kind", "") != "magic-item")
            buy.push_back(
                {id, item.value("name", id), true, {}, {sourceFromJson(item.at("source"))}});
    gearStage.fields.push_back(select("/purchases", "Purchases", buy, true));
    for (const auto &id : picks(e, c, gearStage.fields.back(), 1000, false)) {
        const auto *item = rules.find(id);
        if (!item || !item->contains("costCp"))
            continue;
        const auto path = "/purchaseQuantities/" + id;
        gearStage.fields.push_back(integer(path, item->value("name", id) + " quantity", 1, 1000));
        numeric(e, c, path, 1, 1000, false);
        const int count = std::clamp(number(c, path, 1), 1, 1000);
        spent += static_cast<long long>(item->at("costCp").get<int>()) * count;
        for (int i = 0; i < count; ++i)
            inventory.push_back(id);
    }
    std::map<std::string, int> physical;
    for (const auto &id : inventory)
        ++physical[id];
    Json seed = Json::array();
    for (const auto &[id, quantity] : physical)
        seed.push_back({{"itemId", id}, {"quantity", quantity}});
    e.moduleData["inventory.creationSeed"] = seed;
    auto profiles = inventory;
    for (const auto &id : profiles)
        if (const auto *item = rules.find(id); item && item->contains("weaponProfile")) {
            const auto profile = item->at("weaponProfile").get<std::string>();
            if (entry(rules, profile, "weapon") && !contains(inventory, profile))
                inventory.push_back(profile);
        }
    if (const auto *pact = mode("warlock:pact-of-the-blade")) {
        const auto summoned = pact->value("weaponId", "");
        if (entry(rules, summoned, "weapon") && !contains(inventory, summoned))
            inventory.push_back(summoned);
    }
    if (itemState.initialized)
        inventory = itemState.ownedProfiles;
    if (spent > goldCp)
        issue(e, "equipment.budget", "/purchases",
              "Purchases exceed starting money; keep adventuring acquisitions separate from the "
              "creation budget.",
              "20");
    auto armorOpts = options(rules, "armor"), weaponOpts = options(rules, "weapon");
    armorOpts.insert(armorOpts.begin(), {"none", "No armor", true, {}, {ref("22")}});
    weaponOpts.insert(weaponOpts.begin(), {"none", "Unarmed Strike", true, {}, {ref("189")}});
    for (auto *opts : {&armorOpts, &weaponOpts})
        for (auto &o : *opts)
            if (o.id != "none" && !contains(inventory, o.id)) {
                o.available = false;
                o.reason = "This item is not in your owned equipment.";
            }
    gearStage.fields.push_back(select("/armorId", "Worn armor", armorOpts));
    if (!itemState.initialized)
        defaultedPick(e, c, gearStage.fields.back(), "none");
    gearStage.fields.push_back(select("/weaponId", "Primary weapon", weaponOpts));
    if (!itemState.initialized)
        defaultedPick(e, c, gearStage.fields.back(), "none");
    gearStage.fields.push_back({"/shield",
                                "Wield a Shield",
                                "boolean",
                                0,
                                1,
                                {},
                                false,
                                "Requires owned Shield and shield training for its AC benefit."});
    if (ctx.shield && !contains(inventory, "srd55:shield-equipment"))
        issue(e, "shield.inventory", "/shield", "A Shield must be owned before it is equipped.",
              "92");
    const bool trainedArmor = armor && ctx.armorTraining.contains(armor->value("category", "")),
               trainedShield = ctx.armorTraining.contains("shield");
    if (armor && !trainedArmor)
        issue(e, "armor.untrained", "/armorId",
              "Untrained armor imposes disadvantage on Strength/Dexterity D20 Tests and prevents "
              "spellcasting.",
              "92", "warning");
    if (ctx.shield && !trainedShield)
        issue(e, "shield.untrained", "/shield",
              "A Shield grants no AC bonus without shield training.", "92", "warning");
    if (weapon && ctx.shield && weapon->value("twoHanded", false))
        issue(e, "hands", "/weaponId", "A two-handed weapon and Shield cannot be wielded together.",
              "90");
    std::map<std::string, int> formulas = {{"Standard unarmored", 10 + ctx.modifiers["dexterity"]}};
    if (armor) {
        int ac = armor->at("ac");
        const int dexCap = armor->value("dexCap", 99);
        ac += dexCap == 0 ? 0 : std::min(ctx.modifiers["dexterity"], dexCap);
        formulas = {{armor->value("name", "Armor"), ac}};
    }
    for (const auto &[label, ac] : grants.armorFormulas)
        formulas[label] = ac;
    for (const auto &[label, ac] : features.armorFormulas)
        formulas[label] = ac;
    for (const auto &formula : itemState.armorFormulas) {
        int ac = formula.value("base", 0);
        for (const auto &ability : strings(formula, "/abilities"))
            ac += ctx.modifiers[ability];
        formulas[formula.value("label", "Magic item armor")] = ac;
    }
    if (!grants.transformedForm.empty() && grants.transformedForm.contains("ac"))
        formulas["Wild Shape natural armor"] = grants.transformedForm.at("ac").get<int>();
    int best = 0;
    std::string bestName;
    for (const auto &[label, ac] : formulas)
        if (ac > best) {
            best = ac;
            bestName = label;
        }
    std::vector<Choice> formulaOpts;
    for (const auto &[label, ac] : formulas)
        formulaOpts.push_back({label, label + " = " + std::to_string(ac), true, {}, {ref("25")}});
    if (formulas.size() > 1) {
        auto field = select(itemState.initialized ? "/inventory/armorFormula" : "/armorFormula",
                            "Armor Class formula", formulaOpts);
        if (itemState.initialized)
            field.scope = "resources";
        gearStage.fields.push_back(field);
        const auto selected = defaultedPick(e, itemState.initialized ? d.resources : c,
                                            gearStage.fields.back(), bestName);
        if (formulas.contains(selected)) {
            best = formulas.at(selected);
            bestName = selected;
        }
    }
    const int shieldBonus = ctx.shield && trainedShield ? 2 : 0;
    record(e, combat, "armorClass", "Armor Class",
           best + shieldBonus + featAc + itemState.armorBonus,
           {bestName + " = " + std::to_string(best) + "; Shield " + std::to_string(shieldBonus) +
            "; Defense " + std::to_string(featAc) + "; item bonuses " +
            std::to_string(itemState.armorBonus) + ". Alternative formulas do not stack."},
           "22");
    int speed = species ? species->value("speed", 30) : 30;
    if (lineage && lineage->contains("speed"))
        speed = lineage->at("speed");
    if (!grants.transformedForm.empty())
        speed = number(grants.transformedForm, "/speed/walk", 0);
    speed += grants.speedBonus + features.speedBonus + itemState.speedBonus;
    if (itemState.speedMinimums.contains("walk"))
        speed = std::max(speed, itemState.speedMinimums.at("walk"));
    if (armor && !itemState.ignoreArmorSpeedPenalty &&
        ctx.scores["strength"] < armor->value("strength", 0))
        speed -= 10;
    record(e, combat, "speed", "Speed (feet)", std::max(0, speed),
           {"Species speed plus eligible class features; heavy armor Strength penalty applies "
            "where required."},
           "22");
    record(e, combat, "initiative", "Initiative bonus",
           ctx.modifiers["dexterity"] + featInitiative + grants.initiativeBonus +
               features.initiativeBonus + itemState.initiativeBonus,
           {"Dexterity modifier plus eligible feats/class/item bonuses."}, "22");
    int attackCount = std::max(grants.attackCount, features.attackCount);
    int attackMod = ctx.modifiers["strength"], damageMod = attackMod;
    std::string damage = "1", weaponName = "Unarmed Strike", attackAbility = "strength";
    bool weaponProficient = true;
    if (weapon) {
        weaponName = weapon->value("name", "");
        const bool ranged = weapon->value("ranged", false);
        const auto properties = weapon->value("properties", "");
        attackMod = ranged ? ctx.modifiers["dexterity"] : ctx.modifiers["strength"];
        attackAbility = ranged ? "dexterity" : "strength";
        if (weapon->value("finesse", false)) {
            attackAbility =
                ctx.modifiers["strength"] >= ctx.modifiers["dexterity"] ? "strength" : "dexterity";
            attackMod = ctx.modifiers[attackAbility];
        }
        damageMod = attackMod;
        weaponProficient = ctx.weaponTraining.contains(weapon->value("category", "")) ||
                           ctx.weaponTraining.contains(weapon->value("id", ""));
        damage = weapon->value("damage", "1");
        if (ranged)
            attackMod += featRanged;
        if (weapon->value("heavy", false) && ctx.scores[ranged ? "dexterity" : "strength"] < 13)
            combat.notes.push_back("Heavy weapon: attack rolls have Disadvantage because the "
                                   "required ability is below 13.");
        if (ownedFeats.contains("srd55:great-weapon-fighting") && !ranged &&
            (weapon->value("twoHanded", false) ||
             properties.find("Versatile") != std::string::npos))
            combat.notes.push_back("Great Weapon Fighting: when attacking with two hands, damage "
                                   "dice of 1 or 2 count as 3.");
        if (ownedFeats.contains("srd55:two-weapon-fighting"))
            combat.notes.push_back("Two-Weapon Fighting adds the ability modifier to the "
                                   "Light-property extra attack's damage.");
    }
    if (const auto *martial = mode("monk:martialArts");
        martial && martial->value("enabled", false)) {
        const bool eligible =
            !weapon || (!weapon->value("ranged", false) &&
                        (weapon->value("category", "") == "simple" ||
                         weapon->value("properties", "").find("Light") != std::string::npos));
        if (eligible) {
            attackAbility =
                ctx.modifiers["strength"] >= ctx.modifiers["dexterity"] ? "strength" : "dexterity";
            attackMod = ctx.modifiers[attackAbility];
            damageMod = attackMod;
            weaponProficient = true;
            const int die = martial->value("damageDie", 6);
            int existing = 0;
            const auto pos = damage.find('d');
            if (pos != std::string::npos)
                try {
                    existing =
                        std::stoi(damage.substr(0, pos)) * (std::stoi(damage.substr(pos + 1)) + 1);
                } catch (...) {
                }
            if (die + 1 > existing)
                damage = "1d" + std::to_string(die);
        }
    }
    if (const auto *pact = mode("warlock:pact-of-the-blade");
        pact && weapon && pact->value("weaponId", "") == weapon->value("id", "")) {
        attackAbility = "charisma";
        attackMod = ctx.modifiers["charisma"] + (weapon->value("ranged", false) ? featRanged : 0);
        damageMod = ctx.modifiers["charisma"];
        weaponProficient = true;
        attackCount = std::max(attackCount, pact->value("attackCount", 1));
    }
    if (const auto *sacred = mode("paladin:sacredWeapon");
        sacred && sacred->value("active", false) && weapon && !weapon->value("ranged", false))
        attackMod += sacred->value("attackBonus", 0);
    if (const auto *rage = mode("barbarian:rage");
        rage && rage->value("active", false) && attackAbility == "strength")
        damageMod += rage->value("damageBonus", 0);
    if (mode("paladin:radiantStrikes") && (!weapon || !weapon->value("ranged", false)))
        damage += " + 1d8 Radiant";
    if (weapon) {
        attackMod += itemState.weaponAttackBonus;
        damageMod += itemState.weaponDamageBonus;
    }
    record(e, combat, "attacks", "Attacks per Attack action", attackCount,
           {"Use the greatest eligible Extra Attack value for the current weapon; class grants "
            "never add together."},
           "25");
    record(e, combat, "attack.weapon", weaponName + " attack bonus",
           attackMod + (weaponProficient ? ctx.proficiency : 0),
           {"Eligible attack ability modifier plus proficiency if trained; eligible ranged "
            "Fighting Style bonus."},
           "89");
    record(e, combat, "damage.weapon", weaponName + " damage",
           damage + (damageMod >= 0 ? " + " : " - ") + std::to_string(std::abs(damageMod)),
           {"Damage dice and ability modifier; conditional feature damage is shown separately."},
           "89");
    if (!itemState.initialized)
        record(e, gearSheet, "inventory", "Owned equipment", names(rules, inventory),
               {"Initial class/background kits plus recorded purchases; later class entries never "
                "grant new starting kits."},
               "20");
    record(e, gearSheet, "money.startingCp", "Starting money (cp)", goldCp,
           {"Combined initial class and background kit money."}, "20");
    record(e, gearSheet, "money.spentCp", "Creation purchases (cp)", spent,
           {"Catalog cost multiplied by explicit quantity."}, "89");
    record(e, gearSheet, "money.remainingCp", "Money after purchases (cp)", goldCp - spent,
           {"Creation budget only; current adventuring funds remain a separate resource."}, "20");
    if (itemState.initialized)
        for (auto &field : gearStage.fields)
            if (field.scope == "choices") {
                field.editable = false;
                field.readOnlyReason = "Starting equipment is recorded. Use inventory actions for "
                                       "current equipment and transactions.";
                if (field.path == "/armorId")
                    field.label = "Initial armor selection";
                if (field.path == "/weaponId")
                    field.label = "Initial weapon selection";
            }
    e.stages.push_back(gearStage);
    for (const auto &[id, item] : rules.content)
        if (item.value("kind", "") == "skill") {
            std::string ability = item.at("ability");
            if (grants.skillAbilityOverrides.contains(id))
                ability = grants.skillAbilityOverrides.at(id);
            if (features.skillAbilityOverrides.contains(id))
                ability = features.skillAbilityOverrides.at(id);
            const bool proficient = ctx.skillProficiencies.contains(id),
                       expert = ctx.expertise.contains(id);
            if (expert && !proficient)
                issue(e, "expertise.prerequisite", "/features",
                      "Expertise requires proficiency in " + item.value("name", id) + ".", "25");
            const int prof = proficient
                                 ? (expert ? ctx.proficiency * 2 : ctx.proficiency)
                                 : std::max(grants.halfProficiency, features.halfProficiency);
            const int bonus =
                (grants.skillBonuses.contains(id) ? grants.skillBonuses.at(id) : 0) +
                (features.skillBonuses.contains(id) ? features.skillBonuses.at(id) : 0) +
                (itemState.skillBonuses.contains(id) ? itemState.skillBonuses.at(id) : 0);
            int total = ctx.modifiers[ability] + prof + bonus;
            if (!grants.transformedForm.empty())
                total =
                    std::max(total, number(grants.transformedForm, "/skills/" + slug(id), total));
            record(e, skillSheet, "skill." + slug(id), item.value("name", id), total,
                   {title(ability) + " modifier; " +
                    (expert       ? "Expertise doubles proficiency once."
                     : proficient ? "Proficiency applies once."
                                  : "Untrained; only applicable half-proficiency applies.") +
                    " Wild Shape retains the better applicable skill total."},
                   "8");
        }
    for (const auto &ability : abilities) {
        const int bonus =
            (grants.saveBonuses.contains(ability) ? grants.saveBonuses.at(ability) : 0) +
            (features.saveBonuses.contains(ability) ? features.saveBonuses.at(ability) : 0) +
            (itemState.saveBonuses.contains(ability) ? itemState.saveBonuses.at(ability) : 0);
        int total =
            ctx.modifiers[ability] + (saves.contains(ability) ? ctx.proficiency : 0) + bonus;
        if (!grants.transformedForm.empty())
            total = std::max(total, number(grants.transformedForm, "/saves/" + ability, total));
        record(e, skillSheet, "save." + ability, title(ability) + " saving throw", total,
               {"Current ability modifier plus initial-class/feature proficiency and applicable "
                "save bonuses; retain a better Beast save in Wild Shape."},
               "8");
    }
    record(e, skillSheet, "tools", "Tool proficiencies", names(rules, tools),
           {"Background, initial/multiclass training, and feature/feat grants."}, "20");
    record(e, skillSheet, "languages", "Languages", "Common, " + names(rules, languages),
           {"Two selected Standard Languages plus explicit class/species grants."}, "20");
    int passive = 10 + (e.find("skill.perception") ? e.find("skill.perception")->normal.get<int>()
                                                   : ctx.modifiers["wisdom"]);
    record(e, skillSheet, "passivePerception", "Passive Perception", passive,
           {"10 plus Wisdom (Perception) modifier; situational advantage/disadvantage applies "
            "separately."},
           "186");
    if (species) {
        for (const auto &note : strings(*species, "/notes"))
            summary.notes.push_back(note);
        int dark = species->value("darkvision", 0);
        if (lineage)
            dark = std::max(dark, lineage->value("darkvision", 0));
        dark = std::max(dark, itemState.darkvisionMinimum) + itemState.darkvisionBonus;
        if (dark)
            record(e, summary, "darkvision", "Darkvision (feet)", dark,
                   {"Species and lineage grant; use greatest applicable range."}, "84–86");
        if (speciesId == "srd55:dragonborn") {
            const int dice =
                1 + (ctx.totalLevel >= 5) + (ctx.totalLevel >= 11) + (ctx.totalLevel >= 17);
            record(e, combat, "breath.dc", "Breath Weapon save DC",
                   8 + ctx.proficiency + ctx.modifiers["constitution"],
                   {"8 + proficiency + Constitution modifier; Dexterity save for half damage."},
                   "84");
            record(e, combat, "breath.damage", "Breath Weapon damage", std::to_string(dice) + "d10",
                   {"Damage scales with character level at 5, 11 and 17; ancestry determines "
                    "damage type."},
                   "84");
            e.resources.push_back({"dragonborn.breath",
                                   "Breath Weapon uses",
                                   ctx.proficiency,
                                   "Long Rest",
                                   {ref("84")}});
            if (ctx.totalLevel >= 5)
                e.resources.push_back(
                    {"dragonborn.flight", "Draconic Flight uses", 1, "Long Rest", {ref("84")}});
        }
        if (speciesId == "srd55:dwarf")
            e.resources.push_back({"dwarf.stonecunning",
                                   "Stonecunning uses",
                                   ctx.proficiency,
                                   "Long Rest",
                                   {ref("84")}});
        if (speciesId == "srd55:goliath") {
            e.resources.push_back({"goliath.ancestry",
                                   "Giant Ancestry uses",
                                   ctx.proficiency,
                                   "Long Rest",
                                   {ref("85")}});
            if (ctx.totalLevel >= 5)
                e.resources.push_back(
                    {"goliath.largeForm", "Large Form uses", 1, "Long Rest", {ref("85")}});
        }
        if (speciesId == "srd55:orc") {
            e.resources.push_back({"orc.adrenaline",
                                   "Adrenaline Rush uses",
                                   ctx.proficiency,
                                   "Short or Long Rest",
                                   {ref("86")}});
            e.resources.push_back(
                {"orc.endurance", "Relentless Endurance uses", 1, "Long Rest", {ref("86")}});
        }
    }
    if (lineage)
        for (const auto &note : strings(*lineage, "/notes"))
            summary.notes.push_back(note);
    Stage spellStage{"spellcasting", "Spells, spellbooks and preparation", {}};
    const auto speciesAbility = text(c, "/speciesCastingAbility");
    auto innate = [&](const std::string &id, const std::string &reason, int freeUses,
                      const Json &source) {
        if (!id.empty()) {
            const auto resource = "innate." + speciesId + "." + id;
            extraSpells.push_back({{"spellId", id},
                                   {"ability", speciesAbility},
                                   {"reason", reason},
                                   {"freeUses", freeUses},
                                   {"recharge", freeUses > 0 ? "Long Rest" : "At will"},
                                   {"resourceId", resource},
                                   {"source", source}});
            if (freeUses > 0)
                e.resources.push_back(
                    {resource,
                     reason + ": " + (rules.find(id) ? rules.find(id)->value("name", id) : id) +
                         " free uses",
                     freeUses,
                     "Long Rest",
                     {sourceFromJson(source)}});
        }
    };
    if (lineage) {
        if (lineage->value("chooseCantrip", false)) {
            spellStage.fields.push_back(
                select("/lineageCantrip", "High Elf Wizard cantrip", spellOptions("wizard", 0, 0)));
            innate(pick(e, c, spellStage.fields.back()), "High Elf", -1, lineage->at("source"));
        } else
            for (const auto &id : strings(*lineage, "/cantrips"))
                innate(id, lineage->value("name", ""), -1, lineage->at("source"));
        if (lineage->contains("level1Spell"))
            innate(lineage->at("level1Spell"), lineage->value("name", ""), ctx.proficiency,
                   lineage->at("source"));
        for (const int level : {3, 5})
            if (ctx.totalLevel >= level) {
                const auto key = "level" + std::to_string(level) + "Spell";
                if (lineage->contains(key))
                    innate(lineage->at(key), lineage->value("name", ""), 1, lineage->at("source"));
            }
    }
    if (speciesId == "srd55:tiefling")
        innate("srd55:thaumaturgy", "Otherworldly Presence", -1, species->at("source"));
    std::vector<std::string> casters;
    int casterLevel = 0;
    for (const auto &[id, level] : ctx.classLevels) {
        const auto &cls = *rules.find(id);
        const auto kind = text(cls, "/casting/kind", "none");
        if (kind == "full" || kind == "half") {
            casters.push_back(id);
            casterLevel += kind == "full" ? level : (level + 1) / 2;
        }
    }
    Json sharedSlots = Json::array({0, 0, 0, 0, 0, 0, 0, 0, 0});
    if (casters.size() == 1) {
        const auto &cls = *rules.find(casters.front());
        sharedSlots = cls.at("casting").at("slots").at(
            static_cast<std::size_t>(ctx.classLevels.at(casters.front()) - 1));
    } else if (casters.size() > 1) {
        if (const auto *wizard = entry(rules, "srd55:wizard", "class"))
            sharedSlots = wizard->at("casting").at("slots").at(
                static_cast<std::size_t>(std::clamp(casterLevel, 1, 20) - 1));
        record(e, magic, "spellcastingLevel", "Combined Spellcasting level", casterLevel,
               {"Full caster levels plus each Paladin/Ranger class's half levels rounded up; Pact "
                "Magic remains separate."},
               "26");
    }
    if (const auto *ruling = at(d.campaign, "/srd55DruidSlotRuling"))
        record(
            e, magic, "druid.slotRuling", "Campaign ruling: Druid slot conversion", *ruling,
            {"The published conversion text does not state slot expiration. This is the campaign's "
             "explicit interpretation, recorded separately from the published class progression."},
            "43");
    for (const auto *key :
         {"createdSpellSlots", "druidCreatedSpellSlots", "druidPersistentSpellSlots"})
        if (d.resources.contains(key)) {
            const auto &group = d.resources.at(key);
            bool valid = group.is_object();
            if (valid)
                for (auto it = group.begin(); it != group.end(); ++it)
                    if (it.key().size() != 1 || it.key()[0] < '1' || it.key()[0] > '9' ||
                        !it->is_number_integer() || *it < 0 || *it > 1000)
                        valid = false;
            if (!valid)
                issue(e, "slots.created", "/resources/" + std::string(key),
                      "Created spell-slot counts require levels 1–9 and bounded nonnegative whole "
                      "numbers.",
                      "43, 65");
            if (valid && std::string(key) != "createdSpellSlots" &&
                std::any_of(group.begin(), group.end(),
                            [](const auto &count) { return count > 0; })) {
                const auto policy = text(d.campaign, "/srd55DruidSlotRuling/policy"),
                           reason = text(d.campaign, "/srd55DruidSlotRuling/reason");
                if ((policy != "recover-expended" && policy != "additional-until-long-rest" &&
                     policy != "additional-until-spent") ||
                    reason.find_first_not_of(" \t\r\n") == std::string::npos)
                    issue(e, "slots.druid.ruling", "/campaign/srd55DruidSlotRuling",
                          "Additional Druid slots require a recorded campaign ruling and reason "
                          "because the source leaves their expiration unspecified.",
                          "43");
                if (!ctx.classLevels.contains("srd55:druid") ||
                    ctx.classLevels.at("srd55:druid") < 5)
                    issue(e, "slots.druid.class", "/resources/" + std::string(key),
                          "Druid slot conversions require the source class feature.", "43");
            }
        }
    for (int slot = 1; slot <= 9; ++slot) {
        const int baseCapacity = sharedSlots.at(static_cast<std::size_t>(slot - 1));
        const auto rank = std::to_string(slot);
        const int created = number(d.resources, "/createdSpellSlots/" + rank),
                  druidRest = number(d.resources, "/druidCreatedSpellSlots/" + rank),
                  druidUnspent = number(d.resources, "/druidPersistentSpellSlots/" + rank);
        const int capacity = baseCapacity + std::clamp(created, 0, 1000) +
                             std::clamp(druidRest, 0, 1000) + std::clamp(druidUnspent, 0, 1000);
        if (capacity || (!casters.empty() && slot <= 2)) {
            std::vector<std::string> steps = {
                "Class/multiclass table slots " + std::to_string(baseCapacity) +
                "; Font of Magic created slots " + std::to_string(created) +
                ". Font of Magic slots expire on a Long Rest; additional slots never unlock higher "
                "class spell preparations."};
            if (druidRest || druidUnspent)
                steps.push_back(
                    "Druid slots under the explicit campaign ruling: " + std::to_string(druidRest) +
                    " until Long Rest, " + std::to_string(druidUnspent) +
                    " until spent. These are additional current slots, not permanent class "
                    "progression.");
            record(e, magic, "spellSlots." + rank, "Level " + rank + " Spellcasting slots",
                   capacity, steps, "26, 43, 65");
        }
        if (capacity)
            e.resources.push_back({"spellSlots." + std::to_string(slot),
                                   "Available level " + std::to_string(slot) + " spell slots",
                                   capacity,
                                   "Long Rest",
                                   {ref("26")}});
    }
    Json castingProfiles = Json::array();
    for (const auto &[id, level] : ctx.classLevels) {
        const auto &cls = *rules.find(id);
        if (!cls.contains("casting"))
            continue;
        const auto &casting = cls.at("casting");
        const auto kind = casting.value("kind", "none");
        if (kind == "none")
            continue;
        const auto classSlug = slug(id), base = "/spellcasting/" + classSlug,
                   list = casting.value("list", classSlug), ability = casting.value("ability", "");
        if (!ctx.modifiers.contains(ability)) {
            issue(e, "casting.ability", base, "A supported spellcasting ability is required.");
            continue;
        }
        const auto ownSlots = casting.at("slots").at(static_cast<std::size_t>(level - 1));
        int highest = 0;
        for (int slot = 1; slot <= 9; ++slot)
            if (ownSlots.at(static_cast<std::size_t>(slot - 1)).get<int>() > 0)
                highest = slot;
        if (kind == "pact") {
            const int pactCount = number(casting, "/pactSlots/" + std::to_string(level - 1),
                                         level == 1   ? 1
                                         : level < 11 ? 2
                                         : level < 17 ? 3
                                                      : 4);
            highest = number(casting, "/pactSlotLevel/" + std::to_string(level - 1),
                             std::min(5, (level + 1) / 2));
            record(e, magic, "pactMagic.slots", "Pact Magic slots", pactCount,
                   {"Warlock level determines its separate pool; slots recover on a Short or Long "
                    "Rest."},
                   "71");
            record(
                e, magic, "pactMagic.level", "Pact Magic slot level", highest,
                {"Warlock progression; these are not added to the multiclass Spellcaster table."},
                "71");
            e.resources.push_back({"pactMagic.slots",
                                   "Available Pact Magic slots",
                                   pactCount,
                                   "Short or Long Rest",
                                   {ref("71")}});
        }
        const int cantripCount =
            casting.at("cantrips").at(static_cast<std::size_t>(level - 1)).get<int>() +
            (grants.cantripBonuses.contains(id) ? grants.cantripBonuses.at(id) : 0) +
            (features.cantripBonuses.contains(id) ? features.cantripBonuses.at(id) : 0);
        if (cantripCount) {
            spellStage.fields.push_back(select(base + "/cantrips",
                                               cls.value("name", id) + " cantrips",
                                               spellOptions(list, 0, 0), true));
            const auto selected = picks(e, c, spellStage.fields.back(), cantripCount);
            record(e, magic, classSlug + ".cantrips", cls.value("name", id) + " cantrips",
                   names(rules, selected),
                   {"Class progression plus separately granted cantrips; grants from other sources "
                    "retain their own ability."},
                   casting.value("sourcePage", cls.at("source").value("page", "19")));
            for (const auto &spell : selected)
                castingProfiles.push_back({{"spellId", spell},
                                           {"classId", id},
                                           {"ability", ability},
                                           {"reason", cls.value("name", id)},
                                           {"freeUses", -1}});
        }
        std::set<std::string> book;
        if (kind != "pact" && casting.value("learnMode", "") == "book") {
            for (int learnedLevel = 1; learnedLevel <= level; ++learnedLevel) {
                const auto row = casting.at("slots").at(static_cast<std::size_t>(learnedLevel - 1));
                int limit = 0;
                for (int s = 1; s <= 9; ++s)
                    if (row.at(static_cast<std::size_t>(s - 1)).get<int>())
                        limit = s;
                auto opts = spellOptions(list, 1, limit);
                for (auto &o : opts)
                    if (book.contains(o.id)) {
                        o.available = false;
                        o.reason = "Already learned at an earlier Wizard level.";
                    }
                const auto path = base + "/spellbook/" + std::to_string(learnedLevel);
                spellStage.fields.push_back(select(
                    path, "Wizard spells learned at class level " + std::to_string(learnedLevel),
                    opts, true));
                addSet(book, picks(e, c, spellStage.fields.back(), learnedLevel == 1 ? 6 : 2));
                const auto subclass = text(c, "/subclasses/wizard");
                int previousHighest = 0;
                if (learnedLevel > 1) {
                    const auto &previousSlots =
                        casting.at("slots").at(static_cast<std::size_t>(learnedLevel - 2));
                    for (int s = 1; s <= 9; ++s)
                        if (previousSlots.at(static_cast<std::size_t>(s - 1)).get<int>())
                            previousHighest = s;
                }
                if (subclass == "srd55:evoker" &&
                    (learnedLevel == 3 || (learnedLevel > 3 && limit > previousHighest))) {
                    auto savantOpts = filter(spellOptions(list, 1, limit), [&](const auto &o) {
                        return rules.find(o.id)->value("school", "") == "Evocation";
                    });
                    for (auto &o : savantOpts)
                        if (book.contains(o.id)) {
                            o.available = false;
                            o.reason = "Already in your spellbook; choose a new Evocation spell.";
                        }
                    spellStage.fields.push_back(
                        select(base + "/savant/" + std::to_string(learnedLevel),
                               "Evocation Savant at Wizard " + std::to_string(learnedLevel),
                               savantOpts, true));
                    addSet(book, picks(e, c, spellStage.fields.back(), learnedLevel == 3 ? 2 : 1));
                }
            }
            // Copied spells are explicit acquisition records, never inferred from prepared
            // selections.
            const auto *copied = at(c, base + "/copiedSpells");
            if (copied) {
                if (!copied->is_array())
                    issue(e, "spellbook.copy.type", base + "/copiedSpells",
                          "Copied spells must be acquisition records.", "78");
                else
                    for (std::size_t i = 0; i < copied->size(); ++i) {
                        const auto &item = (*copied)[i];
                        const auto spellId = item.is_object() ? item.value("spellId", "") : "";
                        const auto *spell = entry(rules, spellId, "spell");
                        if (!spell || !contains(strings(*spell, "/lists"), list) ||
                            spell->value("level", 0) < 1 || spell->value("level", 99) > highest) {
                            issue(e, "spellbook.copy.spell",
                                  base + "/copiedSpells/" + std::to_string(i),
                                  "Only a Wizard spell of a level you can prepare can be copied.",
                                  "78");
                            continue;
                        }
                        const int spellLevel = spell->at("level");
                        if (!item.contains("paidCp") || !item["paidCp"].is_number_integer() ||
                            item["paidCp"] < spellLevel * 5000 || !item.contains("minutes") ||
                            !item["minutes"].is_number_integer() ||
                            item["minutes"] < spellLevel * 120)
                            issue(e, "spellbook.copy.cost",
                                  base + "/copiedSpells/" + std::to_string(i),
                                  "Record at least 50 GP and 2 hours per spell level for copying.",
                                  "78");
                        if (!book.insert(spellId).second)
                            issue(e, "spellbook.copy.duplicate",
                                  base + "/copiedSpells/" + std::to_string(i),
                                  "This spell is already in the spellbook.", "78");
                    }
            }
            record(e, magic, "wizard.spellbook", "Wizard spellbook", names(rules, book),
                   {"Initial six, two each Wizard level, applicable Savant grants, and separately "
                    "recorded copying transactions."},
                   "78");
            e.moduleData["spellbooks"][classSlug] = Json(book);
        }
        auto preparedOpts = spellOptions(list, 1, highest);
        if (casting.value("learnMode", "") == "book")
            for (auto &o : preparedOpts)
                if (!book.contains(o.id)) {
                    o.available = false;
                    o.reason = "A prepared Wizard spell must be in the spellbook.";
                }
        // Magical Secrets expands the Bard's selection lists, not its spell-level eligibility.
        if (classSlug == "bard" && level >= 10) {
            preparedOpts = filter(options(rules, "spell"), [&](const auto &o) {
                const auto &spell = *rules.find(o.id);
                const auto lists = strings(spell, "/lists");
                return spell.value("level", 0) >= 1 && spell.value("level", 99) <= highest &&
                       (contains(lists, "bard") || contains(lists, "cleric") ||
                        contains(lists, "druid") || contains(lists, "wizard"));
            });
        }
        for (auto &option : preparedOpts)
            for (const auto &grant : extraSpells)
                if (grant.value("classId", "") == id && grant.value("spellId", "") == option.id) {
                    option.available = false;
                    option.reason = "Already always prepared by a class feature; choose another "
                                    "spell for the normal preparation allowance.";
                }
        const int preparations = casting.at("prepared").at(static_cast<std::size_t>(level - 1));
        spellStage.fields.push_back(select(base + "/preparedSpells",
                                           cls.value("name", id) + " prepared spells", preparedOpts,
                                           true));
        const auto prepared = picks(e, c, spellStage.fields.back(), preparations);
        record(e, magic, classSlug + ".prepared", cls.value("name", id) + " prepared spells",
               names(rules, prepared),
               {"Preparation count and eligible spell levels use " + cls.value("name", id) +
                " class level " + std::to_string(level) +
                ", independently of higher combined spell slots."},
               cls.at("source").value("page", "19"));
        const int dc =
            8 + ctx.proficiency + ctx.modifiers[ability] +
            (grants.spellSaveBonuses.contains(id) ? grants.spellSaveBonuses.at(id) : 0) +
            (features.spellSaveBonuses.contains(id) ? features.spellSaveBonuses.at(id) : 0) +
            itemState.spellSaveBonus;
        const int attack =
            ctx.proficiency + ctx.modifiers[ability] +
            (grants.spellAttackBonuses.contains(id) ? grants.spellAttackBonuses.at(id) : 0) +
            (features.spellAttackBonuses.contains(id) ? features.spellAttackBonuses.at(id) : 0) +
            itemState.spellAttackBonus;
        record(e, magic, classSlug + ".spellDc", cls.value("name", id) + " spell save DC", dc,
               {"8 + " + ability +
                " modifier + total-character proficiency + source-specific bonuses."},
               "104");
        record(e, magic, classSlug + ".spellAttack", cls.value("name", id) + " spell attack",
               attack,
               {ability + " modifier + total-character proficiency + source-specific bonuses."},
               "104");
        for (const auto &spell : prepared)
            castingProfiles.push_back({{"spellId", spell},
                                       {"classId", id},
                                       {"ability", ability},
                                       {"reason", cls.value("name", id)},
                                       {"saveDc", dc},
                                       {"attackBonus", attack}});
        if (classSlug == "wizard") {
            record(e, magic, "arcaneRecovery", "Arcane Recovery slot levels", (level + 1) / 2,
                   {"Half Wizard level rounded up; slots of level 6+ cannot be recovered; once per "
                    "Long Rest after a Short Rest."},
                   "78");
        }
    }
    for (auto grant : extraSpells) {
        const auto spellId = grant.value("spellId", "");
        const auto *spell = entry(rules, spellId, "spell");
        if (!spell) {
            issue(e, "spellGrant.missing", "/spellcasting",
                  "A granted spell is unavailable: " + spellId + ".", "104");
            continue;
        }
        const auto ability = grant.value("ability", "");
        if (grant.value("sourceType", "") == "item") {
            if (grant.contains("fixedDC"))
                grant["saveDc"] = grant.at("fixedDC");
            else
                grant["saveDc"] =
                    8 + ctx.proficiency +
                    (ctx.modifiers.contains(ability) ? ctx.modifiers.at(ability) : 0) +
                    itemState.spellSaveBonus;
            if (grant.contains("fixedAttack"))
                grant["attackBonus"] = grant.at("fixedAttack");
            else
                grant["attackBonus"] =
                    ctx.proficiency +
                    (ctx.modifiers.contains(ability) ? ctx.modifiers.at(ability) : 0) +
                    itemState.spellAttackBonus;
            castingProfiles.push_back(grant);
            continue;
        }
        if (!ctx.modifiers.contains(ability)) {
            issue(e, "spellGrant.ability", "/spellcasting",
                  "Choose an ability for spell grant " + spellId + ".", "104");
            continue;
        }
        const auto owner = grant.value("classId", "");
        grant["saveDc"] =
            8 + ctx.proficiency + ctx.modifiers[ability] +
            (grants.spellSaveBonuses.contains(owner) ? grants.spellSaveBonuses.at(owner) : 0) +
            (features.spellSaveBonuses.contains(owner) ? features.spellSaveBonuses.at(owner) : 0) +
            itemState.spellSaveBonus;
        grant["attackBonus"] =
            ctx.proficiency + ctx.modifiers[ability] +
            (grants.spellAttackBonuses.contains(owner) ? grants.spellAttackBonuses.at(owner) : 0) +
            (features.spellAttackBonuses.contains(owner) ? features.spellAttackBonuses.at(owner)
                                                         : 0) +
            itemState.spellAttackBonus;
        castingProfiles.push_back(grant);
    }
    for (auto &profile : castingProfiles)
        if (profile.contains("classId") && profile.value("sourceType", "") != "item") {
            const auto owner = profile.value("classId", "");
            const auto ability = profile.value("ability", "");
            if (ctx.modifiers.contains(ability)) {
                profile["saveDc"] =
                    8 + ctx.proficiency + ctx.modifiers[ability] +
                    (grants.spellSaveBonuses.contains(owner) ? grants.spellSaveBonuses.at(owner)
                                                             : 0) +
                    (features.spellSaveBonuses.contains(owner) ? features.spellSaveBonuses.at(owner)
                                                               : 0) +
                    itemState.spellSaveBonus;
                profile["attackBonus"] =
                    ctx.proficiency + ctx.modifiers[ability] +
                    (grants.spellAttackBonuses.contains(owner) ? grants.spellAttackBonuses.at(owner)
                                                               : 0) +
                    (features.spellAttackBonuses.contains(owner)
                         ? features.spellAttackBonuses.at(owner)
                         : 0) +
                    itemState.spellAttackBonus;
            }
        }
    Json printedProfiles = Json::array();
    for (const auto &profile : castingProfiles) {
        const auto id = profile.value("spellId", "");
        const auto *spell = rules.find(id);
        printedProfiles.push_back(
            {{"spell", spell ? spell->value("name", id) : id},
             {"source", profile.value("reason", profile.value("classId", ""))},
             {"ability", profile.value("ability", "")},
             {"DC",
              profile.value("saveDc", 8 + ctx.proficiency +
                                          ctx.modifiers[profile.value("ability", "intelligence")])},
             {"attack", profile.value("attackBonus",
                                      ctx.proficiency +
                                          ctx.modifiers[profile.value("ability", "intelligence")])},
             {"casting", profile.value("sourceType", "") == "item"
                             ? "Item at level " + std::to_string(profile.value("castLevel", 0)) +
                                   "; " + std::to_string(profile.value("chargeCost", 0)) +
                                   " charges"
                         : profile.contains("freeUses")
                             ? (profile.value("freeUses", 0) < 0 ? "At will"
                                : profile.value("freeUses", 0) == 0
                                    ? "Spell slots; always prepared"
                                    : std::to_string(profile.value("freeUses", 0)) + " free; " +
                                          profile.value("recharge", "Long Rest"))
                             : "Spell slots"}});
    }
    if (!printedProfiles.empty())
        record(e, magic, "spells.profiles", "Spellcasting profiles", printedProfiles,
               {"Each source retains its own ability and eligibility, even when another source "
                "grants the same spell."},
               "104");
    for (auto &profile : castingProfiles)
        profile["profileId"] =
            profile.value("itemId", profile.value("classId", profile.value("reason", "grant"))) +
            "|" + profile.value("instanceId", profile.value("resourceId", "")) + "|" +
            profile.value("reason", "") + "|" + profile.value("activationActionId", "") + "|" +
            profile.value("spellId", "") + "|" + profile.value("ability", "");
    e.moduleData["castingProfiles"] = castingProfiles;
    if (!spellStage.fields.empty())
        e.stages.push_back(spellStage);
    e.sections.insert(e.sections.begin(), summary);
    e.sections.push_back(abilitySheet);
    e.sections.push_back(combat);
    e.sections.push_back(skillSheet);
    e.sections.push_back(gearSheet);
    if (!magic.calculationIds.empty() || !magic.notes.empty())
        e.sections.push_back(magic);
    Stage currentState{"current-state", "Current condition inputs", {}};
    auto reduction = integer("/hpMaximumReduction", "Current HP maximum reduction", 0, unreducedHp,
                             "A completed Long Rest restores the normal maximum unless the "
                             "specific effect says otherwise.");
    reduction.scope = "resources";
    reduction.advanced = true;
    currentState.fields.push_back(reduction);
    e.stages.push_back(currentState);
    linkSrd55Resources(e);
    e.moduleData["castingAbilities"] = Json(ctx.castingAbilities);
    e.moduleData["skillProficiencies"] = Json(ctx.skillProficiencies);
    appendSrd55CompanionState(d, rules, e);
    return e;
}
} // namespace

std::vector<Message> validateContent(const ContentPack &pack);
std::vector<Message> validateRuleset(const ResolvedRuleset &rules);
} // namespace dnd::srd55v2
namespace dnd {
EditionModule srd55FullModule() {
    return {"srd55",
            "5.5e / SRD 5.2.1 expanded lifecycle (experimental)",
            "2.0.0",
            true,
            srd55v2::run,
            srd55v2::validateContent,
            srd55v2::validateRuleset,
            {{"srd55-core", "2.0.0"}},
            srd55v2::applyLifecycleCommand,
            srd55v2::appendLifecycleActions};
}
} // namespace dnd
