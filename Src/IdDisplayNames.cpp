#include "ViewerRuntime.h"
#include "IdDisplayNames.h"
#include <cwctype>
namespace {
constexpr size_t MaxBytes=8*1024*1024,MaxEntries=100000,MaxName=1024;
void Utf16(const std::wstring& text){for(size_t n=0;n<text.size();++n){auto c=text[n];if(c>=0xd800&&c<=0xdbff){if(++n>=text.size()||text[n]<0xdc00||text[n]>0xdfff)throw std::runtime_error("invalid Unicode pair");}else if(c>=0xdc00&&c<=0xdfff)throw std::runtime_error("invalid Unicode pair");}}
void AtomicWrite(const fs::path& path,const std::string& bytes){
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("temporary name failed");wchar_t value[40]{};StringFromGUID2(guid,value,40);auto temporary=path;temporary+=std::wstring(L".tmp-")+value;
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("temporary file creation failed");
    DWORD written=0;bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);CloseHandle(file);
    if(ok)ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok){DeleteFileW(temporary.c_str());throw std::runtime_error("atomic file replacement failed");}
}
struct FileLock{HANDLE file;explicit FileLock(const fs::path& path){auto lock=path;lock+=L".lock";file=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("name table is in use");}~FileLock(){CloseHandle(file);}};
}
std::wstring CanonicalDisplayId(const std::wstring& id){
    size_t prefix=id.rfind(L"usr_",0)==0||id.rfind(L"grp_",0)==0?4:id.rfind(L"wrld_",0)==0?5:0;
    if(!prefix||id.size()!=prefix+36)return L"";auto value=id;
    for(size_t n=0;n<36;++n){auto c=value[prefix+n];bool dash=n==8||n==13||n==18||n==23;if(dash?c!=L'-':!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F')))return L"";if(c>=L'A'&&c<=L'F')value[prefix+n]=c+32;}return value;
}
std::wstring DisplayIdType(const std::wstring& id){auto value=CanonicalDisplayId(id);return value.empty()?L"":value.rfind(L"usr_",0)==0?L"person":value.rfind(L"wrld_",0)==0?L"world":L"group";}
std::wstring DisplayIdUrl(const std::wstring& id){auto value=CanonicalDisplayId(id),type=DisplayIdType(id);if(value.empty())return L"";return L"https://vrchat.com/home/"+(type==L"person"?L"user":type)+L"/"+value;}
std::wstring ValidateDisplayName(const std::wstring& text){
    if(text.size()>MaxName)throw std::runtime_error("display name exceeds 1024 UTF-16 units");Utf16(text);for(auto c:text)if(c<32||c==127||c==0x85||c==0x2028||c==0x2029)throw std::runtime_error("control character in display name");
    size_t first=0,last=text.size();while(first<last&&iswspace(text[first]))++first;while(last>first&&iswspace(text[last-1]))--last;return text.substr(first,last-first);
}
IdNameImport ParseIdNames(const std::string& bytes){
    if(bytes.size()>MaxBytes)throw std::runtime_error("name table exceeds 8MiB");int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0);if(!count)throw std::runtime_error("invalid UTF-8");std::wstring text(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),text.data(),count);Utf16(text);if(!text.empty()&&text[0]==0xfeff)text.erase(0,1);
    auto object=JsonObject::Parse(text);if(object.GetNamedString(L"format")!=L"KuniVRCPhotEditor.IdDisplayNames"||object.GetNamedNumber(L"version")!=1)throw std::runtime_error("unsupported name table format/version");auto entries=object.GetNamedArray(L"entries");if(entries.Size()>MaxEntries)throw std::runtime_error("too many name table entries");IdNameImport result;
    for(auto value:entries){auto entry=value.GetObject();auto raw=std::wstring(entry.GetNamedString(L"id",L""));if(raw.empty()){++result.skipped;continue;}auto id=CanonicalDisplayId(raw);if(id.empty()||entry.GetNamedString(L"type")!=DisplayIdType(id))throw std::runtime_error("invalid ID/type");auto name=ValidateDisplayName(std::wstring(entry.GetNamedString(L"displayName")));if(name.empty()){++result.skipped;continue;}auto found=result.names.find(id);if(found!=result.names.end()&&found->second!=name)throw std::runtime_error("conflicting IDs in import");result.names[id]=name;
    }return result;
}
std::string SerializeIdNames(const IdNameTable& table){
    if(table.size()>MaxEntries)throw std::runtime_error("too many name table entries");JsonObject root;root.Insert(L"format",JsonValue::CreateStringValue(L"KuniVRCPhotEditor.IdDisplayNames"));root.Insert(L"version",JsonValue::CreateNumberValue(1));JsonArray entries;
    IdNameTable normalized;for(auto& pair:table){auto id=CanonicalDisplayId(pair.first);if(id.empty())throw std::runtime_error("invalid ID");auto name=ValidateDisplayName(pair.second);if(name.empty())continue;auto found=normalized.find(id);if(found!=normalized.end()&&found->second!=name)throw std::runtime_error("conflicting IDs");normalized[id]=name;}
    for(auto& pair:normalized){JsonObject entry;entry.Insert(L"type",JsonValue::CreateStringValue(DisplayIdType(pair.first)));entry.Insert(L"id",JsonValue::CreateStringValue(pair.first));entry.Insert(L"displayName",JsonValue::CreateStringValue(pair.second));entries.Append(entry);}root.Insert(L"entries",entries);auto bytes=winrt::to_string(root.Stringify());if(bytes.size()>MaxBytes)throw std::runtime_error("name table exceeds 8MiB");return bytes;
}
std::string ReadIdNamesFile(const fs::path& path){std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("name table open failed");stream.seekg(0,std::ios::end);auto size=std::streamoff(stream.tellg());if(size<0||size>MaxBytes)throw std::runtime_error("name table size invalid");std::string bytes(size_t(size),'\0');stream.seekg(0);if(!bytes.empty()&&!stream.read(bytes.data(),std::streamsize(bytes.size())))throw std::runtime_error("name table read failed");return bytes;}
IdNameMerge MergeIdNames(IdNameTable& table,const IdNameImport& input){auto merged=table;IdNameMerge result;result.skipped=input.skipped;for(auto& pair:input.names){auto found=merged.find(pair.first);if(found==merged.end()||found->second.empty()){merged[pair.first]=pair.second;++result.added;}else ++result.kept;}SerializeIdNames(merged);table.swap(merged);return result;}
void ExportIdNames(const fs::path& path,const IdNameTable& table){AtomicWrite(path,SerializeIdNames(table));}
void IdNameStore::Load(const fs::path& path){
    std::lock_guard<std::mutex> lock(mutex);file=path;names.clear();original.clear();error.clear();protectedFile=false;exists=false;
    try{std::error_code ec;exists=fs::exists(file,ec);if(ec)throw std::runtime_error("name table access failed");if(exists){original=ReadIdNamesFile(file);auto parsed=ParseIdNames(original);if(parsed.skipped)throw std::runtime_error("local table contains incomplete entries");names=std::move(parsed.names);}}
    catch(...){protectedFile=true;names.clear();error=L"対応表を読み込めません。元ファイルを保全し、ID表示を継続します。";}
}
IdNameTable IdNameStore::Snapshot()const{std::lock_guard<std::mutex> lock(mutex);return names;}
std::wstring IdNameStore::Error()const{std::lock_guard<std::mutex> lock(mutex);return error;}
void IdNameStore::Save(const IdNameTable& table){
    auto bytes=SerializeIdNames(table);auto normalized=ParseIdNames(bytes).names;std::lock_guard<std::mutex> lock(mutex);if(protectedFile||file.empty())throw std::runtime_error("name table is protected after a read failure");FileLock guard(file);
    std::error_code ec;bool present=fs::exists(file,ec);if(ec||present!=exists||(present&&ReadIdNamesFile(file)!=original))throw std::runtime_error("name table changed externally; reopen the dialog");AtomicWrite(file,bytes);original=std::move(bytes);names=std::move(normalized);exists=true;error.clear();
}
