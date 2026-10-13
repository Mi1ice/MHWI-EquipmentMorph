#pragma once
#include <string>
#include <string_view>
namespace mhm {
inline std::string JsonString(std::string_view input){
    constexpr char hex[]="0123456789abcdef";std::string output="\"";
    for(unsigned char c:input){
        if(c=='"'||c=='\\'){output+='\\';output+=static_cast<char>(c);}
        else if(c<32||c>=128){output+="\\u00";output+=hex[c>>4];output+=hex[c&15];}
        else output+=static_cast<char>(c);
    }
    return output+'"';
}
}
