#pragma once
#include <Windows.h>
#include <cstring>
#include <cstdint>
namespace facesmooth {
struct BufferedLog {
    HANDLE file=INVALID_HANDLE_VALUE;
    std::uint64_t writes=0,flushes=0,bytes=0,header=0,rotations=0;
    ~BufferedLog(){if(file!=INVALID_HANDLE_VALUE){Flush();::CloseHandle(file);}}
    bool Flush() noexcept {if(file==INVALID_HANDLE_VALUE)return false;flushes++;return ::FlushFileBuffers(file)!=FALSE;}
    bool Write(const char* line) noexcept {
        if(file==INVALID_HANDLE_VALUE||!line)return false;
        const auto length=std::strlen(line);if(!length||length>65536||line[length-1]!='\n')return false;
        // Keep the startup identity, plus a bounded recent history. Worker only.
        if(bytes+length>8*1024*1024) {
            LARGE_INTEGER position{};position.QuadPart=static_cast<LONGLONG>(header);
            if(!::SetFilePointerEx(file,position,nullptr,FILE_BEGIN)||!::SetEndOfFile(file))return false;
            bytes=header;rotations++;DWORD done=0;
            const char marker[]="{\"event\":\"bounded_log_rotated\",\"rescan_required\":true}\n";
            if(!::WriteFile(file,marker,sizeof(marker)-1,&done,nullptr)||done!=sizeof(marker)-1)return false;
            bytes+=done;
        }
        DWORD written=0;
        const bool ok=::WriteFile(file,line,static_cast<DWORD>(length),&written,nullptr)&&written==length;
        if(ok){writes++;bytes+=written;if(!header)header=bytes;}return ok;
    }
};
}
