#pragma once
#include "PanelLayout.hpp"
namespace managementtest {
inline MhmApi original{};inline unsigned submits=0,queries=0;
inline MhmResult MHM_CALL Submit(uint64_t source,const MhmUpdate* updates,uint32_t count,uint64_t* revision){++submits;return original.submit(source,updates,count,revision);}
inline MhmResult MHM_CALL Identity(uint32_t version,uint32_t bytes,uint64_t handle,MhmAssetIdentity* output){++queries;return MhwMorphGetAssetIdentity(version,bytes,handle,output);}
inline std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
inline unsigned Run(const std::filesystem::path& directory,const MhmApi& api,const morphpreset::Document& fixture){
    using namespace morphpreset;unsigned checks=0;auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);checks++;};
    original=api;auto counted=api;counted.submit=Submit;Store store(directory);
    auto a=fixture;a.id=NewId();a.name="Alpha##中文";store.Save(a);
    auto b=a;b.id=NewId();b.name="Beta";b.parts[0].modelSha=std::string(64,'f');store.Save(b);
    auto c=a;c.id=NewId();c.name="Gamma";for(auto& part:c.parts)part.modelSha=std::string(64,'f');store.Save(c);
    morphgui::Control control;control.ConfigurePresets(FixturePaths(directory));control.Connect(counted,Identity);uint64_t now=0;
    const auto step=[&](){control.Service(now+=20,true);};
    const auto job=[&](Action action,const std::string& id,const std::string& name=std::string{},bool deleted=false){control.PostPreset({action,id,name,deleted});step();return control.Snapshot()->presets.success;};
    const auto choice=[&](const std::string& id){for(const auto& item:*control.Snapshot()->presets.choices)if(item.id==id)return item;throw std::runtime_error("management choice");};
    check(Visible(choice(a.id),"",Filter::Complete)&&!Visible(choice(b.id),"",Filter::Complete)&&!Visible(choice(c.id),"",Filter::Complete),"main selector contains only whole matching presets");
    check(choice(b.id).matched==1&&choice(b.id).total==2&&Visible(choice(b.id),"",Filter::Partial),"partial match filter uses structured per-part counts");
    check(choice(c.id).matched==0&&Visible(choice(c.id),"",Filter::None),"zero-match presets remain available in manager");
    check(Visible(choice(a.id),"alpha",Filter::All)&&Visible(choice(a.id),"中文",Filter::All)&&Visible(choice(a.id),"##",Filter::All),"name search supports case-insensitive ASCII and literal UTF-8 substrings");
    check(!Visible(choice(a.id),"missing",Filter::All),"name search excludes unrelated presets");
    const auto beforeQueries=queries;const auto cached=control.Snapshot()->presets.choices;
    const auto body=control.Snapshot()->data.targets[0].info.handle;control.Post(mhmcontrol::Action::Set,body,0,.75f);step();
    check(queries==beforeQueries&&control.Snapshot()->presets.choices==cached,"weight changes reuse identity and catalog match caches");
    check(job(Action::Apply,a.id),"managed preset applies before file operations");
    const auto source=control.Snapshot()->data.source;const auto weight=control.Snapshot()->data.keys[0].contribution;const auto beforeSubmits=submits;
    const auto oldLibrary=control.Snapshot()->presets.library;
    check(job(Action::Rename,a.id,"Renamed##中文"),"active preset can be renamed");
    check(control.Snapshot()->presets.activeId==a.id&&control.Snapshot()->presets.activeName=="Renamed##中文"&&!control.Snapshot()->presets.selectLast,"rename preserves active preset identity without changing selection");
    auto renamed=store.Load().documents;const auto renamedAt=std::find_if(renamed.begin(),renamed.end(),[&](const auto& doc){return doc.id==a.id;});
    auto equivalent=*renamedAt;equivalent.name=a.name;
    check(Encode(equivalent)==Encode(a),"rename preserves all asset identities, owned zeros and saved weights");
    check(oldLibrary->front().name==a.name,"published document details remain immutable after rename");
    check(!job(Action::Rename,b.id,"Renamed##中文"),"rename rejects duplicate active names");
    check(submits==beforeSubmits&&control.Snapshot()->data.source==source&&control.Snapshot()->data.keys[0].contribution==weight,"renaming never submits or releases panel weights");
    const auto originalFile=Read(directory/(a.id+".json"));
    check(job(Action::Trash,a.id),"delete moves preset into recycle directory");
    check(!std::filesystem::exists(directory/(a.id+".json"))&&Read(directory/L".trash"/(a.id+".json"))==originalFile,"deletion preserves the original file bytes in recycle storage");
    const auto deleted=control.Snapshot();
    check(deleted->presets.activeId.empty()&&deleted->presets.dirty&&deleted->data.source==source&&deleted->data.keys[0].contribution==weight&&submits==beforeSubmits,"deleting active preset leaves pose owned and marks configuration unsaved");
    check(deleted->presets.choices->size()==2&&deleted->presets.removed->size()==1,"deleted preset leaves main catalog and enters recycle catalog");
    check(!job(Action::Trash,a.id)&&submits==beforeSubmits,"duplicate deletion fails without changing live controls");
    morphgui::Control offline;offline.ConfigurePresets(FixturePaths(directory));uint64_t offlineNow=0;
    const auto offlineJob=[&](Action action,const std::string& id,const std::string& name=std::string{},bool removed=false){offline.PostPreset({action,id,name,removed});offline.Service(offlineNow+=20);return offline.Snapshot()->presets.success;};
    check(offline.Snapshot()->presets.removed->size()==1&&!offline.Snapshot()->connected,"recycle entries survive reload without a connected core");
    check(offlineJob(Action::Rename,a.id,"Restore name",true),"recycle presets can be renamed without worn equipment or a core session");
    auto collision=a;collision.id=NewId();collision.name="Restore name";store.Save(collision);offline.ConfigurePresets(FixturePaths(directory));
    check(!offlineJob(Action::Restore,a.id)&&std::filesystem::exists(directory/L".trash"/(a.id+".json")),"restore name collision retains the recycled preset");
    check(offlineJob(Action::Rename,a.id,"Recovered",true)&&offlineJob(Action::Restore,a.id),"rename in recycle resolves restore conflict without overwriting");
    check(offline.Snapshot()->presets.removed->empty()&&offline.Snapshot()->presets.choices->size()==4&&!offline.Snapshot()->presets.selectLast,"restore updates catalogs but does not auto-select or apply");
    check(submits==beforeSubmits&&control.Snapshot()->data.source==source,"offline management never submits changes to live panel source");
    check(offlineJob(Action::Rename,b.id,"Without equipment")&&offlineJob(Action::Trash,b.id)&&offlineJob(Action::Restore,b.id),"rename delete and restore all work with no core or target binding");
    const auto path=directory/(b.id+".json");const auto beforeFile=Read(path);SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY);
    const auto priorOperation=offline.Snapshot()->operation;
    const bool failed=!offlineJob(Action::Rename,b.id,"Must not overwrite");SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
    check(failed&&Read(path)==beforeFile,"failed rename preserves existing file and catalog");
    check(offline.Snapshot()->operation==priorOperation,"file management errors do not replace morph API operation status");
    auto overlap=a;overlap.name="Recycle collision";store.Save(overlap,true);const auto active=Read(directory/(a.id+".json")),recycled=Read(directory/L".trash"/(a.id+".json"));
    bool refused=false;try{store.Trash(a.id);}catch(const std::exception&){refused=true;}
    check(refused&&Read(directory/(a.id+".json"))==active&&Read(directory/L".trash"/(a.id+".json"))==recycled,"delete cannot overwrite a conflicting recycle file");
    refused=false;try{store.Restore(a.id);}catch(const std::exception&){refused=true;}
    check(refused&&Read(directory/(a.id+".json"))==active&&Read(directory/L".trash"/(a.id+".json"))==recycled,"restore cannot overwrite a conflicting active file");
    refused=false;try{store.Trash("../escape");}catch(const std::exception&){refused=true;}check(refused,"delete validates identifier before constructing a path");
    refused=false;try{store.Restore("../escape");}catch(const std::exception&){refused=true;}check(refused,"restore validates identifier before constructing a path");
    const auto permanent=directory/L"permanent";Store permanentStore(permanent);
    auto p=a;p.id=NewId();p.name="Permanent one";permanentStore.Save(p);permanentStore.Save(p,true);
    auto q=p;q.id=NewId();q.name="Hidden by search";permanentStore.Save(q,true);
    morphgui::Control manager;manager.ConfigurePresets(FixturePaths(permanent));uint64_t managerNow=0;
    const auto manage=[&](Action action,const std::string& id=std::string{}){manager.PostPreset({action,id,{},true});manager.Service(managerNow+=20);return manager.Snapshot()->presets.success;};
    const auto preserved=Read(permanent/(p.id+".json"));
    check(manage(Action::Purge,p.id),"permanent deletion works without a core session");
    check(!std::filesystem::exists(permanent/L".trash"/(p.id+".json"))&&Read(permanent/(p.id+".json"))==preserved,"permanent deletion only touches the recycle copy of an identifier");
    check(manager.Snapshot()->presets.removed->size()==1&&manager.Snapshot()->presets.library->size()==1&&!manager.Snapshot()->presets.selectLast,"permanent deletion updates recycle catalog without changing active library or selection");
    check(!manage(Action::Purge,p.id)&&std::filesystem::exists(permanent/(p.id+".json")),"repeated permanent delete cannot fall back to the active directory");
    permanentStore.Save(p,true);manager.ConfigurePresets(FixturePaths(permanent));
    const auto readonly=permanent/L".trash"/(q.id+".json");SetFileAttributesW(readonly.c_str(),FILE_ATTRIBUTE_READONLY);
    check(!manage(Action::Purge,q.id)&&manager.Snapshot()->presets.removed->size()==2,"failed permanent delete retains its recycle entry");
    const auto damaged=permanent/L".trash"/(NewId()+".json");std::ofstream(damaged)<<"invalid preset";
    const auto foreign=permanent/L".trash"/L"notes.txt";std::ofstream(foreign)<<"keep";
    const auto nested=permanent/L".trash"/L"nested";std::filesystem::create_directory(nested);std::ofstream(nested/L"keep.json")<<"keep";
    check(!manage(Action::PurgeAll),"bulk permanent deletion reports partial failures");
    check(!std::filesystem::exists(permanent/L".trash"/(p.id+".json"))&&!std::filesystem::exists(damaged)&&std::filesystem::exists(readonly),"bulk delete includes hidden and damaged identifier files while retaining failed files");
    check(manager.Snapshot()->presets.removed->size()==1&&manager.Snapshot()->presets.removed->front().id==q.id,"partial bulk delete removes only successful entries from memory");
    check(std::filesystem::exists(foreign)&&std::filesystem::exists(nested/L"keep.json")&&Read(permanent/(p.id+".json"))==preserved,"bulk delete preserves active presets, unrelated files and nested directories");
    SetFileAttributesW(readonly.c_str(),FILE_ATTRIBUTE_NORMAL);
    check(manage(Action::PurgeAll)&&manager.Snapshot()->presets.removed->empty(),"bulk delete can retry remaining failed entries");
    check(manage(Action::PurgeAll),"empty recycle deletion is a harmless no-op");
    manager.ConfigurePresets(FixturePaths(permanent));check(manager.Snapshot()->presets.removed->empty(),"permanent deletion remains effective after catalog reload");
    permanentStore.Save(p,true);unsigned keepAliveCalls=0;
    const auto purged=permanentStore.PurgeAll([&](){keepAliveCalls++;});
    check(purged.ids.size()==1&&keepAliveCalls>=2,"bulk deletion offers lease renewal during enumeration and deletion");
    check(submits==beforeSubmits&&control.Snapshot()->data.source==source&&control.Snapshot()->data.keys[0].contribution==weight,"single and bulk permanent deletion never submit or release live weights");
    refused=false;try{permanentStore.Purge("../escape");}catch(const std::exception&){refused=true;}check(refused,"permanent deletion rejects traversal identifiers");
    const auto wide=morphgui::ArrangePanels({0,0,1920,1080},{100,120,860,540},8);
    check(wide.side.x==wide.main.x+wide.main.w+8&&wide.side.y==wide.main.y&&wide.side.h==wide.main.h,"manager follows the main panel's right edge, top and height");
    const auto edge=morphgui::ArrangePanels({20,30,1600,900},{1400,800,860,540},8);
    check(edge.main.x>=20&&edge.side.x+edge.side.w<=1620&&edge.main.y+edge.main.h<=930,"paired windows stay within a non-origin viewport at the right and bottom edges");
    const auto medium=morphgui::ArrangePanels({0,0,1280,720},{60,60,860,800},8);
    check(medium.side.x>=medium.main.x+medium.main.w&&medium.side.x+medium.side.w<=1280&&medium.side.h<=720,"intermediate viewport shrinks the pair to fit side by side");
    const auto narrow=morphgui::ArrangePanels({0,0,800,400},{100,100,860,540},8);
    check(narrow.main.x>=0&&narrow.side.x>=0&&narrow.main.w<=800&&narrow.side.x+narrow.side.w<=800&&narrow.side.h<=400,"small viewport fallback keeps management controls reachable");
    control.Post(mhmcontrol::Action::Release);step();return checks;
}
}
