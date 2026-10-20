#pragma once
#include "Preset.hpp"
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <functional>

namespace morphpreset {
inline std::string NewId(){
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("无法生成预设编号");
    const auto* bytes=reinterpret_cast<const unsigned char*>(&id);constexpr char hex[]="0123456789abcdef";std::string text;
    for(size_t i=0;i<sizeof(id);i++){text+=hex[bytes[i]>>4];text+=hex[bytes[i]&15];}return text;
}
struct Catalog {std::vector<Document> documents;unsigned invalid=0;};
struct Purged {std::vector<std::string> ids;unsigned failed=0,skipped=0;};
class Store {
    std::filesystem::path root;
    static void NoLink(const std::filesystem::path& path){const auto attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("预设目录或文件不能是链接");}
    std::filesystem::path Directory(bool deleted)const{NoLink(root);const auto directory=deleted?root/L".trash":root;NoLink(directory);return directory;}
    void Move(const std::string& id,bool restore)const{
        if(!Hex(id,32))throw std::runtime_error("预设编号无效");
        const auto source=Directory(restore)/(id+".json"),directory=Directory(!restore),destination=directory/(id+".json");
        NoLink(source);NoLink(destination);std::filesystem::create_directories(directory);NoLink(directory);
        if(!MoveFileExW(source.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("移动预设失败：文件不存在、目标编号已存在或目录不可写；原文件保留");
    }
public:
    explicit Store(std::filesystem::path directory):root(std::move(directory)){}
    Catalog Load(bool deleted=false)const{
        const auto directory=Directory(deleted);Catalog catalog;if(!std::filesystem::exists(directory))return catalog;size_t entries=0;
        for(const auto& entry:std::filesystem::directory_iterator(directory)){
            if(++entries>4096)throw std::runtime_error("预设目录文件过多");if(entry.path().extension()!=L".json")continue;
            if(catalog.documents.size()>=(deleted?1024u:256u))throw std::runtime_error("预设目录超过容量限制");
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
    void Save(const Document& doc,bool deleted=false)const{
        const auto text=Encode(doc);const auto directory=Directory(deleted);std::filesystem::create_directories(directory);NoLink(directory);
        const auto destination=directory/(doc.id+".json"),temporary=directory/(doc.id+"."+NewId()+".tmp");NoLink(destination);
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("无法写入预设目录，请检查权限");
        DWORD written=0;const bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size()&&FlushFileBuffers(file);CloseHandle(file);
        if(!ok||!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            DeleteFileW(temporary.c_str());throw std::runtime_error("预设保存失败，原文件保留");
        }
    }
    void Trash(const std::string& id)const{Move(id,false);}
    void Restore(const std::string& id)const{Move(id,true);}
    void Purge(const std::string& id)const{
        if(!Hex(id,32))throw std::runtime_error("预设编号无效");
        const auto path=Directory(true)/(id+".json");NoLink(path);
        if(!DeleteFileW(path.c_str()))throw std::runtime_error("永久删除失败：文件不存在或不可写；请检查回收区文件权限");
    }
    Purged PurgeAll(const std::function<void()>& keepAlive={})const{
        const auto directory=Directory(true);Purged result;if(!std::filesystem::exists(directory))return result;
        std::vector<std::string> ids;size_t entries=0;
        for(const auto& entry:std::filesystem::directory_iterator(directory)){
            if(keepAlive)keepAlive();
            if(++entries>4096)throw std::runtime_error("回收区文件过多，请先手动整理");
            const auto filename=entry.path().filename().wstring();
            if(filename.size()!=37||filename.substr(32)!=L".json"||!std::all_of(filename.begin(),filename.begin()+32,[](wchar_t c){return c<=127;})){result.skipped++;continue;}
            std::string id;for(size_t i=0;i<32;i++)id+=static_cast<char>(filename[i]);
            if(!Hex(id,32)){result.skipped++;continue;}ids.push_back(id);
        }
        for(const auto& id:ids){if(keepAlive)keepAlive();try{Purge(id);result.ids.push_back(id);}catch(const std::exception&){result.failed++;}}
        return result;
    }
};
}
