#pragma once
// Included inside observer, after the checked Read helpers. Each Load performs
// a fresh ReadProcessMemory; only extraction from this local copy uses memcpy.
template<size_t Bytes> struct ReadBlock {
    static_assert(Bytes>0&&Bytes<=0x500);
    std::array<unsigned char,Bytes> bytes{};
    bool Load(std::uintptr_t address) noexcept {return Read(address,bytes);}
    template<class T,size_t Offset> T Value()const noexcept {
        static_assert(Offset<=Bytes&&sizeof(T)<=Bytes-Offset);
        T value{};std::memcpy(&value,bytes.data()+Offset,sizeof(value));return value;
    }
};
