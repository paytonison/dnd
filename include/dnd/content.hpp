#pragma once
#include "dnd/engine.hpp"
#include <filesystem>

namespace dnd {
struct PackLoadResult {
    ContentPack pack;
    std::vector<Message> messages;
    bool valid() const;
};
PackLoadResult loadPack(const std::filesystem::path& directory);
std::vector<ContentPack> loadPackDirectory(const std::filesystem::path& root, std::vector<Message>& messages);
ResolvedRuleset resolveRuleset(const CharacterDocument& document, const std::vector<ContentPack>& available);
std::vector<Message> installPack(const std::filesystem::path& source, const std::filesystem::path& destinationRoot,
                               const std::vector<ContentPack>& available = {});
} // namespace dnd
