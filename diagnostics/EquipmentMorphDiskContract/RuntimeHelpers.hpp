// Generated verbatim from the previously tested prototype runtime helpers.
// Included inside faceup2; faceup::guards is imported by the caller.
std::string Digest(const unsigned char* bytes,std::size_t count) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD objectBytes=0,written=0;
    if(::BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)!=0)
        throw std::runtime_error("SHA provider");
    if(::BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectBytes),sizeof(objectBytes),&written,0)!=0) {
        ::BCryptCloseAlgorithmProvider(algorithm,0);throw std::runtime_error("SHA properties");
    }
    std::vector<unsigned char> object(objectBytes);std::array<unsigned char,32> result{};
    NTSTATUS status=::BCryptCreateHash(algorithm,&hash,object.data(),objectBytes,nullptr,0,0);
    if(status==0)status=::BCryptHashData(hash,const_cast<PUCHAR>(bytes),static_cast<ULONG>(count),0);
    if(status==0)status=::BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0);
    if(hash)::BCryptDestroyHash(hash);::BCryptCloseAlgorithmProvider(algorithm,0);
    if(status!=0)throw std::runtime_error("SHA hash");
    std::string text;constexpr char digits[]="0123456789abcdef";
    for(auto byte:result){text+=digits[byte>>4];text+=digits[byte&15];}return text;
}
std::vector<unsigned char> Load(const wchar_t* path,unsigned expected) {
    HANDLE file=::CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Input file open");
    LARGE_INTEGER size{};bool valid=::GetFileSizeEx(file,&size)&&size.QuadPart==expected;
    std::vector<unsigned char> bytes(valid?expected:0);DWORD count=0;
    if(valid)valid=::ReadFile(file,bytes.data(),expected,&count,nullptr)&&count==expected;
    ::CloseHandle(file);if(!valid)throw std::runtime_error("Input file length/read");return bytes;
}
using Log=facesmooth::BufferedLog;
std::uintptr_t Renderer() {
    std::uintptr_t pointer=0;
    if(!observer::Read(observer::imageBase+0x51C7050,pointer)||pointer<=0xFFFF)throw std::runtime_error("Renderer identity");
    return pointer;
}
bool FunctionsMatch() {
    for(const auto& guard:guards) {
        std::vector<unsigned char> code(guard.bytes);
        if(!observer::Read(observer::imageBase+guard.rva,code.data(),code.size())||Digest(code.data(),code.size())!=guard.sha)return false;
    }
    return true;
}
