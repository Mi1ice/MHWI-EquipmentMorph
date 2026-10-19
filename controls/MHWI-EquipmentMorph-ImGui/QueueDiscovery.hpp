#pragma once
#include <array>
#include <atomic>
#include <mutex>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace morphgui {
using Microsoft::WRL::ComPtr;
// Used only when creation was missed. Match actual backbuffer work, never the
// first/most recent direct queue (uploads and other swapchains may use others).
struct QueueDiscovery {
    struct Recording {ID3D12GraphicsCommandList* list=nullptr;bool finalTransition=false;};
    std::mutex mutex;
    std::atomic_bool pending=false;
    ComPtr<ID3D12Device> device;
    std::array<ComPtr<ID3D12Resource>,8> buffers;
    std::array<Recording,256> recordings{};
    ComPtr<ID3D12CommandQueue> selected;
    std::atomic_uint64_t backbufferTransitions=0,matchedSubmissions=0;
    void ClearLocked(){pending=false;selected.Reset();device.Reset();for(auto& buffer:buffers)buffer.Reset();recordings={};}
    void Clear(){std::scoped_lock lock(mutex);ClearLocked();}
    bool Begin(IDXGISwapChain* swap){
        std::scoped_lock lock(mutex);if(pending||selected)return true;
        DXGI_SWAP_CHAIN_DESC desc{};
        if(FAILED(swap->GetDesc(&desc))||desc.BufferCount<2||desc.BufferCount>buffers.size())return false;
        if(FAILED(swap->GetDevice(IID_PPV_ARGS(&device))))return false;
        for(UINT i=0;i<desc.BufferCount;i++)if(FAILED(swap->GetBuffer(i,IID_PPV_ARGS(&buffers[i])))){ClearLocked();return false;}
        pending=true;return true;
    }
    void Reset(ID3D12GraphicsCommandList* list){
        if(!pending.load())return;
        std::scoped_lock lock(mutex);for(auto& entry:recordings)if(entry.list==list)entry={};
    }
    void Barriers(ID3D12GraphicsCommandList* list,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
        if(!pending.load())return;
        std::scoped_lock lock(mutex);if(!pending)return;
        for(UINT i=0;i<count;i++){
            const auto& b=barriers[i];
            if(b.Type!=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION||b.Flags==D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY)continue;
            bool ours=false;for(const auto& buffer:buffers)if(buffer&&buffer.Get()==b.Transition.pResource){ours=true;break;}
            if(!ours)continue;
            Recording* slot=nullptr;
            for(auto& entry:recordings)if(entry.list==list){slot=&entry;break;}
            if(!slot)for(auto& entry:recordings)if(!entry.list){slot=&entry;slot->list=list;break;}
            if(slot){slot->finalTransition=b.Transition.StateAfter==D3D12_RESOURCE_STATE_PRESENT;backbufferTransitions++;}
        }
    }
    void Submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists){
        if(!pending.load()||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return;
        std::scoped_lock lock(mutex);if(!pending)return;
        ComPtr<ID3D12Device> owner;if(FAILED(queue->GetDevice(IID_PPV_ARGS(&owner)))||owner.Get()!=device.Get())return;
        for(UINT i=0;i<count;i++)for(const auto& entry:recordings)if(entry.list==lists[i]&&entry.finalTransition){
            selected=queue;matchedSubmissions++;pending=false;return;
        }
    }
    ComPtr<ID3D12CommandQueue> Take(){
        std::scoped_lock lock(mutex);auto result=selected;if(result)ClearLocked();return result;
    }
};
}
