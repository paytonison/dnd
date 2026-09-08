#include "dnd/engine.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <stdexcept>

namespace dnd {
namespace {
bool hasErrors(const std::vector<Message> &messages) {
    return std::any_of(messages.begin(), messages.end(),
                       [](const auto &m) { return m.severity == "error"; });
}
Json sourcesJson(const std::vector<SourceRef> &refs) {
    auto result = Json::array();
    for (const auto &s : refs)
        result.push_back({{"publication", s.publication}, {"page", s.page}, {"url", s.url}});
    return result;
}
Json fieldJson(const Field &f) {
    Json options = Json::array();
    for (const auto &o : f.options)
        options.push_back({{"id", o.id},
                           {"label", o.label},
                           {"available", o.available},
                           {"reason", o.reason},
                           {"sources", sourcesJson(o.sources)}});
    return {{"path", f.path},
            {"label", f.label},
            {"kind", f.kind},
            {"minimum", f.minimum},
            {"maximum", f.maximum},
            {"options", options},
            {"advanced", f.advanced},
            {"help", f.help},
            {"scope", f.scope},
            {"editable", f.editable},
            {"readOnlyReason", f.readOnlyReason}};
}
void require(const Json &j, const std::string &key, Json::value_t type) {
    if (!j.contains(key) || j.at(key).type() != type)
        throw std::runtime_error("Character field '" + key + "' has an invalid or missing type.");
}
} // namespace
bool ResolvedRuleset::valid() const { return !hasErrors(messages); }
bool Evaluation::complete() const { return !hasErrors(messages); }
bool TransitionResult::valid() const { return !hasErrors(messages); }
Json effectiveCampaignOptions(const CharacterDocument &document) {
    Json options = document.campaign.is_object() ? document.campaign : Json::object();
    if (document.choices.is_object() && document.choices.contains("options") &&
        document.choices["options"].is_object())
        options.update(document.choices["options"]);
    return options;
}
TransitionResult executeCommand(const CharacterDocument &document, const ResolvedRuleset &ruleset,
                                const CharacterCommand &command) {
    TransitionResult result{document, {}};
    if (!ruleset.valid() || ruleset.edition != document.edition ||
        ruleset.moduleVersion != document.moduleVersion) {
        result.messages.push_back({"error",
                                   "command.ruleset",
                                   "/packs",
                                   "Resolve the exact character rules before applying an action.",
                                   {}});
        return result;
    }
    if (!command.inputs.is_object()) {
        result.messages.push_back(
            {"error", "command.inputs", "/", "Action inputs must be an object.", {}});
        return result;
    }
    const auto *module = findEdition(document.edition, document.moduleVersion);
    if (!module || !module->applyCommand) {
        result.messages.push_back({"error",
                                   "command.unsupported",
                                   "/",
                                   "This rules module does not expose lifecycle actions.",
                                   {}});
        return result;
    }
    const auto evaluated = evaluate(document, ruleset);
    const auto action = std::find_if(evaluated.actions.begin(), evaluated.actions.end(),
                                     [&](const auto &a) { return a.id == command.id; });
    if (action == evaluated.actions.end() || !action->available) {
        result.messages.push_back(
            {"error", "command.unavailable", "/",
             action == evaluated.actions.end()
                 ? "This action is not available for the current character."
                 : action->reason,
             action == evaluated.actions.end() ? std::vector<SourceRef>{} : action->sources});
        return result;
    }
    try {
        for (const auto &field : action->fields) {
            const Json::json_pointer path(field.path);
            if (!command.inputs.contains(path))
                continue;
            const auto &value = command.inputs.at(path);
            if (field.kind == "integer" &&
                (!value.is_number_integer() || value < field.minimum || value > field.maximum))
                throw std::runtime_error(field.label +
                                         " is outside its permitted whole-number range.");
            if (field.kind == "boolean" && !value.is_boolean())
                throw std::runtime_error(field.label + " must be true or false.");
            if ((field.kind == "text" || field.kind == "select") && !value.is_string())
                throw std::runtime_error(field.label + " must be text.");
            if (field.kind == "select" &&
                std::none_of(field.options.begin(), field.options.end(), [&](const auto &option) {
                    return option.available && value == Json(option.id);
                }))
                throw std::runtime_error(field.label + " is not an available choice.");
            if (field.kind == "multiselect") {
                if (!value.is_array())
                    throw std::runtime_error(field.label + " must be a selection list.");
                for (const auto &selected : value)
                    if (!selected.is_string() ||
                        std::none_of(field.options.begin(), field.options.end(),
                                     [&](const auto &option) {
                                         return option.available && selected == Json(option.id);
                                     }))
                        throw std::runtime_error(field.label + " contains an unavailable choice.");
            }
        }
        const auto flat = command.inputs.flatten();
        for (auto it = flat.begin(); it != flat.end(); ++it) {
            bool recognized = command.inputs.empty();
            for (const auto &field : action->fields)
                if (it.key() == field.path || it.key().starts_with(field.path + "/") ||
                    field.path.starts_with(it.key() + "/")) {
                    recognized = true;
                    break;
                }
            if (!recognized)
                throw std::runtime_error("Unexpected action input: " + it.key());
        }
        result = module->applyCommand(document, ruleset, command);
        if (result.valid()) {
            const auto identityBefore = toJson(document), identityAfter = toJson(result.document);
            for (const auto *key : {"schemaVersion", "id", "edition", "moduleVersion", "packs"})
                if (identityBefore.at(key) != identityAfter.at(key))
                    throw std::runtime_error("A lifecycle action cannot change character identity "
                                             "or the exact ruleset.");
            result.document.advancement.push_back(
                {{"kind", "command"},
                 {"sequence", result.document.advancement.size()},
                 {"action", command.id},
                 {"inputs", command.inputs},
                 {"moduleVersion", document.moduleVersion},
                 {"choiceChanges", Json::diff(document.choices, result.document.choices)},
                 {"resourceChanges", Json::diff(document.resources, result.document.resources)},
                 {"sources", sourcesJson(action->sources)}});
        }
    } catch (const std::exception &error) {
        result.messages.push_back({"error", "command.invalid", "/", error.what(), {}});
    }
    if (!result.valid())
        result.document = document;
    return result;
}
const Json *ResolvedRuleset::find(const std::string &id) const {
    const auto it = content.find(id);
    return it == content.end() ? nullptr : &it->second;
}
const Calculation *Evaluation::find(const std::string &id) const {
    const auto it = std::find_if(calculations.begin(), calculations.end(),
                                 [&](const auto &c) { return c.id == id; });
    return it == calculations.end() ? nullptr : &*it;
}
void addCalculation(Evaluation &result, std::string id, std::string label, Json value,
                    std::vector<std::string> steps, std::vector<SourceRef> sources) {
    result.calculations.push_back(
        {std::move(id), std::move(label), value, value, std::move(steps), std::move(sources), {}});
}
SourceRef sourceFromJson(const Json &json) {
    if (!json.is_object())
        return {};
    return {json.value("publication", ""), json.value("page", ""), json.value("url", "")};
}
int integerChoice(const Json &choices, const std::string &key, int fallback) {
    if (!choices.is_object() || !choices.contains(key) || !choices[key].is_number_integer())
        return fallback;
    try {
        const auto n = choices[key].get<std::int64_t>();
        return n >= INT32_MIN && n <= INT32_MAX ? static_cast<int>(n) : fallback;
    } catch (...) {
        return fallback;
    }
}
std::string stringChoice(const Json &choices, const std::string &key, const std::string &fallback) {
    return choices.is_object() && choices.contains(key) && choices[key].is_string()
               ? choices[key].get<std::string>()
               : fallback;
}
Evaluation evaluate(const CharacterDocument &document, const ResolvedRuleset &ruleset) {
    Evaluation result;
    result.messages = ruleset.messages;
    const auto *module = findEdition(document.edition, document.moduleVersion);
    if (!module || module->version != document.moduleVersion ||
        ruleset.edition != document.edition || ruleset.moduleVersion != document.moduleVersion) {
        result.messages.push_back({"error",
                                   "edition.unavailable",
                                   "/edition",
                                   "The exact saved edition module is unavailable. Selections are "
                                   "preserved for inspection.",
                                   {}});
        return result;
    }
    if (!ruleset.valid())
        return result;
    try {
        result = module->evaluate(document, ruleset);
        result.messages.insert(result.messages.begin(), ruleset.messages.begin(),
                               ruleset.messages.end());
    } catch (const std::exception &e) {
        result.messages.push_back({"error",
                                   "document.invalid_input",
                                   "/choices",
                                   std::string("Cannot evaluate an invalid input: ") + e.what(),
                                   {}});
        return result;
    }
    std::vector<std::string> overridden;
    if (!document.overrides.is_array()) {
        result.messages.push_back(
            {"error", "override.invalid", "/overrides", "Overrides must be an array.", {}});
        return result;
    }
    for (const auto &override : document.overrides) {
        if (!override.is_object() || !override.contains("target") ||
            !override["target"].is_string() || !override.contains("reason") ||
            !override["reason"].is_string() || !override.contains("value")) {
            result.messages.push_back({"error",
                                       "override.invalid",
                                       "/overrides",
                                       "An override requires a target, value, and reason.",
                                       {}});
            continue;
        }
        const auto target = override["target"].get<std::string>();
        const auto reason = override["reason"].get<std::string>();
        if (reason.find_first_not_of(" \n\t\r") == std::string::npos ||
            std::find(overridden.begin(), overridden.end(), target) != overridden.end()) {
            result.messages.push_back({"error",
                                       "override.reason",
                                       "/overrides",
                                       "Each override needs a nonblank reason and a unique target.",
                                       {}});
            continue;
        }
        if (target.starts_with("choice:")) {
            const auto path = target.substr(7);
            const Field *field = nullptr;
            for (const auto &stage : result.stages)
                for (const auto &candidate : stage.fields)
                    if (candidate.path == path && candidate.kind == "select" &&
                        candidate.scope == "choices")
                        field = &candidate;
            const Choice *choice = nullptr;
            if (field && override["value"].is_string())
                for (const auto &option : field->options)
                    if (option.id == override["value"].get<std::string>())
                        choice = &option;
            bool matches = false;
            try {
                const Json::json_pointer pointer(path);
                matches = document.choices.contains(pointer) &&
                          document.choices.at(pointer) == override["value"];
            } catch (...) {
            }
            if (!choice || !matches) {
                result.messages.push_back(
                    {"error",
                     "override.unsupported",
                     "/overrides",
                     "Choice override must match an existing selected option; missing rules cannot "
                     "be supplied by an override.",
                     {}});
                continue;
            }
            for (auto &m : result.messages)
                if (m.severity == "error" && (m.path == path || m.path == "/choices" + path)) {
                    // Only the eligibility restriction advertised by this option can be waived.
                    if (!choice->available && !choice->reason.empty() && m.text == choice->reason) {
                        m.severity = "warning";
                        m.text += " DM exception: " + reason;
                    }
                }
            addCalculation(result, target, "Choice exception: " + field->label,
                           choice->available ? "Eligible" : "Unavailable: " + choice->reason,
                           {"Published eligibility: " +
                            (choice->available ? std::string("eligible") : choice->reason)},
                           choice->sources);
            auto &calculation = result.calculations.back();
            calculation.effective = choice->label;
            calculation.overrideReason = reason;
            overridden.push_back(target);
            continue;
        }
        auto it = std::find_if(result.calculations.begin(), result.calculations.end(),
                               [&](const auto &c) { return c.id == target; });
        if (it == result.calculations.end()) {
            result.messages.push_back(
                {"error",
                 "override.unsupported",
                 "/overrides",
                 "Override target '" + target +
                     "' is unavailable. Overrides cannot introduce missing rules.",
                 {}});
            continue;
        }
        if (it->normal.type() != override["value"].type() &&
            !(it->normal.is_number() && override["value"].is_number())) {
            result.messages.push_back(
                {"error",
                 "override.type",
                 "/overrides",
                 "Override value must have the same type as the normal result for " + it->label +
                     ".",
                 {}});
            continue;
        }
        it->effective = override["value"];
        it->overrideReason = reason;
        overridden.push_back(target);
    }
    for (auto &resource : result.resources)
        if (!resource.maximumCalculationId.empty()) {
            if (const auto *calculation = result.find(resource.maximumCalculationId)) {
                try {
                    const auto &maximum = resource.maximumValuePointer.empty()
                                              ? calculation->effective
                                              : calculation->effective.at(Json::json_pointer(
                                                    resource.maximumValuePointer));
                    if (!maximum.is_number_integer() || maximum < 0 || maximum > 1000000)
                        throw std::runtime_error("Resource capacity must be a whole number between "
                                                 "zero and one million.");
                    resource.maximum = maximum.get<int>();
                } catch (const std::exception &error) {
                    result.messages.push_back({"error", "override.resource_capacity", "/overrides",
                                               error.what(), resource.sources});
                }
            }
        }
    for (const auto &resource : result.resources)
        if (document.resources.contains(resource.id)) {
            const auto &current = document.resources.at(resource.id);
            if (!current.is_number_integer() || current < 0 || current > resource.maximum)
                result.messages.push_back(
                    {"warning", "resource.range", "/resources/" + resource.id,
                     "Saved current " + resource.label + " is outside its current capacity of " +
                         std::to_string(resource.maximum) + "; the saved value is preserved.",
                     resource.sources});
        }
    if (module->prepareActions)
        try {
            module->prepareActions(document, ruleset, result);
        } catch (const std::exception &error) {
            result.messages.push_back({"error", "actions.invalid", "/", error.what(), {}});
            result.actions.clear();
        }
    return result;
}
CharacterDocument newCharacter(const std::string &edition, const std::string &version) {
    CharacterDocument document;
    document.edition = edition;
    if (const auto *module = findEdition(edition, version)) {
        document.moduleVersion = module->version;
        document.packs = module->defaultPacks;
    } else
        throw std::runtime_error("Unknown edition: " + edition);
    if (document.packs.empty())
        document.packs.push_back({edition == "bx" ? "bx-core" : "srd55-core", "1.0.0"});
    std::random_device random;
    document.id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
                  "-" + std::to_string(random());
    return document;
}
bool MigrationResult::valid() const { return !hasErrors(messages); }
MigrationResult migrateCharacterVersion(const CharacterDocument &original,
                                        const std::string &version) {
    MigrationResult result{original, {}};
    if (original.edition != "srd55" || original.moduleVersion != "1.0.0" || version != "2.0.0" ||
        !findEdition(original.edition, version)) {
        result.messages.push_back(
            {"error",
             "migration.unsupported",
             "/moduleVersion",
             "No explicit migration is available for this edition/version pair.",
             {}});
        return result;
    }
    if (original.packs.size() != 1 || original.packs.front().id != "srd55-core" ||
        original.packs.front().version != "1.0.0") {
        result.messages.push_back(
            {"error",
             "migration.sources",
             "/packs",
             "This migration supports the original core pack only. Supplemental source mappings "
             "require their own verified migration.",
             {}});
        return result;
    }
    auto &d = result.document;
    const auto fresh = newCharacter("srd55", version);
    d.id = fresh.id;
    d.moduleVersion = version;
    d.packs = fresh.packs;
    auto &c = d.choices;
    auto copy = [&](const std::string &source, const std::string &target) {
        const Json::json_pointer from(source), to(target);
        if (original.choices.contains(from))
            c[to] = original.choices.at(from);
    };
    const auto cls = stringChoice(c, "classId");
    if (cls == "srd55:wizard") {
        copy("/cantrips", "/spellcasting/wizard/cantrips");
        copy("/spellbook", "/spellcasting/wizard/spellbook");
        copy("/preparedSpells", "/spellcasting/wizard/preparedSpells");
        copy("/evocationSavant", "/spellcasting/wizard/savant/3");
        copy("/scholarSkill", "/features/wizard/scholar/0");
        copy("/subclassId", "/subclasses/wizard");
    } else if (cls == "srd55:fighter") {
        copy("/fightingStyle", "/features/fighter/fightingStyle/0");
        copy("/weaponMasteries", "/features/fighter/weaponMasteries");
        copy("/subclassId", "/subclasses/fighter");
    }
    d.advancement.push_back({{"kind", "moduleMigration"},
                             {"fromVersion", original.moduleVersion},
                             {"toVersion", version},
                             {"originalId", original.id},
                             {"originalDocument", toJson(original)}});
    result.messages.push_back(
        {"info",
         "migration.review",
         "/moduleVersion",
         "Created an unsaved copy using module 2.0.0. Original choices, rolls, and history are "
         "retained; review all validation and statistics before saving the copy.",
         {}});
    return result;
}
Json toJson(const CharacterDocument &d) {
    Json pins = Json::array();
    for (const auto &p : d.packs)
        pins.push_back({{"id", p.id}, {"version", p.version}});
    return {{"format", "dungeoning-a-dragon"},
            {"schemaVersion", d.schemaVersion},
            {"id", d.id},
            {"name", d.name},
            {"edition", d.edition},
            {"moduleVersion", d.moduleVersion},
            {"campaign", d.campaign},
            {"packs", pins},
            {"choices", d.choices},
            {"advancement", d.advancement},
            {"rolls", d.rolls},
            {"resources", d.resources},
            {"overrides", d.overrides}};
}
CharacterDocument documentFromJson(const Json &j) {
    if (!j.is_object() || j.value("format", "") != "dungeoning-a-dragon")
        throw std::runtime_error("Not a Dungeoning a Dragon character document.");
    if (!j.contains("schemaVersion") || !j["schemaVersion"].is_number_integer() ||
        j["schemaVersion"] != 1)
        throw std::runtime_error("Unsupported character schema version; original preserved.");
    for (const auto *key : {"id", "name", "edition", "moduleVersion"})
        require(j, key, Json::value_t::string);
    for (const auto *key : {"campaign", "choices", "rolls", "resources"})
        require(j, key, Json::value_t::object);
    for (const auto *key : {"packs", "advancement", "overrides"})
        require(j, key, Json::value_t::array);
    CharacterDocument d;
    d.id = j["id"];
    d.name = j["name"];
    d.edition = j["edition"];
    d.moduleVersion = j["moduleVersion"];
    d.campaign = j["campaign"];
    d.choices = j["choices"];
    d.rolls = j["rolls"];
    d.resources = j["resources"];
    d.advancement = j["advancement"];
    d.overrides = j["overrides"];
    for (const auto &p : j["packs"]) {
        require(p, "id", Json::value_t::string);
        require(p, "version", Json::value_t::string);
        d.packs.push_back({p["id"], p["version"]});
    }
    return d;
}
Json toJson(const Evaluation &e) {
    Json j = {
        {"complete", e.complete()},   {"stages", Json::array()},   {"calculations", Json::array()},
        {"sections", Json::array()},  {"messages", Json::array()}, {"rollRequests", Json::array()},
        {"resources", Json::array()}, {"actions", Json::array()},  {"moduleData", e.moduleData}};
    for (const auto &a : e.actions) {
        Json fields = Json::array();
        for (const auto &f : a.fields)
            fields.push_back(fieldJson(f));
        j["actions"].push_back({{"id", a.id},
                                {"label", a.label},
                                {"description", a.description},
                                {"fields", fields},
                                {"available", a.available},
                                {"reason", a.reason},
                                {"sources", sourcesJson(a.sources)},
                                {"initialInputs", a.initialInputs}});
    }
    for (const auto &r : e.rollRequests)
        j["rollRequests"].push_back({{"id", r.id},
                                     {"label", r.label},
                                     {"path", r.path},
                                     {"category", r.category},
                                     {"sides", r.sides},
                                     {"count", r.count},
                                     {"dropLowest", r.dropLowest}});
    for (const auto &r : e.resources)
        j["resources"].push_back({{"id", r.id},
                                  {"label", r.label},
                                  {"maximum", r.maximum},
                                  {"recharge", r.recharge},
                                  {"sources", sourcesJson(r.sources)},
                                  {"maximumCalculationId", r.maximumCalculationId},
                                  {"maximumValuePointer", r.maximumValuePointer}});
    for (const auto &m : e.messages)
        j["messages"].push_back({{"severity", m.severity},
                                 {"code", m.code},
                                 {"path", m.path},
                                 {"text", m.text},
                                 {"sources", sourcesJson(m.sources)}});
    for (const auto &c : e.calculations)
        j["calculations"].push_back({{"id", c.id},
                                     {"label", c.label},
                                     {"normal", c.normal},
                                     {"effective", c.effective},
                                     {"steps", c.steps},
                                     {"sources", sourcesJson(c.sources)},
                                     {"overrideReason", c.overrideReason}});
    for (const auto &section : e.sections)
        j["sections"].push_back({{"title", section.title},
                                 {"calculationIds", section.calculationIds},
                                 {"notes", section.notes}});
    for (const auto &stage : e.stages) {
        Json fields = Json::array();
        for (const auto &f : stage.fields)
            fields.push_back(fieldJson(f));
        j["stages"].push_back({{"id", stage.id}, {"label", stage.label}, {"fields", fields}});
    }
    return j;
}
} // namespace dnd
