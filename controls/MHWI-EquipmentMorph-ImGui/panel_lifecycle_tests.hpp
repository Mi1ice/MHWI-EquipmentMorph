#pragma once
namespace paneltest {
inline void Test5120PresetAtomicApply(){
    using namespace morphpreset;std::vector<uint64_t> handles;for(uint32_t p=0;p<5;++p)handles.push_back(Attach(p,1024));
    const auto paths=MakeStoragePaths(FixtureDirectory());morphgui::Control control;control.ConfigurePresets(paths);Check(control.Connect(Api(),MhwMorphGetAssetIdentity)==MHM_OK,"5120 control connects");
    for(auto h:handles)for(uint32_t k=0;k<1024;++k)control.Post(mhmcontrol::Action::Set,h,k,k%2?.123456791f:0);
    control.PostPreset({Action::SaveAs,{},"full 5120"});control.Service(1);Check(control.Snapshot()->presets.success&&control.Snapshot()->data.keys.back().contribution==.123456791f,"5120 atomic edits save barrier");
    auto doc=Store(paths.presets).Load().documents.front();Check(doc.parts.size()==5&&doc.parts.back().keys.size()==1024&&doc.parts[0].keys[0].weight==0,"full preset captures owned zeros");
    control.Post(mhmcontrol::Action::Set,handles[0],0,.8f);control.Service(2);control.PostPreset({Action::Apply,doc.id});control.Service(3);
    Check(control.Snapshot()->presets.success&&control.Snapshot()->data.keys[0].contribution==0&&control.Snapshot()->data.keys.back().contribution==.123456791f,"5120 whole preset restores first and last");
    auto bad=doc;bad.id=NewId();bad.name="mismatch";bad.parts.back().packSha=std::string(64,'f');Store(paths.presets).Save(bad);control.ConfigurePresets(paths);const auto revision=control.Snapshot()->data.revision;
    control.PostPreset({Action::Apply,bad.id});control.Service(4);Check(!control.Snapshot()->presets.success&&control.Snapshot()->data.revision==revision&&control.Snapshot()->data.keys.back().owned,"one mismatch rejects whole preset");
    uint64_t frame=0,requested=0;Check(Core().Capture(frame,requested)==MHM_OK,"capture synthetic frame");Check(Core().Acknowledge(frame)==MHM_OK,"acknowledge synthetic frame");Core().ReleaseFrame(frame);control.Service(21,true);
    Check(control.Snapshot()->data.keys.back().state.applied==.123456791f,"read applied only after actual acknowledgement");for(auto h:handles)Core().Detach(h);
}
inline void TestPreviewCloseRestoresDefaults(){
    using namespace morphpreset;const auto h=Attach(MHM_BODY,1024);const auto paths=MakeStoragePaths(FixtureDirectory());morphgui::Control control;control.ConfigurePresets(paths);control.Connect(Api(),MhwMorphGetAssetIdentity);
    control.Post(mhmcontrol::Action::Set,h,1023,.25f);control.PostPreset({Action::SaveAs,{},"default"});control.Service(0);const auto id=control.Snapshot()->presets.activeId;
    control.PostPreset({Action::SetDefault,id});control.Service(1);control.Post(mhmcontrol::Action::Set,h,1023,.8f);control.Service(2);
    morphpreset::Request close{Action::EndPreview};close.discard=true;control.PostPreset(close);control.Service(3);
    Check(control.Snapshot()->presets.success&&control.Snapshot()->data.keys.back().owned&&control.Snapshot()->data.keys.back().contribution==.25f,"close restores last key worn default");
    control.PostPreset({Action::ClearDefault,id});control.Service(4);control.Post(mhmcontrol::Action::Set,h,1023,.9f);control.Service(5);control.PostPreset(close);control.Service(6);
    Check(control.Snapshot()->presets.success&&!control.Snapshot()->data.source&&!control.Snapshot()->data.keys.back().owned,"close without default releases temporary control");Core().Detach(h);
}
inline void TestCloseCancelAndSaveFailure(){
    using namespace morphpreset;const auto h=Attach(MHM_BODY,489);const auto paths=MakeStoragePaths(FixtureDirectory());morphgui::Control control;control.ConfigurePresets(paths);control.Connect(Api(),MhwMorphGetAssetIdentity);
    control.Post(mhmcontrol::Action::Set,h,488,.2f);control.PostPreset({Action::SaveAs,{},"saved"});control.Service(0);const auto id=control.Snapshot()->presets.activeId;const auto file=paths.presets/(id+".json");const auto bytes=ReadFile(file);
    control.Post(mhmcontrol::Action::Set,h,488,.9f);control.Service(1);control.PostPreset({Action::EndPreview});control.Service(2);
    Check(!control.Snapshot()->presets.success&&control.Snapshot()->presets.dirty&&control.Snapshot()->data.keys.back().contribution==.9f,"unconfirmed close keeps draft");
    SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY);morphpreset::Request close{Action::EndPreview};close.saveBefore=true;control.PostPreset(close);control.Service(3);SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL);
    Check(!control.Snapshot()->presets.success&&control.Snapshot()->presets.dirty&&control.Snapshot()->data.keys.back().contribution==.9f&&ReadFile(file)==bytes,"failed save close keeps window contract and file");Core().Detach(h);
}
inline void TestLeaseAndRebind(){
    using namespace morphpreset;auto h=Attach(MHM_BODY,489);const auto paths=MakeStoragePaths(FixtureDirectory());morphgui::Control control;control.ConfigurePresets(paths);control.Connect(Api(),MhwMorphGetAssetIdentity);
    control.Post(mhmcontrol::Action::Set,h,488,.3f);control.PostPreset({Action::SaveAs,{},"lease default"});control.Service(0);const auto id=control.Snapshot()->presets.activeId;control.PostPreset({Action::SetDefault,id});control.Service(1);
    control.Post(mhmcontrol::Action::Set,h,488,.9f);control.Service(2);const auto old=control.Snapshot()->data.source;Core().Tick(60000);control.Service(60000);
    Check(control.Snapshot()->data.source!=old&&control.Snapshot()->data.keys.back().contribution==.3f,"lease loss restores saved default only");
    Core().Detach(h);h=Attach(MHM_BODY,489);control.Service(60300);Check(control.Snapshot()->data.keys.back().target==h&&control.Snapshot()->data.keys.back().contribution==.3f,"new generation matches registered default without old draft");Core().Detach(h);
}
inline void TestSinglePartReplacement(){const auto body=Attach(MHM_BODY,1024);auto arm=Attach(MHM_ARMS,1024);morphgui::Control control;control.Connect(Api());
    control.Post(mhmcontrol::Action::Set,body,1023,.7f);control.Post(mhmcontrol::Action::Set,arm,1023,.8f);control.Service(0);Core().Detach(arm);arm=Attach(MHM_ARMS,1024);control.Service(300);
    Check(control.Snapshot()->data.keys[1023].owned&&control.Snapshot()->data.keys[1023].contribution==.7f&&!control.Snapshot()->data.keys.back().owned,"one part replacement preserves healthy large target");Core().Detach(body);Core().Detach(arm);
}
inline void LifecycleTests(){Test5120PresetAtomicApply();TestPreviewCloseRestoresDefaults();TestCloseCancelAndSaveFailure();TestLeaseAndRebind();TestSinglePartReplacement();}
}
