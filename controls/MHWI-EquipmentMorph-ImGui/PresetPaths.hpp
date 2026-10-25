#pragma once
#include <filesystem>
namespace morphpreset {
struct StoragePaths {std::filesystem::path root,presets,trash,defaults,imports,exports,panelSettings;};
inline StoragePaths MakeStoragePaths(const std::filesystem::path& applicationRoot){
    const auto root=applicationRoot/L"panel-v2";const auto presets=root/L"presets";
    return {root,presets,presets/L".trash",root/L"auto-presets.json",root/L"imports",root/L"exports",applicationRoot/L"panel-settings.json"};
}
}
