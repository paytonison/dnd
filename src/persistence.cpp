#include "dnd/persistence.hpp"
#include <algorithm>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dnd {
namespace {
std::string escape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) { case '&': out += "&amp;"; break; case '<': out += "&lt;"; break; case '>': out += "&gt;"; break; case '"': out += "&quot;"; break; default: out += c; }
    }
    return out;
}
std::string valueText(const Json& value) { return value.is_string() ? value.get<std::string>() : value.dump(); }
std::string labelText(const std::string& key) {
    if (key == "DC" || key == "dc") return "DC";
    if (key == "HP" || key == "hp") return "HP";
    std::string result;
    for (const auto c : key) {
        if (c >= 'A' && c <= 'Z' && !result.empty()) result += ' ';
        result += c == '_' ? ' ' : c;
    }
    if (!result.empty() && result.front() >= 'a' && result.front() <= 'z') result.front() -= ('a' - 'A');
    return escape(result);
}
std::string valueHtml(const Json& value) {
    if (value.is_object()) {
        std::vector<std::string> keys;
        bool numeric = !value.empty();
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() == "id" || it.key() == "label") continue;
            if (it.key() == "source" && it.value().is_object() && it.value().contains("publication")) continue;
            keys.push_back(it.key());
            try { std::size_t end; (void)std::stoi(it.key(), &end); numeric = numeric && end == it.key().size() && it.value().is_primitive(); }
            catch (...) { numeric = false; }
        }
        std::ostringstream out;
        if (value.contains("label") && value["label"].is_string()) out << "<p><b>" << escape(value["label"].get<std::string>()) << "</b></p>";
        out << "<table width='100%' cellspacing='0' cellpadding='2'>";
        if (numeric && keys.size() <= 20) {
            std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return std::stoi(a) > std::stoi(b); });
            out << "<tr>";
            for (const auto& key : keys) out << "<th>" << escape(key) << "</th>";
            out << "</tr><tr>";
            for (const auto& key : keys) out << "<td>" << valueHtml(value.at(key)) << "</td>";
            out << "</tr>";
        } else for (const auto& key : keys) out << "<tr><td>" << labelText(key) << ":&nbsp;</td><td>" << valueHtml(value.at(key)) << "</td></tr>";
        out << "</table>";
        return out.str();
    }
    if (value.is_array()) {
        if (value.empty()) return "None";
        const bool objects = std::all_of(value.begin(), value.end(), [](const Json& j) { return j.is_object(); });
        std::set<std::string> keySet;
        if (objects) for (const auto& item : value) for (auto it = item.begin(); it != item.end(); ++it) {
            if (it.key() == "id" || (it.key() == "source" && it.value().is_object() && it.value().contains("publication"))) continue;
            keySet.insert(it.key());
        }
        std::vector<std::string> keys(keySet.begin(), keySet.end());
        const bool spellProfiles = keySet == std::set<std::string>{"spell", "source", "ability", "DC", "attack", "casting"};
        if (spellProfiles) keys = {"spell", "source", "ability", "DC", "attack", "casting"};
        std::ostringstream out;
        if (objects && keys.size() <= 6) {
            out << "<table width='100%' cellspacing='0' cellpadding='4' border='1' style='font-size:9pt;border-color:#d6dcdf'><thead><tr>";
            const std::map<std::string,int> widths{{"spell",24},{"source",23},{"ability",15},{"DC",7},{"attack",8},{"casting",23}};
            for (const auto& key : keys) out << "<th" << (spellProfiles ? " width='" + std::to_string(widths.at(key)) + "%'" : "") << ">" << (spellProfiles && key=="attack" ? "Atk" : labelText(key)) << "&nbsp;</th>";
            out << "</tr></thead>";
            for (const auto& item : value) {
                out << "<tr>";
                for (const auto& key : keys) out << "<td" << (spellProfiles ? " width='" + std::to_string(widths.at(key)) + "%'" : "") << ">" << (item.contains(key) ? valueHtml(item.at(key)) : "-") << "</td>";
                out << "</tr>";
            }
            out << "</table>";
        } else {
            bool first = true;
            for (const auto& item : value) { if (!first) out << (item.is_primitive() ? ", " : "<br>"); out << valueHtml(item); first = false; }
        }
        return out.str();
    }
    if (value.is_boolean()) return value.get<bool>() ? "Yes" : "No";
    auto displayed = valueText(value);
    if (value.is_string()) {
        for (const auto& [from,to] : std::vector<std::pair<std::string,std::string>>{{"short-rest:all","Short Rest: all"},{"long-rest:all","Long Rest: all"},{"short-rest:1","Short Rest: 1 use"},{";","; "}}) {
            std::size_t start=0;while ((start=displayed.find(from,start))!=std::string::npos){displayed.replace(start,from.size(),to);start+=to.size();}
        }
    }
    return escape(displayed);
}
void atomicWrite(const std::filesystem::path& path, const std::string& text) {
    if (path.empty()) throw std::runtime_error("Choose a filename before saving.");
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    if (!std::filesystem::is_directory(parent)) throw std::runtime_error("Save directory does not exist.");
    std::random_device random;
    const auto temp = parent / (path.filename().string() + "." + std::to_string(random()) + ".tmp");
    try {
#ifdef _WIN32
        HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create temporary save.");
        DWORD written = 0;
        const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!ok) throw std::runtime_error("Cannot flush temporary save.");
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot atomically replace character save.");
#else
        const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) throw std::system_error(errno, std::generic_category(), "Create temporary save");
        std::size_t offset = 0;
        while (offset < text.size()) {
            const auto count = ::write(fd, text.data() + offset, text.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { const int code = errno; ::close(fd); throw std::system_error(code, std::generic_category(), "Write save"); }
            offset += static_cast<std::size_t>(count);
        }
        const int flushResult = ::fsync(fd);
        const int flushError = errno;
        ::close(fd);
        if (flushResult != 0) throw std::system_error(flushError, std::generic_category(), "Flush save");
        std::filesystem::rename(temp, path);
        const int dir = ::open(parent.c_str(), O_RDONLY);
        if (dir >= 0) { ::fsync(dir); ::close(dir); }
#endif
    } catch (...) { std::error_code ignored; std::filesystem::remove(temp, ignored); throw; }
}
}
LoadResult loadCharacter(const std::filesystem::path& path) {
    LoadResult result;
    try {
        if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 16 * 1024 * 1024) throw std::runtime_error("Missing, nonregular, or oversized character file.");
        std::ifstream input(path);
        result.original = Json::parse(input);
        result.document = documentFromJson(result.original);
        const auto* module = findEdition(result.document.edition, result.document.moduleVersion);
        if (!module || module->version != result.document.moduleVersion) throw std::runtime_error("The saved edition module version is unavailable. Inspection only.");
    } catch (const std::exception& e) {
        result.inspectOnly = true;
        result.messages.push_back({"error", "save.inspect_only", "/", e.what(), {}});
    }
    return result;
}
void saveCharacter(const std::filesystem::path& path, const CharacterDocument& document) {
    // Validate structural shape before writing, including future versions.
    const auto json = toJson(document);
    (void)documentFromJson(json);
    atomicWrite(path, json.dump(2) + "\n");
}
std::filesystem::path autosavePath(const std::filesystem::path& path) {
    auto result = path; result += ".autosave"; return result;
}
void writeAutosave(const std::filesystem::path& path, const CharacterDocument& document) { saveCharacter(autosavePath(path), document); }
std::string renderSheetHtml(const CharacterDocument& document, const Evaluation& evaluation, const ResolvedRuleset& ruleset) {
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset='utf-8'><style>"
         << "body{font-family:Helvetica,Arial,sans-serif;font-size:10pt;color:#20242a;}a{color:#20242a;text-decoration:none;}h1{font-size:23pt;margin-bottom:4pt;}"
         << "h2{font-size:13pt;color:#304d54;margin-top:17pt;}p{margin:5pt 0;}table{border-collapse:collapse;width:100%;}"
         << "td,th{padding:5pt;border-bottom:1px solid #d6dcdf;text-align:left;}th{background:#eef1f2;}"
         << ".small{font-size:8pt;color:#46535c;}.warning{color:#8a3800;}.override{color:#643b73;}"
         << "</style></head><body><h1>" << escape(document.name.empty() ? "Unnamed adventurer" : document.name) << "</h1>";
    const auto* module = findEdition(document.edition, document.moduleVersion);
    html << "<p>" << escape(module ? module->name : document.edition) << " &middot; Rules module " << escape(document.moduleVersion);
    if (module && module->experimental) html << " &middot; <b>Experimental coverage</b>";
    html << "</p>";
    if (!evaluation.complete()) html << "<p class='warning'><b>Draft - unresolved validation messages below.</b></p>";
    std::set<std::string> shown;
    auto row = [&](const Calculation& c) {
        shown.insert(c.id);
        if (c.effective.is_object() || (c.effective.is_array() && !c.effective.empty() && std::all_of(c.effective.begin(), c.effective.end(), [](const Json& item) { return item.is_object(); }))) {
            auto displayed=c.effective;
            if(displayed.is_object() && displayed.contains("label") && displayed["label"].is_string() && displayed["label"].get<std::string>() == c.label) displayed.erase("label");
            html << "<tr><td colspan='2'><b><a href='explain:" << escape(c.id) << "'>" << escape(c.label) << "</a></b>" << valueHtml(displayed);
            if (!c.overrideReason.empty()) html << " <span class='override'>[DM override]</span>";
            html << "</td></tr>";
            return;
        }
        html << "<tr><td width='48%'><a href='explain:" << escape(c.id) << "'>" << escape(c.label) << "</a></td><td width='52%'>" << valueHtml(c.effective);
        if (!c.overrideReason.empty()) html << " <span class='override'>[DM override]</span>";
        html << "</td></tr>";
    };
    for (const auto& section : evaluation.sections) {
        if (section.notes.empty() && std::none_of(section.calculationIds.begin(), section.calculationIds.end(), [&](const auto& id) { return evaluation.find(id) != nullptr; })) continue;
        html << "<h2" << (section.title=="Feats" && section.notes.size()>=5 ? " style='page-break-before:always'" : "") << ">" << escape(section.title) << "</h2><table width='100%' cellspacing='0' cellpadding='3'>";
        for (const auto& id : section.calculationIds) if (const auto* c = evaluation.find(id)) row(*c);
        html << "</table>";
        for (const auto& note : section.notes) html << "<p>" << escape(note) << "</p>";
    }
    bool opened = false;
    for (const auto& c : evaluation.calculations) if (!shown.contains(c.id)) {
        if (!opened) { html << "<h2>Additional statistics</h2><table width='100%' cellspacing='0' cellpadding='3'>"; opened = true; }
        row(c);
    }
    if (opened) html << "</table>";
    if (!document.resources.empty()) {
        html << "<h2>Current resources</h2><table>";
        for (auto it = document.resources.begin(); it != document.resources.end(); ++it) html << "<tr><td>" << escape(it.key()) << "</td><td>" << escape(valueText(it.value())) << "</td></tr>";
        html << "</table>";
    }
    bool overridesHeading = false;
    for (const auto& c : evaluation.calculations) if (!c.overrideReason.empty()) {
        if (!overridesHeading) { html << "<h2>DM overrides</h2>"; overridesHeading = true; }
        html << "<p><b>" << escape(c.label) << ":</b> normal " << escape(valueText(c.normal)) << "; effective " << escape(valueText(c.effective)) << ". Reason: " << escape(c.overrideReason) << "</p>";
        for (const auto& step : c.steps) html << "<p class='small'>" << escape(step) << "</p>";
    }
    if (!evaluation.messages.empty()) {
        html << "<h2>Validation</h2>";
        for (const auto& m : evaluation.messages) html << "<p><b>" << escape(m.severity) << ":</b> " << escape(m.text) << "</p>";
    }
    html << "<h2>Sources and campaign</h2>";
    for (const auto& pack : ruleset.packs) {
        const auto& m = pack.manifest;
        html << "<p><b>" << escape(m.value("name", "")) << "</b> " << escape(m.value("version", "")) << " - " << escape(m.value("publisher", "")) << " (" << escape(m.value("origin", "")) << ")</p>";
    }
    std::map<std::string, std::set<std::string>> refs;
    for (const auto& c : evaluation.calculations) for (const auto& s : c.sources) refs[s.publication].insert(s.page);
    for (const auto& [publication, pages] : refs) {
        html << "<p class='small'>" << escape(publication) << ": ";
        bool first = true;
        for (const auto& page : pages) { if (!first) html << "; "; html << escape(page); first = false; }
        html << "</p>";
    }
    if (!document.campaign.empty()) html << "<p class='small'>Campaign settings:</p>" << valueHtml(document.campaign);
    for (const auto& pack : ruleset.packs) if (pack.manifest.contains("license")) html << "<p class='small'>" << escape(pack.manifest["license"].value("text", "")) << "</p>";
    html << "<p class='small'>Dungeoning a Dragon - " << escape(document.id) << "</p></body></html>";
    return html.str();
}
} // namespace dnd
