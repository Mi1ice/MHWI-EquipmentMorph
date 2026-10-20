#pragma once
#include "Preset.hpp"
#include <objbase.h>
#include <filesystem>
#include <fstream>

namespace morphpreset {
inline std::string NewId(){
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("无法生成预设编号");
    const auto* bytes=reinterpret_cast<const unsigned char*>(&id);constexpr char hex[]="0123456789abcdef";std::string text;
    for(size_t i=0;i<sizeof(id);i++){text+=hex[bytes[i]>>4];text+=hex[bytes[i]&15];}return text;
}
struct Catalog {std::vector<Document> documents;unsigned invalid=0;};
class Store {
    std::filesystem::path root;
    static void NoLink(const std::filesystem::path& path){const auto attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("预设目录或文件不能是链接");}
public:
    explicit Store(std::filesystem::path directory):root(std::move(directory)){}
    Catalog Load()const{
        Catalog catalog;NoLink(root);if(!std::filesystem::exists(root))return catalog;size_t entries=0;
        for(const auto& entry:std::filesystem::directory_iterator(root)){
            if(++entries>1024)throw std::runtime_error("预设目录文件过多");if(entry.path().extension()!=L".json")continue;
            if(catalog.documents.size()>=256)throw std::runtime_error("最多支持 256 个预设");
            try{
                NoLink(entry.path());const auto size=entry.file_size();if(!size||size>256*1024)throw std::runtime_error("预设文件大小无效");
                std::ifstream file(entry.path(),std::ios::binary);std::string text(static_cast<size_t>(size),'\0');
                if(!file.read(text.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("无法读取预设");
                auto doc=Decode(text);if(entry.path().stem().string()!=doc.id)throw std::runtime_error("预设编号与文件名不同");
                catalog.documents.push_back(std::move(doc));
            }catch(const std::exception&){catalog.invalid++;}
        }
        std::sort(catalog.documents.begin(),catalog.documents.end(),[](const auto& a,const auto& b){return a.name==b.name?a.id<b.id:a.name<b.name;});return catalog;
    }
    void Save(const Document& doc)const{
        const auto text=Encode(doc);NoLink(root);std::filesystem::create_directories(root);NoLink(root);
        const auto destination=root/(doc.id+".json"),temporary=root/(doc.id+"."+NewId()+".tmp");NoLink(destination);
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("无法写入预设目录，请检查权限");
        DWORD written=0;const bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file);CloseHandle(file);
        if(!ok||!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DeleteFileW(temporary.c_str());throw std::runtime_error("预设保存失败，原文件保留");
        }
    }
};
}
