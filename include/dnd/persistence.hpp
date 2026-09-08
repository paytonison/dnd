#pragma once
#include "dnd/engine.hpp"
#include <filesystem>

namespace dnd {
struct LoadResult {
    CharacterDocument document;
    Json original;
    bool inspectOnly = false;
    std::vector<Message> messages;
};
LoadResult loadCharacter(const std::filesystem::path& path);
void saveCharacter(const std::filesystem::path& path, const CharacterDocument& document);
void writeAutosave(const std::filesystem::path& path, const CharacterDocument& document);
std::filesystem::path autosavePath(const std::filesystem::path& path);
std::string renderSheetHtml(const CharacterDocument& document, const Evaluation& evaluation,
                            const ResolvedRuleset& ruleset);
} // namespace dnd
