#pragma once
#include "PresetPaths.hpp"
namespace paneltest {
inline std::filesystem::path FixtureDirectory(){wchar_t path[MAX_PATH]{};Check(GetModuleFileNameW(nullptr,path,MAX_PATH)!=0,"fixture executable path");return std::filesystem::path(path).parent_path()/L"storage-fixtures"/morphpreset::NewId();}
inline std::string ReadFile(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
inline void TestFreshNamespaceIgnoresLegacy(){
    using namespace morphpreset;const auto root=FixtureDirectory();const auto paths=MakeStoragePaths(root);auto doc=BigPreset();doc.parts.resize(1);doc.parts[0].keys.resize(1);
    Store old(root/L"presets");old.Save(doc);old.Save(doc,true);std::filesystem::create_directories(root/L"imports");
    const auto encoded=Encode(doc);WritePresetText(root/L"imports"/L"old.json",encoded);WritePresetText(root/L"auto-presets.json","broken old default");WritePresetText(root/L"native-expression.json","preserve expression");
    PanelSettings setting(paths.panelSettings);setting.Save(false);const auto savedSwitch=ReadFile(root/L"panel-settings.json");
    morphgui::Control control;control.ConfigurePresets(paths);const auto state=control.Snapshot()->presets;
    Check(state.library->empty()&&state.trash->empty()&&state.defaults->empty()&&state.message.empty()&&state.autoMessage.empty(),"fresh namespace ignores all legacy data silently");
    Check(!state.enabled&&!PanelSettings(paths.panelSettings).Load(),"original switch preserved");
    Check(ReadFile(root/L"panel-settings.json")==savedSwitch&&ReadFile(root/L"auto-presets.json")=="broken old default"&&ReadFile(root/L"native-expression.json")=="preserve expression"&&ReadFile(root/L"imports"/L"old.json")==encoded&&old.Load().documents.size()==1&&old.Load(true).documents.size()==1,"legacy data untouched");
    Check(Transfer(paths.presets).Scan().empty(),"legacy imports not scanned");
}
inline void TestBigStoreImportExport(){
    using namespace morphpreset;const auto paths=MakeStoragePaths(FixtureDirectory());Store store(paths.presets);const auto doc=BigPreset();store.Save(doc);
    auto loaded=store.Load();Check(loaded.invalid==0&&loaded.documents.size()==1&&Contents(loaded.documents[0])==Contents(doc),"large file readable after save");
    Transfer transfer(paths.presets);const auto result=transfer.Export({doc});Check(result.count==1&&!result.failed,"large export");
    const auto exported=paths.root/std::filesystem::path(result.folder)/(doc.id+".json");std::filesystem::create_directories(paths.imports);std::filesystem::copy_file(exported,paths.imports/L"incoming.json");
    const auto incoming=transfer.Scan();Check(incoming.size()==1&&incoming[0].document&&Contents(*incoming[0].document)==Contents(doc),"large import preview content exact");
    morphgui::Control manager;manager.ConfigurePresets(paths);manager.PostPreset({Action::ScanImports});manager.Service(0);const auto preview=manager.Snapshot()->presets.incoming;
    morphpreset::Request incomingRequest{Action::Import};incomingRequest.imports.push_back({preview->front().token,"imported copy"});manager.PostPreset(incomingRequest);manager.Service(1);Check(manager.Snapshot()->presets.success&&store.Load().documents.size()==2,"large import writes new identity");
    DefaultsStore defaults(paths.presets);defaults.Save({{doc.id,MHM_HEAD}});store.Trash(doc.id);Check(store.Load().documents.size()==1&&store.Load(true).documents.size()==1,"large recycle");store.Restore(doc.id);Check(store.Load(true).documents.empty()&&store.Load().documents.size()==2,"large restore");
    const auto original=ReadFile(paths.presets/(doc.id+".json"));auto changed=doc;changed.name="replacement";SetFileAttributesW((paths.presets/(doc.id+".json")).c_str(),FILE_ATTRIBUTE_READONLY);
    Refused([&]{store.Save(changed);},"atomic file replacement fails visibly");SetFileAttributesW((paths.presets/(doc.id+".json")).c_str(),FILE_ATTRIBUTE_NORMAL);Check(ReadFile(paths.presets/(doc.id+".json"))==original,"failed write preserves full original file");
}
inline void StorageTests(){TestFreshNamespaceIgnoresLegacy();TestBigStoreImportExport();}
}
