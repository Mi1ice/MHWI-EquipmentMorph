#pragma once
#include "PresetStore.hpp"

namespace morphpreset {
// Panel ownership only. This switch never disables the shared core API.
class PanelSettings {
    std::filesystem::path path;
public:
    explicit PanelSettings(const std::filesystem::path& file):path(PresetPath(file)){}
    bool Load()const{
        NoPresetLink(path.parent_path());NoPresetLink(path);if(!std::filesystem::exists(path))return true;
        const auto size=std::filesystem::file_size(path);if(!size||size>4096)throw std::runtime_error("面板开关配置大小无效");
        std::ifstream file(path,std::ios::binary);std::string text(static_cast<size_t>(size),'\0');if(!file.read(text.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("无法读取面板开关配置");
        const auto root=json::Reader(text).Parse();json::Fields(root,2);
        if(json::Field(root,"schema",json::Value::Number).number!=1)throw std::runtime_error("面板开关配置版本不支持");
        return json::Field(root,"enabled",json::Value::Boolean).number!=0;
    }
    void Save(bool enabled)const{WritePresetText(path,std::string("{\n  \"schema\": 1,\n  \"enabled\": ")+(enabled?"true":"false")+"\n}\n");}
};
}
