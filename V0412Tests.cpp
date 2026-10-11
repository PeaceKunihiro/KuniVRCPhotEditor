#include "ViewerRuntime.h"
#include "resource.h"
#include <iostream>
void CaptureV0412Ui(const fs::path&,HWND);
void RunV0412Tests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: v0.4.12 "<<label<<'\n';};
    auto old=ParseViewerSettings("{\"version\":3}");check(!old.diskCache&&old.diskCacheMiB==512&&old.diskCacheMinutes==30&&old.diskCacheVisits==5,"legacy disk settings default OFF 512MiB 30min 5visits");
    old.diskCache=true;old.diskCacheMiB=2048;old.diskCacheMinutes=1;old.diskCacheVisits=2;auto restored=ParseViewerSettings(SerializeViewerSettings(old));check(restored.diskCache&&restored.diskCacheMiB==2048&&restored.diskCacheMinutes==1&&restored.diskCacheVisits==2,"disk settings roundtrip");
    for(auto text:{"{\"version\":3,\"diskCacheMiB\":64}","{\"version\":3,\"diskCacheMiB\":129}","{\"version\":3,\"diskCacheMinutes\":0}","{\"version\":3,\"diskCacheVisits\":1}"}){bool rejected=false;try{ParseViewerSettings(text);}catch(...){rejected=true;}check(rejected,"invalid disk settings rejected");}
    auto parent=directory/L"v0412-cache-tests";fs::create_directories(parent);auto root=parent/L"ThumbnailDiskCache",source=parent/L"source.png";
    {std::ofstream out(source,std::ios::binary|std::ios::trunc);out<<"unchanged source";}
    Pixels original{256,192,std::vector<BYTE>(256*192*4,83)},result;
    auto key=ThumbnailCache::Key(source.wstring())+L"|256x192|v1";DiskThumbnailCache cache;
    cache.Configure(root,{false,128ull*1024*1024,30,5});cache.WaitIdle();cache.Clear();cache.WaitIdle();
    cache.Put(key,source.wstring(),original);check(!cache.Get(key,result)&&cache.GetStatus().entries==0,"OFF skips disk reads and writes");
    cache.Configure(root,{true,128ull*1024*1024,30,5});cache.WaitIdle();auto now=DiskThumbnailCache::Clock::now();
    for(int n=0;n<4;++n)cache.Visit(parent.wstring(),now);cache.Put(key,source.wstring(),original);
    check(cache.GetStatus().entries==0&&!cache.Eligible(parent.wstring(),now),"four visits do not persist thumbnails");
    cache.Visit(parent.wstring()+L"\\",now);cache.Put(key,source.wstring(),original);check(cache.Get(key,result)&&result.data==original.data&&result.width==256&&result.height==192,"fifth normalized folder visit stores exact thumbnail");
    check(cache.Eligible(parent.wstring(),now+std::chrono::minutes(30)),"rolling period includes exact boundary");
    check(!cache.Eligible(parent.wstring(),now+std::chrono::minutes(30)+std::chrono::milliseconds(1)),"rolling thirty minute expiry");
    cache.Configure(root,{false,128ull*1024*1024,30,5});cache.WaitIdle();check(!cache.Get(key,result)&&cache.GetStatus().entries==1,"OFF preserves existing files without using them");
    cache.Configure(root,{true,128ull*1024*1024,30,5});cache.WaitIdle();check(cache.Get(key,result)&&!cache.Eligible(parent.wstring()),"persisted data usable before new frequency threshold");
    {DiskThumbnailCache reopened;reopened.Configure(root,{true,128ull*1024*1024,30,5});reopened.WaitIdle();check(reopened.Get(key,result)&&!reopened.Eligible(parent.wstring()),"restart rebuilds index but not visit history");reopened.Stop();}
    check(!cache.Get(key+L"different dimensions",result),"different thumbnail identity misses");
    {std::ofstream out(source,std::ios::app|std::ios::binary);out<<"changed";}check(!cache.Get(ThumbnailCache::Key(source.wstring())+L"|256x192|v1",result),"changed original identity misses");
    auto stored=fs::path();for(auto& e:fs::directory_iterator(root))if(e.path().extension()==L".dtc")stored=e.path();
    {std::fstream out(stored,std::ios::in|std::ios::out|std::ios::binary);out.seekp(-1,std::ios::end);out.put(0);}check(!cache.Get(key,result),"corrupt thumbnail fails checksum");
    for(int n=0;n<5;++n)cache.Visit(parent.wstring());cache.Put(key,source.wstring(),original);check(cache.Get(key,result)&&result.data==original.data,"corrupt entry regenerated");
    fs::remove(stored);check(!cache.Get(key,result),"externally removed disk entry misses");cache.Put(key,source.wstring(),original);check(cache.Get(key,result),"externally removed disk entry regenerated");
    std::vector<std::thread> threads;for(int n=0;n<8;++n)threads.emplace_back([&]{cache.Put(key,source.wstring(),original);Pixels p;cache.Get(key,p);});for(auto& thread:threads)thread.join();check(cache.GetStatus().entries==1,"parallel thumbnail access publishes one entry");
    auto unrelated=root/L"unrelated.txt";{std::ofstream out(unrelated);out<<"keep";}
    auto oversized=root/(L"k12-"+std::wstring(64,L'0')+L".dtc");{std::ofstream out(oversized);out<<"invalid";}fs::resize_file(oversized,129ull*1024*1024);
    {DiskThumbnailCache trimming;trimming.Configure(root,{true,128ull*1024*1024,30,5});trimming.WaitIdle();check(trimming.GetStatus().bytes<=128ull*1024*1024&&!fs::exists(oversized)&&fs::exists(unrelated),"actual byte budget evicts managed entries only");}
    cache.Clear();cache.WaitIdle();check(cache.GetStatus().entries==0&&fs::exists(unrelated)&&fs::file_size(source)==23,"clear leaves original and unrelated file intact");
    // A file occupying the directory path must not break image decoding or be removed.
    auto blocked=parent/L"not-a-directory";{std::ofstream out(blocked);out<<"keep";}DiskThumbnailCache unavailable;unavailable.Configure(blocked,{true,128ull*1024*1024,30,5});unavailable.WaitIdle();for(int n=0;n<5;++n)unavailable.Visit(parent.wstring());unavailable.Put(key,source.wstring(),original);check(!unavailable.Get(key,result)&&fs::file_size(blocked)==4&&!unavailable.GetStatus().error.empty(),"unavailable disk falls back without touching blocker");
    cache.Stop();unavailable.Stop();
}
void RunV0412UiTests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: v0.4.12 "<<label<<'\n';};
    TabCtrl_SetCurSel(app.metadataTabs,0);app.layout=L"right";app.darkTheme=false;Layout();PhotoSummary summary;summary.values[0]=L"test.png";summary.values[2]=L"0";summary.values[4]=L" \t\r\n";ShowSummary(summary);
    auto visible=[](HWND h){return (GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)!=0;};
    check(visible(app.simpleValues[0])&&visible(app.simpleValues[2])&&!visible(app.simpleValues[1])&&!visible(app.simpleLabels[4])&&!visible(GetDlgItem(app.simple,1810)),"empty and whitespace rows plus group button collapse while zero remains");
    RECT first{},second{};GetWindowRect(app.simpleValues[0],&first);GetWindowRect(app.simpleValues[2],&second);check(second.top-first.bottom==8,"collapsed rows leave only normal spacing");
    CaptureV0412Ui(directory/L"v0412-simple-collapsed.png",app.window);
    auto sparseHeight=app.simpleContentHeight;summary.values[1]=L"12 MB";summary.values[10]=L"候補";ShowSummary(summary);check(visible(app.simpleLabels[1])&&visible(app.simpleValues[10])&&app.simpleContentHeight>sparseHeight,"rows reappear after selected metadata changes");
    for(auto placement:{L"left",L"right",L"bottom",L"none"}){app.layout=placement;Layout();check(!visible(app.simpleValues[4]),"empty row stays hidden across side placements");}
    app.layout=L"right";Layout();ShowSummary({},L"画像を選択してください。");check(!visible(app.simpleValues[0])&&visible(app.simpleNote)&&Text(app.simpleNote).find(L"画像を選択")!=std::wstring::npos,"empty selection retains guidance");
    bool inspected=false;ShowVersion([&](HWND h){inspected=true;check(Text(h)==L"KuniVRCPhotEditor のバージョン","version dialog opens");check(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,48,48,LR_SHARED)!=nullptr,"version icon resource available");CaptureV0412Ui(directory/L"v0412-version.png",h);});check(inspected,"version callback inspection delivered");
    ShowDiskCacheDialog([&](HWND h){check(IsDlgButtonChecked(h,2100)==BST_UNCHECKED&&SendDlgItemMessageW(h,2101,CB_GETCOUNT,0,0)==5&&Text(GetDlgItem(h,2102))==L"30"&&Text(GetDlgItem(h,2103))==L"5","disk dialog defaults and supported capacity choices");CaptureV0412Ui(directory/L"v0412-disk-settings.png",h);});
    ShowDiskCacheDialog([&](HWND h){CheckDlgButton(h,2100,BST_CHECKED);SendDlgItemMessageW(h,2101,CB_SETCURSEL,0,0);SetDlgItemTextW(h,2102,L"2");SetDlgItemTextW(h,2103,L"2");SendMessageW(h,WM_COMMAND,IDOK,0);});
    check(app.diskCache&&app.diskCacheMiB==128&&app.diskCacheMinutes==2&&app.diskCacheVisits==2,"disk dialog applies enabled state and numeric settings");
    app.diskCache=false;app.diskCacheMiB=512;app.diskCacheMinutes=30;app.diskCacheVisits=5;ConfigureDiskCache();app.disk.WaitIdle();SaveSettings();
    auto menu=BuildOptionsMenu();wchar_t name[128]{};GetMenuStringW(menu,12,name,128,MF_BYCOMMAND);DestroyMenu(menu);check(std::wstring(name).find(L"ディスクキャッシュ")!=std::wstring::npos,"options exposes disk cache configuration");
    // Real worker integration: five successful loads, persistence and subsequent disk hits.
    app.diskCache=true;app.diskCacheVisits=5;ConfigureDiskCache();app.disk.WaitIdle();app.disk.Clear();app.disk.WaitIdle();
    auto pump=[&](auto complete){auto deadline=GetTickCount64()+15000;while(!complete()){if(GetTickCount64()>deadline)throw std::runtime_error("v0412 async timeout");MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}};
    for(int i=0;i<4;++i){OpenFolder(directory.wstring(),true);pump([]{return !app.folderPending&&!app.files.empty()&&ThumbnailsReady();});}
    OpenFolder(directory.wstring(),true,L"",false);pump([]{return !app.folderPending&&ThumbnailsReady();});check(!app.disk.Eligible(directory.wstring())&&app.disk.GetStatus().entries==0,"internal load is excluded from frequency threshold");
    OpenFolder((directory/L"missing-v0412-folder").wstring());pump([]{return !app.folderPending;});check(!app.disk.Eligible(directory.wstring()),"failed load is excluded from frequency threshold");
    OpenFolder(directory.wstring(),true);pump([]{return !app.folderPending&&ThumbnailsReady();});
    check(app.disk.Eligible(directory.wstring())&&app.disk.GetStatus().entries>0,"fifth successful UI load persists requested thumbnails");
    auto count=app.disk.GetStatus().entries;OpenFolder(directory.wstring(),true,L"",false);pump([]{return !app.folderPending&&ThumbnailsReady();});check(app.disk.GetStatus().entries>=count,"internal reload remains usable without new frequency event");
    app.diskCache=false;ConfigureDiskCache();app.disk.WaitIdle();SaveSettings();
}
