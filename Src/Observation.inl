#include <richedit.h>
#include <future>
#include <deque>
#include <fstream>
#include <regex>
namespace {
constexpr int ObservationMode=2701;
HWND observationText{},observationMode{},observationStatus{},observationWrapButton{};
std::wstring observationDisplay;
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
    auto file=store.loaded.empty()?directory/L"events.txt":store.loaded;auto temp=file;temp+=L".tmp";
    ObservationSafe(file);ObservationSafe(temp);
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        for(auto& e:store.events)out<<e<<"\r\n";
        out.flush();if(!out)throw std::runtime_error("Observation write failed");
    }
    if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Observation replace failed");
    store.dirty=false;
}
ObservationResult ReadObservation(fs::path directory,fs::path logs,size_t limit,bool reset,bool enabled,fs::path storageFile={}){
    ObservationResult result;
    try{
        auto& store=observationStore;
        ObservationSafe(directory.parent_path());ObservationSafe(directory);
        auto file=storageFile.empty()?directory/L"events.txt":storageFile;ObservationSafe(file);
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
        size_t displayed=0;
        for(auto i=store.events.rbegin();i!=store.events.rend()&&displayed<2000;++i,++displayed){
            auto row=std::wstring(winrt::to_hstring(*i));
            if(row.size()<21)continue;
            auto separator=row.find(L'-',14);
            if(separator==std::wstring::npos)continue;
            result.text+=row.substr(7,2)+L":"+row.substr(9,2)+L":"+row.substr(11,2)+L" "+row.substr(14,separator-14)+L" "+row.substr(separator+1)+L"\r\n";
            if(result.text.size()>500000)break;
        }
    }catch(const std::exception& e){result.error=std::wstring(winrt::to_hstring(e.what()));}
    return result;
}

COLORREF ObservationTagColor(const std::wstring& tag){
    auto& colors=app.observationColors[app.darkTheme?1:0];
    if(tag==L"join")return colors[5];
    if(tag==L"leave")return colors[6];
    if(tag==L"world")return colors[7];
    if(tag==L"movie")return colors[8];
    return colors[9];
}
void ObservationColor(LONG start,LONG end,COLORREF color){
    CHARRANGE range{start,end};
    SendMessageW(observationText,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
    CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR;format.crTextColor=color;
    SendMessageW(observationText,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
}
void FormatObservation(const std::wstring& text){
    auto& colors=app.observationColors[app.darkTheme?1:0];
    ObservationColor(0,LONG(text.size()),colors[2]);
    auto dc=GetDC(observationText);
    int dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(observationText,dc);
    RECT client{};GetClientRect(observationText,&client);
    static const std::wregex urls(LR"(https?://[^\s<>""]+)",std::regex::icase);
    static const std::wregex ids(LR"(usr_[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");
    for(size_t start=0;start<text.size();){
        auto end=text.find(wchar_t(13),start);if(end==std::wstring::npos)end=text.size();
        auto tagEnd=text.find(L' ',start+9);
        if(tagEnd<end&&tagEnd>=start+9){
            ObservationColor(LONG(start),LONG(start+8),colors[1]);
            auto tag=text.substr(start+9,tagEnd-start-9);
            ObservationColor(LONG(start+9),LONG(tagEnd),ObservationTagColor(tag));
            auto content=text.substr(tagEnd+1,end-tagEnd-1);
            for(std::wsregex_iterator it(content.begin(),content.end(),ids),last;it!=last;++it)
                ObservationColor(LONG(tagEnd+1+it->position()),LONG(tagEnd+1+it->position()+it->length()),colors[4]);
            // Apply URLs last, so URL coloring also covers embedded user IDs.
            for(std::wsregex_iterator it(content.begin(),content.end(),urls),last;it!=last;++it)
                ObservationColor(LONG(tagEnd+1+it->position()),LONG(tagEnd+1+it->position()+it->length()),colors[3]);
            CHARRANGE paragraph{LONG(start),LONG(end)};
            SendMessageW(observationText,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&paragraph));
            PARAFORMAT2 format{};format.cbSize=sizeof(format);
            format.dwMask=PFM_STARTINDENT|PFM_OFFSET;format.dxStartIndent=0;
            int indent=std::min<int>(UiTextWidth(text.substr(start,tagEnd-start+1)),std::max(0L,client.right/2));
            format.dxOffset=app.observationWrap?MulDiv(indent,1440,dpi):0;
            SendMessageW(observationText,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&format));
        }
        start=end+1;
    }
}
void RenderObservation(const std::wstring& display,bool latest){
    POINT point{};LONG anchor=LONG(SendMessageW(observationText,EM_CHARFROMPOS,0,reinterpret_cast<LPARAM>(&point)));
    CHARRANGE selected{};SendMessageW(observationText,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
    observationDisplay=display;
    auto text=display;
    for(size_t p=0;(p=text.find(L"\r\n",p))!=std::wstring::npos;)text.replace(p,2,L"\r");
    SendMessageW(observationText,WM_SETREDRAW,FALSE,0);
    SETTEXTEX settings{ST_DEFAULT,1200};
    SendMessageW(observationText,EM_SETTEXTEX,reinterpret_cast<WPARAM>(&settings),reinterpret_cast<LPARAM>(text.c_str()));
    FormatObservation(text);
    CHARRANGE target{latest?0:std::min(anchor,LONG(text.size())),latest?0:std::min(anchor,LONG(text.size()))};
    SendMessageW(observationText,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&target));
    SendMessageW(observationText,EM_SCROLLCARET,0,0);
    if(!latest){
        auto line=SendMessageW(observationText,EM_EXLINEFROMCHAR,0,target.cpMin);
        auto first=SendMessageW(observationText,EM_GETFIRSTVISIBLELINE,0,0);
        SendMessageW(observationText,EM_LINESCROLL,0,line-first);
        SendMessageW(observationText,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    }
    SendMessageW(observationText,WM_SETREDRAW,TRUE,0);InvalidateRect(observationText,nullptr,TRUE);
}
void UpdateObservationWrap(){
    CHARRANGE selected{};SendMessageW(observationText,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&selected));
    POINT point{};LONG anchor=LONG(SendMessageW(observationText,EM_CHARFROMPOS,0,reinterpret_cast<LPARAM>(&point)));
    SendMessageW(observationText,EM_SETTARGETDEVICE,0,app.observationWrap?0:1);
    ShowScrollBar(observationText,SB_HORZ,app.observationWrap?FALSE:TRUE);
    SetWindowTextW(observationWrapButton,app.observationWrap?L"折り返し：ON":L"折り返し：OFF");
    SendMessageW(observationWrapButton,BM_SETCHECK,app.observationWrap?BST_CHECKED:BST_UNCHECKED,0);
    auto text=observationDisplay;
    for(size_t p=0;(p=text.find(L"\r\n",p))!=std::wstring::npos;)text.replace(p,2,L"\r");
    SendMessageW(observationText,WM_SETREDRAW,FALSE,0);
    FormatObservation(text);
    auto line=SendMessageW(observationText,EM_EXLINEFROMCHAR,0,anchor);
    auto first=SendMessageW(observationText,EM_GETFIRSTVISIBLELINE,0,0);
    SendMessageW(observationText,EM_LINESCROLL,0,line-first);
    SendMessageW(observationText,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    SendMessageW(observationText,WM_SETREDRAW,TRUE,0);InvalidateRect(observationText,nullptr,TRUE);
}
}
void CreateObservation(){
    TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(L"観測");
    TabCtrl_InsertItem(app.metadataTabs,TabCtrl_GetItemCount(app.metadataTabs),&tab);
    observationMode=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,140,180,app.side,reinterpret_cast<HMENU>(INT_PTR(ObservationMode)),GetModuleHandleW(nullptr),nullptr);
    for(auto label:{L"オフ",L"標準（10秒）",L"高（2秒）"})SendMessageW(observationMode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendMessageW(observationMode,CB_SETCURSEL,0,0);
    observationText=CreateWindowExW(WS_EX_CLIENTEDGE,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,0,0,1,1,app.side,reinterpret_cast<HMENU>(INT_PTR(2702)),GetModuleHandleW(nullptr),nullptr);
    observationStatus=CreateWindowExW(0,L"STATIC",L"停止",WS_CHILD|WS_VISIBLE,0,0,1,1,app.side,nullptr,GetModuleHandleW(nullptr),nullptr);
    observationWrapButton=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX|BS_PUSHLIKE,0,0,1,1,app.side,reinterpret_cast<HMENU>(INT_PTR(2703)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(observationText,EM_EXLIMITTEXT,0,600000);
    SendMessageW(observationText,EM_SETBKGNDCOLOR,0,RGB(255,255,255));
    UpdateObservationWrap();
    for(auto h:{observationMode,observationWrapButton,observationText,observationStatus})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),TRUE);
}
void LayoutObservation(RECT page,bool visible){
    int header=UiControlHeight();
    MoveWindow(observationMode,page.left,page.top,std::max(45L,std::min(170L,(page.right-page.left)/2)),200,FALSE);
    int modeWidth=std::max(45L,std::min(170L,(page.right-page.left)/2));
    MoveWindow(observationWrapButton,page.left+modeWidth+4,page.top,std::max(40L,page.right-page.left-modeWidth-4),header,FALSE);
    MoveWindow(observationStatus,page.left,page.top+header+3,std::max(1L,page.right-page.left),header,FALSE);
    MoveWindow(observationText,page.left,page.top+header*2+6,std::max(1L,page.right-page.left),std::max(1L,page.bottom-page.top-header*2-6),FALSE);
    static LONG formattedWidth=0;
    if(formattedWidth!=page.right-page.left){formattedWidth=page.right-page.left;UpdateObservationWrap();}
    for(auto h:{observationMode,observationWrapButton,observationText,observationStatus}){ShowWindow(h,visible?SW_SHOW:SW_HIDE);if(visible)SetWindowPos(h,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
}
bool ObservationCommand(WPARAM command){
    if(LOWORD(command)!=2703||HIWORD(command)!=BN_CLICKED)return false;
    app.observationWrap=SendMessageW(observationWrapButton,BM_GETCHECK,0,0)==BST_CHECKED;
    UpdateObservationWrap();SaveSettings();return true;
}
void RefreshObservationAppearance(){
    if(!observationText)return;
    SendMessageW(observationText,EM_SETBKGNDCOLOR,0,app.darkTheme?RGB(0,0,0):RGB(255,255,255));
    RenderObservation(observationDisplay,false);
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
        if(observationDisplay!=result.text)RenderObservation(result.text,true);
    }
    static int previousCapacity=-1;
    if(!mode&&previousCapacity==app.observationMB)return;
    if(GetTickCount64()<observationNext&&previousCapacity==app.observationMB)return;
    try{
        auto logs=mode?stage3::ObservationLogFolder():std::wstring{};
        auto storageFile=ObservationDataFile(app.settings.parent_path());
        auto directory=storageFile.parent_path();
        bool reset=observationReset;observationReset=false;previousCapacity=app.observationMB;
        observationNext=GetTickCount64()+(mode==2?2000:10000);
        auto limit=size_t(app.observationMB)*1000000;
        observationFuture=std::async(std::launch::async,[directory,logs,limit,reset,mode,storageFile]{return ReadObservation(directory,logs,limit,reset,mode!=0,storageFile);});
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
        auto migration=directory/(L"migration-"+std::to_wstring(GetTickCount64()));
        fs::create_directories(migration/L"MetadataDB");
        {std::ofstream out(migration/L"MetadataDB"/L"fixture.txt");out<<"preserve";}
        auto moved=RuntimeDataDirectory(migration,L"MetadataDB");
        if(moved!=migration/L"Data"/L"MetadataDB"||!fs::exists(moved/L"fixture.txt")||fs::exists(migration/L"MetadataDB"))throw std::runtime_error("directory migration");
        fs::create_directories(migration/L"Observation");
        {std::ofstream out(migration/L"Observation"/L"events.txt");out<<"event";}
        auto eventFile=ObservationDataFile(migration);
        if(eventFile!=migration/L"Data"/L"observation-events.txt"||fs::file_size(eventFile)!=5)throw std::runtime_error("event migration");
        auto conflict=migration/L"conflict";fs::create_directories(conflict/L"MetadataDB");fs::create_directories(conflict/L"Data"/L"MetadataDB");
        if(RuntimeDataDirectory(conflict,L"MetadataDB")!=conflict/L"MetadataDB"||!fs::exists(conflict/L"Data"/L"MetadataDB"))throw std::runtime_error("migration conflict preservation");
        report<<"ALL PASSED\n";return 0;
    }catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}
}

void CaptureV0412Ui(const fs::path&,HWND);
int ObservationUiTest(){
    std::ofstream report(app.settings.parent_path()/L"observation-ui-result.txt");
    try{
        if(TabCtrl_GetItemCount(app.metadataTabs)!=4||SendMessageW(observationMode,CB_GETCURSEL,0,0)!=0)throw std::runtime_error("initial observation OFF tab");
        auto old=app.layout;app.layout=L"right";
        const std::wstring user=L"usr_12345678-abcd-1234-abcd-123456789abc";
        auto sample=L"02:11:38 movie https://example.com/"+user+L"?long=abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"+L"\r\n"+
            L"02:11:37 join Name&"+user+L"\r\n"+
            L"02:11:36 leave Name&"+user+L"\r\n"+
            L"02:11:35 world World&wrld_example\r\n"+
            L"02:11:34 other Other\r\n";
        RenderObservation(sample,true);
        auto displayed=sample;displayed.erase(std::remove(displayed.begin(),displayed.end(),wchar_t(10)),displayed.end());
        auto colorAt=[&](const std::wstring& value,COLORREF expected,size_t from=0){
            auto index=displayed.find(value,from);
            if(index==std::wstring::npos)throw std::runtime_error("colored span missing");
            CHARRANGE range{LONG(index),LONG(index+1)};
            SendMessageW(observationText,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));
            CHARFORMAT2W format{};format.cbSize=sizeof(format);
            SendMessageW(observationText,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
            if(format.crTextColor!=expected)throw std::runtime_error("span color mismatch: "+winrt::to_string(value)+" actual="+std::to_string(format.crTextColor)+" expected="+std::to_string(expected));
            return index;
        };
        colorAt(L"join",RGB(0,0,205));colorAt(L"leave",RGB(128,0,0));
        colorAt(L"world",RGB(0,100,0));colorAt(L"movie",RGB(0,0,0));colorAt(L"other",RGB(0,0,0));
        colorAt(L"https://",RGB(100,149,237));
        colorAt(user,RGB(100,149,237));
        colorAt(user,RGB(154,205,50),displayed.find(L"join"));
        if(displayed.find(L"02:11:37 join ")==std::wstring::npos)throw std::runtime_error("spaced display format");
        CHARRANGE before{};SendMessageW(observationText,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&before));
        SendMessageW(observationWrapButton,BM_CLICK,0,0);
        if(app.observationWrap)throw std::runtime_error("wrap OFF");
        CHARRANGE after{};SendMessageW(observationText,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
        if(before.cpMin!=after.cpMin||before.cpMax!=after.cpMax)throw std::runtime_error("wrap selection preservation");
        SendMessageW(observationWrapButton,BM_CLICK,0,0);
        if(!app.observationWrap)throw std::runtime_error("wrap ON");
        ViewerSettings wrapSettings;wrapSettings.observationWrap=false;
        if(ParseViewerSettings(SerializeViewerSettings(wrapSettings)).observationWrap)throw std::runtime_error("wrap setting persistence");
        SetWindowPos(app.side,nullptr,0,0,220,400,SWP_NOMOVE|SWP_NOZORDER);
        LayoutSideView();
        if(!(GetWindowLongPtrW(app.sideSelector,GWL_STYLE)&WS_VISIBLE)||SendMessageW(app.sideSelector,CB_GETCOUNT,0,0)!=4)throw std::runtime_error("narrow dropdown");
        SendMessageW(app.sideSelector,CB_SETCURSEL,3,0);
        SendMessageW(app.side,WM_COMMAND,MAKEWPARAM(2690,CBN_SELCHANGE),0);
        if(TabCtrl_GetCurSel(app.metadataTabs)!=3||!(GetWindowLongPtrW(observationText,GWL_STYLE)&WS_VISIBLE))throw std::runtime_error("observation selection");
        if(!(GetWindowLongPtrW(observationWrapButton,GWL_STYLE)&WS_VISIBLE))throw std::runtime_error("wrap toggle visible in observation tab");
        RedrawWindow(app.side,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
        CaptureV0412Ui(app.settings.parent_path()/L"observation-ui.png",app.side);
        app.darkTheme=true;ApplyTheme();
        colorAt(L"movie",RGB(255,255,255));colorAt(L"02:11:38",RGB(255,255,255));colorAt(L"Other",RGB(255,255,255));
        CaptureV0412Ui(app.settings.parent_path()/L"observation-night.png",app.side);
        app.darkTheme=false;ApplyTheme();
        auto originalColors=app.observationColors;
        ShowFontDialog([&](HWND h){
            SetDlgItemTextW(h,2801,L"#GG0000");SendMessageW(h,WM_COMMAND,IDOK,0);
            if(!IsWindow(h)||Text(GetDlgItem(h,2899)).empty())throw std::runtime_error("invalid palette rejected");
            SendMessageW(h,WM_COMMAND,IDCANCEL,0);
        });
        if(app.observationColors!=originalColors)throw std::runtime_error("palette invalid/cancel changed settings");
        ShowFontDialog([&](HWND h){
            SetDlgItemTextW(h,2801,L"#112233");SetDlgItemTextW(h,2803,L"#ABCDEF");
            CaptureV0412Ui(app.settings.parent_path()/L"observation-font-colors.png",h);
            SendMessageW(h,WM_COMMAND,IDOK,0);
        });
        RenderObservation(sample,true);
        colorAt(L"02:11:38",RGB(17,34,51));colorAt(L"https://",RGB(171,205,239));
        ViewerSettings paletteSettings;paletteSettings.observationColors=app.observationColors;
        if(ParseViewerSettings(SerializeViewerSettings(paletteSettings)).observationColors!=app.observationColors)throw std::runtime_error("palette persistence");
        app.observationColors=originalColors;RefreshObservationAppearance();SaveSettings();
        ShowCacheSettings(0,[&](HWND h){
            RECT client{},capacity{};
            GetClientRect(h,&client);GetWindowRect(GetDlgItem(h,2441),&capacity);
            MapWindowPoints(nullptr,h,reinterpret_cast<POINT*>(&capacity),2);
            if(!(GetWindowLongPtrW(GetDlgItem(h,2413),GWL_STYLE)&WS_VISIBLE)||capacity.top<0||capacity.bottom>client.bottom)throw std::runtime_error("observation capacity row visible");
            SendMessageW(h,WM_COMMAND,2413,0);
            if(SendDlgItemMessageW(h,2413,BM_GETCHECK,0,0)!=BST_CHECKED||!(GetWindowLongPtrW(GetDlgItem(h,2443),GWL_STYLE)&WS_VISIBLE))throw std::runtime_error("observation details selected");
            if(GetWindowLongPtrW(GetDlgItem(h,2430),GWL_STYLE)&WS_VISIBLE)throw std::runtime_error("memory details hidden for observation");
            RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
            if(Text(GetDlgItem(h,2423)).find(L" / ")==std::wstring::npos)throw std::runtime_error("current usage / configured capacity");
            CaptureV0412Ui(app.settings.parent_path()/L"observation-cache-settings.png",h);
            if(Text(GetDlgItem(h,2441))!=L"10")throw std::runtime_error("initial capacity");
            RECT minimum{0,0,480+GetSystemMetrics(SM_CXVSCROLL),320};
            AdjustWindowRectEx(&minimum,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));
            SetWindowPos(h,nullptr,0,0,minimum.right-minimum.left,minimum.bottom-minimum.top,SWP_NOMOVE|SWP_NOZORDER);
            SendMessageW(h,WM_VSCROLL,SB_BOTTOM,0);
            RECT footer{},minimumClient{};GetClientRect(h,&minimumClient);GetWindowRect(GetDlgItem(h,IDOK),&footer);MapWindowPoints(nullptr,h,reinterpret_cast<POINT*>(&footer),2);
            if(minimumClient.right<480||minimumClient.bottom<320||footer.top<0||footer.bottom>minimumClient.bottom)throw std::runtime_error("minimum dialog size and fixed footer");
            if(!(GetWindowLongPtrW(GetDlgItem(h,2443),GWL_STYLE)&WS_VISIBLE))throw std::runtime_error("observation details reachable at minimum size");
            RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
            CaptureV0412Ui(app.settings.parent_path()/L"cache-settings-minimum.png",h);
            SetDlgItemTextW(h,2441,L"1");SendMessageW(h,WM_COMMAND,IDOK,0);
        });
        if(app.observationMB!=1)throw std::runtime_error("capacity apply");
        ViewerSettings settings;settings.observationMB=42;
        if(ParseViewerSettings(SerializeViewerSettings(settings)).observationMB!=42||!ParseViewerSettings("{\"version\":3}").memoryCache)throw std::runtime_error("settings roundtrip");
        app.layout=old;Layout();
        report<<"ALL PASSED\n";return 0;
    }catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}
}
