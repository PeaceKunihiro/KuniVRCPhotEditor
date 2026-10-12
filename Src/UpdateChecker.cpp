#include "ViewerRuntime.h"
#include "UpdateChecker.h"
#include "AppVersion.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <commdlg.h>
#include <array>
#include <optional>
#include <chrono>
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"comdlg32.lib")
namespace {
constexpr auto StableUrl=L"https://github.com/PeaceKunihiro/KuniVRCPhotEditor/tree/main/Stable";
constexpr uint64_t MaxFile=100ull*1024*1024;
struct Version { std::array<unsigned,3> parts{}; bool operator<(const Version& other)const{return parts<other.parts;} };
std::optional<Version> ParseVersion(const std::wstring& text){
    Version v;size_t pos=0;
    for(int i=0;i<3;++i){size_t start=pos;unsigned value=0;while(pos<text.size()&&text[pos]>=L'0'&&text[pos]<=L'9'){if(pos-start>=6)return {};value=value*10+text[pos++]-L'0';}if(pos==start||(pos-start>1&&text[start]==L'0'))return {};v.parts[i]=value;if(i<2&&(pos==text.size()||text[pos++]!=L'.'))return {};}
    if(pos!=text.size())return {};return v;
}
struct Asset {Version version;std::wstring name,sha;uint64_t size=0;bool zip=false;};
std::optional<Asset> ParseName(const std::wstring& name){
    const std::wstring prefix=L"KuniVRCPhotEditor_v";if(name.find(prefix)!=0||name.size()<prefix.size()+9)return {};
    auto ext=name.substr(name.size()-4);if(ext!=L".exe"&&ext!=L".zip")return {};
    auto version=ParseVersion(name.substr(prefix.size(),name.size()-prefix.size()-4));if(!version)return {};return Asset{*version,name,L"",0,ext==L".zip"};
}
bool ValidSha(const std::wstring& s){return s.size()==40&&std::all_of(s.begin(),s.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');});}
std::optional<Asset> Latest(const std::string& json){
    auto entries=JsonArray::Parse(winrt::to_hstring(json));if(entries.Size()>=1000)throw std::runtime_error("Stable has too many entries");std::optional<Asset> best;
    for(auto item:entries){auto o=item.GetObject();if(o.GetNamedString(L"type",L"")!=L"file")continue;auto a=ParseName(o.GetNamedString(L"name",L"").c_str());if(!a)continue;
        auto size=o.GetNamedNumber(L"size",0);a->sha=o.GetNamedString(L"sha",L"");if(size<=0||size>MaxFile||std::floor(size)!=size||!ValidSha(a->sha))continue;a->size=static_cast<uint64_t>(size);
        if(!best||best->version<a->version||(!(a->version<best->version)&&a->zip))best=*a;
    }return best;
}
struct Internet {HINTERNET h=nullptr;explicit Internet(HINTERNET value):h(value){if(!h)throw std::runtime_error("WinHTTP initialization failed");}~Internet(){WinHttpCloseHandle(h);}Internet(const Internet&)=delete;};
void NetCheck(BOOL ok){if(!ok)throw std::runtime_error("Network error "+std::to_string(GetLastError()));}
std::wstring HttpError(DWORD status){if(status==404)return L"Stableを取得できません（未公開・アクセス権・パスを確認してください）。非公開リポジトリの認証には未対応です。";if(status==403||status==429)return L"アクセス拒否またはGitHubの利用制限です。時間をおいて再確認してください。";return L"GitHub応答 HTTP "+std::to_wstring(status);}
void Fetch(const wchar_t* host,const std::wstring& path,std::atomic<bool>& cancel,uint64_t limit,const std::function<void(const BYTE*,DWORD)>& consume){
    Internet session(WinHttpOpen(L"KuniVRCPhotEditor/" KUNI_VERSION_TEXT,WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    NetCheck(WinHttpSetTimeouts(session.h,10000,10000,10000,15000));Internet connection(WinHttpConnect(session.h,host,INTERNET_DEFAULT_HTTPS_PORT,0));
    Internet request(WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;NetCheck(WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect)));
    const auto start=std::chrono::steady_clock::now();auto stopped=[&]{if(cancel)throw std::runtime_error("Cancelled");if(std::chrono::steady_clock::now()-start>std::chrono::minutes(5))throw std::runtime_error("Transfer deadline exceeded");};stopped();
    NetCheck(WinHttpSendRequest(request.h,L"Accept: application/vnd.github+json\r\n",static_cast<DWORD>(-1),WINHTTP_NO_REQUEST_DATA,0,0,0));stopped();NetCheck(WinHttpReceiveResponse(request.h,nullptr));
    DWORD status=0,bytes=sizeof(status);NetCheck(WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&bytes,WINHTTP_NO_HEADER_INDEX));if(status!=200)throw std::runtime_error(winrt::to_string(HttpError(status)));
    BYTE buffer[64*1024];uint64_t total=0;for(;;){stopped();DWORD count=0;NetCheck(WinHttpReadData(request.h,buffer,sizeof(buffer),&count));stopped();if(!count)break;total+=count;if(total>limit)throw std::runtime_error("Response size limit exceeded");consume(buffer,count);}
}
struct BlobHash {
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    explicit BlobHash(uint64_t size){if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT))throw std::runtime_error("Hash provider failed");if(!CryptCreateHash(provider,CALG_SHA1,0,0,&hash)){CryptReleaseContext(provider,0);throw std::runtime_error("Hash failed");}auto prefix="blob "+std::to_string(size);prefix.push_back('\0');Add(reinterpret_cast<const BYTE*>(prefix.data()),static_cast<DWORD>(prefix.size()));}
    ~BlobHash(){if(hash)CryptDestroyHash(hash);if(provider)CryptReleaseContext(provider,0);}
    void Add(const BYTE* data,DWORD size){if(!CryptHashData(hash,data,size,0))throw std::runtime_error("Hash failed");}
    std::wstring Finish(){BYTE digest[20];DWORD size=20;if(!CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0))throw std::runtime_error("Hash failed");std::wstring result;for(auto b:digest){result+=L"0123456789abcdef"[b>>4];result+=L"0123456789abcdef"[b&15];}return result;}
};
using Source=std::function<void(const std::function<void(const BYTE*,DWORD)>&)>;
void SaveDownload(const Asset& asset,const fs::path& destination,std::atomic<bool>& cancel,const Source& source,const std::function<void(uint64_t)>& progress){
    GUID id{};winrt::check_hresult(CoCreateGuid(&id));wchar_t guid[40];StringFromGUID2(id,guid,40);auto temporary=destination;temporary+=std::wstring(L".")+guid+L".part";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create download file");
    try{BlobHash hash(asset.size);uint64_t total=0;source([&](const BYTE* data,DWORD size){if(cancel)throw std::runtime_error("Cancelled");if(total+size>asset.size)throw std::runtime_error("Download size mismatch");DWORD written=0;if(!WriteFile(file,data,size,&written,nullptr)||written!=size)throw std::runtime_error("Cannot write download file");hash.Add(data,size);total+=size;progress(total);});
        if(cancel)throw std::runtime_error("Cancelled");if(total!=asset.size||hash.Finish()!=asset.sha)throw std::runtime_error("Download integrity mismatch; retry update check");if(!FlushFileBuffers(file))throw std::runtime_error("Cannot flush download file");CloseHandle(file);file=INVALID_HANDLE_VALUE;
        if(cancel)throw std::runtime_error("Cancelled");if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot finalize download (existing files are not replaced)");
    }catch(...){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);DeleteFileW(temporary.c_str());throw;}
}
struct DialogState {
    std::thread worker;std::atomic<bool> cancel{false},done{false};std::atomic<uint64_t> received{0};std::mutex mutex;
    std::optional<Asset> asset;std::wstring message;fs::path destination;bool busy=false,closing=false,downloading=false,complete=false;
    std::function<void(HWND)> fixtureCapture;bool fixture=false,fixtureOk=false;int fixtureStage=0;fs::path fixtureFolder;ULONGLONG fixtureStart=0;
};
void Begin(DialogState& s,bool download){if(s.worker.joinable())s.worker.join();s.cancel=false;s.done=false;s.received=0;s.busy=true;s.downloading=download;s.complete=false;
    s.worker=std::thread([&s,download]{try{winrt::init_apartment(winrt::apartment_type::multi_threaded);struct Apartment{~Apartment(){winrt::uninit_apartment();}} apartment;
        if(download){auto asset=*s.asset;SaveDownload(asset,s.destination,s.cancel,[&](const auto& consume){if(s.fixture)consume(reinterpret_cast<const BYTE*>("hello"),5);else Fetch(L"raw.githubusercontent.com",L"/PeaceKunihiro/KuniVRCPhotEditor/main/Stable/"+asset.name,s.cancel,asset.size,consume);},[&](uint64_t count){s.received=count;});std::lock_guard<std::mutex> lock(s.mutex);s.message=L"ダウンロード完了。アプリを終了してから、EXEを手動で差し替えてください。ZIPは展開して使用してください。";s.complete=true;}
        else{std::string json;if(s.fixture)json=R"([{"type":"file","name":"KuniVRCPhotEditor_v999.0.0.zip","size":5,"sha":"b6fc4c620b67d95f953a5c1c1230aaab5db5a1b0"}])";else Fetch(L"api.github.com",L"/repos/PeaceKunihiro/KuniVRCPhotEditor/contents/Stable?ref=main",s.cancel,2*1024*1024,[&](const BYTE* data,DWORD size){json.append(reinterpret_cast<const char*>(data),size);});auto a=Latest(json);std::lock_guard<std::mutex> lock(s.mutex);s.asset=a;
            if(!a)s.message=L"有効な配布ファイルが見つかりませんでした。";else if(*ParseVersion(KUNI_VERSION_TEXT)<a->version)s.message=L"新版が見つかりました。\r\n使用中：v" KUNI_VERSION_TEXT L"\r\n配布版："+a->name+L"\r\n保存先を指定してダウンロードできます。";else s.message=L"使用中：v" KUNI_VERSION_TEXT L"\r\nより新しい配布版はありません。";
        }
    }catch(const std::exception& e){std::lock_guard<std::mutex> lock(s.mutex);s.message=s.cancel?L"キャンセルしました。":L"確認／ダウンロードに失敗しました。\r\n"+std::wstring(winrt::to_hstring(e.what()));}catch(...){std::lock_guard<std::mutex> lock(s.mutex);s.message=L"確認／ダウンロードに失敗しました。";}s.done=true;});
}
INT_PTR CALLBACK Dialog(HWND h,UINT m,WPARAM wp,LPARAM lp){auto s=reinterpret_cast<DialogState*>(GetWindowLongPtrW(h,DWLP_USER));
    if(m==WM_INITDIALOG){s=reinterpret_cast<DialogState*>(lp);SetWindowLongPtrW(h,DWLP_USER,lp);SetWindowTextW(h,L"アップデートを確認 — v" KUNI_VERSION_TEXT);
        auto control=[&](const wchar_t* type,const wchar_t* text,int x,int y,int w,int height,int id,DWORD style=0){auto c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,height,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return c;};
        control(L"STATIC",L"GitHubのStableを確認中…",12,12,530,95,100);control(PROGRESS_CLASSW,L"",12,115,530,20,101);SendDlgItemMessageW(h,101,PBM_SETRANGE32,0,100);
        control(L"BUTTON",L"ダウンロード…",12,150,135,28,102,WS_TABSTOP);control(L"BUTTON",L"Stableを開く",155,150,120,28,103,WS_TABSTOP);control(L"BUTTON",L"保存先を開く",283,150,120,28,104,WS_TABSTOP);control(L"BUTTON",L"キャンセル",411,150,130,28,IDCANCEL,WS_TABSTOP);EnableWindow(GetDlgItem(h,102),FALSE);EnableWindow(GetDlgItem(h,104),FALSE);SetTimer(h,1,100,nullptr);if(s->fixture){s->fixtureStart=GetTickCount64();SetTimer(h,2,100,nullptr);}Begin(*s,false);return TRUE;
    }
    if(!s)return FALSE;
    if(m==WM_TIMER&&wp==2){if(GetTickCount64()-s->fixtureStart>30000){s->cancel=true;s->closing=true;if(!s->busy)EndDialog(h,IDCANCEL);return TRUE;}if(!s->busy){if(s->fixtureStage==0){if(!IsWindowEnabled(GetDlgItem(h,102))){EndDialog(h,IDCANCEL);return TRUE;}s->fixtureCapture(h);s->fixtureStage=1;SendMessageW(h,WM_COMMAND,102,0);}else{s->fixtureOk=s->complete&&IsWindowEnabled(GetDlgItem(h,104));EndDialog(h,IDOK);}}return TRUE;}
    if(m==WM_TIMER){if(s->busy){if(s->downloading&&s->asset){auto bytes=s->received.load();SendDlgItemMessageW(h,101,PBM_SETPOS,static_cast<WPARAM>(bytes*100/s->asset->size),0);SetDlgItemTextW(h,100,(L"ダウンロード中："+std::to_wstring(bytes/1024)+L" / "+std::to_wstring(s->asset->size/1024)+L" KB").c_str());}if(s->done){s->worker.join();s->busy=false;std::lock_guard<std::mutex> lock(s->mutex);SetDlgItemTextW(h,100,s->message.c_str());SetDlgItemTextW(h,IDCANCEL,L"閉じる");EnableWindow(GetDlgItem(h,102),s->asset&&*ParseVersion(KUNI_VERSION_TEXT)<s->asset->version&&!s->complete);EnableWindow(GetDlgItem(h,104),s->complete);if(s->closing)EndDialog(h,IDCANCEL);}}return TRUE;}
    if(m==WM_CLOSE||(m==WM_COMMAND&&LOWORD(wp)==IDCANCEL)){if(s->busy){s->cancel=true;s->closing=true;SetDlgItemTextW(h,IDCANCEL,L"終了待ち…");EnableWindow(GetDlgItem(h,IDCANCEL),FALSE);}else EndDialog(h,IDCANCEL);return TRUE;}
    if(m==WM_COMMAND){switch(LOWORD(wp)){case 103:ShellExecuteW(h,L"open",StableUrl,nullptr,nullptr,SW_SHOWNORMAL);return TRUE;case 104:ShellExecuteW(h,L"open",s->destination.parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);return TRUE;case 102:{if(s->busy||!s->asset)return TRUE;wchar_t path[32768]{};wcscpy_s(path,s->asset->name.c_str());OPENFILENAMEW ofn{sizeof(ofn)};ofn.hwndOwner=h;ofn.lpstrFile=path;ofn.nMaxFile=32768;ofn.lpstrFilter=L"配布ファイル\0*.exe;*.zip\0\0";ofn.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;ofn.lpstrTitle=L"新版の保存先（既存ファイルは上書きしません）";if(!s->fixture){if(!GetSaveFileNameW(&ofn))return TRUE;s->destination=path;}std::error_code existsError;if(fs::exists(s->destination,existsError)||existsError){MessageBoxW(h,L"別のファイル名を指定してください。既存ファイルは上書きしません。",L"保存先",MB_OK|MB_ICONINFORMATION);return TRUE;}EnableWindow(GetDlgItem(h,102),FALSE);Begin(*s,true);SetDlgItemTextW(h,IDCANCEL,L"キャンセル");return TRUE;}}}
    return FALSE;
}
}
void ShowUpdateChecker(HWND parent){DialogState state;struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;} t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME,0,0,0,0,370,130},0,0,0};DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,parent,Dialog,reinterpret_cast<LPARAM>(&state));state.cancel=true;if(state.worker.joinable())state.worker.join();}
void RunUpdateTests(const fs::path& folder,std::ostream& report){
    auto require=[&](bool ok,const char* message){if(!ok)throw std::runtime_error(message);report<<"PASS: "<<message<<'\n';};
    require(*ParseVersion(L"0.4.9")<*ParseVersion(L"0.4.10")&&!ParseVersion(L"0.4.6-beta")&&!ParseVersion(L"0.04.6"),"v0.4.6 numeric version boundaries");require(ParseName(L"KuniVRCPhotEditor_v0.4.7.zip")&&!ParseName(L"Other_v9.0.0.exe")&&!ParseName(L"KuniVRCPhotEditor_v0.4.7-beta.exe"),"v0.4.6 distribution name filter");
    auto json="[{\"type\":\"file\",\"name\":\"KuniVRCPhotEditor_v0.4.10.exe\",\"size\":5,\"sha\":\"b6fc4c620b67d95f953a5c1c1230aaab5db5a1b0\"},{\"type\":\"file\",\"name\":\"KuniVRCPhotEditor_v0.4.10.zip\",\"size\":5,\"sha\":\"b6fc4c620b67d95f953a5c1c1230aaab5db5a1b0\"}]";auto asset=Latest(json);require(asset&&asset->zip&&!Latest("[]"),"v0.4.6 latest selection prefers ZIP and empty differs");
    auto target=folder/L"update-download-fixture.zip";fs::remove(target);std::atomic<bool> cancel{false};auto source=[](const auto& consume){consume(reinterpret_cast<const BYTE*>("hello"),5);};SaveDownload(*asset,target,cancel,source,[](uint64_t){});std::ifstream input(target,std::ios::binary);std::string result((std::istreambuf_iterator<char>(input)),{});input.close();require(result=="hello","v0.4.6 download atomic completion and Git blob hash");
    bool failed=false;try{SaveDownload(*asset,target,cancel,source,[](uint64_t){});}catch(...){failed=true;}require(failed&&fs::file_size(target)==5,"v0.4.6 existing destination preserved");fs::remove(target);
    for(int mode:{0,1,2}){failed=false;cancel=false;try{SaveDownload(*asset,target,cancel,[&](const auto& consume){if(mode==0)consume(reinterpret_cast<const BYTE*>("bad!!"),5);if(mode==1)consume(reinterpret_cast<const BYTE*>("hi"),2);if(mode==2){cancel=true;consume(reinterpret_cast<const BYTE*>("hello"),5);}},[](uint64_t){});}catch(...){failed=true;}require(failed&&!fs::exists(target),"v0.4.6 mismatch/truncation/cancellation leave no complete file");}
    bool partial=false;for(auto& item:fs::directory_iterator(folder))partial|=item.path().filename().wstring().find(L"update-download-fixture.zip.")==0;require(!partial,"v0.4.6 partial files removed");
}
void RunUpdateNetworkTests(const fs::path& folder,std::ostream& report){
    std::atomic<bool> cancel{false};std::string json;Fetch(L"api.github.com",L"/repos/PeaceKunihiro/KuniVRCPhotEditor/contents/Stable?ref=main",cancel,2*1024*1024,[&](const BYTE* data,DWORD size){json.append(reinterpret_cast<const char*>(data),size);});auto asset=Latest(json);if(!asset)throw std::runtime_error("No distribution asset");report<<"PASS: live Stable API; latest="<<winrt::to_string(asset->name)<<'\n';
    auto target=folder/(L"network-fixture-"+asset->name);if(fs::exists(target))throw std::runtime_error("Network fixture already exists");SaveDownload(*asset,target,cancel,[&](const auto& consume){Fetch(L"raw.githubusercontent.com",L"/PeaceKunihiro/KuniVRCPhotEditor/main/Stable/"+asset->name,cancel,asset->size,consume);},[](uint64_t){});report<<"PASS: live raw download size and Git blob hash; bytes="<<fs::file_size(target)<<'\n';fs::remove(target);
}

void RunUpdateDialogTests(const fs::path& folder,std::ostream& report,const std::function<void(HWND)>& capture){DialogState state;state.fixtureCapture=capture;state.fixture=true;state.fixtureFolder=folder;state.destination=folder/L"update-dialog-fixture.zip";fs::remove(state.destination);struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;} t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME,0,0,0,0,370,130},0,0,0};DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,app.window,Dialog,reinterpret_cast<LPARAM>(&state));state.cancel=true;if(state.worker.joinable())state.worker.join();bool ok=state.fixtureOk&&fs::exists(state.destination)&&fs::file_size(state.destination)==5;fs::remove(state.destination);if(!ok)throw std::runtime_error("v0.4.6 update dialog check/download failed");report<<"PASS: v0.4.6 update dialog newer notification, download button, completion and folder control\n";}
