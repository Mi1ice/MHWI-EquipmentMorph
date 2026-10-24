#pragma once
// Offline bounded adaptation of the existing morphpreset::json::Reader.
// The application parser and its original guards remain unchanged.
#include <charconv>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace morphreference {
namespace json {
struct Value {enum Kind{String,Number,Object,Array,Boolean} kind=String;std::string text;double number=0;std::map<std::string,Value> object;std::vector<Value> array;};
class Reader {
    std::string_view input;size_t at=0,nodes=0;
    [[noreturn]] static void Fail(){throw std::runtime_error("预设 JSON 格式无效");}
    void Space(){while(at<input.size()&&(input[at]==' '||input[at]=='\n'||input[at]=='\r'||input[at]=='\t'))at++;}
    bool Take(char c){Space();if(at<input.size()&&input[at]==c){at++;return true;}return false;}
    unsigned Code(){unsigned code=0;for(unsigned i=0;i<4;i++){if(at==input.size())Fail();const char c=input[at++];unsigned d=16;if(c>='0'&&c<='9')d=unsigned(c-'0');else if(c>='a'&&c<='f')d=unsigned(c-'a')+10;else if(c>='A'&&c<='F')d=unsigned(c-'A')+10;if(d>15)Fail();code=code*16+d;}return code;}
    std::string String(){
        if(!Take('"'))Fail();std::string out;
        while(at<input.size()){
            const unsigned char c=static_cast<unsigned char>(input[at++]);if(c=='"')return out;if(c<32)Fail();
            if(c!='\\'){out+=static_cast<char>(c);continue;}
            if(at==input.size())Fail();const char e=input[at++];
            if(e=='"'||e=='\\'||e=='/')out+=e;
            else if(e=='n')out+='\n';else if(e=='r')out+='\r';else if(e=='t')out+='\t';else if(e=='b')out+='\b';else if(e=='f')out+='\f';
            else if(e=='u'){
                unsigned code=Code();if(code>=0xd800&&code<=0xdbff){if(at+2>input.size()||input.substr(at,2)!="\\u")Fail();at+=2;const auto low=Code();if(low<0xdc00||low>0xdfff)Fail();code=0x10000+((code-0xd800)<<10)+(low-0xdc00);}else if(code>=0xdc00&&code<=0xdfff)Fail();
                if(code<0x80)out+=static_cast<char>(code);
                else if(code<0x800){out+=static_cast<char>(0xc0|(code>>6));out+=static_cast<char>(0x80|(code&63));}
                else if(code<0x10000){out+=static_cast<char>(0xe0|(code>>12));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
                else{out+=static_cast<char>(0xf0|(code>>18));out+=static_cast<char>(0x80|((code>>12)&63));out+=static_cast<char>(0x80|((code>>6)&63));out+=static_cast<char>(0x80|(code&63));}
            }else Fail();
            if(out.size()>16384)Fail();
        }Fail();
    }
    Value Read(unsigned depth){
        if(depth>16||++nodes>16000000)Fail();Space();if(at==input.size())Fail();Value value;
        if(input[at]=='"'){value.text=String();return value;}
        if(input.substr(at,4)=="true"){at+=4;value.kind=Value::Boolean;value.number=1;return value;}
        if(input.substr(at,5)=="false"){at+=5;value.kind=Value::Boolean;return value;}
        if(Take('{')){value.kind=Value::Object;if(Take('}'))return value;do{auto key=String();if(!Take(':')||value.object.size()>=4096)Fail();auto item=Read(depth+1);if(!value.object.emplace(std::move(key),std::move(item)).second)Fail();if(Take('}'))return value;}while(Take(','));Fail();}
        if(Take('[')){value.kind=Value::Array;if(Take(']'))return value;do{if(value.array.size()>=8000000)Fail();value.array.push_back(Read(depth+1));if(Take(']'))return value;}while(Take(','));Fail();}
        const auto start=at;if(input[at]=='-')at++;if(at==input.size())Fail();
        if(input[at]=='0')at++;else{if(input[at]<'1'||input[at]>'9')Fail();while(at<input.size()&&input[at]>='0'&&input[at]<='9')at++;}
        if(at<input.size()&&input[at]=='.'){at++;const auto begin=at;while(at<input.size()&&input[at]>='0'&&input[at]<='9')at++;if(at==begin)Fail();}
        if(at<input.size()&&(input[at]=='e'||input[at]=='E')){at++;if(at<input.size()&&(input[at]=='+'||input[at]=='-'))at++;const auto begin=at;while(at<input.size()&&input[at]>='0'&&input[at]<='9')at++;if(at==begin)Fail();}
        value.kind=Value::Number;const auto result=std::from_chars(input.data()+start,input.data()+at,value.number);
        if(result.ec!=std::errc{}||result.ptr!=input.data()+at||!std::isfinite(value.number))Fail();return value;
    }
public:
    explicit Reader(std::string_view text):input(text){if(input.size()>128*1024*1024)Fail();}
    Value Parse(){auto value=Read(0);Space();if(at!=input.size())Fail();return value;}
};
inline const Value& Field(const Value& value,const char* name,Value::Kind kind){
    if(value.kind!=Value::Object)throw std::runtime_error("预设对象类型错误");const auto found=value.object.find(name);
    if(found==value.object.end()||found->second.kind!=kind)throw std::runtime_error("预设字段缺失或类型错误");return found->second;
}
inline void Fields(const Value& value,size_t count){if(value.kind!=Value::Object||value.object.size()!=count)throw std::runtime_error("预设包含未知字段");}
}
}
