#pragma once
#include "PresetTransfer.hpp"
#include "PresetDefaults.hpp"
#include "PanelSettings.hpp"
#include "EquipmentLabels.hpp"
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
    std::shared_ptr<const std::vector<Incoming>> incoming=std::make_shared<std::vector<Incoming>>();
    std::shared_ptr<const std::vector<DefaultRef>> defaults=std::make_shared<std::vector<DefaultRef>>();
    std::array<std::string,5> worn{};std::string autoMessage;bool paused=false,canSave=false,preview=false,enabled=true;
    std::string activeId,activeName,message,lastId;
    bool available=false,dirty=false,success=false,selectLast=false;uint64_t completed=0;
};
enum class Action {Apply,Save,SaveAs,Rename,Trash,Restore,Purge,PurgeAll,ScanImports,Import,Export,ExportAll,Select,SetDefault,ClearDefault,Pause,Resume,EndPreview,EnableMorph,DisableMorph};
inline bool Management(Action action){return action!=Action::Apply&&action!=Action::Select&&action!=Action::Save&&action!=Action::SaveAs;}
struct Request {Action action=Action::Apply;std::string id,name;bool deleted=false;std::vector<ImportEdit> imports;bool saveBefore=false,discard=false;std::string editor;bool checkEditor=false;};
// Worker-only. Disk IO, identity queries and batch submission never run in Draw.
class Manager {
    std::unique_ptr<Store> store;std::vector<Document> documents,deleted;Identities identities{};
    std::unique_ptr<Transfer> transfer;std::vector<Incoming> incoming;
    std::unique_ptr<DefaultsStore> defaultsStore;std::vector<DefaultRef> defaults;
    std::unique_ptr<PanelSettings> settings;std::array<std::string,5> appliedIds{};
    struct Origin {std::string preset;uint64_t handle=0;bool automatic=false;};std::array<Origin,5> origins{};
    std::array<uint64_t,5> observed{};uint32_t force=0;bool policyLoadFailed=false;
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
    void PublishDefaults(){state.defaults=std::make_shared<std::vector<DefaultRef>>(defaults);choicesDirty=true;}
    void StoreDefaults(std::vector<DefaultRef> next){next=ResolveDefaults(next,documents);defaultsStore->Save(next);defaults=std::move(next);policyLoadFailed=false;PublishDefaults();}
    static uint32_t PartOf(const mhmcontrol::Response& data,uint64_t handle){for(uint32_t i=0;i<data.targetCount;i++)if(data.targets[i].info.handle==handle)return data.targets[i].info.part;return 5;}
    static std::vector<MhmUpdate> Owned(const mhmcontrol::Response& data){std::vector<MhmUpdate> result;for(uint32_t i=0;i<data.keyCount;i++){const auto& key=data.keys[i];if(key.owned)result.push_back({key.target,key.index,key.contribution});}return result;}
    Match PartCheck(const Document& doc,const Part& part,const mhmcontrol::Response& data)const{return Check({doc.id,doc.name,{part}},data,identities);}
    Document EditedDocument(const Document& existing,const mhmcontrol::Response& data)const{
        auto doc=existing;
        for(uint32_t i=0;i<data.targetCount;i++){
            const auto& target=data.targets[i];const auto partId=target.info.part;
            if(origins[partId].preset!=doc.id||origins[partId].handle!=target.info.handle)continue;
            if(!ValidIdentity(identities[partId],target.info.handle))throw std::runtime_error("编辑部位身份尚未就绪");
            Part part;part.part=partId;part.model=target.info.model_id;part.modelSha=identities[partId].model_sha256;part.packSha=identities[partId].pack_sha256;
            for(uint32_t k=0;k<target.count;k++){const auto& key=data.keys[target.first+k];if(key.owned)part.keys.push_back({key.info.id,key.contribution});}
            std::erase_if(doc.parts,[&](const auto& item){return item.part==partId;});if(!part.keys.empty())doc.parts.push_back(std::move(part));
        }
        if(doc.parts.empty())throw std::runtime_error("不能保存空预设；可保留零权重接管，或取消穿戴默认");Validate(doc);return doc;
    }
    void SaveDocument(const Document& doc){
        auto nextDocuments=documents;for(auto& item:nextDocuments)if(item.id==doc.id)item=doc;
        const auto nextDefaults=ResolveDefaults(defaults,nextDocuments);const bool changed=nextDefaults!=defaults;
        if(changed)defaultsStore->Save(nextDefaults);
        try{store->Save(doc);}catch(...){if(changed){try{defaultsStore->Save(defaults);}catch(...){state.autoMessage="自动关联回滚失败，请重新设置穿戴默认";defaults=nextDefaults;PublishDefaults();}}throw;}
        documents=std::move(nextDocuments);if(changed){defaults=nextDefaults;PublishDefaults();}PublishLibrary();
    }
    void AssociateOwned(const std::string& id,const mhmcontrol::Response& data,bool replaceAll){
        if(replaceAll)origins={};
        for(uint32_t i=0;i<data.keyCount;i++)if(data.keys[i].owned){const auto part=PartOf(data,data.keys[i].target);if(part<5)origins[part]={id,data.keys[i].target,false};}
        for(uint32_t i=0;i<data.targetCount;i++)observed[data.targets[i].info.part]=data.targets[i].info.handle;
    }
    MhmResult Select(const Request& request,mhmcontrol::Session& session,const mhmcontrol::Response& data){
        if(request.checkEditor&&request.editor!=state.activeId)throw std::runtime_error("编辑对象已因装备变化而改变，请重新选择");
        const auto* selected=Find(request.id);if(!selected)throw std::runtime_error("预设不存在");Document doc=*selected;
        const auto* previous=Find(state.activeId);
        if(state.dirty&&!request.saveBefore&&!request.discard)throw std::runtime_error("当前有未保存修改，请选择保存、放弃或取消");
        if(state.dirty&&request.saveBefore&&!previous)throw std::runtime_error("当前配置尚未命名，请先另存为");
        if(state.dirty&&previous&&request.saveBefore){SaveDocument(EditedDocument(*previous,data));doc=*Find(request.id);}
        auto updates=Owned(data);auto next=origins;unsigned matched=0;
        const auto replace=[&](uint32_t part,const std::vector<MhmUpdate>& values){std::erase_if(updates,[&](const auto& item){return PartOf(data,item.target)==part;});updates.insert(updates.end(),values.begin(),values.end());};
        previous=Find(state.activeId);
        if(state.dirty&&!previous&&request.discard){std::erase_if(updates,[&](const auto& item){const auto part=PartOf(data,item.target);return part<5&&(!origins[part].automatic||!Find(origins[part].preset));});for(auto& origin:next)if(!origin.automatic||!Find(origin.preset))origin={};}
        if(state.dirty&&previous&&request.discard)for(uint32_t part=0;part<5;part++)if(origins[part].preset==previous->id){
            const auto* saved=FindPart(*previous,part);if(saved){const auto match=PartCheck(*previous,*saved,data);if(match.compatible){replace(part,match.updates);next[part]={previous->id,match.updates.front().target,false};}}else{replace(part,{});next[part]={};}
        }
        for(const auto& part:doc.parts){const auto match=PartCheck(doc,part,data);if(!match.compatible)continue;
            replace(part.part,match.updates);next[part.part]={doc.id,match.updates.front().target,false};matched++;
        }
        if(!matched)throw std::runtime_error("当前没有与此预设匹配的部位");
        const auto result=session.Replace(updates);if(result!=MHM_OK){state.message="选择应用失败，原权重保持（接口结果 "+std::to_string(result)+"）";return result;}
        origins=std::move(next);for(uint32_t i=0;i<data.targetCount;i++)observed[data.targets[i].info.part]=data.targets[i].info.handle;
        state.activeId=doc.id;state.activeName=doc.name;state.lastId=doc.id;state.success=true;state.selectLast=true;state.preview=true;
        state.message.clear();return MHM_OK;
    }
    void Preview(const mhmcontrol::Response& data,bool ready){
        auto result=std::make_shared<std::vector<Incoming>>(incoming);std::vector<std::string> names,signatures;
        for(const auto& doc:documents){names.push_back(doc.name);if(!incoming.empty())signatures.push_back(Contents(doc,false));}
        for(auto& item:*result)if(item.document){
            const auto signature=Contents(*item.document,false);
            for(unsigned suffix=0;suffix<=512;suffix++){
                item.name=ImportName(item.document->name,suffix);item.duplicate=false;
                for(size_t i=0;i<documents.size();i++)if(documents[i].name==item.name&&signatures[i]==signature){item.duplicate=true;break;}
                if(item.duplicate||std::find(names.begin(),names.end(),item.name)==names.end())break;
            }
            names.push_back(item.name);const auto match=Check(*item.document,data,identities);
            item.status=query&&ready?match.message:"当前无法评估装备匹配；仍可导入";
        }state.incoming=std::move(result);
    }
    void Exchange(const Request& request,mhmcontrol::Session* session){
        uint64_t renewAt=0;bool leaseLost=false;
        const auto keepAlive=[&](){const auto now=GetTickCount64();if(session&&now>=renewAt){if(session->Heartbeat()!=MHM_OK)leaseLost=true;renewAt=now+500;}};
        if(request.action==Action::ScanImports){auto scanned=transfer->Scan(keepAlive);incoming=std::move(scanned);choicesDirty=true;state.success=true;state.message="已刷新导入预览；修改源文件后请再次刷新";}
        else if(request.action==Action::Import){
            if(request.imports.empty()||request.imports.size()>256)throw std::runtime_error("请选择 1 至 256 份预设");
            unsigned count=0,skipped=0,failed=0;std::set<std::string> seen;std::string firstError;
            for(const auto& edit:request.imports){
                keepAlive();try{
                    if(!seen.insert(edit.token).second)throw std::runtime_error("同一导入条目重复提交");
                    const auto it=std::find_if(incoming.begin(),incoming.end(),[&](const auto& item){return item.token==edit.token;});
                    if(it==incoming.end()||!it->document)throw std::runtime_error("导入预览已失效或文件无效，请刷新");
                    auto doc=*it->document;doc.name=edit.name;Validate(doc);const auto signature=Contents(doc);
                    if(std::any_of(documents.begin(),documents.end(),[&](const auto& other){return other.name==doc.name&&Contents(other)==signature;})){skipped++;continue;}
                    CheckName(doc.name,{});if(documents.size()>=256)throw std::runtime_error("预设库已满");
                    do{doc.id=NewId();}while(Find(doc.id)||Find(doc.id,true));store->Save(doc,false,false);documents.push_back(std::move(doc));count++;
                }catch(const std::exception& e){failed++;if(firstError.empty())firstError=e.what();}
            }
            PublishLibrary();state.success=failed==0;state.message="导入完成：新增 "+std::to_string(count)+"，重复跳过 "+std::to_string(skipped)+"，失败 "+std::to_string(failed);
            if(!firstError.empty())state.message+="；"+firstError;
        }else{
            std::vector<Document> output;
            if(request.action==Action::ExportAll)output=documents;
            else{const auto* doc=Find(request.id);if(!doc)throw std::runtime_error("请选择正常列表中的预设");output.push_back(*doc);}
            const auto result=transfer->Export(output,keepAlive);state.success=result.failed==0;
            state.message="已导出 "+std::to_string(result.count)+" 份，失败 "+std::to_string(result.failed)+"；位置：nativePC/plugins/MHWEquipmentMorph/"+result.folder;
            if(!result.error.empty())state.message+="；"+result.error;
        }
        state.lastId.clear();if(leaseLost)state.message+="；控制租期已失效，请重新调节";
    }
    void ChooseEditor(){
        std::string candidate;bool mixed=false;for(const auto& origin:origins)if(!origin.preset.empty()){if(candidate.empty())candidate=origin.preset;else if(candidate!=origin.preset)mixed=true;}
        if(!mixed)if(const auto* doc=Find(candidate)){state.activeId=doc->id;state.activeName=doc->name;}
    }
    void Manage(const Request& request,mhmcontrol::Session* session,const mhmcontrol::Response& data){
        if(request.action==Action::DisableMorph){
            if(request.checkEditor&&request.editor!=state.activeId)throw std::runtime_error("编辑对象已改变，请重新关闭捏人");
            if(state.dirty&&!request.discard){const auto* doc=Find(state.activeId);if(!request.saveBefore||!doc||!state.canSave)throw std::runtime_error("请先保存或明确放弃未保存修改，再关闭捏人");SaveDocument(EditedDocument(*doc,data));}
            settings->Save(false);state.enabled=false;SourceExpired();
            const auto result=session?session->Release():MHM_OK;
            if(result!=MHM_OK)throw std::runtime_error("关闭状态已保存，但释放控制失败；请等待控制租期结束");
            state.success=true;state.message="已关闭捏人";return;
        }
        if(request.action==Action::EnableMorph){
            if(policyLoadFailed){defaults=ResolveDefaults(defaultsStore->Load(),documents);PublishDefaults();policyLoadFailed=false;}
            settings->Save(true);state.enabled=true;state.paused=false;SourceExpired();force=31;state.autoMessage.clear();state.success=true;state.message="已开启捏人";return;
        }
        if(request.action==Action::EndPreview){
            if(request.checkEditor&&request.editor!=state.activeId)throw std::runtime_error("编辑对象已改变，请重新关闭窗口");
            if(state.dirty&&!request.discard){const auto* doc=Find(state.activeId);if(!request.saveBefore||!doc||!state.canSave)throw std::runtime_error("请先保存或明确放弃未保存修改，再关闭预览");SaveDocument(EditedDocument(*doc,data));}
            std::vector<MhmUpdate> updates;std::array<Origin,5> next{};
            if(query&&lastReady&&state.enabled&&!state.paused)for(uint32_t part=0;part<5;part++)for(const auto& ref:defaults){
                if(ref.part!=part)continue;const auto* doc=Find(ref.preset);const auto* asset=doc?FindPart(*doc,part):nullptr;if(!asset)continue;
                const auto match=PartCheck(*doc,*asset,data);if(!match.compatible)continue;
                updates.insert(updates.end(),match.updates.begin(),match.updates.end());next[part]={doc->id,match.updates.front().target,true};break;
            }
            const auto result=session?session->Replace(updates):MHM_OK;
            if(result!=MHM_OK)throw std::runtime_error("恢复穿戴默认失败（接口结果 "+std::to_string(result)+"）；窗口保持打开，请重试");
            origins=std::move(next);observed={};force=0;
            if(lastReady)for(uint32_t i=0;i<data.targetCount;i++)observed[data.targets[i].info.part]=data.targets[i].info.handle;
            state.activeId.clear();state.activeName.clear();ChooseEditor();state.preview=false;state.success=true;
            state.message=state.paused?"预览已结束；自动使用仍暂停":updates.empty()?"预览已结束；无匹配默认，已释放临时控制":"预览已结束；已恢复各部位的穿戴默认";return;
        }
        if(request.action==Action::Pause){
            if(request.checkEditor&&request.editor!=state.activeId)throw std::runtime_error("编辑对象已改变，请重新操作");
            if(state.dirty&&!request.discard){const auto* doc=Find(state.activeId);if(!request.saveBefore||!doc||!state.canSave)throw std::runtime_error("请先保存或明确放弃未保存修改，再暂时还原");SaveDocument(EditedDocument(*doc,data));}
            if(session&&session->Release()!=MHM_OK)throw std::runtime_error("暂时还原失败，请稍后重试");Pause();state.success=true;state.message="已暂时还原并暂停自动使用；恢复自动使用或重启游戏后按已保存预设恢复";return;
        }
        if(request.action==Action::Resume){if(!state.enabled)throw std::runtime_error("请先开启捏人");if(policyLoadFailed){defaults=ResolveDefaults(defaultsStore->Load(),documents);PublishDefaults();policyLoadFailed=false;}state.paused=false;observed={};force=31;state.autoMessage.clear();state.success=true;state.message="已重试应用";return;}
        if(request.action==Action::SetDefault||request.action==Action::ClearDefault){
            const auto* doc=Find(request.id);if(!doc)throw std::runtime_error("预设不存在");auto next=defaults;
            std::erase_if(next,[&](const auto& ref){return ref.preset==doc->id;});
            if(request.action==Action::SetDefault)for(const auto& part:doc->parts)next.push_back({doc->id,part.part});
            StoreDefaults(std::move(next));state.autoMessage.clear();state.success=true;
            state.message=request.action==Action::SetDefault?"已应用预设":"已取消应用";return;
        }
        if(request.action==Action::ScanImports||request.action==Action::Import||request.action==Action::Export||request.action==Action::ExportAll){Exchange(request,session);return;}
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
            auto next=defaults;std::erase_if(next,[&](const auto& ref){return ref.preset==doc.id;});const bool unlinked=next!=defaults;if(unlinked)StoreDefaults(std::move(next));
            try{store->Trash(doc.id);}catch(const std::exception& e){throw std::runtime_error((unlinked?std::string("穿戴默认已取消，但移入回收区失败："):std::string{})+e.what());}
            deleted.push_back(doc);std::erase_if(documents,[&](const auto& item){return item.id==doc.id;});
            if(state.activeId==doc.id){state.activeId.clear();state.activeName.clear();}
            state.message="已移入回收区："+doc.name+"；当前形态键控制保持";
        }else if(request.action==Action::Purge){
            store->Purge(doc.id);std::erase_if(deleted,[&](const auto& item){return item.id==doc.id;});state.message="已永久删除："+doc.name+"；当前形态键控制保持";
        }else{
            if(documents.size()>=256)throw std::runtime_error("预设数量已满，请先整理当前预设");
            if(Find(doc.id))throw std::runtime_error("当前列表已有相同编号，无法覆盖恢复");
            for(const auto& other:documents)if(other.name==doc.name)throw std::runtime_error("同名预设已存在，请先重命名回收区中的预设");
            store->Restore(doc.id);documents.push_back(doc);std::erase_if(deleted,[&](const auto& item){return item.id==doc.id;});state.message="已恢复："+doc.name+"；不会恢复穿戴默认关联，主面板选中后才应用";
        }
        PublishLibrary();state.lastId=doc.id;state.success=true;
    }
public:
    const State& Snapshot()const{return state;}
    void Configure(const std::filesystem::path& directory){
        store=std::make_unique<Store>(directory);
        transfer=std::make_unique<Transfer>(directory);incoming.clear();state.incoming=std::make_shared<std::vector<Incoming>>();
        defaultsStore=std::make_unique<DefaultsStore>(directory);defaults.clear();observed={};force=0;
        try{auto catalog=store->Load();auto removed=store->Load(true);documents=std::move(catalog.documents);deleted=std::move(removed.documents);PublishLibrary();if(catalog.invalid+removed.invalid)state.message="已跳过 "+std::to_string(catalog.invalid+removed.invalid)+" 个无效预设文件";}
        catch(const std::exception& e){state.message=e.what();}
        policyLoadFailed=false;try{defaults=ResolveDefaults(defaultsStore->Load(),documents);state.autoMessage.clear();}catch(const std::exception& e){policyLoadFailed=true;state.autoMessage=std::string("自动配置读取失败，本次未启用自动使用：")+e.what();}PublishDefaults();
        settings=std::make_unique<PanelSettings>(directory);try{state.enabled=settings->Load();}catch(const std::exception& e){state.enabled=false;state.autoMessage=std::string("开关配置读取失败，捏人已关闭：")+e.what();}
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
        if(choicesDirty){
            state.choices=choices(documents);state.removed=choices(deleted);Preview(data,ready);appliedIds={};
            if(query&&ready)for(const auto& ref:defaults){const auto* doc=Find(ref.preset);const auto* asset=doc?FindPart(*doc,ref.part):nullptr;if(asset&&PartCheck(*doc,*asset,data).compatible)appliedIds[ref.part]=doc->id;}
            choicesDirty=false;
        }
        const auto* active=Find(state.activeId);state.canSave=false;
        for(uint32_t i=0;i<data.targetCount;i++){const auto& target=data.targets[i];if(active&&origins[target.info.part].preset==active->id&&origins[target.info.part].handle==target.info.handle)state.canSave=ready;}
        if(active){try{state.dirty=state.canSave&&Contents(EditedDocument(*active,data))!=Contents(*active);}catch(const std::exception&){state.dirty=state.canSave;}}
        else{
            state.dirty=false;for(uint32_t i=0;i<data.keyCount;i++)if(data.keys[i].owned){const auto part=PartOf(data,data.keys[i].target);if(part>=5||origins[part].handle!=data.keys[i].target||!origins[part].automatic||!Find(origins[part].preset)){state.dirty=true;break;}}
        }
        state.worn={};for(uint32_t i=0;i<data.targetCount;i++){
            const auto& target=data.targets[i];const auto part=target.info.part;bool owned=false;for(uint32_t k=0;k<target.count;k++)owned=owned||data.keys[target.first+k].owned!=0;
            const auto* doc=origins[part].handle==target.info.handle?Find(origins[part].preset):nullptr;const auto* applied=Find(appliedIds[part]);
            auto line=std::string(PartName(part))+" · "+morphequipment::Label(target.info.model_id)+" · "+(applied?"应用："+applied->name:"未应用");
            const bool dirty=state.dirty&&(!doc||doc->id==state.activeId);
            if(owned&&(!doc||!applied||doc->id!=applied->id||dirty))line+=" · 预览："+(doc?doc->name:std::string("未命名"))+(dirty?"*":"");
            state.worn[part]=std::move(line);
        }
    }
    void Pause(){state.paused=true;origins={};state.canSave=false;state.preview=false;}
    void SourceExpired(){origins={};observed={};state.activeId.clear();state.activeName.clear();state.preview=false;}
    void Edited(uint64_t target,const mhmcontrol::Response& data){const auto part=PartOf(data,target);if(part<5){observed[part]=target;origins[part]={state.activeId,target,false};state.preview=true;}}
    bool Automatic(mhmcontrol::Session* session,const mhmcontrol::Response& data,bool ready){
        if(!session||!query||!ready||state.paused||!state.enabled)return false;
        std::array<uint64_t,5> handles{};for(uint32_t i=0;i<data.targetCount;i++)handles[data.targets[i].info.part]=data.targets[i].info.handle;
        auto updates=Owned(data);auto next=origins;bool changed=false;
        for(uint32_t part=0;part<5;part++){
            const bool replaced=handles[part]!=observed[part];if(!replaced&&!(force&(1u<<part)))continue;
            observed[part]=handles[part];force&=~(1u<<part);if(replaced)next[part]={};if(!handles[part])continue;
            for(const auto& ref:defaults){
                if(ref.part!=part)continue;const auto* doc=Find(ref.preset);const auto* asset=doc?FindPart(*doc,part):nullptr;if(!asset)continue;
                const auto match=PartCheck(*doc,*asset,data);if(!match.compatible)continue;
                if(!replaced&&origins[part].handle==handles[part]&&origins[part].preset==doc->id&&state.activeId==doc->id)break;
                std::erase_if(updates,[&](const auto& item){return PartOf(data,item.target)==part;});updates.insert(updates.end(),match.updates.begin(),match.updates.end());next[part]={doc->id,handles[part],true};changed=true;break;
            }
        }
        if(!changed){origins=std::move(next);return false;}
        const auto result=session->Replace(updates);
        if(result!=MHM_OK){state.autoMessage="自动使用失败（接口结果 "+std::to_string(result)+"），当前控制保持；可点击恢复自动使用重试";return false;}
        origins=std::move(next);state.autoMessage.clear();
        const bool editable=std::any_of(origins.begin(),origins.end(),[&](const auto& origin){return !state.activeId.empty()&&origin.preset==state.activeId;});
        if(state.activeId.empty()||!editable){
            state.activeId.clear();state.activeName.clear();
            ChooseEditor();
        }return true;
    }
    void Fail(uint64_t ticket,const std::string& message){state.completed=ticket;state.success=false;state.selectLast=false;state.message=message;}
    MhmResult Process(const Request& request,uint64_t ticket,mhmcontrol::Session* session,const mhmcontrol::Response& data,bool ready){
        state.completed=ticket;state.success=false;state.selectLast=false;
        try{
            if(!store)throw std::runtime_error("预设目录尚未就绪");
            if(Management(request.action)){Manage(request,session,data);return state.success?MHM_OK:MHM_INVALID;}
            if(!state.enabled)throw std::runtime_error("请先开启捏人");
            if(!session||!query||!ready)throw std::runtime_error("预设尚未就绪，请检查配套核心与装备绑定");
            if(request.action==Action::Select)return Select(request,*session,data);
            const auto* existing=Find(request.id);Document doc;
            if(request.action==Action::Apply){
                if(!existing)throw std::runtime_error("预设不存在，请重新选择");const auto match=Check(*existing,data,identities);
                if(!match.compatible)throw std::runtime_error(match.message);
                const auto result=session->Replace(match.updates);if(result!=MHM_OK){state.message="应用失败，原控制保持（接口结果 "+std::to_string(result)+"）";return result;}
                doc=*existing;origins={};for(const auto& update:match.updates){const auto part=PartOf(data,update.target);if(part<5)origins[part]={doc.id,update.target,false};}
                for(uint32_t i=0;i<data.targetCount;i++)observed[data.targets[i].info.part]=data.targets[i].info.handle;
                state.message="已应用："+doc.name;
            }else{
                if(request.action==Action::Save&&(!existing||request.id!=state.activeId))throw std::runtime_error("请先应用该预设，或使用另存为");
                const auto id=request.action==Action::Save?existing->id:NewId();
                const auto name=request.action==Action::Save?existing->name:request.name;
                CheckName(name,id);
                if(request.action==Action::SaveAs&&documents.size()>=256)throw std::runtime_error("最多支持 256 个预设");
                if(request.action==Action::Save){if(!state.canSave)throw std::runtime_error("当前没有可保存的编辑部位");doc=EditedDocument(*existing,data);SaveDocument(doc);}
                else{doc=Capture(data,identities,id,name);store->Save(doc);documents.push_back(doc);PublishLibrary();AssociateOwned(doc.id,data,true);state.preview=true;}
                state.message="保存成功";
            }
            state.activeId=doc.id;state.activeName=doc.name;state.lastId=doc.id;state.success=true;state.selectLast=true;return MHM_OK;
        }catch(const std::exception& e){state.message=e.what();return MHM_INVALID;}
    }
};
}
