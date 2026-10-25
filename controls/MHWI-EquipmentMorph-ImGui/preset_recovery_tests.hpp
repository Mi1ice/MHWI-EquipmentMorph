#pragma once
namespace recoverytest {
inline MhmApi api{};inline bool loseLease=false,failRead=false;
inline MhmResult MHM_CALL Heartbeat(uint64_t source){
    if(loseLease){loseLease=false;api.close_source(source);}return api.heartbeat(source);
}
inline MhmResult MHM_CALL Read(uint64_t target,MhmWeightState* values,uint32_t capacity,uint32_t* count,uint64_t* requested,uint64_t* applied){
    if(failRead)return MHM_NOT_READY;return api.read_weights(target,values,capacity,count,requested,applied);
}
inline unsigned Run(const std::filesystem::path& directory,const MhmApi& original){
    using namespace morphpreset;api=original;auto checked=api;checked.heartbeat=Heartbeat;checked.read_weights=Read;
    unsigned checks=0;const auto check=[&](bool ok,const char* why){if(!ok)throw std::runtime_error(why);checks++;};
    mhm::Asset asset;asset.part=MHM_HEAD;asset.model="pl/recovery/head";asset.modelSha=std::string(64,'a');asset.packSha=std::string(64,'b');asset.basis.resize(64);
    mhm::Key definition;strcpy_s(definition.info.id,"A");definition.info.maximum=1;asset.keys.push_back(definition);uint64_t handle=0;
    if(Core().Attach(std::move(asset),handle)!=MHM_OK)throw std::runtime_error("recovery fixture attach");
    const Document doc{NewId(),"Recovery preset",{{MHM_HEAD,"pl/recovery/head",std::string(64,'a'),std::string(64,'b'),{{"A",.25f}}}}};
    const auto key=[&](const morphgui::Control& control){const auto snapshot=control.Snapshot();for(uint32_t i=0;i<snapshot->data.keyCount;i++)if(snapshot->data.keys[i].target==handle)return snapshot->data.keys[i];throw std::runtime_error("recovery key missing");};
    {
        const auto presets=directory/L"draft"/L"presets";Store(presets).Save(doc);DefaultsStore(presets).Save({{doc.id,MHM_HEAD}});
        morphgui::Control control;control.ConfigurePresets(FixturePaths(presets));control.Connect(checked,MhwMorphGetAssetIdentity);control.Service(100);
        control.Post(mhmcontrol::Action::Set,handle,0,.8f);control.Service(120);
        check(control.Snapshot()->presets.dirty&&key(control).contribution==.8f,"read failure fixture starts with a dirty named draft");
        const auto saved=managementtest::Read(presets/(doc.id+".json"));failRead=true;control.Service(400);
        check(!control.Snapshot()->ready&&control.Snapshot()->presets.dirty&&!control.Snapshot()->presets.canSave,"temporary read failure preserves dirty state while disabling save");
        Request close{Action::EndPreview};close.editor=doc.id;close.checkEditor=true;
        control.PostPreset(close);control.Service(420);
        check(!control.Snapshot()->presets.success&&control.Snapshot()->presets.dirty&&key(control).contribution==.8f,"closing during read failure cannot silently discard a named draft");
        auto saveClose=close;saveClose.saveBefore=true;control.PostPreset(saveClose);control.Service(440);
        check(!control.Snapshot()->presets.success&&managementtest::Read(presets/(doc.id+".json"))==saved,"save and close cannot write stale readback");
        auto discardClose=close;discardClose.discard=true;control.PostPreset(discardClose);control.Service(460);
        check(!control.Snapshot()->presets.success&&control.Snapshot()->data.source!=0,"even confirmed close waits until default restoration can be evaluated");
        control.PostPreset({Action::DisableMorph});control.Service(480);
        check(!control.Snapshot()->presets.success&&control.Snapshot()->presets.enabled,"read failure cannot bypass unsaved confirmation when disabling");
        failRead=false;control.Service(800);
        check(control.Snapshot()->ready&&control.Snapshot()->presets.dirty&&control.Snapshot()->presets.canSave&&key(control).contribution==.8f,"recovering reads retains the original editable draft");
        control.PostPreset(saveClose);control.Service(820);
        check(control.Snapshot()->presets.success&&!control.Snapshot()->presets.dirty&&key(control).contribution==.8f&&Store(presets).Load().documents[0].parts[0].keys[0].weight==.8f,"save and close succeeds after read recovery using the retained draft");
        failRead=true;control.Service(1100);control.PostPreset(close);control.Service(1120);
        check(!control.Snapshot()->presets.success&&control.Snapshot()->data.source!=0,"clean preview also remains open when defaults cannot be read");
        failRead=false;control.Service(1400);control.Post(mhmcontrol::Action::Set,handle,0,.6f);control.Service(1420);
        failRead=true;control.Service(1700);Request disable{Action::DisableMorph};disable.discard=true;control.PostPreset(disable);control.Service(1720);
        check(control.Snapshot()->presets.success&&!control.Snapshot()->presets.enabled&&!control.Snapshot()->presets.dirty,"explicit discard and disable remains possible during read failure");
        failRead=false;control.Service(2000);check(!key(control).owned,"discarded disabled drafts cannot return after read recovery");
    }
    {
        const auto presets=directory/L"unnamed"/L"presets";morphgui::Control control;control.ConfigurePresets(FixturePaths(presets));control.Connect(checked,MhwMorphGetAssetIdentity);control.Service(100);
        control.Post(mhmcontrol::Action::Set,handle,0,.7f);control.Service(120);failRead=true;control.Service(400);
        check(control.Snapshot()->presets.dirty&&control.Snapshot()->presets.activeId.empty(),"unnamed drafts also remain dirty across read failures");
        control.PostPreset({Action::EndPreview});control.Service(420);check(!control.Snapshot()->presets.success,"unnamed draft cannot close without consent during read failure");
        failRead=false;control.Service(800);Request discard{Action::EndPreview};discard.discard=true;control.PostPreset(discard);control.Service(820);
        check(control.Snapshot()->presets.success&&!key(control).owned&&!control.Snapshot()->presets.dirty,"explicit discard releases an unnamed draft after read recovery");
    }
    for(const auto action:{Action::ScanImports,Action::Import,Action::Export,Action::ExportAll,Action::PurgeAll}){
        const auto presets=directory/std::to_string(static_cast<unsigned>(action))/L"presets";Store store(presets);store.Save(doc);DefaultsStore(presets).Save({{doc.id,MHM_HEAD}});
        auto incoming=doc;incoming.id=NewId();incoming.name="Imported recovery preset";
        WritePresetText(presets.parent_path()/L"imports"/L"incoming.json",Encode(incoming));store.Save(incoming,true);
        morphgui::Control control;control.ConfigurePresets(FixturePaths(presets));control.Connect(checked,MhwMorphGetAssetIdentity);control.Service(100);
        Request request{action,doc.id};
        if(action==Action::Import){control.PostPreset({Action::ScanImports});control.Service(110);const auto& item=control.Snapshot()->presets.incoming->front();request.imports.push_back({item.token,item.name});}
        control.Post(mhmcontrol::Action::Set,handle,0,.8f);control.Service(115);const auto previous=control.Snapshot()->data.source;
        loseLease=true;control.PostPreset(request);control.Post(mhmcontrol::Action::Set,handle,0,.9f);control.Service(120);
        check(!loseLease&&control.Snapshot()->presets.success,"batch IO encounters stale lease without rejecting its file operation");
        check(key(control).owned&&key(control).contribution==.25f&&control.Snapshot()->data.source!=previous,"batch lease loss automatically restores saved defaults and rejects later queued draft edits");
        check(!control.Snapshot()->presets.dirty&&control.Snapshot()->presets.activeId==doc.id,"lease recovery restores a clean saved editor");
        const auto restored=control.Snapshot()->data.source;control.Service(1000);control.Service(2000);
        check(key(control).owned&&key(control).contribution==.25f&&control.Snapshot()->data.source==restored,"ordinary later scans retain the recovered source without replaying drafts");
        check(Store(presets).Load().documents.front().parts[0].keys[0].weight==.25f,"lease recovery never saves a lost draft");
        if(action==Action::PurgeAll)check(!std::filesystem::exists(presets/L".trash"/(incoming.id+".json")),"purge completes while lease recovery remains independent");
    }
    {
        const auto presets=directory/L"catalog"/L"presets";Store store(presets);store.Save(doc);DefaultsStore policy(presets);policy.Save({{doc.id,MHM_HEAD}});
        const auto saved=managementtest::Read(presets/(doc.id+".json")),linked=managementtest::Read(presets.parent_path()/L"auto-presets.json");
        {std::ofstream broken(presets/L".trash");broken<<"not a directory";}
        morphgui::Control control;control.ConfigurePresets(FixturePaths(presets));control.Connect(checked,MhwMorphGetAssetIdentity);control.Service(100);
        check(control.Snapshot()->presets.library->size()==1&&control.Snapshot()->presets.trash->empty(),"trash directory failure does not hide the healthy normal library");
        check(control.Snapshot()->presets.defaults->size()==1&&key(control).owned&&key(control).contribution==.25f,"trash directory failure does not disable healthy automatic defaults");
        check(control.Snapshot()->presets.message.find("回收区")!=std::string::npos,"trash failure retains a specific visible error");
        check(managementtest::Read(presets/(doc.id+".json"))==saved&&managementtest::Read(presets.parent_path()/L"auto-presets.json")==linked,"partial library loading never rewrites valid preset or policy files");
        control.PostPreset({Action::Rename,doc.id,"Renamed recovery preset"});control.Service(120);
        check(control.Snapshot()->presets.success&&store.Load().documents.front().name=="Renamed recovery preset","normal preset management still works while the trash is unavailable");
    }
    Core().Detach(handle);return checks;
}
}
