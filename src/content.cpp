#include "dnd/content.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include <set>

namespace dnd {
namespace {
void error(std::vector<Message>& messages, const std::string& code, const std::string& text) {
    messages.push_back({"error", code, "/packs", text, {}});
}
bool errors(const std::vector<Message>& messages) {
    return std::any_of(messages.begin(), messages.end(), [](const auto& m) { return m.severity == "error"; });
}
bool stringField(const Json& j, const char* key) { return j.is_object() && j.contains(key) && j[key].is_string() && !j[key].get<std::string>().empty(); }
Json readJson(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 16 * 1024 * 1024)
        throw std::runtime_error("Missing, nonregular, or oversized JSON file: " + path.filename().string());
    std::ifstream input(path);
    return Json::parse(input);
}
const std::regex packId("^[a-z][a-z0-9-]*$");
const std::regex version("^[0-9]+\\.[0-9]+\\.[0-9]+$");
const std::regex contentId("^[a-z][a-z0-9-]*:[a-zA-Z0-9_.:-]+$");

void fieldError(std::vector<Message>& messages, const std::string& code, const std::string& path,
                const std::string& text) {
    messages.push_back({"error", code, path, text, {}});
}
std::string manifestPath(const Json& manifest, const std::string& fallback = "manifest") {
    return "/packs/" + (stringField(manifest, "id") ? manifest["id"].get<std::string>() : fallback);
}
void validateSource(const Json& source, const std::string& path, bool requirePage,
                    std::vector<Message>& messages) {
    if (!source.is_object()) {
        fieldError(messages, "pack.source", path, "Source reference must be an object."); return;
    }
    if (!stringField(source, "publication"))
        fieldError(messages, "pack.source", path + "/publication", "Source publication must be nonempty text.");
    if (requirePage && !stringField(source, "page"))
        fieldError(messages, "pack.source", path + "/page", "Content source page must be nonempty text.");
    for (const auto* key : {"page", "url"})
        if (source.contains(key) && !source[key].is_string())
            fieldError(messages, "pack.source", path + "/" + key, "Source metadata must be text.");
}
void validateManifest(const Json& manifest, const std::string& path, std::vector<Message>& messages) {
    if (!manifest.is_object()) {
        fieldError(messages, "pack.manifest", path, "Manifest must be an object."); return;
    }
    if (!manifest.contains("schemaVersion") || !manifest["schemaVersion"].is_number_integer() || manifest["schemaVersion"] != 1)
        fieldError(messages, "pack.schema", path + "/schemaVersion", "Unsupported pack schema version.");
    for (const auto* key : {"id", "version", "edition", "name", "publisher", "origin"})
        if (!stringField(manifest, key)) fieldError(messages, "pack.manifest", path + "/" + key, "Manifest requires nonempty text.");
    if (stringField(manifest, "id") && !std::regex_match(manifest["id"].get<std::string>(), packId))
        fieldError(messages, "pack.id", path + "/id", "Invalid pack identifier.");
    if (stringField(manifest, "version") && !std::regex_match(manifest["version"].get<std::string>(), version))
        fieldError(messages, "pack.version", path + "/version", "Pack version must be an exact major.minor.patch version.");
    if (stringField(manifest, "origin") && manifest["origin"] != "official" && manifest["origin"] != "third-party" && manifest["origin"] != "homebrew")
        fieldError(messages, "pack.origin", path + "/origin", "Unknown pack origin.");
    if (!manifest.contains("license") || !manifest["license"].is_object())
        fieldError(messages, "pack.license", path + "/license", "License must be an object containing id and text.");
    else for (const auto* key : {"id", "text"})
        if (!stringField(manifest["license"], key))
            fieldError(messages, "pack.license", path + "/license/" + key, "License field must be nonempty text.");
    if (!manifest.contains("sources") || !manifest["sources"].is_array() || manifest["sources"].empty())
        fieldError(messages, "pack.sources", path + "/sources", "Source references are required.");
    else for (std::size_t index = 0; index < manifest["sources"].size(); ++index)
        validateSource(manifest["sources"][index], path + "/sources/" + std::to_string(index), false, messages);
    for (const auto* key : {"dependencies", "conflicts", "dataFiles"})
        if (!manifest.contains(key) || !manifest[key].is_array()) fieldError(messages, "pack.manifest", path + "/" + key, "Manifest requires an array.");
    if (manifest.contains("dependencies") && manifest["dependencies"].is_array())
        for (std::size_t index = 0; index < manifest["dependencies"].size(); ++index) {
            const auto& dep = manifest["dependencies"][index];
            const auto itemPath = path + "/dependencies/" + std::to_string(index);
            if (!dep.is_object()) { fieldError(messages, "pack.dependency", itemPath, "Dependency must be an object."); continue; }
            if (!stringField(dep, "id") || !std::regex_match(dep["id"].get<std::string>(), packId))
                fieldError(messages, "pack.dependency", itemPath + "/id", "Dependency requires a pack identifier.");
            if (!stringField(dep, "version") || !std::regex_match(dep["version"].get<std::string>(), version))
                fieldError(messages, "pack.dependency", itemPath + "/version", "Dependency requires an exact version.");
        }
    if (manifest.contains("conflicts") && manifest["conflicts"].is_array())
        for (std::size_t index = 0; index < manifest["conflicts"].size(); ++index)
            if (!manifest["conflicts"][index].is_string()) fieldError(messages, "pack.conflict", path + "/conflicts/" + std::to_string(index), "Conflicts must contain pack identifiers.");
    if (manifest.contains("moduleVersions") && (!manifest["moduleVersions"].is_array() || manifest["moduleVersions"].empty() || !std::all_of(manifest["moduleVersions"].begin(), manifest["moduleVersions"].end(), [](const Json& value) { return value.is_string() && std::regex_match(value.get<std::string>(), version); })))
        fieldError(messages, "pack.module_versions", path + "/moduleVersions", "moduleVersions must be a nonempty array of exact versions.");
    if (manifest.contains("dataFiles") && manifest["dataFiles"].is_array()) {
        if (manifest["dataFiles"].empty()) fieldError(messages, "pack.files", path + "/dataFiles", "At least one data file is required.");
        std::set<std::string> files;
        for (std::size_t index = 0; index < manifest["dataFiles"].size(); ++index) {
            const auto& file = manifest["dataFiles"][index]; const auto itemPath = path + "/dataFiles/" + std::to_string(index);
            if (!file.is_string()) { fieldError(messages, "pack.file", itemPath, "Data file names must be strings."); continue; }
            const auto name = file.get<std::string>(); const std::filesystem::path relative(name);
            if (relative.empty() || relative.is_absolute() || relative.extension() != ".json" || name.find("..") != std::string::npos || !files.insert(name).second)
                fieldError(messages, "pack.path", itemPath, "Data files must be unique relative JSON paths inside the pack.");
        }
    }
}
void validateEntries(const ContentPack& pack, std::vector<Message>& messages) {
    std::set<std::string> ids;
    const auto root = manifestPath(pack.manifest);
    for (std::size_t index = 0; index < pack.entries.size(); ++index) {
        const auto& entry = pack.entries[index];
        const auto path = root + "/" + (stringField(entry, "id") ? entry["id"].get<std::string>() : "entries/" + std::to_string(index));
        if (!entry.is_object()) { fieldError(messages, "pack.entry", path, "Content entry must be an object."); continue; }
        for (const auto* key : {"id", "kind", "name"})
            if (!stringField(entry, key)) fieldError(messages, "pack.entry", path + "/" + key, "Content entry requires nonempty text.");
        if (stringField(entry, "id")) {
            const auto id = entry["id"].get<std::string>();
            if (!std::regex_match(id, contentId)) fieldError(messages, "pack.namespace", path + "/id", "Content identifier must be namespaced.");
            if (!ids.insert(id).second) fieldError(messages, "pack.duplicate", path + "/id", "Duplicate content identifier: " + id);
        }
        if (!entry.contains("source")) fieldError(messages, "pack.source", path + "/source", "Content requires a publication and page reference.");
        else validateSource(entry["source"], path + "/source", true, messages);
        if (entry.contains("replaces") && (!entry["replaces"].is_string() || !std::regex_match(entry["replaces"].get<std::string>(), contentId)))
            fieldError(messages, "pack.replacement", path + "/replaces", "Replacement must name a content identifier.");
    }
}
void validateMechanics(const EditionModule& module, const ContentPack& pack, std::vector<Message>& messages) {
    if (!module.validateContent) return;
    try {
        const auto result = module.validateContent(pack);
        messages.insert(messages.end(), result.begin(), result.end());
    } catch (const std::exception& exception) {
        // Isolate a throwing record only on the failure path. A module may
        // validate relationships across entries, so keep the normal whole-pack call.
        bool located = false;
        for (const auto& entry : pack.entries) {
            ContentPack single; single.manifest = pack.manifest; single.directory = pack.directory; single.entries = {entry};
            try { (void)module.validateContent(single); }
            catch (const std::exception& detail) {
                fieldError(messages, "pack.mechanics", manifestPath(pack.manifest) + "/" + entry.at("id").get<std::string>(), "Mechanical payload validation failed: " + std::string(detail.what()));
                located = true;
            }
        }
        if (!located) fieldError(messages, "pack.mechanics", manifestPath(pack.manifest), "Mechanical payload validation failed: " + std::string(exception.what()));
    }
}
}
bool PackLoadResult::valid() const { return !errors(messages); }
PackLoadResult loadPack(const std::filesystem::path& directory) {
    PackLoadResult result;
    result.pack.directory = directory.string();
    try {
        auto& manifest = result.pack.manifest;
        manifest = readJson(directory / "manifest.json");
        validateManifest(manifest, manifestPath(manifest), result.messages);
        if (!result.valid()) return result;
        const auto base = std::filesystem::canonical(directory);
        for (const auto& file : manifest["dataFiles"]) {
            const auto name = file.get<std::string>();
            const std::filesystem::path relative(name);
            const auto full = std::filesystem::weakly_canonical(base / relative);
            const auto rel = full.lexically_relative(base);
            if (rel.empty() || *rel.begin() == "..") { error(result.messages, "pack.path", "A data path leaves the pack directory."); continue; }
            auto data = readJson(full);
            if (!data.is_array()) { error(result.messages, "pack.data", "Data file must contain an array of content entries."); continue; }
            for (const auto& entry : data) result.pack.entries.push_back(entry);
        }
        validateEntries(result.pack, result.messages);
        if (result.valid()) {
            for (const auto& supported : manifest.value("moduleVersions", Json::array({"1.0.0"}))) {
            const auto* module = findEdition(manifest["edition"].get<std::string>(), supported.get<std::string>());
            if (!module) error(result.messages, "pack.edition", "No implementation supports this pack's exact edition module version.");
            else validateMechanics(*module, result.pack, result.messages);
            }
        }
    } catch (const std::exception& e) { error(result.messages, "pack.read", e.what()); }
    return result;
}
std::vector<ContentPack> loadPackDirectory(const std::filesystem::path& root, std::vector<Message>& messages) {
    std::vector<ContentPack> packs;
    if (!std::filesystem::exists(root)) return packs;
    std::vector<std::filesystem::path> directories;
    for (const auto& item : std::filesystem::directory_iterator(root)) if (item.is_directory() && std::filesystem::exists(item.path() / "manifest.json")) directories.push_back(item.path());
    std::sort(directories.begin(), directories.end());
    for (const auto& directory : directories) {
        auto result = loadPack(directory);
        messages.insert(messages.end(), result.messages.begin(), result.messages.end());
        if (result.valid()) packs.push_back(std::move(result.pack));
    }
    return packs;
}
ResolvedRuleset resolveRuleset(const CharacterDocument& document, const std::vector<ContentPack>& available) {
    ResolvedRuleset result;
    result.edition = document.edition; result.moduleVersion = document.moduleVersion;
    const auto* module = findEdition(document.edition, document.moduleVersion);
    if (!module || module->version != document.moduleVersion) error(result.messages, "module.missing", "Exact edition module " + document.edition + " " + document.moduleVersion + " is unavailable.");
    std::map<std::string, const ContentPack*> selected;
    for (const auto& pin : document.packs) {
        if (selected.contains(pin.id)) { error(result.messages, "pack.pin_duplicate", "Pack selected twice: " + pin.id); continue; }
        const ContentPack* found = nullptr;
        for (std::size_t index = 0; index < available.size(); ++index) {
            const auto& pack = available[index];
            if (!stringField(pack.manifest, "id") || !stringField(pack.manifest, "version")) {
                validateManifest(pack.manifest, manifestPath(pack.manifest, "available/" + std::to_string(index)), result.messages);
                continue;
            }
            if (pack.manifest["id"] == pin.id && pack.manifest["version"] == pin.version) {
                if (found) error(result.messages, "pack.ambiguous", "More than one installed pack matches " + pin.id + " " + pin.version);
                found = &pack;
            }
        }
        if (!found) { error(result.messages, "pack.missing", "Missing exact pack " + pin.id + " " + pin.version + "; selections retained."); continue; }
        std::vector<Message> manifestMessages;
        validateManifest(found->manifest, manifestPath(found->manifest), manifestMessages);
        result.messages.insert(result.messages.end(), manifestMessages.begin(), manifestMessages.end());
        if (errors(manifestMessages)) continue;
        if (found->manifest.value("edition", "") != document.edition) error(result.messages, "pack.edition", "Incompatible edition in pack " + pin.id);
        const auto supportedVersions = found->manifest.value("moduleVersions", Json::array({"1.0.0"}));
        if (std::find(supportedVersions.begin(), supportedVersions.end(), Json(document.moduleVersion)) == supportedVersions.end()) error(result.messages, "pack.module_version", "Pack " + pin.id + " " + pin.version + " does not support module " + document.moduleVersion);
        selected.emplace(pin.id, found);
    }
    if (document.packs.empty()) error(result.messages, "pack.none", "Select a content pack to evaluate the character.");
    // Resolution is also a public entry point for in-memory packs. Do not rely on
    // a caller having loaded or validated every selected pack beforehand.
    if (result.valid()) {
        for (const auto& [id, pack] : selected) {
            (void)id;
            std::vector<Message> entryMessages;
            validateEntries(*pack, entryMessages);
            result.messages.insert(result.messages.end(), entryMessages.begin(), entryMessages.end());
            if (!errors(entryMessages) && module) validateMechanics(*module, *pack, result.messages);
        }
    }
    if (!result.valid()) return result;
    std::map<std::string, std::string> replacements;
    std::map<std::string, const ContentPack*> owners;
    for (const auto& [id, pack] : selected) {
        for (const auto& dep : pack->manifest.value("dependencies", Json::array())) {
            const auto depId = dep.value("id", "");
            if (!selected.contains(depId) || selected.at(depId)->manifest.value("version", "") != dep.value("version", "")) error(result.messages, "pack.dependency", "Pack " + id + " requires enabled " + depId + " " + dep.value("version", ""));
        }
        for (const auto& conflict : pack->manifest.value("conflicts", Json::array())) if (selected.contains(conflict.get<std::string>())) error(result.messages, "pack.conflict", "Pack " + id + " conflicts with " + conflict.get<std::string>());
        result.packs.push_back(*pack);
        for (const auto& entry : pack->entries) {
            const auto entryId = entry.at("id").get<std::string>();
            if (!result.content.emplace(entryId, entry).second) error(result.messages, "content.duplicate", "Duplicate enabled content identifier: " + entryId);
            owners.emplace(entryId, pack);
            if (entry.contains("replaces")) {
                const auto target = entry["replaces"].get<std::string>();
                if (!replacements.emplace(target, entryId).second) error(result.messages, "content.conflict", "Conflicting replacements for " + target);
            }
        }
    }
    // Reject dependency cycles, including self-dependencies, independent of install order.
    std::map<std::string, int> visit;
    std::function<void(const std::string&)> walk = [&](const std::string& id) {
        if (visit[id] == 1) { error(result.messages, "pack.cycle", "Dependency cycle at " + id); return; }
        if (visit[id] == 2) return;
        visit[id] = 1;
        for (const auto& dep : selected.at(id)->manifest.value("dependencies", Json::array())) { const auto child = dep.value("id", ""); if (selected.contains(child)) walk(child); }
        visit[id] = 2;
    };
    for (const auto& [id, pack] : selected) { (void)pack; walk(id); }
    for (const auto& [target, replacement] : replacements) {
        if (!result.content.contains(target)) error(result.messages, "content.replacement_missing", "Replacement target is unavailable: " + target);
        else if (replacements.contains(replacement) || target == replacement) error(result.messages, "content.replacement_chain", "Replacement chains and cycles are unsupported: " + target);
        else if (result.content.at(target).at("kind") != result.content.at(replacement).at("kind")) {
            result.messages.push_back({"error", "content.replacement_kind",
                "/packs/" + owners.at(replacement)->manifest.at("id").get<std::string>() + "/" + replacement + "/replaces",
                "Replacement '" + replacement + "' must retain the " + result.content.at(target).at("kind").get<std::string>() + " kind required by '" + target + "'.", {}});
        } else if (module && module->validateContent) {
            // Validate the replacement in its effective target role before
            // making any aliases available. Same-kind entries can still have
            // different supported mechanical schemas (for example, tables).
            ContentPack effective;
            effective.manifest = owners.at(replacement)->manifest;
            effective.entries.push_back(result.content.at(replacement));
            effective.entries.back()["id"] = target;
            validateMechanics(*module, effective, result.messages);
        }
    }
    if (!result.valid()) return result;
    for (const auto& [target, replacement] : replacements) {
        auto entry = result.content.at(replacement);
        entry["replacementId"] = replacement;
        entry["id"] = target;
        result.content[target] = std::move(entry);
        result.content.erase(replacement);
    }
    if (result.valid() && module && module->validateRuleset) {
        try {
            const auto references = module->validateRuleset(result);
            result.messages.insert(result.messages.end(), references.begin(), references.end());
        } catch (const std::exception& exception) {
            error(result.messages, "content.references", "Resolved reference validation failed: " + std::string(exception.what()));
        }
    }
    return result;
}
std::vector<Message> installPack(const std::filesystem::path& source, const std::filesystem::path& destinationRoot,
                               const std::vector<ContentPack>& available) {
    auto loaded = loadPack(source);
    if (!loaded.valid()) return loaded.messages;
    auto catalog = available;
    if (catalog.empty()) catalog = loadPackDirectory(destinationRoot, loaded.messages);
    if (errors(loaded.messages)) return loaded.messages;
    catalog.push_back(loaded.pack);
    for (std::size_t index = 0; index < catalog.size(); ++index)
        validateManifest(catalog[index].manifest, manifestPath(catalog[index].manifest, "available/" + std::to_string(index)), loaded.messages);
    if (errors(loaded.messages)) return loaded.messages;
    CharacterDocument check;
    check.edition = loaded.pack.manifest["edition"].get<std::string>();
    check.moduleVersion = loaded.pack.manifest.value("moduleVersions", Json::array({"1.0.0"})).front().get<std::string>();
    std::set<std::string> pinned;
    std::function<void(const ContentPack&)> pin = [&](const ContentPack& pack) {
        const auto id = pack.manifest["id"].get<std::string>();
        if (!pinned.insert(id).second) return;
        check.packs.push_back({id, pack.manifest["version"].get<std::string>()});
        for (const auto& dependency : pack.manifest["dependencies"]) {
            const auto it = std::find_if(catalog.begin(), catalog.end(), [&](const auto& p) {
                return p.manifest["id"] == dependency["id"] && p.manifest["version"] == dependency["version"];
            });
            if (it != catalog.end()) pin(*it);
        }
    };
    pin(loaded.pack);
    std::vector<Message> resolutionErrors;
    for (const auto& supported : loaded.pack.manifest.value("moduleVersions", Json::array({"1.0.0"}))) {
        check.moduleVersion = supported.get<std::string>();
        const auto resolved = resolveRuleset(check, catalog);
        if (!resolved.valid()) resolutionErrors.insert(resolutionErrors.end(), resolved.messages.begin(), resolved.messages.end());
    }
    if (!resolutionErrors.empty()) return resolutionErrors;
    const auto name = loaded.pack.manifest["id"].get<std::string>() + "-" + loaded.pack.manifest["version"].get<std::string>();
    const auto destination = destinationRoot / name;
    const auto staging = destinationRoot / ("." + name + ".importing");
    try {
        std::filesystem::create_directories(destinationRoot);
        if (std::filesystem::exists(destination) || std::filesystem::exists(staging)) throw std::runtime_error("Pack version or unfinished import already exists: " + name);
        std::filesystem::create_directory(staging);
        std::filesystem::copy_file(source / "manifest.json", staging / "manifest.json");
        for (const auto& file : loaded.pack.manifest["dataFiles"]) {
            const auto relative = std::filesystem::path(file.get<std::string>());
            std::filesystem::create_directories((staging / relative).parent_path());
            std::filesystem::copy_file(source / relative, staging / relative);
        }
        auto checked = loadPack(staging);
        if (!checked.valid()) { std::filesystem::remove_all(staging); return checked.messages; }
        std::filesystem::rename(staging, destination);
    } catch (const std::exception& e) { error(loaded.messages, "pack.install", e.what()); }
    return loaded.messages;
}
} // namespace dnd
