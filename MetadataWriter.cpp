#define NOMINMAX
#include "MetadataWriter.h"
#include "ExtendedXmp.h"
#include <msxml6.h>
#include <wrl/client.h>
#include <winrt/base.h>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace {
constexpr size_t Limit=16*1024*1024;
const char Standard[]="http://ns.adobe.com/xap/1.0/";
const char Extension[]="http://ns.adobe.com/xmp/extension/";
const wchar_t* Names[]{L"xmp:CreateDate",L"xmp:Author",L"vrc:WorldDisplayName",L"vrc:WorldID",L"kpe:Subjects"};
const wchar_t* Uris[]{L"http://ns.adobe.com/xap/1.0/",L"http://ns.adobe.com/xap/1.0/",L"http://ns.vrchat.com/vrc/1.0/",L"http://ns.vrchat.com/vrc/1.0/",L"urn:kunivrcphoteditor:metadata:1.0"};
struct Bstr { BSTR value; Bstr(const std::wstring& s):value(SysAllocStringLen(s.data(),static_cast<UINT>(s.size()))) { if(!value) throw std::bad_alloc(); } ~Bstr(){SysFreeString(value);} operator BSTR() const {return value;} };
struct Handle { HANDLE value=INVALID_HANDLE_VALUE; ~Handle(){if(value!=INVALID_HANDLE_VALUE) CloseHandle(value);} };
void Check(BOOL result) { if(!result) winrt::throw_last_error(); }
std::string Utf8(const std::wstring& s) {
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(!n && !s.empty()) winrt::throw_last_error(); std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr); return out;
}
std::wstring Wide(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n && !s.empty()) winrt::throw_last_error(); std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n); return out;
}
std::string ReadHandle(HANDLE file) {
    LARGE_INTEGER length{}; Check(GetFileSizeEx(file,&length));
    if(length.QuadPart<8 || length.QuadPart>256LL*1024*1024) throw std::runtime_error("Image size must be 8 bytes to 256MiB");
    LARGE_INTEGER zero{}; Check(SetFilePointerEx(file,zero,nullptr,FILE_BEGIN));
    std::string bytes(static_cast<size_t>(length.QuadPart),'\0'); DWORD done=0;
    Check(ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&done,nullptr));
    if(done!=bytes.size()) throw std::runtime_error("Incomplete image read"); return bytes;
}
std::string ReadFileBytes(const std::wstring& path) {
    Handle file; file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file.value!=INVALID_HANDLE_VALUE); return ReadHandle(file.value);
}
uint32_t BE(const char* p){auto b=reinterpret_cast<const unsigned char*>(p);return (uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(uint32_t(b[2])<<8)|b[3];}
void Put32(std::string& s,uint32_t v){for(int i=24;i>=0;i-=8)s+=char(v>>i);}
uint32_t CRC(const std::string& s){uint32_t c=~0U;for(unsigned char b:s){c^=b;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320:0);}return ~c;}
ComPtr<IXMLDOMDocument2> Dom(const std::string& xml) {
    if(xml.size()>Limit) throw std::runtime_error("XMP too large");
    XmpFields fields;std::wstring error;if(!ParseXmp(xml,fields,error))throw std::runtime_error("Invalid XMP XML");
    ComPtr<IXMLDOMDocument2> doc; winrt::check_hresult(CoCreateInstance(__uuidof(DOMDocument60),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&doc)));
    winrt::check_hresult(doc->put_async(VARIANT_FALSE)); winrt::check_hresult(doc->put_resolveExternals(VARIANT_FALSE));
    winrt::check_hresult(doc->put_preserveWhiteSpace(VARIANT_TRUE));
    VARIANT v{};v.vt=VT_BOOL;v.boolVal=VARIANT_TRUE;winrt::check_hresult(doc->setProperty(Bstr(L"ProhibitDTD"),v));
    VARIANT_BOOL ok=VARIANT_FALSE;winrt::check_hresult(doc->loadXML(Bstr(Wide(xml)),&ok));if(!ok)throw std::runtime_error("Invalid XMP document");
    v.vt=VT_BSTR;Bstr ns(L"xmlns:xmp='http://ns.adobe.com/xap/1.0/' xmlns:vrc='http://ns.vrchat.com/vrc/1.0/' xmlns:kpe='urn:kunivrcphoteditor:metadata:1.0' xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#' xmlns:note='http://ns.adobe.com/xmp/note/'");v.bstrVal=ns.value;
    winrt::check_hresult(doc->setProperty(Bstr(L"SelectionNamespaces"),v));return doc;
}
std::string Xml(IXMLDOMDocument2* doc){BSTR s{};winrt::check_hresult(doc->get_xml(&s));std::wstring text(s,SysStringLen(s));SysFreeString(s);return Utf8(text);}
void Remove(IXMLDOMDocument2* doc,const std::wstring& name){
    ComPtr<IXMLDOMNodeList> nodes; winrt::check_hresult(doc->selectNodes(Bstr(L"//"+name+L" | //@"+name),&nodes));
    long length=0;nodes->get_length(&length);
    for(long i=length-1;i>=0;--i){ComPtr<IXMLDOMNode> node,parent,removed;nodes->get_item(i,&node);node->get_parentNode(&parent);
        DOMNodeType type{};node->get_nodeType(&type);
        if(type==NODE_ATTRIBUTE){ComPtr<IXMLDOMAttribute> attr;winrt::check_hresult(node.As(&attr));
            // Attributes do not expose parentNode in MSXML: locate their owner explicitly.
            ComPtr<IXMLDOMNodeList> owners;doc->selectNodes(Bstr(L"//*[@"+name+L"]"),&owners);long count=0;owners->get_length(&count);
            for(long j=0;j<count;++j){ComPtr<IXMLDOMNode> owner;owners->get_item(j,&owner);ComPtr<IXMLDOMNamedNodeMap> map;owner->get_attributes(&map);BSTR nodeName{};node->get_nodeName(&nodeName);winrt::check_hresult(map->removeNamedItem(nodeName,&removed));SysFreeString(nodeName);}
        }else if(parent)winrt::check_hresult(parent->removeChild(node.Get(),&removed));
    }
}
ComPtr<IXMLDOMNode> Element(IXMLDOMDocument2* doc,const wchar_t* name,const wchar_t* uri){VARIANT type{};type.vt=VT_I4;type.lVal=NODE_ELEMENT;ComPtr<IXMLDOMNode> n;winrt::check_hresult(doc->createNode(type,Bstr(name),Bstr(uri),&n));return n;}
const std::string Empty="<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description rdf:about=''/></rdf:RDF></x:xmpmeta>";
std::string Change(const std::string& xml,const MetadataEdit& edit,bool add,bool stripReference=true){
    auto doc=Dom(xml);if(stripReference)Remove(doc.Get(),L"note:HasExtendedXMP");
    for(size_t i=0;i<5;++i)if(edit.actions[i]!=EditAction::Keep)Remove(doc.Get(),Names[i]);
    if(add){ComPtr<IXMLDOMNode> description;doc->selectSingleNode(Bstr(L"//rdf:RDF/rdf:Description"),&description);if(!description)throw std::runtime_error("Unsupported XMP RDF structure");
        for(size_t i=0;i<5;++i)if(edit.actions[i]==EditAction::Set){auto node=Element(doc.Get(),Names[i],Uris[i]);
            if(i!=4)winrt::check_hresult(node->put_text(Bstr(edit.values[i])));
            else {auto seq=Element(doc.Get(),L"rdf:Seq",L"http://www.w3.org/1999/02/22-rdf-syntax-ns#");size_t start=0;
                while(start<edit.values[i].size()){size_t end=edit.values[i].find(L'\n',start);auto value=edit.values[i].substr(start,end==std::wstring::npos?end:end-start);if(!value.empty()&&value.back()==L'\r')value.pop_back();
                    if(!value.empty()){auto li=Element(doc.Get(),L"rdf:li",L"http://www.w3.org/1999/02/22-rdf-syntax-ns#");winrt::check_hresult(li->put_text(Bstr(value)));winrt::check_hresult(seq->appendChild(li.Get(),nullptr));}
                    if(end==std::wstring::npos)break;start=end+1;}
                winrt::check_hresult(node->appendChild(seq.Get(),nullptr));}
            winrt::check_hresult(description->appendChild(node.Get(),nullptr));}
    }return Xml(doc.Get());
}
struct Container {bool png=false;std::vector<std::string> parts;std::vector<bool> xmp;std::string standard,extended;};
Container Inspect(const std::string& bytes){
    Container c;std::vector<std::string> extension;std::vector<std::string> standards;
    if(bytes.compare(0,8,"\x89PNG\r\n\x1a\n",8)==0){c.png=true;c.parts.push_back(bytes.substr(0,8));c.xmp.push_back(false);size_t p=8;bool end=false;
        while(p<bytes.size()){if(bytes.size()-p<12)throw std::runtime_error("Truncated PNG");size_t n=BE(bytes.data()+p);if(n>bytes.size()-p-12)throw std::runtime_error("PNG length");auto type=bytes.substr(p+4,4),data=bytes.substr(p+8,n);
            if(CRC(type+data)!=BE(bytes.data()+p+8+n))throw std::runtime_error("PNG checksum");bool xmp=false;
            if(type=="iTXt"||type=="tEXt"||type=="zTXt"){auto sep=data.find('\0');if(sep!=std::string::npos&&data.substr(0,sep)=="XML:com.adobe.xmp"){
                if(type=="zTXt")throw std::runtime_error("Compressed PNG XMP cannot be preserved safely");xmp=true;size_t start=sep+1;
                if(type=="iTXt"){if(start+2>data.size()||data[start]!=0||data[start+1]!=0)throw std::runtime_error("Unsupported PNG XMP compression");start+=2;for(int i=0;i<2;++i){auto q=data.find('\0',start);if(q==std::string::npos)throw std::runtime_error("PNG iTXt fields");start=q+1;}}
                standards.push_back(data.substr(start));}}
            c.parts.push_back(bytes.substr(p,n+12));c.xmp.push_back(xmp);p+=n+12;if(type=="IEND"){end=true;if(p!=bytes.size())throw std::runtime_error("PNG trailing data cannot be preserved");break;}}
        if(!end)throw std::runtime_error("Missing PNG IEND");
    }else if(static_cast<unsigned char>(bytes[0])==255&&static_cast<unsigned char>(bytes[1])==216){c.parts.push_back(bytes.substr(0,2));c.xmp.push_back(false);size_t p=2;bool scan=false;
        while(p<bytes.size()){size_t start=p;if(static_cast<unsigned char>(bytes[p++])!=255)throw std::runtime_error("JPEG marker");while(p<bytes.size()&&static_cast<unsigned char>(bytes[p])==255)++p;if(p==bytes.size())throw std::runtime_error("JPEG marker");unsigned char marker=bytes[p++];
            if(marker==0xda){c.parts.push_back(bytes.substr(start));c.xmp.push_back(false);scan=true;break;}
            if(marker==0xd9)break;if(marker==0x01||(marker>=0xd0&&marker<=0xd7)){c.parts.push_back(bytes.substr(start,p-start));c.xmp.push_back(false);continue;}
            if(bytes.size()-p<2)throw std::runtime_error("JPEG length");size_t n=(static_cast<unsigned char>(bytes[p])<<8)|static_cast<unsigned char>(bytes[p+1]);if(n<2||n>bytes.size()-p)throw std::runtime_error("JPEG length");auto data=bytes.substr(p+2,n-2);bool xmp=false;
            if(marker==0xe1&&data.compare(0,sizeof(Standard),Standard,sizeof(Standard))==0){standards.push_back(data.substr(sizeof(Standard)));xmp=true;}
            else if(marker==0xe1&&data.compare(0,sizeof(Extension),Extension,sizeof(Extension))==0){extension.push_back(data);xmp=true;}
            c.parts.push_back(bytes.substr(start,p+n-start));c.xmp.push_back(xmp);p+=n;}
        if(!scan)throw std::runtime_error("JPEG scan missing");
    }else throw std::runtime_error("Only PNG and JPEG can be edited");
    if(standards.size()>1)throw std::runtime_error("Multiple standard XMP packets cannot be edited safely");
    if(!standards.empty())c.standard=standards.front();
    if(!c.standard.empty())Dom(c.standard);
    XmpFields fields;std::wstring error;if(!c.standard.empty())ParseXmp(c.standard,fields,error);
    if(!extension.empty()||!fields.extendedGuid.empty()){std::wstring notes;c.extended=RestoreExtendedXmp(standards,extension,notes);
        if(c.extended.empty())throw std::runtime_error("Invalid or unreferenced Extended XMP; copy was not saved");
        auto guid=Utf8(fields.extendedGuid);for(const auto& part:extension)if(part.size()<sizeof(Extension)+32||part.compare(sizeof(Extension),32,guid)!=0)throw std::runtime_error("Unreferenced Extended XMP");}
    if(c.standard.size()+c.extended.size()>Limit)throw std::runtime_error("XMP limit");return c;
}
std::string Chunk(const std::string& type,const std::string& data){std::string s;Put32(s,static_cast<uint32_t>(data.size()));s+=type+data;Put32(s,CRC(type+data));return s;}
std::string App1(const std::string& data){if(data.size()>65533)throw std::runtime_error("JPEG APP1 too large");std::string s="\xff\xe1";size_t n=data.size()+2;s+=char(n>>8);s+=char(n);return s+data;}
std::string Fingerprint(const std::string& bytes){return XmpMd5(bytes);}
std::array<std::wstring,5> Values(const Container& c){XmpFields f;std::wstring error;
    for(const auto& s:{c.standard,c.extended})if(!s.empty()){XmpFields p;if(!ParseXmp(s,p,error))throw std::runtime_error("XMP parse");if(f.date.empty())f.date=p.date;if(f.author.empty())f.author=p.author;if(f.worldName.empty())f.worldName=p.worldName;if(f.worldId.empty())f.worldId=p.worldId;if(f.subjects.empty())f.subjects=p.subjects;}
    return {f.date,f.author,f.worldName,f.worldId,f.subjects};}
std::string Preserved(const Container& c){std::string s;for(size_t i=0;i<c.parts.size();++i)if(!c.xmp[i])s+=c.parts[i];return s;}
}
std::wstring MetadataFingerprint(const std::wstring& path){return Wide(Fingerprint(ReadFileBytes(path)));}
EditableMetadata InspectEditable(const std::wstring& path){
    auto bytes=ReadFileBytes(path);auto c=Inspect(bytes);
    if(!c.standard.empty()){auto doc=Dom(c.standard);ComPtr<IXMLDOMNode> description;winrt::check_hresult(doc->selectSingleNode(Bstr(L"//rdf:RDF/rdf:Description"),&description));if(!description)throw std::runtime_error("Unsupported XMP RDF structure; view only");}
    return {Values(c),Wide(Fingerprint(bytes))};
}
void SaveMetadataCopy(const std::wstring& source,const std::wstring& destination,const std::wstring& expected,const MetadataEdit& edit,bool allowLockedRename){
    auto error=edit.Validate();if(!error.empty())throw std::runtime_error(Utf8(error));if(!edit.Changed())throw std::runtime_error("No metadata changes");
    if(fs::exists(destination))throw std::runtime_error("Destination already exists; overwrite is disabled");
    Handle input;input.value=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ|(allowLockedRename?FILE_SHARE_DELETE:0),nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(input.value!=INVALID_HANDLE_VALUE);
    auto original=ReadHandle(input.value);if(Wide(Fingerprint(original))!=expected)throw std::runtime_error("Source changed; reload before saving");auto c=Inspect(original);
    auto extension=fs::path(destination).extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),towlower);
    if((c.png&&extension!=L".png")||(!c.png&&extension!=L".jpg"&&extension!=L".jpeg"))throw std::runtime_error("Destination format must match source");
    std::string standard=Change(c.standard.empty()?Empty:c.standard,edit,true),extended;
    if(!c.extended.empty())extended=Change(c.extended,edit,false);
    std::string metadata;
    if(c.png){std::string data="XML:com.adobe.xmp";data.append(5,'\0');data+=standard;metadata=Chunk("iTXt",data);}
    else {if(standard.size()+sizeof(Standard)>65533){if(!extended.empty())throw std::runtime_error("Standard XMP exceeds APP1 with existing extension");extended=standard;standard=Empty;}
        if(!extended.empty()){auto doc=Dom(standard);ComPtr<IXMLDOMNode> description;doc->selectSingleNode(Bstr(L"//rdf:RDF/rdf:Description"),&description);if(!description)throw std::runtime_error("XMP description missing");auto ref=Element(doc.Get(),L"note:HasExtendedXMP",L"http://ns.adobe.com/xmp/note/");auto guid=XmpMd5(extended);winrt::check_hresult(ref->put_text(Bstr(Wide(guid))));winrt::check_hresult(description->appendChild(ref.Get(),nullptr));standard=Xml(doc.Get());
            for(size_t off=0;off<extended.size();){size_t n=std::min<size_t>(65000,extended.size()-off);std::string part(Extension,sizeof(Extension));part+=guid;Put32(part,static_cast<uint32_t>(extended.size()));Put32(part,static_cast<uint32_t>(off));part+=extended.substr(off,n);metadata+=App1(part);off+=n;}}
        metadata=App1(std::string(Standard,sizeof(Standard))+standard)+metadata;}
    if(standard.size()+extended.size()>Limit)throw std::runtime_error("Output XMP too large");
    std::string output;bool inserted=false;for(size_t i=0;i<c.parts.size();++i){if(c.xmp[i])continue;
        bool location=c.png?c.parts[i].size()>=8&&c.parts[i].substr(4,4)=="IEND":i>0;
        if(!inserted&&location){output+=metadata;inserted=true;}output+=c.parts[i];}
    if(!inserted)throw std::runtime_error("Metadata insertion failed");
    auto verify=Inspect(output);if(Preserved(c)!=Preserved(verify))throw std::runtime_error("Image data preservation failed");
    auto actual=Values(verify),before=Values(c);for(size_t i=0;i<5;++i){auto wanted=edit.actions[i]==EditAction::Set?edit.values[i]:edit.actions[i]==EditAction::Remove?L"":before[i];
        if(i==4){auto normalize=[](std::wstring s){s.erase(std::remove(s.begin(),s.end(),L'\r'),s.end());while(!s.empty()&&s.back()==L'\n')s.pop_back();return s;};if(normalize(actual[i])!=normalize(wanted))throw std::runtime_error("Subjects verification failed");}
        else if(actual[i]!=wanted)throw std::runtime_error("Edited metadata verification failed");}
    // Serializing the same DOM after removing edited properties must preserve all other XML.
    MetadataEdit mask=edit;for(size_t i=0;i<5;++i)if(mask.actions[i]!=EditAction::Keep)mask.actions[i]=EditAction::Remove;
    auto oldStandard=Change(c.standard.empty()?Empty:c.standard,mask,false);
    auto newStandard=Change(verify.standard,mask,false);
    bool promoted=c.extended.empty()&&!verify.extended.empty()&&standard!=Change(c.standard.empty()?Empty:c.standard,edit,true);
    if(promoted){if(Change(verify.extended,mask,false)!=oldStandard)throw std::runtime_error("Unknown XML preservation failed");}
    else {if(oldStandard!=newStandard)throw std::runtime_error("Unknown XML preservation failed");if(!c.extended.empty()&&Change(c.extended,mask,false)!=Change(verify.extended,mask,false))throw std::runtime_error("Extended XML preservation failed");}
    GUID guid{};winrt::check_hresult(CoCreateGuid(&guid));wchar_t id[40]{};StringFromGUID2(guid,id,40);auto temp=destination+L"."+id+L".tmp";
    try{Handle file;file.value=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file.value!=INVALID_HANDLE_VALUE);DWORD done{};Check(WriteFile(file.value,output.data(),static_cast<DWORD>(output.size()),&done,nullptr));if(done!=output.size())throw std::runtime_error("Incomplete write");Check(FlushFileBuffers(file.value));CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;
        auto reread=ReadFileBytes(temp);if(reread!=output)throw std::runtime_error("Saved file verification failed");Inspect(reread);
        Check(MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH));
    }catch(...){DeleteFileW(temp.c_str());throw;}
}
