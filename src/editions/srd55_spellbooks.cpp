#include "srd55_v2_internal.hpp"
#include "dnd/srd55_inventory.hpp"
#include <stdexcept>

namespace dnd::srd55v2 {
namespace {
const Json emptyArray = Json::array();
const Json& instances(const CharacterDocument& d) {
    const auto* value = at(d.resources, "/inventory/instances");
    return value && value->is_array() ? *value : emptyArray;
}
bool isBook(const Json& item, const ResolvedRuleset& rules) {
    const auto* definition = rules.find(text(item, "/itemId"));
    const auto* required = rules.find("srd55:spellbook");
    return definition && text(*definition, "/kind") == "gear" &&
        ((required && text(*definition, "/id") == text(*required, "/id")) || text(*definition, "/id") == "srd55:spellbook" ||
         text(*definition, "/baseProfile") == "srd55:spellbook");
}
bool accessible(const Json& item) {
    return text(item, "/status", "owned") == "owned" &&
        number(item, "/quantity") == 1 && text(item, "/location", "carried") == "carried";
}
const Json* findBook(const CharacterDocument& d, const std::string& id) {
    for (const auto& item : instances(d))
        if (text(item, "/id") == id) return &item;
    return nullptr;
}
Json& writableBook(CharacterDocument& d, const ResolvedRuleset& rules, const std::string& id) {
    auto& values = d.resources.at("inventory").at("instances");
    for (auto& item : values)
        if (text(item, "/id") == id) {
            if (!isBook(item, rules) || !accessible(item) || !item.contains("spellbook"))
                throw std::runtime_error("Choose an owned, carried, registered spellbook containing one physical book.");
            return item["spellbook"];
        }
    throw std::runtime_error("The exact spellbook instance is unavailable.");
}
int rank(const ResolvedRuleset& rules, const std::string& id) {
    const auto* spell = rules.find(id);
    if (!spell || text(*spell, "/kind") != "spell") return 0;
    const auto lists = strings(*spell, "/lists");
    return std::find(lists.begin(), lists.end(), "wizard") == lists.end() ? 0 : number(*spell, "/level");
}
std::set<std::string> setOf(const Json& value, const std::string& path) {
    const auto values = strings(value, path);
    return {values.begin(), values.end()};
}
int amount(const Json& input, const std::string& key, int minimum) {
    const auto* value = at(input, "/" + key);
    if (!value || !value->is_number_integer() || *value < minimum || *value > 1000000000)
        throw std::runtime_error(key + " must be a whole number of at least " + std::to_string(minimum) + ".");
    return value->get<int>();
}
void addReceipt(Json& book, const Json& receipt) {
    auto seen = setOf(book, "/spells");
    for (const auto& id : receipt.at("spells")) {
        if (!id.is_string() || !seen.insert(id.get<std::string>()).second)
            throw std::runtime_error("Choose each spell once, and omit spells already in the destination book.");
        book.at("spells").push_back(id);
    }
    book.at("receipts").push_back(receipt);
}
std::vector<Choice> bookOptions(const CharacterDocument& d, const ResolvedRuleset& rules,
                                bool registered) {
    std::vector<Choice> result;
    for (const auto& item : instances(d))
        if (isBook(item, rules) && accessible(item) && item.contains("spellbook") == registered)
            result.push_back({text(item, "/id"), "Spellbook — " + text(item, "/id"), true, {}, {ref("78")}});
    return result;
}
bool originalDisposed(const CharacterDocument& d, const std::string& id) {
    const auto* events = at(d.resources, "/inventory/events");
    if (events && events->is_array())
        for (const auto& event : *events)
            if (text(event, "/action") == "srd55.inventory.dispose" && text(event, "/instanceId") == id) return true;
    return false;
}
bool firstWizardLevelPending(const CharacterDocument& d, const ResolvedRuleset& rules) {
    if (!profileLevel(d.choices, rules, "wizard") || !d.advancement.is_array()) return false;
    for (auto it = d.advancement.rbegin(); it != d.advancement.rend(); ++it) {
        const auto kind = text(*it, "/kind");
        if (!kind.starts_with("srd55.history.")) continue;
        const auto* before = at(*it, "/before/choices");
        return kind == "srd55.history.begin-advance" && before && profileLevel(*before, rules, "wizard") == 0;
    }
    return false;
}
}

std::string originalWizardBookId(const CharacterDocument& d, const ResolvedRuleset& rules) {
    const auto granted = text(d.resources, "/inventory/wizardInitialBookId");
    if (!granted.empty()) {
        const auto* item = findBook(d, granted);
        return item && isBook(*item, rules) ? granted : "";
    }
    const auto* events = at(d.resources, "/inventory/events");
    int initialCount = 0;
    if (events && events->is_array())
        for (const auto& event : *events) if (text(event, "/action") == "srd55.inventory.initialize") {
            initialCount = number(event, "/itemCount"); break;
        }
    std::string result;
    for (const auto& item : instances(d)) if (isBook(item, rules)) {
        const auto id = text(item, "/id");
        if (!id.starts_with("item-")) continue;
        try { std::size_t used = 0; const int index = std::stoi(id.substr(5), &used);
            if (used != id.size() - 5 || index < 1 || index > initialCount) continue;
        } catch (...) { continue; }
        if (!result.empty()) return {}; // Ambiguous original identity must not grant free contents.
        result = id;
    }
    return result;
}

namespace {
const Json* wizardDefinition(const CharacterDocument& d, const ResolvedRuleset& rules) {
    const Json* definition = nullptr;
    const int total = number(d.choices, "/level", 1);
    for (int level = 1; level <= std::min(total, 20); ++level) {
        const bool multi = at(d.choices, "/multiclass") && *at(d.choices, "/multiclass") == true;
        const auto id = !multi || level == 1 ? text(d.choices, "/classId") : text(d.choices, "/advancement/" + std::to_string(level) + "/classId");
        if (classProfile(rules, id) == "wizard") { definition = rules.find(id); break; }
    }
    return definition;
}
int preparationLimit(const Json* definition, int classLevel) {
    if (classLevel < 1) return 0;
    const auto* row = definition ? at(*definition, "/casting/slots/" + std::to_string(classLevel - 1)) : nullptr;
    int highest = 0;
    if (row && row->is_array())
        for (int sl = 1; sl <= 9 && static_cast<std::size_t>(sl) <= row->size(); ++sl)
            if ((*row)[sl - 1].is_number_integer() && (*row)[sl - 1] > 0) highest = sl;
    return highest;
}

std::set<std::string> acquiredWizardSpells(const CharacterDocument& d, const ResolvedRuleset& rules,
                                            int level, const Json* definition, const Json* subclass) {
    std::set<std::string> result;
    auto learn = [&](const std::string& path, bool savant) {
        const auto* records = at(d.choices, path);
        if (!records || (!records->is_object() && !records->is_array())) return;
        if (savant && (!subclass || text(*subclass, "/rulesProfile") != "evoker")) return;
        for (const auto& [key, ids] : records->items()) {
            int acquired = 0;
            try { std::size_t end = 0; acquired = std::stoi(key, &end); if (end != key.size()) continue; }
            catch (...) { continue; }
            if (acquired < 1 || acquired > level || !ids.is_array()) continue;
            const int highest = preparationLimit(definition, acquired);
            if (savant && acquired != 3 && (acquired < 3 || highest <= preparationLimit(definition, acquired - 1))) continue;
            for (const auto& value : ids) {
                if (!value.is_string()) continue;
                const auto id = value.get<std::string>(); const int sl = rank(rules, id);
                if (sl > 0 && sl <= highest &&
                    (!savant || text(*rules.find(id), "/school") == "Evocation")) result.insert(id);
            }
        }
    };
    learn("/spellcasting/wizard/spellbook", false);
    learn("/spellcasting/wizard/savant", true);
    const auto* copies = at(d.choices, "/spellcasting/wizard/copiedSpells");
    if (copies && copies->is_array())
        for (const auto& copy : *copies) {
            const auto id = text(copy, "/spellId"); const int sl = rank(rules, id);
            if (sl > 0 && sl <= preparationLimit(definition, level) &&
                number(copy, "/paidCp", -1) >= sl * 5000 && number(copy, "/minutes", -1) >= sl * 120)
                result.insert(id);
        }
    return result;
}

} // namespace

int wizardPreparationLimit(const CharacterDocument& d, const ResolvedRuleset& rules, int classLevel) {
    return preparationLimit(wizardDefinition(d, rules),
        classLevel < 0 ? profileLevel(d.choices, rules, "wizard") : classLevel);
}

std::set<std::string> wizardAcquiredSpells(const CharacterDocument& d, const ResolvedRuleset& rules) {
    const auto* subclass = rules.find(text(d.choices, "/subclasses/wizard", text(d.choices, "/subclassId")));
    if (subclass && classProfile(rules, text(*subclass, "/classId")) != "wizard") subclass = nullptr;
    return acquiredWizardSpells(d, rules, profileLevel(d.choices, rules, "wizard"),
                                wizardDefinition(d, rules), subclass);
}

std::set<std::string> wizardAcquiredSpells(const Context& context) {
    // Feature evaluation receives already-resolved class identity and levels.
    // Do not reconstruct or default those from a possibly partial document.
    return acquiredWizardSpells(context.document, context.rules, profileLevel(context, "wizard"),
        context.rules.find(selectedClassId(context, "wizard")), selectedSubclass(context, "wizard"));
}

std::set<std::string> wizardRetainedSelection(const CharacterDocument& d, const std::string& path) {
    const Json* choices = &d.choices;
    if (d.resources.contains("wizardSpellbooks") && d.advancement.is_array())
        for (auto it = d.advancement.rbegin(); it != d.advancement.rend(); ++it) {
            const auto kind = text(*it, "/kind");
            if (!kind.starts_with("srd55.history.")) continue;
            const bool pending = kind == "srd55.history.begin-advance" || kind == "srd55.history.begin-change";
            const auto* accepted = at(*it, pending ? "/before/choices" : "/after/choices");
            if (accepted) choices = accepted;
            break;
        }
    const auto* value = at(*choices, path);
    if (value && value->is_string()) return {value->get<std::string>()};
    return setOf(*choices, path);
}

std::set<std::string> wizardPreparedSpells(const CharacterDocument& d, const ResolvedRuleset& rules) {
    const auto acquired = wizardAcquiredSpells(d, rules);
    auto result = wizardRetainedSelection(d, "/spellcasting/wizard/preparedSpells");
    const int level = profileLevel(d.choices, rules, "wizard");
    if (level >= 18)
        for (const auto* sl : {"1", "2"}) {
            const auto selected = wizardRetainedSelection(d, std::string("/features/wizard/spellMastery/") + sl);
            result.insert(selected.begin(), selected.end());
        }
    if (level >= 20) {
        const auto signature = wizardRetainedSelection(d, "/features/wizard/signatureSpells");
        result.insert(signature.begin(), signature.end());
    }
    std::erase_if(result, [&](const auto& id) { return !acquired.contains(id); });
    return result;
}

WizardBookState resolveWizardSpellbooks(const CharacterDocument& d, const ResolvedRuleset& rules) {
    WizardBookState out;
    const auto acquired = wizardAcquiredSpells(d, rules);
    out.prepared = wizardPreparedSpells(d, rules);
    const auto* tracking = at(d.resources, "/wizardSpellbooks");
    if (!tracking) {
        for (const auto& item : instances(d)) if (item.contains("spellbook"))
            out.messages.push_back({"error", "spellbook.physical.orphan", "/resources/wizardSpellbooks", "Restore the physical-book tracking record; removing it cannot restore lost contents.", {ref("78")}});
        out.accessibleSpells = acquired; out.destinationSpells = acquired; out.hasAccessibleBook = !acquired.empty();
        if (at(d.resources, "/inventory/initialized") && *at(d.resources, "/inventory/initialized") == true) {
            const auto* original = findBook(d, originalWizardBookId(d, rules));
            if ((!original || !accessible(*original)) && !firstWizardLevelPending(d, rules)) {
                out.hasAccessibleBook = false; out.accessibleSpells.clear(); out.destinationSpells.clear();
                if (profileLevel(d.choices, rules, "wizard"))
                    out.messages.push_back({"warning", "spellbook.legacy.unavailable", "/resources/inventory", "The original untracked spellbook is unavailable. Prepared spells are retained, but its contents do not supply rituals or study. Legacy book loss requires explicit migration; a blank replacement cannot receive historical contents for free.", {ref("78")}});
            }
        }
        return out;
    }
    out.tracked = true;
    auto fail = [&](const std::string& path, const std::string& message) {
        out.messages.push_back({"error", "spellbook.physical.invalid", path, message, {ref("78")}});
    };
    if (!tracking->is_object() || number(*tracking, "/version") != 1 ||
        !d.campaign.contains("srd55History") || !profileLevel(d.choices, rules, "wizard") ||
        !at(d.resources, "/inventory/initialized") || *at(d.resources, "/inventory/initialized") != true)
        fail("/resources/wizardSpellbooks", "Physical spellbooks require version 1, Wizard levels, and an accepted character history.");
    out.destinationId = text(*tracking, "/destinationId");
    const auto initialId = text(*tracking, "/initialInstanceId");
    bool initialFound = false, destinationFound = false;
    for (const auto& item : instances(d)) {
        if (!item.contains("spellbook")) continue;
        const auto id = text(item, "/id"), path = "/resources/inventory/instances/" + id + "/spellbook";
        initialFound = initialFound || id == initialId;
        destinationFound = destinationFound || id == out.destinationId;
        const auto& book = item["spellbook"];
        const auto* spells = at(book, "/spells"); const auto* receipts = at(book, "/receipts");
        if (!isBook(item, rules) || number(item, "/quantity", -1) < 0 || number(item, "/quantity") > 1 ||
            number(book, "/version") != 1 || !spells || !spells->is_array() || !receipts || !receipts->is_array()) {
            fail(path, "A registered spellbook must be one physical book with versioned contents and copying receipts."); continue;
        }
        std::set<std::string> contents, recorded;
        for (const auto& value : *spells)
            if (!value.is_string() || !acquired.contains(value.get<std::string>()) || !contents.insert(value.get<std::string>()).second)
                fail(path + "/spells", "Book contents must be distinct, available Wizard spells supported by preserved acquisition history.");
        for (std::size_t i = 0; i < receipts->size(); ++i) {
            const auto& receipt = (*receipts)[i]; const auto kind = text(receipt, "/kind");
            const auto* ids = at(receipt, "/spells"); int levels = 0;
            if (!ids || !ids->is_array() || ids->empty()) { fail(path + "/receipts", "A book receipt must record the spells actually written."); continue; }
            for (const auto& value : *ids)
                if (!value.is_string() || !recorded.insert(value.get<std::string>()).second)
                    fail(path + "/receipts", "Each written spell must occur exactly once in the book's receipts.");
                else levels += rank(rules, value.get<std::string>());
            if (kind == "initial") {
                if (id != initialId || i != 0) fail(path, "Only the original initialized instance receives the initial spellbook grant.");
            } else if (kind == "advancement") {
                if (number(receipt, "/classLevel") < 2 || number(receipt, "/classLevel") > profileLevel(d.choices, rules, "wizard"))
                    fail(path, "A level-grant receipt must identify a reached Wizard class level.");
            } else if (kind == "copy" || kind == "external") {
                const int rate = kind == "copy" ? 1 : 2;
                if (number(receipt, "/minutes", -1) < levels * 60 * rate ||
                    number(receipt, "/paidCp", -1) < levels * (rate == 1 ? 1000 : 5000) || text(receipt, "/sourceId").empty())
                    fail(path, "Copying receipts require their source and the published time and material cost.");
            } else fail(path, "Unsupported spellbook receipt kind.");
        }
        if (recorded != contents) fail(path, "Physical contents must match the preserved writing receipts.");
        if (accessible(item)) {
            out.hasAccessibleBook = true;
            out.accessibleSpells.insert(contents.begin(), contents.end());
            if (id == out.destinationId) { out.destinationAccessible = true; out.destinationSpells = contents; }
        }
    }
    if (!initialFound || !destinationFound) fail("/resources/wizardSpellbooks", "Preserve the exact original and destination book instances, including lost or destroyed records.");
    // Pending advancement may offer its newly researched spells for preparation.
    // The commit action, not evaluation, writes those grants into the destination.
    if (out.destinationAccessible && d.advancement.is_array())
        for (auto it = d.advancement.rbegin(); it != d.advancement.rend(); ++it) {
            const auto kind = text(*it, "/kind");
            if (!kind.starts_with("srd55.history.")) continue;
            if (kind == "srd55.history.begin-advance") {
                auto old = d; const auto* choices = at(*it, "/before/choices");
                if (choices) { old.choices = *choices; const auto previous = wizardAcquiredSpells(old, rules);
                    for (const auto& id : acquired) if (!previous.contains(id)) out.accessibleSpells.insert(id); }
            }
            break;
        }
    return out;
}

void appendWizardSpellbookState(const CharacterDocument& d, const ResolvedRuleset& rules, Evaluation& e) {
    const auto state = resolveWizardSpellbooks(d, rules);
    e.messages.insert(e.messages.end(), state.messages.begin(), state.messages.end());
    if (!state.tracked) return;
    Json rows = Json::array();
    for (const auto& item : instances(d)) if (item.contains("spellbook")) {
        Json names = Json::array();
        for (const auto& id : strings(item["spellbook"], "/spells")) {
            const auto* spell = rules.find(id); names.push_back(spell ? text(*spell, "/name", id) : id);
        }
        rows.push_back({{"Book", text(item, "/id")}, {"Status", text(item, "/status")},
                        {"Location", text(item, "/location")}, {"Available for study", accessible(item)}, {"Spells", names}});
    }
    addCalculation(e, "wizard.physicalSpellbooks", "Physical Wizard spellbooks", rows,
        {"Each book retains its own written spells. Lost and stored books do not supply readable spells; currently prepared spells remain prepared.",
         "New research and external copies go to " + state.destinationId + ". Own-book copying and prepared-spell reconstruction cost 1 hour and 10 GP per spell level."}, {ref("78"), ref("79"), ref("104")});
    e.sections.push_back({"Physical spellbooks", {"wizard.physicalSpellbooks"}, {}});
    if (!state.hasAccessibleBook)
        issue(e, "spellbook.physical.unavailable", "/resources/wizardSpellbooks", "No registered spellbook is carried. Retained prepared spells remain usable; book rituals, book study, and new book selections require an accessible book.", "78-79", "warning");
}

void appendWizardSpellbookActions(const CharacterDocument& d, const ResolvedRuleset& rules, Evaluation& e) {
    if (!profileLevel(d.choices, rules, "wizard")) return;
    const auto state = resolveWizardSpellbooks(d, rules);
    const bool valid = e.complete() && d.campaign.contains("srd55History");
    auto add = [&](const std::string& id, const std::string& name, const std::string& description,
                   std::vector<Field> fields, bool available) {
        e.actions.push_back({"srd55.spellbooks." + id, name, description, fields, valid && available,
            !valid ? "Accept the completed character history and resolve its validation errors first." : "Acquire and carry the required individual spellbook first.", {ref("78"), ref("79"), ref("104")}});
    };
    auto blank = bookOptions(d, rules, false), written = bookOptions(d, rules, true);
    if (!state.tracked) {
        const auto originalId = originalWizardBookId(d, rules);
        std::erase_if(blank, [&](const auto& option) { return option.id != originalId || originalDisposed(d, originalId); });
        add("initialize", "Track physical Wizard spellbooks", "Bind the preserved initial and learned spells to one exact owned book once. Later backups receive only spells actually copied.",
            {select("/instanceId", "Original spellbook", blank)}, !blank.empty());
        if (blank.empty()) e.actions.back().reason = "Tracking must begin with the original carried inventory-grant book before any recorded disposal. Legacy lost-book contents cannot be inferred or granted to a replacement.";
        return;
    }
    add("register", "Register a blank spellbook", "Designate one acquired book as blank. Its purchase or acquisition uses the owned-inventory action; no replacement price is invented.",
        {select("/instanceId", "Acquired blank book", blank)}, !blank.empty());
    add("destination", "Choose the book for new Wizard research", "Choose the carried book receiving level grants and external copying. Other books retain independent contents.",
        {select("/instanceId", "Destination spellbook", written)}, !written.empty());
    auto sources = written;
    const bool lost = std::any_of(instances(d).begin(), instances(d).end(), [](const auto& item) {
        return item.contains("spellbook") && text(item, "/status", "owned") != "owned";
    });
    sources.push_back({"prepared", "Reconstruct currently prepared Wizard spells", lost, "Requires a recorded book loss; copy an accessible owned book otherwise.", {ref("78")}});
    std::vector<Choice> spells;
    auto selectable = state.accessibleSpells; selectable.insert(state.prepared.begin(), state.prepared.end());
    for (const auto& id : selectable)
        if (const auto* spell = rules.find(id)) spells.push_back({id, text(*spell, "/name", id), true, {}, {sourceFromJson(spell->at("source"))}});
    add("copy", "Copy or reconstruct an owned spellbook", "Copy selected spells from an accessible owned book, or reconstruct currently prepared Wizard spells after book loss. Total cost is 10 GP and one hour per copied spell level; the preview records the exact contents and payment together.",
        {select("/sourceId", "Copying source", sources), select("/destinationId", "Destination spellbook", written),
         select("/spells", "Spells to write", spells, true), integer("/minutes", "Minutes spent copying", 0, 1000000),
         integer("/paidCp", "Materials paid (copper pieces; 1 GP = 100 cp)", 0, 1000000000)}, !written.empty() && !spells.empty());
}

void recordWizardBookExternalCopy(CharacterDocument& d, const ResolvedRuleset& rules,
                                  const std::string& spell, int minutes, int paidCp, const std::string& source) {
    if (!d.resources.contains("wizardSpellbooks")) return;
    auto& book = writableBook(d, rules, text(d.resources, "/wizardSpellbooks/destinationId"));
    addReceipt(book, {{"kind", "external"}, {"spells", Json::array({spell})},
                     {"sourceId", source}, {"minutes", minutes}, {"paidCp", paidCp}});
}

void recordWizardBookAdvancement(const CharacterDocument& before, CharacterDocument& after, const ResolvedRuleset& rules) {
    if (!profileLevel(before.choices, rules, "wizard") && profileLevel(after.choices, rules, "wizard") &&
        at(after.resources, "/inventory/initialized") && *at(after.resources, "/inventory/initialized") == true) {
        auto granted = applySrd55InventoryCommand(after, rules, {"srd55.inventory.acquire",
            {{"itemId", "srd55:spellbook"}, {"quantity", 1}, {"source", "gift"}, {"paidCp", 0}, {"reason", "Initial spellbook granted by Wizard Spellcasting, SRD 78"}}});
        if (!granted.valid()) throw std::runtime_error("The first Wizard level could not record its initial spellbook grant.");
        after = std::move(granted.document);
        after.resources["inventory"]["wizardInitialBookId"] = after.resources["inventory"]["instances"].back().at("id");
    }
    if (!after.resources.contains("wizardSpellbooks")) return;
    const auto previous = wizardAcquiredSpells(before, rules), next = wizardAcquiredSpells(after, rules);
    Json added = Json::array();
    for (const auto& id : next) if (!previous.contains(id)) added.push_back(id);
    if (added.empty()) return;
    auto& book = writableBook(after, rules, text(after.resources, "/wizardSpellbooks/destinationId"));
    addReceipt(book, {{"kind", "advancement"}, {"classLevel", profileLevel(after.choices, rules, "wizard")}, {"spells", added}});
}

TransitionResult applyWizardSpellbookCommand(const CharacterDocument& original, const ResolvedRuleset& rules, const CharacterCommand& command) {
    TransitionResult result{original, {}};
    try {
        const auto state = resolveWizardSpellbooks(original, rules);
        if (!evaluate(original, rules).complete() || !original.campaign.contains("srd55History"))
            throw std::runtime_error("Accept a valid character history before tracking physical spellbooks.");
        auto& d = result.document; const auto& input = command.inputs;
        const auto id = text(input, "/instanceId");
        if (command.id == "srd55.spellbooks.initialize" || command.id == "srd55.spellbooks.register") {
            const bool initial = command.id == "srd55.spellbooks.initialize";
            if (initial == state.tracked) throw std::runtime_error("Physical spellbooks can be initialized only once; register later books as blank.");
            if (initial && (id != originalWizardBookId(original, rules) || originalDisposed(original, id)))
                throw std::runtime_error("Initialize only the original inventory-grant book before any recorded disposal; replacement books cannot receive historical spells for free.");
            bool found = false;
            for (auto& item : d.resources.at("inventory").at("instances")) if (text(item, "/id") == id) {
                if (!isBook(item, rules) || !accessible(item) || item.contains("spellbook"))
                    throw std::runtime_error("Choose one owned, carried, unregistered spellbook.");
                item["spellbook"] = {{"version", 1}, {"spells", Json::array()}, {"receipts", Json::array()}};
                if (initial) addReceipt(item["spellbook"], {{"kind", "initial"}, {"spells", Json(wizardAcquiredSpells(d, rules))}});
                found = true;
            }
            if (!found) throw std::runtime_error("The chosen spellbook instance is unavailable.");
            if (initial) d.resources["wizardSpellbooks"] = {{"version", 1}, {"initialInstanceId", id}, {"destinationId", id}};
        } else if (command.id == "srd55.spellbooks.destination") {
            (void)writableBook(d, rules, id);
            d.resources["wizardSpellbooks"]["destinationId"] = id;
        } else if (command.id == "srd55.spellbooks.copy") {
            const auto sourceId = text(input, "/sourceId"), destinationId = text(input, "/destinationId");
            if (sourceId == destinationId) throw std::runtime_error("Source and destination must be different physical books.");
            std::set<std::string> available;
            if (sourceId == "prepared") {
                bool lost = false;
                for (const auto& item : instances(original))
                    if (item.contains("spellbook") && text(item, "/status", "owned") != "owned") lost = true;
                if (!lost) throw std::runtime_error("Prepared-spell reconstruction requires a recorded lost, disposed, or destroyed spellbook; copy an accessible owned book otherwise.");
                available = state.prepared;
            } else {
                const auto* source = findBook(original, sourceId);
                if (!source || !accessible(*source) || !source->contains("spellbook")) throw std::runtime_error("The source book must be registered, owned, and carried.");
                available = setOf((*source)["spellbook"], "/spells");
            }
            const auto* selected = at(input, "/spells"); int levels = 0; std::set<std::string> seen;
            if (!selected || !selected->is_array() || selected->empty()) throw std::runtime_error("Choose at least one spell to copy.");
            for (const auto& value : *selected) {
                if (!value.is_string() || !available.contains(value.get<std::string>()) || !seen.insert(value.get<std::string>()).second)
                    throw std::runtime_error("Choose each spell once from the exact source book or retained Wizard preparations.");
                levels += rank(rules, value.get<std::string>());
            }
            const int minutes = amount(input, "minutes", levels * 60), paid = amount(input, "paidCp", levels * 1000);
            auto& book = writableBook(d, rules, destinationId);
            const auto* money = at(d.resources, "/currencyCp");
            if (!money || !money->is_number_integer() || *money < paid) throw std::runtime_error("Insufficient current funds for the copying materials.");
            addReceipt(book, {{"kind", "copy"}, {"sourceId", sourceId}, {"spells", *selected}, {"minutes", minutes}, {"paidCp", paid}});
            d.resources["currencyCp"] = money->get<long long>() - paid;
            d.resources["lifecycle"]["restWindow"] = "";
        } else throw std::runtime_error("Unsupported physical spellbook action.");
        const auto checked = evaluate(d, rules);
        if (!checked.complete()) {
            for (const auto& message : checked.messages) if (message.severity == "error") result.messages.push_back(message);
            result.document = original;
        }
    } catch (const std::exception& error) {
        result.document = original;
        result.messages.push_back({"error", "spellbook.command.invalid", "/resources/wizardSpellbooks", error.what(), {ref("78")}});
    }
    return result;
}
} // namespace dnd::srd55v2
