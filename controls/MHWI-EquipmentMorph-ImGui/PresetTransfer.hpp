#pragma once
#include "PresetStore.hpp"
#include <memory>

namespace morphpreset {
struct Incoming {
    std::string token,file,error,name,status;bool duplicate=false;
    std::shared_ptr<const Document> document;
};
struct ImportEdit {std::string token,name;};
inline std::string Contents(Document doc,bool includeName=true){
    doc.id=std::string(32,'0');
    if(!includeName)doc.name="comparison";
    std::sort(doc.parts.begin(),doc.parts.end(),[](const auto& a,const auto& b){return a.part<b.part;});
    for(auto& part:doc.parts)std::sort(part.keys.begin(),part.keys.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    return Encode(doc);
}
inline std::string ImportName(const std::string& original,unsigned suffix){
    if(!suffix)return original;const auto ending=suffix==1?std::string("（导入）"):"（导入 "+std::to_string(suffix)+"）";
    auto prefix=original.substr(0,128-ending.size());while(!prefix.empty()&&!Utf8(prefix))prefix.pop_back();return prefix+ending;
}
struct Exported {std::string folder,error;unsigned count=0,failed=0;};
// These directories are siblings of presets, so staging never enters its library.
class Transfer {
    std::filesystem::path base;
    static void NoLink(const std::filesystem::path& path){const auto attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("导入／导出目录或文件不能是链接");}
    std::filesystem::path Directory(const wchar_t* name)const{NoLink(base);const auto path=base/name;NoLink(path);std::filesystem::create_directories(path);NoLink(path);return path;}
    static std::string Utf8Path(const std::filesystem::path& path){const auto text=path.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
public:
    explicit Transfer(const std::filesystem::path& presets):base(PresetPath(presets).parent_path()){}
    std::vector<Incoming> Scan(const std::function<void()>& keepAlive={})const{
        const auto directory=Directory(L"imports");std::vector<std::filesystem::path> files;size_t entries=0;
        for(const auto& entry:std::filesystem::directory_iterator(directory)){
            if(keepAlive)keepAlive();if(++entries>4096)throw std::runtime_error("导入目录条目过多，请先整理");
            const auto extension=entry.path().extension().wstring();if(CompareStringOrdinal(extension.c_str(),-1,L".json",-1,TRUE)!=CSTR_EQUAL)continue;
            if(files.size()>=256)throw std::runtime_error("一次最多预览 256 个导入文件，请分批处理");files.push_back(entry.path());
        }
        std::sort(files.begin(),files.end());std::vector<Incoming> result;
        for(const auto& path:files){
            if(keepAlive)keepAlive();Incoming item;item.token=NewId();item.file=Utf8Path(path.filename());
            try{
                NoLink(path);const auto size=std::filesystem::file_size(path);if(!size||size>MaxFileBytes)throw std::runtime_error("文件大小无效（最多 1 MiB）");
                std::ifstream file(path,std::ios::binary);std::string text(static_cast<size_t>(size),'\0');if(!file.read(text.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("无法读取文件");
                std::string_view json=text;if(json.starts_with("\xef\xbb\xbf"))json.remove_prefix(3);
                auto doc=Decode(json);item.name=doc.name;item.document=std::make_shared<const Document>(std::move(doc));
            }catch(const std::exception& e){item.error=e.what();}result.push_back(std::move(item));
        }return result;
    }
    Exported Export(const std::vector<Document>& documents,const std::function<void()>& keepAlive={})const{
        if(documents.empty())throw std::runtime_error("没有可导出的预设");
        const auto directory=Directory(L"exports");SYSTEMTIME time{};GetLocalTime(&time);char stamp[40]{};
        sprintf_s(stamp,"export-%04u%02u%02u-%02u%02u%02u-",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
        const auto name=std::string(stamp)+NewId();const auto folder=directory/name;
        if(!std::filesystem::create_directory(folder))throw std::runtime_error("无法创建独立导出目录");Store output(folder);Exported result;result.folder="exports/"+name;
        for(const auto& doc:documents){if(keepAlive)keepAlive();try{output.Save(doc,false,false);result.count++;}catch(const std::exception& e){result.failed++;if(result.error.empty())result.error=e.what();}}
        return result;
    }
};
}
