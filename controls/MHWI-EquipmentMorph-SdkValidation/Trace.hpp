#pragma once
#include "SdkSession.hpp"
#include <charconv>

namespace mhmvalidation {
struct TraceRecord {
    std::string event,phase,error,caseId;size_t step=0;uint64_t nowMs=0,acceptedRevision=0;
    Observation reference,observation;std::vector<float> targets;
    bool accepted=false,applicationConfirmed=false,zeroConfirmed=false,normalEnd=false,evidenceComplete=true;
};
inline std::string Quote(std::string_view value){
    std::string out="\"";constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=static_cast<char>(c);}return out+'"';
}
inline std::string Number(float value){Require(std::isfinite(value),"Nonfinite trace weight");char buffer[48]{};const auto r=std::to_chars(buffer,buffer+sizeof(buffer),value);Require(r.ec==std::errc{},"Trace weight encoding");return std::string(buffer,r.ptr);}
inline std::string EncodeTrace(const TraceRecord& r){
    auto bit=[](bool b){return b?"true":"false";};
    std::string out="{\"schema\":\"mhwi-sdk-validation-trace-v1\",\"event\":"+Quote(r.event)+",\"phase\":"+Quote(r.phase)+",\"error\":"+Quote(r.error)+",\"case\":"+Quote(r.caseId)+",\"step\":"+std::to_string(r.step)+",\"time_ms\":"+std::to_string(r.nowMs);
    out+=",\"epoch\":"+std::to_string(r.reference.epoch)+",\"target_handle\":"+std::to_string(r.reference.target)+",\"source\":"+std::to_string(r.reference.source)+",\"mode\":"+Quote(r.reference.mode);
    out+=",\"model_sha256\":"+Quote(std::string(r.reference.identity.model_sha256,strnlen_s(r.reference.identity.model_sha256,65)))+",\"pack_sha256\":"+Quote(std::string(r.reference.identity.pack_sha256,strnlen_s(r.reference.identity.pack_sha256,65)));
    out+=",\"accepted_revision\":"+std::to_string(r.acceptedRevision)+",\"accepted\":"+bit(r.accepted)+",\"application_confirmed\":"+bit(r.applicationConfirmed)+",\"zero_confirmed\":"+bit(r.zeroConfirmed)+",\"normal_end\":"+bit(r.normalEnd)+",\"evidence_complete\":"+bit(r.evidenceComplete);
    out+=",\"read_result\":"+std::to_string(r.observation.result)+",\"observed_epoch\":"+std::to_string(r.observation.epoch)+",\"requested_revision\":"+std::to_string(r.observation.requestedRevision)+",\"applied_revision\":"+std::to_string(r.observation.appliedRevision)+",\"weights\":[";
    for(size_t i=0;i<r.targets.size();++i){if(i)out+=',';const bool observed=i<r.observation.weights.size();out+="{\"id\":"+Quote(i<r.reference.liveIds.size()?r.reference.liveIds[i]:std::string())+",\"target\":"+Number(r.targets[i]);
        if(observed){const auto& w=r.observation.weights[i];out+=",\"requested\":"+Number(w.requested)+",\"applied\":"+Number(w.applied)+",\"owner\":"+std::to_string(w.source);}out+='}';}
    return out+"]}\n";
}
struct TraceWriter {virtual ~TraceWriter()=default;virtual bool Append(const TraceRecord&)=0;};
class FileTraceWriter final:public TraceWriter {
    HANDLE file=INVALID_HANDLE_VALUE;uint64_t bytes=0,limit;bool failed=false;
public:
    explicit FileTraceWriter(const std::filesystem::path& path,uint64_t maximum=32ull*1024*1024):limit(maximum){file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);}
    ~FileTraceWriter(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    bool Append(const TraceRecord& record)override{
        if(failed||file==INVALID_HANDLE_VALUE)return false;
        try{const auto line=EncodeTrace(record);if(bytes>limit||line.size()>limit-bytes){failed=true;return false;}DWORD written=0;
            if(!WriteFile(file,line.data(),static_cast<DWORD>(line.size()),&written,nullptr)||written!=line.size()||!FlushFileBuffers(file)){failed=true;return false;}bytes+=written;return true;
        }catch(...){failed=true;return false;}
    }
};
}
