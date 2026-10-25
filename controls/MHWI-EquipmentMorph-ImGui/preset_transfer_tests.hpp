#pragma once
namespace transfertest {
inline unsigned Run(const std::filesystem::path& directory,const MhmApi& original,const morphpreset::Document& fixture){
    using namespace morphpreset;unsigned checks=0;const auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);checks++;};
    const auto presets=directory/L"presets",imports=directory/L"imports",exports=directory/L"exports";Store store(presets);Transfer transfer(presets);
    auto a=fixture;a.id=NewId();a.name="Saved A";store.Save(a);auto b=a;b.id=NewId();b.name="Saved B";store.Save(b);
    auto recycled=a;recycled.id=NewId();recycled.name="Recycle only";store.Save(recycled,true);
    auto counted=original;counted.submit=managementtest::Submit;
    morphgui::Control control;control.ConfigurePresets(FixturePaths(presets));control.Connect(counted,MhwMorphGetAssetIdentity);uint64_t now=0;
    const auto step=[&](){control.Service(now+=20,true);};
    const auto job=[&](Request request){control.PostPreset(std::move(request));step();return control.Snapshot()->presets.success;};
    check(job({Action::Apply,a.id}),"apply a baseline before file transfer checks");
    const auto body=control.Snapshot()->data.targets[0].info.handle;control.Post(mhmcontrol::Action::Set,body,0,.91f);step();
    const auto source=control.Snapshot()->data.source;const auto submitted=managementtest::submits;const auto weight=control.Snapshot()->data.keys[0].contribution;
    const bool exported=job({Action::Export,a.id});check(exported,("single export works through worker request: "+control.Snapshot()->presets.message).c_str());
    std::vector<std::filesystem::path> folders;for(const auto& entry:std::filesystem::directory_iterator(exports))folders.push_back(entry.path());
    check(folders.size()==1&&Contents(Store(folders.front()).Load().documents.front())==Contents(a),"export uses saved document rather than unsaved live contributions");
    check(job({Action::ExportAll}),"whole-library export works");
    folders.clear();unsigned total=0;for(const auto& entry:std::filesystem::directory_iterator(exports)){folders.push_back(entry.path());total+=static_cast<unsigned>(Store(entry.path()).Load().documents.size());}
    check(folders.size()==2&&total==3,"exports create independent folders and exclude recycled documents");
    check(control.Snapshot()->presets.activeId==a.id&&control.Snapshot()->presets.dirty,"export retains active preset and its unsaved state");
    check(!job({Action::Export,recycled.id}),"single export rejects recycle-only identifiers");
    check(transfer.Scan().empty()&&std::filesystem::exists(imports),"scan creates a separate empty imports directory");
    const auto write=[&](const wchar_t* name,const std::string& text){std::ofstream file(imports/name,std::ios::binary);file<<text;};
    write(L"01-duplicate.json",Encode(a));
    auto collision=a;collision.parts[0].keys[0].weight=collision.parts[0].keys[0].weight==.5f?.6f:.5f;write(L"02-collision.json",Encode(collision));
    auto incompatible=a;incompatible.name="Imported unmatched";for(auto& part:incompatible.parts)part.modelSha=std::string(64,'f');write(L"03-incompatible.JSON",std::string("\xef\xbb\xbf")+Encode(incompatible));
    write(L"04-broken.json","{");auto version=Encode(a);version.replace(version.find("\"schema\": 2"),11,"\"schema\": 1");write(L"05-version.json",version);
    write(L"06-large.json",std::string(1024*1024+1,' '));
    std::filesystem::create_directory(imports/L"nested");{std::ofstream file(imports/L"nested"/L"ignored.json");file<<Encode(a);}
    check(job({Action::ScanImports}),"scan publishes valid and invalid preview rows");
    const auto previews=control.Snapshot()->presets.incoming;
    const auto find=[&](const std::string& filename){for(const auto& item:*previews)if(item.file==filename)return item;throw std::runtime_error("missing transfer preview");};
    const auto duplicate=find("01-duplicate.json"),conflict=find("02-collision.json"),unmatched=find("03-incompatible.JSON"),broken=find("04-broken.json");
    check(previews->size()==6&&duplicate.duplicate&&duplicate.document,"preview ignores subfolders and recognizes an exact duplicate");
    check(conflict.name=="Saved A（导入）"&&!conflict.duplicate,"name collision gets an editable distinct-name suggestion");
    check(unmatched.document&&unmatched.document->name==incompatible.name,"scan accepts UTF-8 BOM and uppercase JSON extension without identifier filenames");
    check(!broken.document&&!find("05-version.json").document&&!find("06-large.json").document,"invalid schema, damaged JSON and oversized files stay nonimportable");
    const auto import=[&](std::vector<ImportEdit> edits){Request request;request.action=Action::Import;request.imports=std::move(edits);return job(std::move(request));};
    check(import({{duplicate.token,duplicate.name}})&&control.Snapshot()->presets.library->size()==2,"duplicate import is skipped without adding a document");
    check(!import({{conflict.token,a.name}})&&Contents(store.Load().documents.front())==Contents(a),"same-name different-content import fails without overwriting existing data");
    check(import({{conflict.token,conflict.name}}),"suggested collision name can be imported");
    const auto importedAt=std::find_if(control.Snapshot()->presets.library->begin(),control.Snapshot()->presets.library->end(),[&](const auto& doc){return doc.name==conflict.name;});
    check(importedAt!=control.Snapshot()->presets.library->end()&&importedAt->id!=collision.id&&importedAt->id!=recycled.id,"import generates a new local identifier");
    auto expected=collision;expected.name=conflict.name;
    check(Contents(*importedAt)==Contents(expected),"import preserves identities and every stored weight including owned zeros");
    write(L"03-incompatible.JSON","changed after preview");
    check(import({{unmatched.token,unmatched.name}}),"equipment mismatch does not prevent importing the validated preview snapshot");
    bool unavailable=false;for(const auto& choice:*control.Snapshot()->presets.choices)if(choice.name==unmatched.name)unavailable=!choice.compatible;
    check(unavailable&&control.Snapshot()->presets.library->size()==4,"unmatched import remains outside the usable preset selector");
    check(import({{unmatched.token,unmatched.name}})&&control.Snapshot()->presets.library->size()==4,"repeated identical import does not create copies");
    check(!import({{broken.token,"Bad"},{duplicate.token,"Imported partial"}})&&control.Snapshot()->presets.library->size()==5,"one rejected file does not block other valid selected imports");
    check(!import({{"../escape","Unsafe"}})&&!std::filesystem::exists(directory/L"escape.json"),"import accepts preview tokens rather than arbitrary input paths");
    check(!import({{duplicate.token,std::string(129,'x')}}),"import validates edited names before writing");
    check(job({Action::ScanImports})&&!import({{duplicate.token,"Old preview"}}),"refresh invalidates old preview tokens");
    bool renamedDuplicate=false;for(const auto& item:*control.Snapshot()->presets.incoming)if(item.file=="02-collision.json")renamedDuplicate=item.duplicate&&item.name==conflict.name;
    check(renamedDuplicate,"refresh recognizes a previously imported automatic collision name");
    check(control.Snapshot()->presets.activeId==a.id&&control.Snapshot()->presets.dirty&&control.Snapshot()->data.source==source&&control.Snapshot()->data.keys[0].contribution==weight&&managementtest::submits==submitted,"all transfer operations preserve active association, live source and unsaved weights without submissions");
    bool refused=false;auto overwrite=a;overwrite.name="Must not overwrite";try{store.Save(overwrite,false,false);}catch(const std::exception&){refused=true;}
    check(refused&&Contents(Decode(managementtest::Read(presets/(a.id+".json"))))==Contents(a),"new-file save refuses an existing identifier instead of replacing it");
    std::string longName;for(unsigned i=0;i<42;i++)longName+="中";const auto suggested=ImportName(longName,1);
    check(Utf8(suggested)&&suggested.size()<=128&&suggested.ends_with("（导入）"),"generated collision suffix respects the UTF-8 byte limit without splitting characters");
    auto reordered=a;std::reverse(reordered.parts.begin(),reordered.parts.end());for(auto& part:reordered.parts)std::reverse(part.keys.begin(),part.keys.end());
    check(Contents(reordered)==Contents(a),"duplicate comparison ignores part and key serialization order");
    morphgui::Control offline;offline.ConfigurePresets(FixturePaths(presets));offline.PostPreset({Action::ExportAll});offline.Service(20);
    check(offline.Snapshot()->presets.success&&!offline.Snapshot()->connected,"export works with no connected morph core");
    offline.PostPreset({Action::ScanImports});offline.Service(40);auto offlineRequest=Request{Action::Import};
    for(const auto& item:*offline.Snapshot()->presets.incoming)if(item.document){offlineRequest.imports.push_back({item.token,"Offline import"});break;}
    offline.PostPreset(std::move(offlineRequest));offline.Service(60);
    check(offline.Snapshot()->presets.success&&!offline.Snapshot()->presets.selectLast,"import works offline and never auto-selects or applies");
    const auto beforeLimit=control.Snapshot()->presets.incoming;
    for(unsigned i=0;i<251;i++){std::ofstream file(imports/("limit-"+std::to_string(i)+".json"));file<<"{}";}
    check(!job({Action::ScanImports})&&control.Snapshot()->presets.incoming==beforeLimit,"oversized import directory is rejected without discarding the previous preview");
    const auto failedBase=directory/L"blocked";std::filesystem::create_directory(failedBase);{std::ofstream file(failedBase/L"exports");file<<"keep";}
    refused=false;try{Transfer(failedBase/L"presets").Export({a});}catch(const std::exception&){refused=true;}
    check(refused&&managementtest::Read(failedBase/L"exports")=="keep","blocked export directory leaves its existing file intact");
    const auto roomy=morphgui::ArrangePanels({0,0,2560,1440},{80,80,860,540},8,920);
    check(roomy.side.w==920&&roomy.side.x==roomy.main.x+roomy.main.w+8,"sidebar layout accepts a user-selected width while retaining its anchor");
    const auto bounded=morphgui::ArrangePanels({0,0,1280,720},{0,0,860,540},8,1600);
    check(bounded.side.x+bounded.side.w<=1280&&bounded.side.w>=480,"requested sidebar width remains inside viewport limits");
    control.Post(mhmcontrol::Action::Release);step();return checks;
}
}
