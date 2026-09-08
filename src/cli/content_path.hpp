#pragma once

#include <filesystem>

namespace dnd {

// Resolve installed content from this executable, independently of the current directory.
std::filesystem::path defaultCliPackDirectory(const char *argumentZero);

} // namespace dnd
