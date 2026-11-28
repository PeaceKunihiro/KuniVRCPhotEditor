#include <future>
#include <deque>
#include <fstream>
#include <regex>
namespace {
constexpr int ObservationMode=2701;
HWND observationText{},observationMode{},observationStatus{};
int observationFrequency=0;
ULONGLONG observationNext=0;
bool observationReset=true;
struct ObservationResult {std::wstring text,error;};
std::future<ObservationResult> observationFuture;
struct ObservationStore {
    std::deque<std::string> events;
    size_t bytes=0;bool dirty=false;
    fs::path loaded,source;
    uint64_t offset=0;
    std::wstring identity,worldName,worldId;
    std::string unfinished;
};
ObservationStore observationStore;
std::wstring ObservationEscape(std::wstring value){
    std::wstring out;
    for(auto c:value){
        if(c==92)out+=L"\\\\";
        else if(c==13)out+=L"\\r";
        else if(c==10)out+=L"\\n";
        else if(c==L'&')out+=L"\\&";
        else out+=c;
    }
    return out;
}
std::wstring ObservationStamp(stage3::Time time){
    ULARGE_INTEGER n{};n.QuadPart=time*10000;FILETIME file{n.LowPart,n.HighPart};
    SYSTEMTIME utc{},local{};FileTimeToSystemTime(&file,&utc);
    SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local);
    wchar_t text[32]{};
    swprintf_s(text,L"%02u%02u%02u-%02u%02u%02u",local.wYear%100,local.wMonth,local.wDay,local.wHour,local.wMinute,local.wSecond);
    return text;
}
std::wstring ObservationEvent(const std::wstring& line,ObservationStore& store){
    stage3::Event event;
    std::wstring tag,content;
    if(stage3::ParseLogLine(line,event)&&event.time){
        if(event.kind==stage3::Kind::RoomName){store.worldName=event.worldName;return {};}
        if(event.kind==stage3::Kind::Join||event.kind==stage3::Kind::Leave){
            tag=event.kind==stage3::Kind::Join?L"join":L"leave";
            content=ObservationEscape(event.person.name)+L"&"+ObservationEscape(event.person.id);
        }else if(event.kind==stage3::Kind::Room){
            tag=L"world";store.worldId=event.world;
            content=ObservationEscape(store.worldName)+L"&"+ObservationEscape(event.world);store.worldName.clear();
        }else return {};
    }else {
        // Only explicit video loading lines are eligible, not arbitrary URLs in logs.
        auto folded=line;std::transform(folded.begin(),folded.end(),folded.begin(),towlower);
        if(folded.find(L"video")==std::wstring::npos||
           (folded.find(L"load")==std::wstring::npos&&folded.find(L"play")==std::wstring::npos))return {};
        static const std::wregex url(LR"(https?://[^\s"<>]+)",std::regex::icase);
        std::wsmatch match;if(!std::regex_search(line,match,url))return {};
        event.time=stage3::ParseTime(line.substr(0,19));if(!event.time)return {};
        tag=L"movie";content=ObservationEscape(match.str());
    }
    return ObservationStamp(event.time)+L"-"+tag+L"-"+content;
}
void ObservationSafe(const fs::path& p){
    auto attributes=GetFileAttributesW(p.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("Observation path is redirected");
}
void TrimObservation(ObservationStore& store,size_t limit){
    while(store.bytes>limit&&!store.events.empty()){
        store.dirty=true;store.bytes-=store.events.front().size()+2;store.events.pop_front();
    }
}
void SaveObservation(const fs::path& directory,ObservationStore& store,size_t limit){
    TrimObservation(store,limit);
    ObservationSafe(directory.parent_path());ObservationSafe(directory);
    fs::create_directory(directory);
    auto file=directory/L"events.txt",temp=directory/L"events.tmp";
    ObservationSafe(file);ObservationSafe(temp);
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        for(auto& e:store.events)out<<e<<"\r\n";
        out.flush();if(!out)throw std::runtime_error("Observation write failed");
    }
    if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Observation replace failed");
    store.dirty=false;
}
ObservationResult ReadObservation(fs::path directory,fs::path logs,size_t limit,bool reset,bool enabled){
    ObservationResult result;
    try{
        auto& store=observationStore;
        ObservationSafe(directory.parent_path());ObservationSafe(directory);
        auto file=directory/L"events.txt";ObservationSafe(file);
        if(store.loaded!=file){
            store={};store.loaded=file;
            std::ifstream input(file,std::ios::binary);std::string line;
            while(std::getline(input,line)){
                if(!line.empty()&&line.back()==13)line.pop_back();
                if(!line.empty()){store.bytes+=line.size()+2;store.events.push_back(std::move(line));TrimObservation(store,limit);}
            }
        }
        if(reset){store.source.clear();store.identity.clear();store.unfinished.clear();store.worldName.clear();}
        bool changed=store.bytes>limit;
        if(enabled){
            auto source=stage3::LatestLog(logs);
            if(!source.empty()){
                HANDLE handle=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("VRChat log cannot be opened");
                struct Close{HANDLE h;~Close(){CloseHandle(h);}} close{handle};
                BY_HANDLE_FILE_INFORMATION info{};
                if(!GetFileInformationByHandle(handle,&info))throw std::runtime_error("VRChat log identity unavailable");
                uint64_t length=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
                auto identity=source.wstring()+L"|"+std::to_wstring(info.dwVolumeSerialNumber)+L"|"+std::to_wstring(info.nFileIndexHigh)+L"|"+std::to_wstring(info.nFileIndexLow);
                if(store.identity!=identity||length<store.offset){
                    bool first=store.identity.empty();
                    store.identity=identity;store.source=source;store.offset=first&&reset?length:0;store.unfinished.clear();store.worldName.clear();
                }
                LARGE_INTEGER position{};position.QuadPart=store.offset;
                if(!SetFilePointerEx(handle,position,nullptr,FILE_BEGIN))throw std::runtime_error("VRChat log seek failed");
                size_t budget=2*1024*1024;
                char buffer[65536];
                while(store.offset<length&&budget){
                    DWORD count=0,wanted=DWORD(std::min<uint64_t>({sizeof(buffer),length-store.offset,budget}));
                    if(!ReadFile(handle,buffer,wanted,&count,nullptr))throw std::runtime_error("VRChat log read failed");
                    if(!count)break;
                    store.offset+=count;budget-=count;store.unfinished.append(buffer,count);
                    size_t start=0,end;
                    while((end=store.unfinished.find(char(10),start))!=std::string::npos){
                        auto raw=store.unfinished.substr(start,end-start);start=end+1;
                        if(!raw.empty()&&raw.back()==13)raw.pop_back();
                        auto event=ObservationEvent(std::wstring(winrt::to_hstring(raw)),store);
                        if(!event.empty()){auto encoded=winrt::to_string(event);store.bytes+=encoded.size()+2;store.dirty=true;store.events.push_back(std::move(encoded));TrimObservation(store,limit);changed=true;}
                    }
                    store.unfinished.erase(0,start);
                    if(store.unfinished.size()>1024*1024)throw std::runtime_error("VRChat log line too long");
                }
            }else result.error=L"VRChatログが見つかりません。";
        }
        if(changed||store.dirty)SaveObservation(directory,store,limit);
        std::wstring day;
        size_t displayed=0;
        for(auto i=store.events.rbegin();i!=store.events.rend()&&displayed<2000;++i,++displayed){
            auto row=std::wstring(winrt::to_hstring(*i));
            if(row.size()<21)continue;
            auto date=row.substr(0,6);
            if(date!=day){result.text+=L"-----20"+date.substr(0,2)+L"/"+date.substr(2,2)+L"/"+date.substr(4,2)+L"-----\r\n";day=date;}
            result.text+=row.substr(7,2)+L":"+row.substr(9,2)+L":"+row.substr(11,2)+L" "+row.substr(14)+L"\r\n";
            if(result.text.size()>500000)break;
        }
    }catch(const std::exception& e){result.error=std::wstring(winrt::to_hstring(e.what()));}
    return result;
}
}
void CreateObservation(){
    TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(L"観測");
    TabCtrl_InsertItem(app.metadataTabs,TabCtrl_GetItemCount(app.metadataTabs),&tab);
    observationMode=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,140,180,app.side,reinterpret_cast<HMENU>(INT_PTR(ObservationMode)),GetModuleHandleW(nullptr),nullptr);
    for(auto label:{L"オフ",L"標準（10秒）",L"高（2秒）"})SendMessageW(observationMode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendMessageW(observationMode,CB_SETCURSEL,0,0);
    observationText=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_HSCROLL|ES_AUTOHSCROLL|WS_TABSTOP,0,0,1,1,app.side,nullptr,GetModuleHandleW(nullptr),nullptr);
    observationStatus=CreateWindowExW(0,L"STATIC",L"停止",WS_CHILD|WS_VISIBLE,0,0,1,1,app.side,nullptr,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(observationText,EM_SETLIMITTEXT,600000,0);
    for(auto h:{observationMode,observationText,observationStatus})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),TRUE);
}
void LayoutObservation(RECT page,bool visible){
    int header=UiControlHeight();
    MoveWindow(observationMode,page.left,page.top,std::min(190L,page.right-page.left),200,FALSE);
    MoveWindow(observationStatus,page.left,page.top+header+3,std::max(1L,page.right-page.left),header,FALSE);
    MoveWindow(observationText,page.left,page.top+header*2+6,std::max(1L,page.right-page.left),std::max(1L,page.bottom-page.top-header*2-6),FALSE);
    for(auto h:{observationMode,observationText,observationStatus}){ShowWindow(h,visible?SW_SHOW:SW_HIDE);if(visible)SetWindowPos(h,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
}
void ObservationTick(){
    if(!observationMode||app.closing)return;
    int mode=int(SendMessageW(observationMode,CB_GETCURSEL,0,0));
    if(mode!=observationFrequency){observationReset=true;observationFrequency=mode;observationNext=0;}
    if(observationFuture.valid()){
        if(observationFuture.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        auto result=observationFuture.get();
        if(!result.error.empty())SetWindowTextW(observationStatus,result.error.c_str());
        else SetWindowTextW(observationStatus,mode?L"観測中":L"停止");
        if(Text(observationText)!=result.text){SetWindowTextW(observationText,result.text.c_str());SendMessageW(observationText,EM_SETSEL,0,0);SendMessageW(observationText,EM_SCROLLCARET,0,0);}
    }
    static int previousCapacity=-1;
    if(!mode&&previousCapacity==app.observationMB)return;
    if(GetTickCount64()<observationNext&&previousCapacity==app.observationMB)return;
    try{
        auto logs=mode?stage3::ObservationLogFolder():std::wstring{};
        auto directory=app.settings.parent_path()/L"Observation";
        bool reset=observationReset;observationReset=false;previousCapacity=app.observationMB;
        observationNext=GetTickCount64()+(mode==2?2000:10000);
        auto limit=size_t(app.observationMB)*1000000;
        observationFuture=std::async(std::launch::async,[directory,logs,limit,reset,mode]{return ReadObservation(directory,logs,limit,reset,mode!=0);});
    }catch(const std::exception& e){SetWindowTextW(observationStatus,std::wstring(winrt::to_hstring(e.what())).c_str());}
}
void StopObservation(){if(observationFuture.valid())observationFuture.get();}
int ObservationTest(){
    auto directory=app.settings.parent_path()/L"self-test-data"/L"observation";
    fs::create_directories(directory);
    std::ofstream report(directory/L"result.txt");
    try{
        ObservationStore store;
        auto join=ObservationEvent(L"2026.11.28 02:11:37 Log - [Behaviour] OnPlayerJoined Name (usr_12345678-abcd-1234-abcd-123456789abc)",store);
        if(join.find(L"-join-Name&usr_")==std::wstring::npos)throw std::runtime_error("join parse");
        auto movie=ObservationEvent(L"2026.11.28 02:11:38 Log - [Video Playback] Attempting to load URL https://example.com/video",store);
        if(movie.find(L"-movie-https://example.com/video")==std::wstring::npos)throw std::runtime_error("movie parse");
        if(!ObservationEvent(L"2026.11.28 02:11:38 Log - unrelated https://example.com/",store).empty())throw std::runtime_error("unrelated URL");
        store.events={"old","new"};store.bytes=10;SaveObservation(directory,store,5);
        if(store.events.size()!=1||store.events.front()!="new")throw std::runtime_error("oldest eviction");
        auto logs=directory/L"logs";fs::create_directories(logs);
        auto log=logs/L"output_log_2026-11-28_02-00-00.txt";
        {std::ofstream out(log,std::ios::trunc);out<<"2026.11.28 02:10:00 Log - [Behaviour] OnPlayerJoined Old\n";}
        observationStore={};
        ReadObservation(directory,logs,100000,true,true);
        {std::ofstream out(log,std::ios::app);out<<"2026.11.28 02:11:37 Log - [Behaviour] OnPlayerJoined Current\n";}
        auto shown=ReadObservation(directory,logs,100000,false,true);
        if(shown.text.find(L"Current")==std::wstring::npos||shown.text.find(L"Old")!=std::wstring::npos)throw std::runtime_error("start baseline and incremental read");
        auto before=observationStore.events.size();
        ReadObservation(directory,logs,100000,false,true);
        if(observationStore.events.size()!=before)throw std::runtime_error("duplicate read");
        {std::ofstream out(log,std::ios::app);out<<"2026.11.28 02:12:37 Log - [Behaviour] OnPlayerLeft Current\n";}
        ReadObservation(directory,logs,100000,false,false);
        if(observationStore.events.size()!=before)throw std::runtime_error("OFF read");
        ReadObservation(directory,logs,100000,true,true);
        if(observationStore.events.size()!=before)throw std::runtime_error("resume baseline");
        report<<"ALL PASSED\n";return 0;
    }catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}
}

void CaptureV0412Ui(const fs::path&,HWND);
int ObservationUiTest(){
    std::ofstream report(app.settings.parent_path()/L"observation-ui-result.txt");
    try{
        if(TabCtrl_GetItemCount(app.metadataTabs)!=4||SendMessageW(observationMode,CB_GETCURSEL,0,0)!=0)throw std::runtime_error("initial observation OFF tab");
        auto old=app.layout;app.layout=L"right";
        SetWindowPos(app.side,nullptr,0,0,220,400,SWP_NOMOVE|SWP_NOZORDER);
        LayoutSideView();
        if(!(GetWindowLongPtrW(app.sideSelector,GWL_STYLE)&WS_VISIBLE)||SendMessageW(app.sideSelector,CB_GETCOUNT,0,0)!=4)throw std::runtime_error("narrow dropdown");
        SendMessageW(app.sideSelector,CB_SETCURSEL,3,0);
        SendMessageW(app.side,WM_COMMAND,MAKEWPARAM(2690,CBN_SELCHANGE),0);
        if(TabCtrl_GetCurSel(app.metadataTabs)!=3||!(GetWindowLongPtrW(observationText,GWL_STYLE)&WS_VISIBLE))throw std::runtime_error("observation selection");
        CaptureV0412Ui(app.settings.parent_path()/L"observation-ui.png",app.side);
        ShowCacheSettings(0,[&](HWND h){
            if(Text(GetDlgItem(h,2441))!=L"10")throw std::runtime_error("initial capacity");
            SetDlgItemTextW(h,2441,L"1");SendMessageW(h,WM_COMMAND,IDOK,0);
        });
        if(app.observationMB!=1)throw std::runtime_error("capacity apply");
        ViewerSettings settings;settings.observationMB=42;
        if(ParseViewerSettings(SerializeViewerSettings(settings)).observationMB!=42||!ParseViewerSettings("{\"version\":3}").memoryCache)throw std::runtime_error("settings roundtrip");
        app.layout=old;Layout();
        report<<"ALL PASSED\n";return 0;
    }catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}
}
