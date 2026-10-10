#include "ViewerRuntime.h"
#include "Stage3.h"
#include <tlhelp32.h>
#include <map>
#include <set>
#include <chrono>
#include <regex>
namespace stage3 {
std::filesystem::path LatestLog(const std::filesystem::path& folder){
    if(!fs::is_directory(folder))return {};Time latest=0;fs::path selected;bool duplicate=false;
    static const std::wregex pattern(LR"(^output_log_([0-9]{4})([-_])([0-9]{2})\2([0-9]{2})([-_])([0-9]{2})-([0-9]{2})-([0-9]{2})\.txt$)");
    for(auto& file:fs::directory_iterator(folder)){if(!file.is_regular_file())continue;auto name=file.path().filename().wstring();std::wsmatch m;if(!std::regex_match(name,m,pattern))continue;if((m[2]==L"-"&&m[5]!=L"_")||(m[2]==L"_"&&m[5]!=L"-"))continue;
        auto time=ParseTime(m[1].str()+L"-"+m[3].str()+L"-"+m[4].str()+L" "+m[6].str()+L":"+m[7].str()+L":"+m[8].str());if(time>latest){latest=time;selected=file.path();duplicate=false;}else if(time&&time==latest)duplicate=true;
    }
    if(duplicate)throw std::runtime_error("Latest session log is ambiguous");return selected;
}
namespace {
std::mutex mutex;std::condition_variable wake;std::thread thread;bool stop=false,running=false;std::wstring state=L"停止";std::vector<Item> items;
struct Pending {Item item;std::wstring logPath;Time detected=0,retryAt=0;std::wstring revision,session;int stable=0;};
fs::path testDataRoot;
fs::path Data(const wchar_t* name){return (testDataRoot.empty()?app.settings.parent_path():testDataRoot)/name;}
void AtomicJson(const fs::path& path,const JsonObject& value){auto bytes=winrt::to_string(value.Stringify());auto temp=path;temp+=L".tmp";{std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(bytes.data(),bytes.size());out.flush();if(!out)throw std::runtime_error("Stage3 record write failed");}HANDLE flushed=CreateFileW(temp.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(flushed==INVALID_HANDLE_VALUE)winrt::throw_last_error();auto ok=FlushFileBuffers(flushed);auto code=GetLastError();CloseHandle(flushed);if(!ok)winrt::throw_hresult(HRESULT_FROM_WIN32(code));if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))winrt::throw_last_error();}
JsonObject ReadJson(const fs::path& path){if(!fs::exists(path))return nullptr;if(fs::file_size(path)>8*1024*1024)throw std::runtime_error("Stage3 record limit");std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Stage3 record unreadable");auto object=JsonObject::Parse(winrt::to_hstring(std::string((std::istreambuf_iterator<char>(file)),{})));if(object.GetNamedNumber(L"version")!=1)throw std::runtime_error("Unsupported Stage3 record version");return object;}
Config LoadConfig(){Config c;wchar_t folder[32768]{};auto count=ExpandEnvironmentStringsW(L"%AppData%\\..\\LocalLow\\VRChat\\VRChat",folder,32768);if(count&&count<=32768)c.logFolder=fs::path(folder).lexically_normal().wstring();PWSTR pictures{};if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures,0,nullptr,&pictures))){c.photoFolder=(fs::path(pictures)/L"VRChat").wstring();CoTaskMemFree(pictures);}auto o=ReadJson(Data(L"stage3-settings.json"));if(!o)return c;auto savedLog=o.GetNamedString(L"logFolder",L"");auto savedPhotos=o.GetNamedString(L"photoFolder",L"");if(!savedLog.empty())c.logFolder=savedLog;if(!savedPhotos.empty())c.photoFolder=savedPhotos;c.outputFolder=o.GetNamedString(L"outputFolder",L"");c.selectedLog=o.GetNamedString(L"selectedLog",L"");c.autoRecord=o.GetNamedBoolean(L"autoRecord",false);auto pid=o.GetNamedNumber(L"processId",0);if(!std::isfinite(pid)||pid<0||pid>MAXDWORD||std::floor(pid)!=pid)throw std::runtime_error("Invalid process ID");c.processId=static_cast<DWORD>(pid);return c;}
void SaveConfig(const Config& c){JsonObject o;o.Insert(L"version",JsonValue::CreateNumberValue(1));o.Insert(L"logFolder",JsonValue::CreateStringValue(c.logFolder));o.Insert(L"photoFolder",JsonValue::CreateStringValue(c.photoFolder));o.Insert(L"outputFolder",JsonValue::CreateStringValue(c.outputFolder));o.Insert(L"selectedLog",JsonValue::CreateStringValue(c.selectedLog));o.Insert(L"processId",JsonValue::CreateNumberValue(c.processId));o.Insert(L"autoRecord",JsonValue::CreateBooleanValue(c.autoRecord));AtomicJson(Data(L"stage3-settings.json"),o);}
std::set<std::wstring> Photos(const std::wstring& folder){std::set<std::wstring> found;for(fs::recursive_directory_iterator it(folder),end;it!=end;++it){auto attr=GetFileAttributesW(it->path().c_str());if(attr==INVALID_FILE_ATTRIBUTES)continue;if(attr&FILE_ATTRIBUTE_REPARSE_POINT){if(it->is_directory())it.disable_recursion_pending();continue;}if(!it->is_regular_file())continue;if(it->path().filename().wstring().size()>=3&&_wcsnicmp(it->path().filename().c_str(),L"AB_",3)==0)continue;auto ext=it->path().extension().wstring();if(Same(ext,L".png")||Same(ext,L".jpg")||Same(ext,L".jpeg")||Same(ext,L".webp")){found.insert(it->path().wstring());if(found.size()>20000)throw std::runtime_error("Photo monitoring limit: 20000 files");}}return found;}
std::vector<DWORD> Processes(){std::vector<DWORD> ids;HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(h==INVALID_HANDLE_VALUE)return ids;PROCESSENTRY32W p{sizeof(p)};if(Process32FirstW(h,&p))do{if(_wcsicmp(p.szExeFile,L"VRChat.exe")==0)ids.push_back(p.th32ProcessID);}while(Process32NextW(h,&p));CloseHandle(h);return ids;}
void Journal(const std::map<std::wstring,Pending>& pending){JsonObject root;root.Insert(L"version",JsonValue::CreateNumberValue(1));JsonArray entries;for(auto& pair:pending){auto& p=pair.second;JsonObject e;e.Insert(L"path",JsonValue::CreateStringValue(p.item.path));e.Insert(L"state",JsonValue::CreateStringValue(p.item.state));e.Insert(L"reason",JsonValue::CreateStringValue(p.item.reason));e.Insert(L"output",JsonValue::CreateStringValue(p.item.output));e.Insert(L"session",JsonValue::CreateStringValue(p.session));e.Insert(L"logPath",JsonValue::CreateStringValue(p.logPath));e.Insert(L"retryAt",JsonValue::CreateStringValue(std::to_wstring(p.retryAt)));e.Insert(L"detected",JsonValue::CreateStringValue(std::to_wstring(p.detected)));e.Insert(L"revision",JsonValue::CreateStringValue(p.revision));entries.Append(e);}root.Insert(L"items",entries);AtomicJson(Data(L"stage3-journal.json"),root);}
std::map<std::wstring,Pending> Restore(){std::map<std::wstring,Pending> restored;auto root=ReadJson(Data(L"stage3-journal.json"));if(!root)return restored;auto array=root.GetNamedArray(L"items");if(array.Size()>20000)throw std::runtime_error("Journal item limit");for(auto v:array){auto o=v.GetObject();Pending p;p.item.path=o.GetNamedString(L"path");p.item.state=o.GetNamedString(L"state");p.item.reason=o.GetNamedString(L"reason");p.item.output=o.GetNamedString(L"output");p.session=o.GetNamedString(L"session");p.logPath=o.GetNamedString(L"logPath",L"");p.detected=std::stoll(o.GetNamedString(L"detected").c_str());p.retryAt=std::stoll(o.GetNamedString(L"retryAt",o.GetNamedString(L"detected")).c_str());p.revision=o.GetNamedString(L"revision");restored.emplace(p.item.path,std::move(p));}return restored;}
void Publish(const std::wstring& text,const std::map<std::wstring,Pending>& pending){std::lock_guard<std::mutex> lock(mutex);state=text;items.clear();for(auto& pair:pending)items.push_back(pair.second.item);if(app.window)PostMessageW(app.window,WM_APP+3,0,0);}
void Monitor(Config config,bool synthetic=false){
    auto initialized=RoInitialize(RO_INIT_MULTITHREADED);std::map<std::wstring,Pending> pending;
    try{winrt::check_hresult(initialized);pending=Restore();auto baseline=Photos(config.photoFolder);std::map<std::wstring,LogMonitor> logs;std::wstring selected;DWORD process=0;bool clockInvalid=false;Time previousWall=Now();ULONGLONG previousMono=GetTickCount64();
        for(;;){ {std::lock_guard<std::mutex> lock(mutex);if(stop)break;}
            Time now=Now();auto monotonic=GetTickCount64();if(std::abs((now-previousWall)-static_cast<Time>(monotonic-previousMono))>2000){clockInvalid=true;for(auto& entry:logs)entry.second.timeline.Invalidate();for(auto& pair:pending)if(pair.second.item.state==L"待機"){pair.second.item.state=L"保留";pair.second.item.reason=L"システム時刻変更：時刻対応を無効化";}Journal(pending);}previousWall=now;previousMono=monotonic;
            auto processes=synthetic?std::vector<DWORD>{123}:Processes();if(config.processId)processes.erase(std::remove_if(processes.begin(),processes.end(),[&](auto id){return id!=config.processId;}),processes.end());bool observing=false;std::wstring message;auto allProcesses=Processes();std::wstring pids;for(auto id:allProcesses)pids+=std::to_wstring(id)+L" ";
            if(processes.empty()){message=L"VRChat起動待ち（取得済み写真は処理可能）";}
            else if(processes.size()!=1){message=L"複数VRChatセッション：自動選択できません。対象PIDを指定しログを選び直してください";}
            else{process=processes[0];observing=true;message=L"ログ・写真監視中";}
            try{
                auto latest=synthetic&&!config.selectedLog.empty()?fs::path(config.selectedLog):LatestLog(config.logFolder);
                if(latest.empty()){observing=false;message=L"最新セッションログ待ち";}
                else {
                    if(selected!=latest.wstring()){selected=latest.wstring();logs.try_emplace(selected);}
                    logs[selected].Read(selected);
                    // Keep old session timelines while their photos await delayed events.
                    for(auto& pair:pending){auto& p=pair.second;if(p.item.state==L"待機"&&!p.logPath.empty()&&p.logPath!=selected){try{logs[p.logPath].Read(p.logPath);}catch(...){p.item.reason=L"元セッションログを読取りできません";}}}
                    if(!synthetic&&process){HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process);FILETIME created{},exit{},kernel{},user{};bool valid=h&&GetProcessTimes(h,&created,&exit,&kernel,&user);if(h)CloseHandle(h);WIN32_FILE_ATTRIBUTE_DATA info{};if(!valid||!GetFileAttributesExW(selected.c_str(),GetFileExInfoStandard,&info)||CompareFileTime(&info.ftCreationTime,&created)<0){observing=false;message=L"最新ログとVRChatプロセスの対応待ち／不明";}}
                    if(!synthetic&&allProcesses.size()>1){observing=false;message=L"複数VRChat：最新ログと対象PIDの対応不明のため保留";}
                }
            }catch(const std::exception& e){message=L"ログ未取得・無効："+std::wstring(winrt::to_hstring(e.what()).c_str());observing=false;}catch(...){message=L"ログ未取得・無効";observing=false;}
            if(clockInvalid){observing=false;message=L"時刻対応無効：監視を停止・再開始して再同期してください";}
            if(observing){auto current=Photos(config.photoFolder);for(auto& path:current)if(baseline.insert(path).second){Pending p;p.item.path=path;p.item.state=L"待機";p.item.reason=L"書込み安定・遅延ログ待ち";p.detected=now;p.retryAt=now;p.logPath=selected;p.session=logs[selected].Session();pending.emplace(path,std::move(p));}Journal(pending);}
            bool changed=false;
            for(auto& pair:pending){auto& p=pair.second;if(p.item.state!=L"待機")continue;
                try{auto relative=fs::path(p.item.path).lexically_relative(fs::path(config.photoFolder));if(relative.empty()||*relative.begin()==L".."){p.item.state=L"保留";p.item.reason=L"監視入力外の復旧項目";changed=true;continue;}
                    if(p.logPath.empty()||!SameFolderAddress(fs::path(p.logPath).parent_path().wstring(),config.logFolder)){p.item.state=L"保留";p.item.reason=L"元ログ不明／設定外の復旧項目";changed=true;continue;}
                    auto& log=logs[p.logPath];if(p.session!=log.Session()){p.item.state=L"保留";p.item.reason=L"セッション変更：一覧を転用しません";changed=true;continue;}
                    auto key=ThumbnailCache::Key(p.item.path);if(key!=p.revision){if(p.stable>=2){p.item.state=L"保留";p.item.reason=L"安定確認後に外部変更を検出";changed=true;continue;}p.revision=key;p.stable=0;}else ++p.stable;
                    if(now-p.retryAt>config.retrySeconds*1000LL){p.item.state=L"保留";p.item.reason=L"書込み・ログ・時刻照合の期限超過："+p.item.reason;changed=true;continue;}
                    if(p.stable<2||now-p.detected<config.graceSeconds*1000LL)continue;
                    auto extension=fs::path(p.item.path).extension().wstring();if(Same(extension,L".webp")){p.item.state=L"対象外";p.item.reason=L"Stage3A自動保存はPNG/JPEGのみ";changed=true;continue;}
                    if(!config.autoRecord){p.item.state=L"保留";p.item.reason=L"メタデータ自動記録OFF";changed=true;continue;}
                    auto editable=InspectEditable(p.item.path);auto decision=Correlate(p.item.path,editable.values[0],editable.values[3],p.detected,log);if(!decision.ready){p.item.reason=decision.reason;continue;}
                    if(key!=ThumbnailCache::Key(p.item.path))continue;
                    {std::lock_guard<std::mutex> lock(mutex);if(stop)break;}
                    p.item.output=p.item.path;
                    // Persist intent before the existing verified backup-and-replace transaction.
                    p.item.reason=L"バックアップ作成・上書き保存待ち";Journal(pending);
                    MetadataEdit edit;edit.stage3Xml=RecordXml(decision.record);
                    auto warning=SaveMetadataOverwrite(p.item.path,editable.fingerprint,edit,true,config.backupLimit,Data(L"backups.json"));
                    p.item.state=L"完了";p.item.reason=L"AB_バックアップを作成し、撮影画像へ参加者情報を保存（被写体確定ではありません）"+warning;changed=true;
                }catch(const std::exception& e){p.item.reason=winrt::to_hstring(e.what()).c_str();if(now-p.retryAt>config.retrySeconds*1000LL){p.item.state=L"失敗";changed=true;}}
                catch(const winrt::hresult_error& e){p.item.reason=ErrorText(e.code());if(now-p.retryAt>config.retrySeconds*1000LL){p.item.state=L"失敗";changed=true;}}
            }
            for(auto it=logs.begin();it!=logs.end();){bool needed=it->first==selected||std::any_of(pending.begin(),pending.end(),[&](const auto& pair){return pair.second.item.state==L"待機"&&pair.second.logPath==it->first;});if(!needed)it=logs.erase(it);else ++it;}
            if(changed)Journal(pending);if(!selected.empty()&&logs[selected].invalid)message+=L"（ログ部分取得・形式不明あり）";Publish(message+(pids.empty()?L"":L" | VRChat PID: "+pids),pending);
            std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::seconds(1),[]{return stop;});if(stop)break;
        }
    }catch(const std::exception& e){Publish(L"監視失敗："+std::wstring(winrt::to_hstring(e.what()).c_str()),pending);}catch(...){Publish(L"監視失敗：設定・記録・アクセス権を確認してください",pending);}
    {std::lock_guard<std::mutex> lock(mutex);running=false;}if(SUCCEEDED(initialized))RoUninitialize();
}
void Start(const Config& c){Shutdown();if(c.logFolder.empty()||!fs::is_directory(c.photoFolder))throw std::runtime_error("Select log path and an existing photo folder");SaveConfig(c);{std::lock_guard<std::mutex> lock(mutex);stop=false;running=true;state=L"記録開始：基準一覧とログを読込み中";}auto activeConfig=c;activeConfig.autoRecord=true;activeConfig.backupLimit=app.backupLimit;thread=std::thread(Monitor,activeConfig,false);}
struct DialogState {Config config;HWND edits[5]{},record{},status{},list{},toggle{};};
std::wstring Choose(HWND owner,bool file){ComPtr<IFileOpenDialog> dialog;winrt::check_hresult(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|(file?FOS_FILEMUSTEXIST:FOS_PICKFOLDERS));if(FAILED(dialog->Show(owner)))return {};ComPtr<IShellItem> item;dialog->GetResult(&item);PWSTR p{};winrt::check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH,&p));std::wstring path(p);CoTaskMemFree(p);return path;}
INT_PTR CALLBACK Dialog(HWND h,UINT message,WPARAM wp,LPARAM lp){auto d=reinterpret_cast<DialogState*>(GetWindowLongPtrW(h,DWLP_USER));
    if(message==WM_INITDIALOG){d=reinterpret_cast<DialogState*>(lp);SetWindowLongPtrW(h,DWLP_USER,lp);SetWindowTextW(h,L"人物記録設定");auto control=[&](const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int height,int id){auto c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,height,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return c;};
        const wchar_t* labels[]{L"ログフォルダー",L"対象セッションのログ",L"写真フォルダー（再帰）",L"出力フォルダー"};std::wstring values[]{d->config.logFolder,d->config.selectedLog,d->config.photoFolder,d->config.outputFolder};
        for(int i=0;i<3;++i){control(L"STATIC",labels[i],0,10,10+34*i,158,24,0);d->edits[i]=control(L"EDIT",values[i].c_str(),WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL|(i==1?ES_READONLY:0),170,8+34*i,430,26,100+i);if(i!=1)control(L"BUTTON",L"選択…",WS_TABSTOP,610,8+34*i,76,26,110+i);}
        control(L"STATIC",L"撮影画像に上書き／AB_バックアップ必須（既存の上限設定を使用）",0,10,112,675,24,0);
        control(L"STATIC",L"対象VRChat PID（0=単一起動）",0,10,145,270,24,0);d->edits[4]=control(L"EDIT",std::to_wstring(d->config.processId).c_str(),WS_BORDER|ES_NUMBER|WS_TABSTOP,290,142,120,26,104);
        control(L"STATIC",L"記録中は写真共有時に参加者情報も含まれます。対象ログは常に最新を参照します。",0,10,180,675,28,0);
        d->toggle=control(L"BUTTON",L"記録開始／停止",WS_TABSTOP,10,220,130,28,121);control(L"BUTTON",L"保留・失敗を再試行",WS_TABSTOP,150,220,160,28,122);control(L"BUTTON",L"設定を保存",WS_TABSTOP,330,220,130,28,124);control(L"BUTTON",L"閉じる",WS_TABSTOP,580,220,105,28,IDCANCEL);
        d->status=control(L"STATIC",L"停止",0,10,257,675,42,0);d->list=control(L"EDIT",L"",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY,10,307,675,245,0);SetTimer(h,1,500,nullptr);return TRUE;
    }
    if(!d)return FALSE;
    if(message==WM_TIMER){std::lock_guard<std::mutex> lock(mutex);SetWindowTextW(d->status,state.c_str());for(auto c:d->edits)if(c)EnableWindow(c,!running);for(int id:{110,112,122,124})EnableWindow(GetDlgItem(h,id),!running);SetWindowTextW(d->toggle,running?L"記録停止":L"記録開始");try{auto latest=LatestLog(d->config.logFolder);SetWindowTextW(d->edits[1],latest.empty()?L"ログ未検出":latest.c_str());}catch(...){SetWindowTextW(d->edits[1],L"最新ログを一意に選択できません");}std::wstring rows;for(auto& i:items)rows+=i.state+L" | "+i.path+L"\r\n"+i.reason+(i.output.empty()?L"":L"\r\n出力："+i.output)+L"\r\n\r\n";SetWindowTextW(d->list,rows.c_str());return TRUE;}
    if(message==WM_COMMAND){auto id=LOWORD(wp);try{
        if((id==110||id==112)&&!Recording()){auto path=Choose(h,id==111);if(!path.empty()){SetWindowTextW(d->edits[id-110],path.c_str());if(id==111)SetWindowTextW(d->edits[0],fs::path(path).parent_path().c_str());}return TRUE;}
        if(id==121||id==122||id==124){bool active;{std::lock_guard<std::mutex> lock(mutex);active=running;}if(active&&id==121){Shutdown();return TRUE;}if(active&&id==124)return TRUE;Shutdown();
            d->config.processId=std::stoul(Text(d->edits[4]));d->config.logFolder=Text(d->edits[0]);d->config.photoFolder=Text(d->edits[2]);d->config.autoRecord=true;if(id==124){SaveConfig(d->config);return TRUE;}
            if(id==122){auto pending=Restore();for(auto& pair:pending){auto& p=pair.second;if(p.item.state==L"保留"||p.item.state==L"失敗"){p.item.state=L"待機";p.retryAt=Now();p.stable=0;}}Journal(pending);}Start(d->config);return TRUE;}
        if(id==IDCANCEL){EndDialog(h,IDCANCEL);return TRUE;}
    }catch(const std::exception& e){MessageBoxW(h,winrt::to_hstring(e.what()).c_str(),L"Stage3 設定・監視エラー",MB_OK|MB_ICONERROR);}catch(...){MessageBoxW(h,L"設定・記録を保全しました。パスと権限を確認してください。",L"Stage3 エラー",MB_OK|MB_ICONERROR);}return TRUE;}
    if(message==WM_CLOSE){EndDialog(h,IDCANCEL);return TRUE;}return FALSE;
}
}
bool Recording(){std::lock_guard<std::mutex> lock(mutex);return running;}
void ToggleRecording(){try{if(Recording()){Shutdown();return;}auto config=LoadConfig();Start(config);}catch(const std::exception& e){MessageBoxW(app.window,winrt::to_hstring(e.what()).c_str(),L"人物記録設定を確認してください",MB_OK|MB_ICONERROR);}catch(...){MessageBoxW(app.window,L"人物記録設定を確認してください。設定と記録は保全しています。",L"人物記録",MB_OK|MB_ICONERROR);}}
std::wstring MonitorStatus(){std::lock_guard<std::mutex> lock(mutex);return running||state.find(L"失敗")!=std::wstring::npos?state:L"";}
void Shutdown(){ {std::lock_guard<std::mutex> lock(mutex);stop=true;}wake.notify_all();if(thread.joinable())thread.join();std::lock_guard<std::mutex> lock(mutex);running=false;if(state.find(L"失敗")==std::wstring::npos)state=L"停止";}
void RunMonitorTests(const fs::path& directory,std::ostream& report){
    auto require=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: "<<label<<'\n';};
    Shutdown();auto folder=directory/L"monitor";auto input=folder/L"input",output=folder/L"output";fs::create_directories(input);fs::create_directories(output);testDataRoot=folder;
    struct Cleanup{~Cleanup(){Shutdown();testDataRoot.clear();}}cleanup;
    auto logPath=folder/L"output_log_2026-10-10_12-00-00.txt";auto switchedLog=folder/L"output_log_2026_10_10-12-01-00.txt";fs::remove(switchedLog);{std::ofstream file(logPath,std::ios::trunc);}
    Config config;config.logFolder=folder.wstring();config.photoFolder=input.wstring();config.outputFolder=output.wstring();config.selectedLog.clear();config.autoRecord=true;config.graceSeconds=1;config.retrySeconds=15;
    auto ignored=input/L"existing.png";fs::copy_file(directory.parent_path()/L"xmp-日本語.png",ignored,fs::copy_options::overwrite_existing);
    auto switchedPhoto=input/L"sub"/L"new-session.png";fs::remove(switchedPhoto);
    auto fresh=input/L"sub"/L"fresh.png",destination=fresh;fs::create_directories(fresh.parent_path());fs::remove(fresh);fs::remove(folder/L"stage3-journal.json");
    {std::lock_guard<std::mutex> lock(mutex);stop=false;running=true;state=L"初期化";}thread=std::thread(Monitor,config,true);
    auto wait=[&](auto complete){auto deadline=GetTickCount64()+20000;while(!complete()){if(GetTickCount64()>deadline)throw std::runtime_error("Stage3 monitor timeout");std::this_thread::sleep_for(std::chrono::milliseconds(20));}};
    wait([]{std::lock_guard<std::mutex> lock(mutex);return state.find(L"ログ・写真監視中")!=std::wstring::npos;});
    auto lineTime=[](Time t){ULARGE_INTEGER u{};u.QuadPart=t*10000;FILETIME f{u.LowPart,u.HighPart};SYSTEMTIME utc{},s{};FileTimeToSystemTime(&f,&utc);SystemTimeToTzSpecificLocalTime(nullptr,&utc,&s);wchar_t text[32]{};swprintf_s(text,L"%04u.%02u.%02u %02u:%02u:%02u",s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond);return std::wstring(text);};
    auto now=Now();{std::ofstream file(logPath,std::ios::app);file<<winrt::to_string(lineTime(now-10000)+L" Log - [Behaviour] Joining wrld_aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa:1\n"+lineTime(now-9000)+L" Log - [Behaviour] OnPlayerJoined A (usr_11111111-1111-1111-1111-111111111111)\n"+lineTime(now)+L" Log - [VRC Camera] Took screenshot to: "+fresh.wstring()+L"\n");}
    fs::copy_file(directory.parent_path()/L"sample.jpg",fresh,fs::copy_options::overwrite_existing);
    // Use a genuine PNG with no capture date, to exercise photo-log fallback.
    fs::copy_file(directory.parent_path()/L"writer-alpha.png",fresh,fs::copy_options::overwrite_existing);
    auto originalFingerprint=MetadataFingerprint(fresh.wstring());
    wait([&]{std::lock_guard<std::mutex> lock(mutex);return std::any_of(items.begin(),items.end(),[&](auto& item){return item.path==fresh.wstring()&&item.state==L"待機";});});
    {std::ofstream file(switchedLog);file<<winrt::to_string(lineTime(now-10000)+L" Log - [Behaviour] Joining wrld_aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa:2\n"+lineTime(now-9000)+L" Log - [Behaviour] OnPlayerJoined B (usr_22222222-2222-2222-2222-222222222222)\n"+lineTime(now)+L" Log - [VRC Camera] Took screenshot to: "+switchedPhoto.wstring()+L"\n");}
    fs::copy_file(directory.parent_path()/L"writer-alpha.png",switchedPhoto,fs::copy_options::overwrite_existing);
    wait([&]{std::lock_guard<std::mutex> lock(mutex);return std::any_of(items.begin(),items.end(),[&](auto& item){return item.path==fresh.wstring()&&item.state==L"完了";});});
    wait([&]{std::lock_guard<std::mutex> lock(mutex);return std::any_of(items.begin(),items.end(),[&](auto& item){return item.path==switchedPhoto.wstring()&&item.state==L"完了";});});
    Shutdown();require(ReadPhotoSummary(fresh.wstring()).values[9]==L"A"&&ReadPhotoSummary(switchedPhoto.wstring()).values[9]==L"B","v0.4.1 log switch preserves pending old session and records new session separately");
    require(fs::exists(destination)&&ReadPhotoSummary(ignored.wstring()).xmpDetail.find(L"InstanceMembers")==std::wstring::npos,"Stage3 monitor overwrites recursive addition and excludes initial baseline");
    auto backups=ReadBackupLedger(Data(L"backups.json"));require(std::any_of(backups.begin(),backups.end(),[&](const auto& backup){return backup.source==fs::absolute(fresh).wstring()&&MetadataFingerprint(backup.path)==originalFingerprint;}),"Stage3 mandatory AB backup preserves exact original");
    require(Photos(input.wstring()).size()==3,"Stage3 excludes AB backups from photo detection");
    auto restored=Restore();require(restored.size()==2&&std::all_of(restored.begin(),restored.end(),[](const auto& pair){return pair.second.item.state==L"完了";}),"Stage3 journal restores completed file without duplicate processing");
    require(ReadPhotoSummary(destination.wstring()).xmpDetail.find(L"InstanceMembers")!=std::wstring::npos,"Stage3 monitor output contains structured participant record");
}
void ShowMonitorDialog(){try{DialogState d;d.config=LoadConfig();struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;}t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER,0,0,0,0,470,390},0,0,0};DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,app.window,Dialog,reinterpret_cast<LPARAM>(&d));}catch(...){MessageBoxW(app.window,L"Stage3設定が読めません。元ファイルを保全し、監視は開始しません。",L"Stage3",MB_OK|MB_ICONERROR);}}
}
