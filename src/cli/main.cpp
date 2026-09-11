#include "content_path.hpp"
#include "dnd/content.hpp"
#include "dnd/persistence.hpp"
#include <fstream>
#include <iostream>

using namespace dnd;
int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            std::cout
                << "Dungeoning a Dragon v" DND_APP_VERSION "\n  dnd-cli --version\n  dnd-cli editions\n  dnd-cli validate-pack DIRECTORY\n  "
                   "dnd-cli new EDITION FILE [MODULE_VERSION]\n  dnd-cli migrate FILE "
                   "MODULE_VERSION NEW_FILE\n  dnd-cli evaluate FILE [PACK_DIRECTORY]\n  dnd-cli "
                   "sheet FILE HTML_FILE [PACK_DIRECTORY]\n  dnd-cli preview FILE ACTION_ID "
                   "INPUTS_JSON_FILE [PACK_DIRECTORY]\n  dnd-cli apply FILE ACTION_ID "
                   "INPUTS_JSON_FILE OUTPUT_FILE [PACK_DIRECTORY]\n";
            return 0;
        }
        const std::string command = argv[1];
        if (command == "--version") {
            std::cout << "Dungeoning a Dragon v" DND_APP_VERSION "\n";
            return 0;
        }
        if (command == "editions") {
            for (const auto &module : editions())
                std::cout << module.id << "\t" << module.version << "\t" << module.name
                          << (module.experimental ? " [experimental]" : "") << '\n';
            return 0;
        }
        if (command == "validate-pack" && argc == 3) {
            const auto result = loadPack(argv[2]);
            for (const auto &m : result.messages)
                std::cout << m.severity << " " << m.path << ": " << m.text << '\n';
            if (result.valid())
                std::cout << "Valid: " << result.pack.entries.size() << " content entries\n";
            return result.valid() ? 0 : 2;
        }
        if (command == "new" && (argc == 4 || argc == 5)) {
            if (std::filesystem::exists(argv[3]))
                throw std::runtime_error("Destination exists.");
            saveCharacter(argv[3], newCharacter(argv[2], argc == 5 ? argv[4] : ""));
            return 0;
        }
        if (command == "migrate" && argc == 5) {
            if (std::filesystem::exists(argv[4]))
                throw std::runtime_error("Migration destination exists; choose a new file.");
            const auto loaded = loadCharacter(argv[2]);
            if (loaded.inspectOnly)
                throw std::runtime_error("The original cannot be migrated because its exact module "
                                         "or save schema is unavailable.");
            const auto migrated = migrateCharacterVersion(loaded.document, argv[3]);
            for (const auto &message : migrated.messages)
                std::cout << message.severity << ": " << message.text << '\n';
            if (!migrated.valid())
                return 2;
            saveCharacter(argv[4], migrated.document);
            return 0;
        }
        if ((command == "preview" && (argc == 5 || argc == 6)) ||
            (command == "apply" && (argc == 6 || argc == 7))) {
            const auto loaded = loadCharacter(argv[2]);
            if (loaded.inspectOnly)
                throw std::runtime_error(
                    "The character is inspection-only and cannot perform actions.");
            const std::filesystem::path inputsPath = argv[4];
            if (!std::filesystem::is_regular_file(inputsPath) ||
                std::filesystem::file_size(inputsPath) > 16 * 1024 * 1024)
                throw std::runtime_error("Action inputs must be a bounded JSON file.");
            std::ifstream inputsFile(inputsPath);
            const auto inputs = Json::parse(inputsFile);
            const int packArgument = command == "apply" ? 6 : 5;
            std::vector<Message> messages;
            const auto packs =
                loadPackDirectory(argc > packArgument ? std::filesystem::path(argv[packArgument])
                                                      : defaultCliPackDirectory(argv[0]),
                                  messages);
            auto rules = resolveRuleset(loaded.document, packs);
            rules.messages.insert(rules.messages.end(), messages.begin(), messages.end());
            const auto result = executeCommand(loaded.document, rules, {argv[3], inputs});
            Json report = {
                {"valid", result.valid()},
                {"messages", Json::array()},
                {"choiceChanges", Json::diff(loaded.document.choices, result.document.choices)},
                {"resourceChanges",
                 Json::diff(loaded.document.resources, result.document.resources)}};
            for (const auto &m : result.messages)
                report["messages"].push_back({{"severity", m.severity},
                                              {"code", m.code},
                                              {"path", m.path},
                                              {"text", m.text}});
            if (command == "preview")
                report["proposedCharacter"] = toJson(result.document);
            else if (result.valid()) {
                saveCharacter(argv[5], result.document);
                report["savedTo"] = std::filesystem::absolute(argv[5]).string();
            }
            std::cout << report.dump(2) << '\n';
            return result.valid() ? 0 : 2;
        }
        if ((command == "evaluate" && argc >= 3 && argc <= 4) ||
            (command == "sheet" && argc >= 4 && argc <= 5)) {
            const auto loaded = loadCharacter(argv[2]);
            if (loaded.inspectOnly) {
                std::cerr << loaded.messages.front().text << '\n';
                std::cout << loaded.original.dump(2) << '\n';
                return 2;
            }
            const int packArgument = command == "sheet" ? 4 : 3;
            std::vector<Message> messages;
            const auto packs =
                loadPackDirectory(argc > packArgument ? std::filesystem::path(argv[packArgument])
                                                      : defaultCliPackDirectory(argv[0]),
                                  messages);
            auto ruleset = resolveRuleset(loaded.document, packs);
            ruleset.messages.insert(ruleset.messages.end(), messages.begin(), messages.end());
            const auto result = evaluate(loaded.document, ruleset);
            if (command == "evaluate")
                std::cout << toJson(result).dump(2) << '\n';
            else {
                std::ofstream out(argv[3]);
                out << renderSheetHtml(loaded.document, result, ruleset);
                if (!out)
                    throw std::runtime_error("Cannot write sheet.");
            }
            return result.complete() ? 0 : 2;
        }
        throw std::runtime_error("Invalid command; run dnd-cli for usage.");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
