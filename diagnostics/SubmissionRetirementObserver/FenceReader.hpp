#pragma once
#include <d3d12.h>
#include <limits>
namespace frameobserver {
enum class FenceState {fresh,ready,queryFailed,deviceRemoved};
// The game instance retains the successful QI reference until process exit.
// Tests use their own fake COM object and release that reference explicitly.
class FenceReader {
    ID3D12Fence* retained=nullptr;
    FenceState state=FenceState::fresh;
    HRESULT query=E_PENDING;
public:
    bool Attach(ID3D12Fence* borrowed) noexcept {
        if(state!=FenceState::fresh||!borrowed)return false;
        query=borrowed->QueryInterface(__uuidof(ID3D12Fence),reinterpret_cast<void**>(&retained));
        if(FAILED(query)||!retained){state=FenceState::queryFailed;return false;}
        state=FenceState::ready;return true;
    }
    bool Poll(std::uint64_t& value) noexcept {
        if(state!=FenceState::ready)return false;
        value=retained->GetCompletedValue();
        if(value==std::numeric_limits<std::uint64_t>::max()){state=FenceState::deviceRemoved;return false;}
        return true;
    }
    ID3D12Fence* Retained()const noexcept{return retained;}
    FenceState State()const noexcept{return state;}
    HRESULT QueryResult()const noexcept{return query;}
};
}
