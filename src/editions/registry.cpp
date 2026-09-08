#include "dnd/engine.hpp"
#include <algorithm>
namespace dnd {
const std::vector<EditionModule>& editions() {
    static const std::vector<EditionModule> modules{bxModule(), srd55Module(), srd55FullModule()};
    return modules;
}
const EditionModule* findEdition(const std::string& id, const std::string& version) {
    const auto& modules = editions();
    const auto it = std::find_if(modules.begin(), modules.end(), [&](const auto& module) { return module.id == id && (version.empty() || module.version == version); });
    return it == modules.end() ? nullptr : &*it;
}
}
