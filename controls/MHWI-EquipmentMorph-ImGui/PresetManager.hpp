#pragma once
#include "PresetStore.hpp"
#include "../../runtime/EquipmentMorphControlBridge/Session.hpp"
#include <memory>

namespace morphpreset {
using IdentityQuery=decltype(&MhwMorphGetAssetIdentity);
struct Choice {std::string id,name,status;bool compatible=false;unsigned matched=0,total=0;std::vector<PartMatch> parts;};
enum class Filter {All,Complete,Partial,None};
inline bool Visible(const Choice& choice,std::string_view search,Filter filter){
    const auto fold=[](unsigned char c){return c>='A'&&c<='Z'?static_cast<unsigned char>(c+'a'-'A'):c;};
    if(!search.empty()&&std::search(choice.name.begin(),choice.name.end(),search.begin(),search.end(),[&](unsigned char a,unsigned char b){return fold(a)==fold(b);})==choice.name.end())return false;
    return filter==Filter::All||(filter==Filter::Complete&&choice.compatible)||(filter==Filter::Partial&&choice.matched>0&&!choice.compatible)||(filter==Filter::None&&choice.matched==0);
}
struct State {
    std::shared_ptr<const std::vector<Choice>> choices=std::make_shared<std::vector<Choice>>();
    std::shared_ptr<const std::vector<Choice>> removed=std::make_shared<std::vector<Choice>>();
    std::shared_ptr<const std::vector<Document>> library=std::make_shared<std::vector<Document>>(),trash=std::make_shared<std::vector<Document>>();
    std::string activeId,activeName,message,lastId;
    bool available=false,dirty=false,success=false,selectLast=false;uint64_t completed=0;
};
enum class Action {Apply,Save,SaveAs,Rename,Trash,Restore,Purge,PurgeAll};
inline bool Management(Action action){return action==Action::Rename||action==Action::Trash||action==Action::Restore||action==Action::Purge||action==Action::PurgeAll;}
struct Request {Action action=Action::Apply;std::string id,name;bool deleted=false;};
// Worker-only. Disk IO, identity queries and batch submission never run in Draw.
class Manager {
    std::unique_ptr<Store> store;std::vector<Document> documents,deleted;Identities identities{};
    IdentityQuery query=nullptr;State state;
    std::array<uint64_t,5> identityHandles{};bool lastReady=false,choicesDirty=true;
    const Document* Find(const std::string& id,bool removed=false)const{const auto& list=removed?deleted:documents;const auto it=std::find_if(list.begin(),list.end(),[&](const auto& doc){return doc.id==id;});return it==list.end()?nullptr:&*it;}
    void PublishLibrary(){
        const auto sort=[](auto& list){std::sort(list.begin(),list.end(),[](const auto& a,const auto& b){return a.name==b.name?a.id<b.id:a.name<b.name;});};sort(documents);sort(deleted);
        state.library=std::make_shared<std::vector<Document>>(documents);state.trash=std::make_shared<std::vector<Document>>(deleted);
        choicesDirty=true;
    }
    void CheckName(const std::string& name,const std::string& id)const{
        if(!Utf8(name))throw std::runtime_error("请输入有效名称（最多 128 个 UTF-8 字节）");
        for(const auto& other:documents)if(other.id!=id&&other.name==name)throw std::runtime_error("同名预设已存在，请更换名称");
    }
    void Manage(const Request& request,mhmcontrol::Session* session){
        if(request.action==Action::PurgeAll){
            uint64_t renewAt=0;bool leaseLost=false;
            const auto result=store->PurgeAll([&](){const auto now=GetTickCount64();if(session&&now>=renewAt){if(session->Heartbeat()!=MHM_OK)leaseLost=true;renewAt=now+500;}});
            std::erase_if(deleted,[&](const auto& doc){return std::find(result.ids.begin(),result.ids.end(),doc.id)!=result.ids.end();});
            PublishLibrary();state.lastId.clear();state.success=result.failed==0;
            state.message="已永久删除 "+std::to_string(result.ids.size())+" 份回收区预设";
            if(result.failed)state.message+="；"+std::to_string(result.failed)+" 份删除失败，原文件保留，请检查权限";
            if(result.skipped)state.message+="；保留 "+std::to_string(result.skipped)+" 项非预设文件或目录";
            if(leaseLost)state.message+="；控制租期已失效，请重新调节";
            return;
        }
        const bool removed=request.action==Action::Restore||request.action==Action::Purge||(request.action==Action::Rename&&request.deleted);
        const auto* existing=Find(request.id,removed);if(!existing)throw std::runtime_error("预设不存在，请重新选择");Document doc=*existing;
        if(request.action==Action::Rename){
            CheckName(request.name,doc.id);doc.name=request.name;store->Save(doc,removed);
            auto& list=removed?deleted:documents;for(auto& item:list)if(item.id==doc.id)item=doc;
            if(!removed&&state.activeId==doc.id)state.activeName=doc.name;state.message="已重命名："+doc.name;
        }else if(request.action==Action::Trash){
            if(deleted.size()>=1024)throw std::runtime_error("回收区已满，请先恢复或手动整理文件");
            if(Find(doc.id,true))throw std::runtime_error("回收区已有相同编号，原预设保留");
            store->Trash(doc.id);deleted.push_back(doc);std::erase_if(documents,[&](const auto& item){return item.id==doc.id;});
            if(state.activeId==doc.id){state.activeId.clear();state.activeName.clear();}
            state.message="已移入回收区："+doc.name+"；当前形态键控制保持";
        }else if(request.action==Action::Purge){
            store->Purge(doc.id);std::erase_if(deleted,[&](const auto& item){return item.id==doc.id;});state.message="已永久删除："+doc.name+"；当前形态键控制保持";
        }else{
            if(documents.size()>=256)throw std::runtime_error("预设数量已满，请先整理当前预设");
            if(Find(doc.id))throw std::runtime_error("当前列表已有相同编号，无法覆盖恢复");
            for(const auto& other:documents)if(other.name==doc.name)throw std::runtime_error("同名预设已存在，请先重命名回收区中的预设");
            store->Restore(doc.id);documents.push_back(doc);std::erase_if(deleted,[&](const auto& item){return item.id==doc.id;});state.message="已恢复："+doc.name+"；选择并点击应用后才会改变权重";
        }
        PublishLibrary();state.lastId=doc.id;state.success=true;
    }
public:
    const State& Snapshot()const{return state;}
    void Configure(const std::filesystem::path& directory){
        store=std::make_unique<Store>(directory);
        try{auto catalog=store->Load();auto removed=store->Load(true);documents=std::move(catalog.documents);deleted=std::move(removed.documents);PublishLibrary();if(catalog.invalid+removed.invalid)state.message="已跳过 "+std::to_string(catalog.invalid+removed.invalid)+" 个无效预设文件";}
        catch(const std::exception& e){state.message=e.what();}
    }
    void SetQuery(IdentityQuery value){query=value;state.available=query!=nullptr;identityHandles={};lastReady=false;choicesDirty=true;}
    void Update(const mhmcontrol::Response& data,bool ready){
        std::array<uint64_t,5> handles{};if(ready)for(uint32_t i=0;i<data.targetCount;i++)handles[data.targets[i].info.part]=data.targets[i].info.handle;
        if(handles!=identityHandles||ready!=lastReady){
            identities={};identityHandles=handles;lastReady=ready;choicesDirty=true;
            if(query&&ready)for(uint32_t i=0;i<data.targetCount;i++){
                const auto& target=data.targets[i];MhmAssetIdentity identity{};
                if(query(MHM_IDENTITY_VERSION,sizeof(identity),target.info.handle,&identity)==MHM_OK&&ValidIdentity(identity,target.info.handle))identities[target.info.part]=identity;
            }
        }
        const auto choices=[&](const auto& list){
            auto result=std::make_shared<std::vector<Choice>>();for(const auto& doc:list){auto match=Check(doc,data,identities);
                result->push_back({doc.id,doc.name,query?(ready?match.message:"等待核心或装备绑定"):"核心不支持预设身份查询，请更新配套核心",query&&ready&&match.compatible,query&&ready?match.matched:0,match.total,std::move(match.parts)});
            }return result;
        };
        if(choicesDirty){state.choices=choices(documents);state.removed=choices(deleted);choicesDirty=false;}
        const auto* active=Find(state.activeId);
        state.dirty=active?(!ready||!SameContributions(*active,data,identities)):std::any_of(data.keys.begin(),data.keys.begin()+data.keyCount,[](const auto& key){return key.owned!=0;});
    }
    void Fail(uint64_t ticket,const std::string& message){state.completed=ticket;state.success=false;state.selectLast=false;state.message=message;}
    MhmResult Process(const Request& request,uint64_t ticket,mhmcontrol::Session* session,const mhmcontrol::Response& data,bool ready){
        state.completed=ticket;state.success=false;state.selectLast=false;
        try{
            if(!store)throw std::runtime_error("预设目录尚未就绪");
            if(Management(request.action)){Manage(request,session);return state.success?MHM_OK:MHM_INVALID;}
            if(!session||!query||!ready)throw std::runtime_error("预设尚未就绪，请检查配套核心与装备绑定");
            const auto* existing=Find(request.id);Document doc;
            if(request.action==Action::Apply){
                if(!existing)throw std::runtime_error("预设不存在，请重新选择");const auto match=Check(*existing,data,identities);
                if(!match.compatible)throw std::runtime_error(match.message);
                const auto result=session->Replace(match.updates);if(result!=MHM_OK){state.message="应用失败，原控制保持（接口结果 "+std::to_string(result)+"）";return result;}
                doc=*existing;state.message="已应用："+doc.name;
            }else{
                if(request.action==Action::Save&&(!existing||request.id!=state.activeId))throw std::runtime_error("请先应用该预设，或使用另存为");
                const auto id=request.action==Action::Save?existing->id:NewId();
                const auto name=request.action==Action::Save?existing->name:request.name;
                CheckName(name,id);
                if(request.action==Action::SaveAs&&documents.size()>=256)throw std::runtime_error("最多支持 256 个预设");
                doc=Capture(data,identities,id,name);store->Save(doc);
                const auto found=std::find_if(documents.begin(),documents.end(),[&](const auto& item){return item.id==id;});
                if(found==documents.end())documents.push_back(doc);else *found=doc;
                PublishLibrary();state.message="已保存："+doc.name;
            }
            state.activeId=doc.id;state.activeName=doc.name;state.lastId=doc.id;state.success=true;state.selectLast=true;return MHM_OK;
        }catch(const std::exception& e){state.message=e.what();return MHM_INVALID;}
    }
};
}
