#pragma once

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace dnd {
using Json = nlohmann::json;

struct SourceRef {
    std::string publication;
    std::string page;
    std::string url;
};
struct Message {
    std::string severity; // error, warning, info
    std::string code;
    std::string path;
    std::string text;
    std::vector<SourceRef> sources;
};
struct Calculation {
    std::string id;
    std::string label;
    Json normal;
    Json effective;
    std::vector<std::string> steps;
    std::vector<SourceRef> sources;
    std::string overrideReason;
};
struct Choice {
    std::string id;
    std::string label;
    bool available = true;
    std::string reason;
    std::vector<SourceRef> sources;
};
struct Field {
    std::string path; // JSON pointer within document.choices
    std::string label;
    std::string kind; // text, integer, boolean, select, multiselect
    int minimum = 0;
    int maximum = 999999;
    std::vector<Choice> options;
    bool advanced = false;
    std::string help;
    std::string scope = "choices"; // choices or resources; state remains explicitly separate
    bool editable = true;
    std::string readOnlyReason = {};
};
struct ActionDefinition {
    std::string id;
    std::string label;
    std::string description;
    std::vector<Field> fields; // pointers within CharacterCommand.inputs
    bool available = true;
    std::string reason;
    std::vector<SourceRef> sources;
    Json initialInputs = Json::object();
};
struct CharacterCommand {
    std::string id;
    Json inputs = Json::object();
};
struct Stage {
    std::string id;
    std::string label;
    std::vector<Field> fields;
};
struct SheetSection {
    std::string title;
    std::vector<std::string> calculationIds;
    std::vector<std::string> notes;
};
struct RollRequest {
    std::string id;
    std::string label;
    std::string path;     // JSON pointer within document.choices
    std::string category; // abilities, hitPoints, money
    int sides = 6;
    int count = 1;
    int dropLowest = 0;
};
struct ResourceDefinition {
    std::string id; // key within document.resources
    std::string label;
    int maximum = 0;
    std::string recharge;
    std::vector<SourceRef> sources;
    std::string maximumCalculationId = {};
    std::string maximumValuePointer = {}; // empty means the calculation's whole numeric value
};
struct PackPin {
    std::string id;
    std::string version;
};
struct CharacterDocument {
    int schemaVersion = 1;
    std::string id;
    std::string name;
    std::string edition = "bx";
    std::string moduleVersion = "1.0.0";
    Json campaign = Json::object();
    std::vector<PackPin> packs;
    Json choices = Json::object();
    Json advancement = Json::array();
    Json rolls = Json::object();
    Json resources = Json::object();
    Json overrides = Json::array(); // {target, value, reason}, targets are calculation ids
};
struct TransitionResult {
    CharacterDocument document;
    std::vector<Message> messages;
    bool valid() const;
};
struct ContentPack {
    Json manifest;
    std::vector<Json> entries; // {id, kind, name, source:{publication,page,url}, ...mechanics}
    std::string directory;
};
struct ResolvedRuleset {
    std::string edition;
    std::string moduleVersion;
    std::vector<ContentPack> packs;
    std::map<std::string, Json> content;
    std::vector<Message> messages;
    bool valid() const;
    const Json *find(const std::string &id) const;
};
struct Evaluation {
    std::vector<Stage> stages;
    std::vector<Calculation> calculations;
    std::vector<SheetSection> sections;
    std::vector<Message> messages;
    std::vector<RollRequest> rollRequests;
    std::vector<ResourceDefinition> resources;
    std::vector<ActionDefinition> actions;
    Json moduleData =
        Json::object(); // Edition-owned transition inputs; not character-sheet statistics.
    bool complete() const;
    const Calculation *find(const std::string &id) const;
};
struct EditionModule {
    std::string id;
    std::string name;
    std::string version;
    bool experimental = false;
    std::function<Evaluation(const CharacterDocument &, const ResolvedRuleset &)> evaluate;
    std::function<std::vector<Message>(const ContentPack &)> validateContent;
    std::function<std::vector<Message>(const ResolvedRuleset &)> validateRuleset;
    std::vector<PackPin> defaultPacks = {};
    std::function<TransitionResult(const CharacterDocument &, const ResolvedRuleset &,
                                   const CharacterCommand &)>
        applyCommand = {};
    std::function<void(const CharacterDocument &, const ResolvedRuleset &, Evaluation &)>
        prepareActions = {};
};

EditionModule bxModule();
EditionModule srd51Module();
EditionModule srd55Module();
EditionModule srd55FullModule();
const std::vector<EditionModule> &editions();
const EditionModule *findEdition(const std::string &id, const std::string &version = "");
Evaluation evaluate(const CharacterDocument &document, const ResolvedRuleset &ruleset);
TransitionResult executeCommand(const CharacterDocument &document, const ResolvedRuleset &ruleset,
                                const CharacterCommand &command);
CharacterDocument newCharacter(const std::string &edition, const std::string &version = "");
// Character options override campaign defaults per key, including explicit false.
// Retain malformed values so the edition can diagnose them without changing saved inputs.
Json effectiveCampaignOptions(const CharacterDocument &document);
struct MigrationResult {
    CharacterDocument document;
    std::vector<Message> messages;
    bool valid() const;
};
MigrationResult migrateCharacterVersion(const CharacterDocument &document,
                                        const std::string &version);
Json toJson(const CharacterDocument &document);
CharacterDocument documentFromJson(const Json &json);
Json toJson(const Evaluation &evaluation);
void addCalculation(Evaluation &result, std::string id, std::string label, Json value,
                    std::vector<std::string> steps, std::vector<SourceRef> sources = {});
SourceRef sourceFromJson(const Json &json);
int integerChoice(const Json &choices, const std::string &key, int fallback = 0);
std::string stringChoice(const Json &choices, const std::string &key,
                         const std::string &fallback = "");
} // namespace dnd
