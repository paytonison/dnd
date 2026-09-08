#include "content_path.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace dnd {
namespace {

std::filesystem::path executablePath(const char *argumentZero) {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0)
        return std::filesystem::canonical(buffer.data());
#elif defined(_WIN32)
    std::vector<wchar_t> buffer(256);
    for (;;) {
        const auto size =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
            break;
        if (size < buffer.size())
            return std::filesystem::canonical(std::wstring(buffer.data(), size));
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    std::error_code error;
    const auto path = std::filesystem::canonical("/proc/self/exe", error);
    if (!error)
        return path;
#endif

    // Also support systems without a native executable-path API. A bare argv[0]
    // names a PATH lookup; it is not necessarily relative to the working directory.
    const std::filesystem::path invoked = argumentZero ? argumentZero : "";
    if (invoked.empty())
        throw std::runtime_error("Cannot locate dnd-cli; supply an explicit pack directory.");
    if (invoked.has_parent_path())
        return std::filesystem::canonical(invoked);
    const char *searchPath = std::getenv("PATH");
    const std::string directories = searchPath ? searchPath : "";
#if defined(_WIN32)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    std::size_t start = 0;
    do {
        const auto end = directories.find(separator, start);
        auto candidate = std::filesystem::path(directories.substr(start, end - start)) / invoked;
#if defined(_WIN32)
        if (!candidate.has_extension())
            candidate += ".exe";
#endif
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
            return std::filesystem::canonical(candidate);
        if (end == std::string::npos)
            break;
        start = end + 1;
    } while (start <= directories.size());
    throw std::runtime_error("Cannot locate dnd-cli; supply an explicit pack directory.");
}

} // namespace

std::filesystem::path defaultCliPackDirectory(const char *argumentZero) {
    const auto executable = executablePath(argumentZero);
    const auto installed =
        executable.parent_path().parent_path() / "share/dungeoning-a-dragon/packs";
    if (std::filesystem::is_directory(installed))
        return installed;

    // The checkout fallback belongs only to the original build executable. A
    // copied/installed binary must not silently recover missing pinned packs from
    // the developer's checkout, even when that checkout still exists.
    std::error_code error;
    if (std::filesystem::canonical(DND_DEVELOPMENT_EXECUTABLE, error) == executable && !error)
        return DND_DATA_DIR;
    return installed;
}

} // namespace dnd
