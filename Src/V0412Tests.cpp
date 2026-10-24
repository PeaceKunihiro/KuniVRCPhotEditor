#include "ViewerRuntime.h"
#include "resource.h"
#include <iostream>
void CaptureV0412Ui(const fs::path&,HWND);
void RunV055UiTests(const fs::path&,std::ostream&);
namespace {
void BookmarkCacheTests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: v0.5.5 "<<label<<'\n';};
    auto legacy=ParseViewerSettings("{\"version\":3}");check(!legacy.bookmarkCache,"legacy bookmark priority defaults OFF");
    legacy.diskCache=legacy.bookmarkCache=true;auto parsed=ParseViewerSettings(SerializeViewerSettings(legacy));check(parsed.bookmarkCache&&parsed.diskCache,"shared priority setting persists");
    check(!ParseViewerSettings("{\"version\":3,\"diskCache\":false,\"bookmarkCache\":true}").bookmarkCache,"disabled master suppresses priority setting");
    auto base=directory/L"v055-bookmark-cache";fs::remove_all(base);fs::create_directories(base/L"book"/L"old");fs::create_directories(base/L"book"/L"new");fs::create_directories(base/L"book-other");
    auto a=base/L"book"/L"old"/L"a.png",b=base/L"book"/L"new"/L"b.png",n=base/L"book-other"/L"n.png";
    for(auto& p:{a,b,n}){std::ofstream out(p);out<<"original";}
    auto key=[](const fs::path& p){return ThumbnailCache::Key(p.wstring())+L"|256x256|v2";};
    DiskThumbnailCache::Config cfg{true,128ull*1024*1024,30,5,true,{(base/L"book").wstring()}};
    auto root=base/L"ThumbnailDiskCache";Pixels pixels{256,256,std::vector<BYTE>(256*256*4,73)},out;DiskThumbnailCache cache;cache.Configure(root,cfg);cache.WaitIdle();
    check(cache.PriorityFolder(a.parent_path().wstring())&&cache.Eligible(a.parent_path().wstring())&&!cache.PriorityFolder(n.parent_path().wstring())&&!cache.Eligible(n.parent_path().wstring()),"bookmark descendants bypass frequency without prefix collision");
    cache.Visit(a.parent_path().wstring());cache.Put(key(a),a.wstring(),pixels);cache.Visit(b.parent_path().wstring());cache.Put(key(b),b.wstring(),pixels);
    for(int i=0;i<5;++i)cache.Visit(n.parent_path().wstring());cache.Put(key(n),n.wstring(),pixels);cache.WaitIdle();
    check(cache.Get(key(a),out)&&out.data==pixels.data&&cache.GetStatus().entries==3,"priority and ordinary automatic cache coexist");
    cfg.bookmarkPriority=false;cache.Configure(root,cfg);cache.WaitIdle();check(cache.GetStatus().entries==3&&!cache.Eligible(a.parent_path().wstring())&&cache.Get(key(a),out),"priority OFF retains files and restores automatic admission");
    cfg.bookmarkPriority=true;cache.Configure(root,cfg);cache.WaitIdle();cache.Stop();
    std::map<std::wstring,fs::path> files;
    for(auto& e:fs::directory_iterator(root))if(e.path().extension()==L".dtc"){
        std::ifstream in(e.path(),std::ios::binary);uint32_t header[4]{};in.read(reinterpret_cast<char*>(header),16);in.seekg(48);std::wstring stored(header[3],L'\0');in.read(reinterpret_cast<char*>(stored.data()),stored.size()*sizeof(wchar_t));files[stored]=e.path();
    }
    check(files.count(key(a))&&files.count(key(b))&&files.count(key(n)),"cache identities retained on disk");
    // Inflate managed entries to exercise the real 128MiB minimum without decoding large originals.
    fs::resize_file(files.at(key(a)),64ull*1024*1024);fs::resize_file(files.at(key(b)),64ull*1024*1024);
    DiskThumbnailCache reopened;reopened.Configure(root,cfg);reopened.WaitIdle();
    check(!fs::exists(files.at(key(n)))&&!fs::exists(files.at(key(a)))&&fs::exists(files.at(key(b)))&&reopened.GetStatus().bytes<=128ull*1024*1024,"restart restores folder order; ordinary then older bookmark evicted first");
    // An older priority folder must not evict a newer one, even if its thumbnail is newly requested.
    reopened.Stop();fs::resize_file(files.at(key(b)),126ull*1024*1024-128*1024);
    DiskThumbnailCache constrained;constrained.Configure(root,cfg);constrained.WaitIdle();constrained.Put(key(a),a.wstring(),pixels);
    check(constrained.GetStatus().entries==1&&fs::exists(files.at(key(b)))&&!fs::exists(files.at(key(a))),"older bookmark cannot displace newer folder when budget is full");
    for(int i=0;i<5;++i)constrained.Visit(n.parent_path().wstring());constrained.Put(key(n),n.wstring(),pixels);check(constrained.GetStatus().entries==1&&fs::exists(files.at(key(b))),"ordinary incoming thumbnail cannot displace priority folder");
    cfg.bookmarks.clear();constrained.Configure(root,cfg);constrained.WaitIdle();check(!constrained.PriorityFolder(a.parent_path().wstring()),"bookmark removal immediately removes priority");
    constrained.Clear();constrained.WaitIdle();check(constrained.GetStatus().entries==0&&fs::file_size(a)==8&&fs::file_size(b)==8&&fs::file_size(n)==8,"explicit clear leaves all originals intact");constrained.Stop();
}
}
void RunV0412Tests(const fs::path& directory,std::ostream& report){
    BookmarkCacheTests(directory,report);
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
    RECT first{},second{};GetWindowRect(app.simpleValues[0],&first);GetWindowRect(app.simpleValues[2],&second);check(second.top-first.bottom==2,"collapsed rows leave only normal spacing");
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
    RunV055UiTests(directory,report);
}

void RunV055UiTests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: v0.5.5 "<<label<<'\n';};
    auto pump=[&](auto complete){auto deadline=GetTickCount64()+15000;while(!complete()){if(GetTickCount64()>deadline)throw std::runtime_error("v055 async timeout");MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}};
    // v0.5.5: one shared setting, cancellation, both themes and real background prefetch.
    auto bookmarks=app.bookmarks;
    auto book=directory/L"v055-ui-book";fs::create_directories(book/L"unvisited");
    for(int i=0;i<80;++i)fs::copy_file(directory/L"sample.jpg",book/(std::to_wstring(100+i)+L".jpg"),fs::copy_options::overwrite_existing);
    fs::copy_file(directory/L"sample.jpg",book/L"unvisited"/L"nested.jpg",fs::copy_options::overwrite_existing);
    check(AddBookmark(book.wstring()),"v055 bookmark fixture added");
    auto bookmarkMenu=BuildBookmarkMenu();check(!(GetMenuState(bookmarkMenu,2,MF_BYCOMMAND)&MF_CHECKED),"v055 bookmark toggle starts OFF");DestroyMenu(bookmarkMenu);
    for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();ShowDiskCacheDialog([&](HWND h){
        check(IsDlgButtonChecked(h,2106)==BST_UNCHECKED&&Text(GetDlgItem(h,2107)).find(L"64MiB")==std::wstring::npos&&Text(GetDlgItem(h,2109)).find(L"サムネイルのみ")!=std::wstring::npos,"v055 checkbox and thumbnail-only notice; 64MiB removed");
        CheckDlgButton(h,2106,BST_CHECKED);SendMessageW(h,WM_COMMAND,MAKEWPARAM(2106,BN_CLICKED),0);check(IsDlgButtonChecked(h,2100)==BST_CHECKED,"v055 priority checks master");
        CaptureV0412Ui(directory/(dark?L"v055-cache-dark.png":L"v055-cache-light.png"),h);
    });check(!app.bookmarkCache&&!app.diskCache,"v055 cancel discards checkbox change");}
    app.darkTheme=false;ApplyTheme();app.disk.Clear();app.disk.WaitIdle();
    ShowDiskCacheDialog([&](HWND h){CheckDlgButton(h,2106,BST_CHECKED);SendMessageW(h,WM_COMMAND,MAKEWPARAM(2106,BN_CLICKED),0);SendMessageW(h,WM_COMMAND,IDOK,0);});
    bookmarkMenu=BuildBookmarkMenu();check(app.bookmarkCache&&app.diskCache&&(GetMenuState(bookmarkMenu,2,MF_BYCOMMAND)&MF_CHECKED),"v055 dialog updates shared bookmark toggle");DestroyMenu(bookmarkMenu);
    OpenFolder(book.wstring(),true);pump([]{return !app.folderPending&&app.diskPrefetchCursor==80&&app.thumbnailActive==0&&app.disk.GetStatus().entries>=80;});
    check(app.allFiles.size()==80&&app.disk.GetStatus().entries==80,"v055 one load caches offscreen images without traversing nested folder");
    auto prior=app.disk.GetStatus().entries;auto stale=Job{1,app.generation,0,-1,(book/L"100.jpg").wstring(),L"",app.thumbnailGeneration,256};stale.diskPrefetch=true;stale.diskGeneration=app.diskGeneration;
    SetBookmarkCache(false,false);app.disk.WaitIdle();check(app.diskCache&&!app.bookmarkCache&&!CurrentJob(stale)&&app.disk.GetStatus().entries==prior,"v055 bookmark OFF preserves cache and cancels old prefetch");
    SetBookmarkCache(true,false);ShowDiskCacheDialog([&](HWND h){CheckDlgButton(h,2100,BST_UNCHECKED);SendMessageW(h,WM_COMMAND,MAKEWPARAM(2100,BN_CLICKED),0);check(IsDlgButtonChecked(h,2106)==BST_UNCHECKED,"v055 master OFF clears priority checkbox");SendMessageW(h,WM_COMMAND,IDOK,0);});
    check(!app.diskCache&&!app.bookmarkCache,"v055 master OFF applies both settings");
    app.bookmarks=bookmarks;ConfigureDiskCache();app.disk.WaitIdle();SaveSettings();
}

int BookmarkCacheTest(){auto directory=app.settings.parent_path();std::ofstream report(directory/L"bookmark-cache-result.txt");try{RunV055UiTests(directory,report);report<<"ALL PASSED\n";return 0;}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}}
