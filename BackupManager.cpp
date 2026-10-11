#define NOMINMAX
#include "BackupManager.h"
#include "ExtendedXmp.h"
#include "ThumbnailCache.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <wrl.h>
#include <shobjidl.h>
#include <fstream>
#include <mutex>
#include <thread>
#include <algorithm>
#include <stdexcept>
namespace fs=std::filesystem;
using namespace winrt::Windows::Data::Json;
namespace {
std::recursive_mutex ledgerMutex;
struct Handle { HANDLE value=INVALID_HANDLE_VALUE; ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);} };
void Check(BOOL ok){if(!ok)winrt::throw_last_error();}
bool Prefix(const std::wstring& s){return s.size()>=3&&_wcsnicmp(s.c_str(),L"AB_",3)==0;}
bool Image(const fs::path& p){auto e=p.extension().wstring();return _wcsicmp(e.c_str(),L".png")==0||_wcsicmp(e.c_str(),L".jpg")==0||_wcsicmp(e.c_str(),L".jpeg")==0||_wcsicmp(e.c_str(),L".webp")==0;}
bool PlainFile(const std::wstring& path){auto attr=GetFileAttributesW(path.c_str());return attr!=INVALID_FILE_ATTRIBUTES && !(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));}
std::wstring Id(){GUID g{};winrt::check_hresult(CoCreateGuid(&g));wchar_t s[40]{};StringFromGUID2(g,s,40);return s;}
std::wstring Hash(const std::string& data){return winrt::to_hstring(XmpMd5(data)).c_str();}
std::string Read(HANDLE file){LARGE_INTEGER size{},zero{};Check(GetFileSizeEx(file,&size));if(size.QuadPart<8||size.QuadPart>256LL*1024*1024)throw std::runtime_error("Backup input limit");Check(SetFilePointerEx(file,zero,nullptr,FILE_BEGIN));std::string data(static_cast<size_t>(size.QuadPart),'\0');DWORD done{};Check(ReadFile(file,data.data(),static_cast<DWORD>(data.size()),&done,nullptr));if(done!=data.size())throw std::runtime_error("Incomplete original read");return data;}
void NewFile(const std::wstring& path,const std::string& data){Handle file;file.value=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file.value!=INVALID_HANDLE_VALUE);try{DWORD done{};Check(WriteFile(file.value,data.data(),static_cast<DWORD>(data.size()),&done,nullptr));if(done!=data.size())throw std::runtime_error("Incomplete backup write");Check(FlushFileBuffers(file.value));}catch(...){CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;DeleteFileW(path.c_str());throw;}}
void Rename(HANDLE file,const std::wstring& path){auto full=fs::absolute(path).wstring();std::vector<BYTE> buffer(sizeof(FILE_RENAME_INFO)+full.size()*sizeof(wchar_t));auto info=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());info->ReplaceIfExists=FALSE;info->RootDirectory=nullptr;info->FileNameLength=static_cast<DWORD>(full.size()*sizeof(wchar_t));memcpy(info->FileName,full.data(),info->FileNameLength);Check(SetFileInformationByHandle(file,FileRenameInfo,info,static_cast<DWORD>(buffer.size())));}
}
std::vector<BackupEntry> ReadBackupLedger(const fs::path& ledger){std::lock_guard<std::recursive_mutex> lock(ledgerMutex);if(!fs::exists(ledger))return {};if(fs::file_size(ledger)>4*1024*1024)throw std::runtime_error("Backup ledger limit");std::ifstream in(ledger,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});auto json=JsonObject::Parse(winrt::to_hstring(bytes));if(json.GetNamedNumber(L"version")!=1)throw std::runtime_error("Backup ledger version");std::vector<BackupEntry> entries;for(auto item:json.GetNamedArray(L"entries")){auto o=item.GetObject();BackupEntry e;e.path=o.GetNamedString(L"path");e.source=o.GetNamedString(L"source");e.fingerprint=o.GetNamedString(L"fingerprint");e.sourceFingerprint=o.GetNamedString(L"sourceFingerprint",e.fingerprint);e.created=std::stoull(std::wstring(o.GetNamedString(L"created")));entries.push_back(std::move(e));}return entries;}
void WriteBackupLedger(const fs::path& ledger,const std::vector<BackupEntry>& entries){std::lock_guard<std::recursive_mutex> lock(ledgerMutex);JsonObject json;json.Insert(L"version",JsonValue::CreateNumberValue(1));JsonArray list;for(auto& e:entries){JsonObject o;o.Insert(L"path",JsonValue::CreateStringValue(e.path));o.Insert(L"source",JsonValue::CreateStringValue(e.source));o.Insert(L"fingerprint",JsonValue::CreateStringValue(e.fingerprint));o.Insert(L"created",JsonValue::CreateStringValue(std::to_wstring(e.created)));o.Insert(L"sourceFingerprint",JsonValue::CreateStringValue(e.sourceFingerprint.empty()?e.fingerprint:e.sourceFingerprint));list.Append(o);}json.Insert(L"entries",list);auto bytes=winrt::to_string(json.Stringify());if(bytes.size()>4*1024*1024)throw std::runtime_error("Backup ledger limit");auto temp=ledger;temp+=Id()+L".tmp";NewFile(temp.wstring(),bytes);if(!MoveFileExW(temp.c_str(),ledger.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){auto error=GetLastError();DeleteFileW(temp.c_str());winrt::throw_hresult(HRESULT_FROM_WIN32(error));}}
std::vector<BackupCandidate> FindBackups(const std::wstring& folder,const fs::path& ledger){std::vector<BackupEntry> managed;try{managed=ReadBackupLedger(ledger);}catch(...){}std::vector<BackupCandidate> found;for(auto& entry:fs::directory_iterator(folder)){auto path=entry.path();if(!Prefix(path.filename().wstring())||!Image(path)||!PlainFile(path.wstring()))continue;WIN32_FILE_ATTRIBUTE_DATA data{};if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data))continue;BackupCandidate c;c.path=path.wstring();c.sourceKey=ThumbnailCache::Key(c.path);c.bytes=(static_cast<unsigned long long>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;c.modified=data.ftLastWriteTime;
    auto record=std::find_if(managed.begin(),managed.end(),[&](auto& e){return _wcsicmp(e.path.c_str(),c.path.c_str())==0;});if(record!=managed.end()){try{c.managed=MetadataFingerprint(c.path)==record->fingerprint;}catch(...){}}
    found.push_back(std::move(c));}std::sort(found.begin(),found.end(),[](auto& a,auto& b){return a.path<b.path;});return found;}
namespace {
// Refuse a permanent-delete fallback even when the volume cannot recycle an item.
class RecycleSink final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,IFileOperationProgressSink> {
public:
    HRESULT STDMETHODCALLTYPE StartOperations() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD,IShellItem*,LPCWSTR) override{return E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR) override{return E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR) override{return E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags,IShellItem*) override{return flags&TSF_DELETE_RECYCLE_IF_POSSIBLE?S_OK:E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD,IShellItem*,HRESULT,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD,IShellItem*,LPCWSTR) override{return E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD,IShellItem*,LPCWSTR,LPCWSTR,DWORD,HRESULT,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT,UINT) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE ResetTimer() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PauseTimer() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE ResumeTimer() override{return S_OK;}
};
}
static bool RecycleBackupSta(const std::wstring& path, std::wstring* error){if(error)*error=L"ごみ箱へ移動できません。使用中・アクセス権・保存先のごみ箱対応を確認してください。";if(!PlainFile(path)||!Prefix(fs::path(path).filename().wstring())||!Image(path))return false;Microsoft::WRL::ComPtr<IFileOperation> operation;auto hr=CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&operation));if(FAILED(hr))return false;
    hr=operation->SetOperationFlags(FOF_ALLOWUNDO|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_SILENT|FOFX_RECYCLEONDELETE|FOFX_EARLYFAILURE);if(FAILED(hr))return false;auto sink=Microsoft::WRL::Make<RecycleSink>();
    Microsoft::WRL::ComPtr<IShellItem> item;hr=SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item));if(SUCCEEDED(hr))hr=operation->DeleteItem(item.Get(),sink.Get());if(SUCCEEDED(hr))hr=operation->PerformOperations();BOOL aborted=TRUE;if(SUCCEEDED(hr))operation->GetAnyOperationsAborted(&aborted);if(FAILED(hr)&&error){wchar_t code[40]{};swprintf_s(code,L" (HRESULT=0x%08X)",static_cast<unsigned>(hr));*error+=code;}return SUCCEEDED(hr)&&!aborted&&!fs::exists(path);}
bool RecycleBackup(const std::wstring& path, std::wstring* error){APTTYPE type;APTTYPEQUALIFIER qualifier;if(SUCCEEDED(CoGetApartmentType(&type,&qualifier))&&(type==APTTYPE_STA||type==APTTYPE_MAINSTA))return RecycleBackupSta(path, error);bool recycled=false;std::thread shell([&]{auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(SUCCEEDED(hr)){try{recycled=RecycleBackupSta(path, error);}catch(...){}CoUninitialize();}});shell.join();return recycled;}
std::wstring PruneBackups(const std::wstring& folder,int limit,const fs::path& ledger,const std::vector<std::wstring>& protectedPaths){std::lock_guard<std::recursive_mutex> lock(ledgerMutex);if(limit<1||limit>1000)throw std::runtime_error("Invalid backup limit");auto records=ReadBackupLedger(ledger);std::vector<BackupEntry> candidates;for(auto& e:records)if(_wcsicmp(fs::path(e.path).parent_path().c_str(),fs::path(folder).c_str())==0&&PlainFile(e.path)&&std::none_of(protectedPaths.begin(),protectedPaths.end(),[&](const auto& p){return _wcsicmp(p.c_str(),e.path.c_str())==0;})){try{if(MetadataFingerprint(e.path)==e.fingerprint)candidates.push_back(e);}catch(...){}}
    std::sort(candidates.begin(),candidates.end(),[](auto& a,auto& b){return a.created<b.created;});int excess=static_cast<int>(candidates.size())-limit;std::wstring warning;for(auto& e:candidates){if(excess<=0)break;if(RecycleBackup(e.path)){records.erase(std::remove_if(records.begin(),records.end(),[&](auto& r){return r.path==e.path;}),records.end());--excess;}else {warning+=L"バックアップを整理できません。新しい世代は保持します: "+e.path+L"\r\n";break;}}
    WriteBackupLedger(ledger,records);return warning;}
std::wstring SaveMetadataOverwrite(const std::wstring& source,const std::wstring& fingerprint,const MetadataEdit& edit,bool backup,int limit,const fs::path& ledger,const std::wstring& prepared,const std::wstring& preparedFingerprint,bool deferPrune){
    std::lock_guard<std::recursive_mutex> lock(ledgerMutex);if(limit<1||limit>1000)throw std::runtime_error("Invalid backup limit");if(!PlainFile(source))throw std::runtime_error("Original is missing or is a link");
    auto attr=GetFileAttributesW(source.c_str());if(attr&FILE_ATTRIBUTE_READONLY)throw std::runtime_error("Original is read only");
    auto path=fs::path(source);auto id=Id();auto stage=prepared.empty()?path.parent_path()/(L".Kuni-stage-"+id+path.extension().wstring()):fs::path(prepared);auto recovery=path.parent_path()/(L".Kuni-recovery-"+id+path.extension().wstring());
    Handle original;original.value=CreateFileW(source.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(original.value!=INVALID_HANDLE_VALUE);
    auto bytes=Read(original.value);if(Hash(bytes)!=fingerprint)throw std::runtime_error("Original changed; reload before overwrite");
    bool renamed=false,committed=false;std::wstring backupPath;
    try{if(prepared.empty())SaveMetadataCopy(source,stage.wstring(),fingerprint,edit,true);else if(!PlainFile(prepared)||MetadataFingerprint(prepared)!=preparedFingerprint)throw std::runtime_error("Prepared copy changed");
        FILETIME created{};Check(GetFileTime(original.value,&created,nullptr,nullptr));{Handle staged;staged.value=CreateFileW(stage.c_str(),FILE_WRITE_ATTRIBUTES,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(staged.value!=INVALID_HANDLE_VALUE);Check(SetFileTime(staged.value,&created,nullptr,nullptr));}

        if(backup){auto records=ReadBackupLedger(ledger);SYSTEMTIME now{};GetSystemTime(&now);FILETIME time{};GetSystemTimeAsFileTime(&time);wchar_t stamp[40]{};swprintf_s(stamp,L"%04u%02u%02uT%02u%02u%02u%03uZ",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);
            auto stem=path.stem().wstring();if(stem.size()>120)stem.resize(120);backupPath=(path.parent_path()/(L"AB_"+stem+L"__"+stamp+L"_"+id+path.extension().wstring())).wstring();MetadataEdit tag;tag.actions[5]=EditAction::Append;tag.values[5]=L"【AutoBackup】";SaveMetadataCopy(source,backupPath,fingerprint,tag,true);auto backupFingerprint=MetadataFingerprint(backupPath);auto before=InspectEditable(source),after=InspectEditable(backupPath);for(size_t i=0;i<5;++i)if(before.values[i]!=after.values[i])throw std::runtime_error("Backup metadata verification failed");if(after.values[5]!=NormalizeTags(before.values[5]+L"\n【AutoBackup】"))throw std::runtime_error("Backup tag verification failed");records.push_back({backupPath,fs::absolute(source).wstring(),backupFingerprint,(static_cast<unsigned long long>(time.dwHighDateTime)<<32)|time.dwLowDateTime,fingerprint});WriteBackupLedger(ledger,records);}
        // Rename through the locked original handle; no check/replace race and no overwrite of a competing file.
        Rename(original.value,recovery.wstring());renamed=true;Check(MoveFileExW(stage.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH));committed=true;
        FILE_DISPOSITION_INFO dispose{TRUE};if(!SetFileInformationByHandle(original.value,FileDispositionInfo,&dispose,sizeof(dispose)))return L"保存完了。復旧用原本が残っています: "+recovery.wstring();
    }catch(...){DeleteFileW(stage.c_str());if(renamed&&!committed){try{Rename(original.value,source);}catch(...){throw std::runtime_error(winrt::to_string(L"復旧用原本を保持しました: "+recovery.wstring()+L"。元パスに別ファイルがある場合は上書きしません。"));}}throw;}
    if(deferPrune)return L"";
    try{return backup?PruneBackups(path.parent_path().wstring(),limit,ledger):L"";}catch(...){return L"保存完了。バックアップ管理記録または上限整理を確認してください。";}
}
