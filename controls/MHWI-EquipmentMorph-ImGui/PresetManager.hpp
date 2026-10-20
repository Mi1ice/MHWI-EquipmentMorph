#pragma once
#include "PresetStore.hpp"
#include "../../runtime/EquipmentMorphControlBridge/Session.hpp"
#include <memory>

namespace morphpreset {
using IdentityQuery=decltype(&MhwMorphGetAssetIdentity);
struct Choice {std::string id,name,status;bool compatible=false;};
struct State {
    std::shared_ptr<const std::vector<Choice>> choices=std::make_shared<std::vector<Choice>>();
    std::string activeId,activeName,message,lastId;
    bool available=false,dirty=false,success=false;uint64_t completed=0;
};
enum class Action {Apply,Save,SaveAs};
struct Request {Action action=Action::Apply;std::string id,name;};
// Worker-only. Disk IO, identity queries and batch submission never run in Draw.
class Manager {
    std::unique_ptr<Store> store;std::vector<Document> documents;Identities identities{};
    IdentityQuery query=nullptr;State state;
    const Document* Find(const std::string& id)const{const auto it=std::find_if(documents.begin(),documents.end(),[&](const auto& doc){return doc.id==id;});return it==documents.end()?nullptr:&*it;}
public:
    const State& Snapshot()const{return state;}
    void Configure(const std::filesystem::path& directory){
        store=std::make_unique<Store>(directory);
        try{auto catalog=store->Load();documents=std::move(catalog.documents);if(catalog.invalid)state.message="已跳过 "+std::to_string(catalog.invalid)+" 个无效预设文件";}
        catch(const std::exception& e){state.message=e.what();}
    }
    void SetQuery(IdentityQuery value){query=value;state.available=query!=nullptr;}
    void Update(const mhmcontrol::Response& data,bool ready){
        identities={};
        if(query&&ready)for(uint32_t i=0;i<data.targetCount;i++){
            const auto& target=data.targets[i];MhmAssetIdentity identity{};
            if(query(MHM_IDENTITY_VERSION,sizeof(identity),target.info.handle,&identity)==MHM_OK&&ValidIdentity(identity,target.info.handle))identities[target.info.part]=identity;
        }
        auto choices=std::make_shared<std::vector<Choice>>();
        for(const auto& doc:documents){const auto match=Check(doc,data,identities);choices->push_back({doc.id,doc.name,query?match.message:"核心不支持预设身份查询，请更新配套核心",query&&ready&&match.compatible});}
        state.choices=std::move(choices);const auto* active=Find(state.activeId);state.dirty=active&&(!ready||!SameContributions(*active,data,identities));
    }
    void Fail(uint64_t ticket,const std::string& message){state.completed=ticket;state.success=false;state.message=message;}
    MhmResult Process(const Request& request,uint64_t ticket,mhmcontrol::Session& session,const mhmcontrol::Response& data,bool ready){
        state.completed=ticket;state.success=false;
        try{
            if(!store||!query||!ready)throw std::runtime_error("预设尚未就绪，请检查配套核心与装备绑定");
            const auto* existing=Find(request.id);Document doc;
            if(request.action==Action::Apply){
                if(!existing)throw std::runtime_error("预设不存在，请重新选择");const auto match=Check(*existing,data,identities);
                if(!match.compatible)throw std::runtime_error(match.message);
                const auto result=session.Replace(match.updates);if(result!=MHM_OK){state.message="应用失败，原控制保持（接口结果 "+std::to_string(result)+"）";return result;}
                doc=*existing;state.message="已应用："+doc.name;
            }else{
                if(request.action==Action::Save&&(!existing||request.id!=state.activeId))throw std::runtime_error("请先应用该预设，或使用另存为");
                const auto id=request.action==Action::Save?existing->id:NewId();
                const auto name=request.action==Action::Save?existing->name:request.name;
                for(const auto& other:documents)if(other.id!=id&&other.name==name)throw std::runtime_error("预设名称已存在，请使用其他名称");
                if(request.action==Action::SaveAs&&documents.size()>=256)throw std::runtime_error("最多支持 256 个预设");
                doc=Capture(data,identities,id,name);store->Save(doc);
                const auto found=std::find_if(documents.begin(),documents.end(),[&](const auto& item){return item.id==id;});
                if(found==documents.end())documents.push_back(doc);else *found=doc;
                std::sort(documents.begin(),documents.end(),[](const auto& a,const auto& b){return a.name==b.name?a.id<b.id:a.name<b.name;});state.message="已保存："+doc.name;
            }
            state.activeId=doc.id;state.activeName=doc.name;state.lastId=doc.id;state.success=true;return MHM_OK;
        }catch(const std::exception& e){state.message=e.what();return MHM_INVALID;}
    }
};
}
