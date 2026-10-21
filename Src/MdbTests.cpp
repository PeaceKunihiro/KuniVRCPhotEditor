#include "ViewerRuntime.h"
#include "V048Features.h"
#include <set>
void CaptureV0412Ui(const fs::path&,HWND);
void RunMdbTests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool v,const char* text){if(!v)throw std::runtime_error(text);report<<"PASS: v0.5.6 "<<text<<'\n';};
    auto old=ParseViewerSettings("{\"version\":3}");check(!old.mdbEnabled&&old.mdbMode==0&&old.mdbCapacityMB==100&&old.categoryMode==0,"MDB legacy defaults OFF 100MB");
    old.mdbEnabled=true;old.mdbMode=1;old.mdbCapacityMB=4000;old.categoryMode=6;auto back=ParseViewerSettings(SerializeViewerSettings(old));check(back.mdbEnabled&&back.mdbMode==1&&back.mdbCapacityMB==4000&&back.categoryMode==6,"MDB settings roundtrip all modes and 4GB boundary");
    for(auto s:{"{\"version\":3,\"mdbCapacityMB\":99}","{\"version\":3,\"mdbCapacityMB\":4001}","{\"version\":3,\"mdbMode\":3}"}){bool invalid=false;try{ParseViewerSettings(s);}catch(...){invalid=true;}check(invalid,"MDB invalid settings rejected");}
    check(MdbConfig{true,1,100}.Limit()==4000000000ULL&&MdbConfig{true,0,4000}.Limit()==4000000000ULL&&MdbConfig{true,2,100}.Limit()==0,"MDB decimal 4GB safety and unlimited are distinct");
    auto parent=directory/L"mdb-unit";fs::create_directories(parent);auto root=parent/L"MetadataDB";auto source=parent/L"photo.png";{std::ofstream f(source,std::ios::trunc);f<<"source";}
    std::atomic<unsigned> reads{0};auto loader=[&](const std::wstring& path){++reads;MdbRecord r;r.path=path;r.revision=ThumbnailCache::Key(path);std::ifstream f{fs::path(path)};std::string text((std::istreambuf_iterator<char>(f)),{});r.text=std::wstring(text.begin(),text.end());r.backup=fs::path(path).filename().wstring().find(L"AB_")==0;r.terms={{0,L"only once",L"only once",0},{3,L"wrld_test",L"World",0},{2,L"Member",L"Member",1}};if(fs::path(path).filename().wstring().find(L"large")==0)r.text.assign(6000000,L'x');return r;};
    MetadataDb db;MdbConfig cfg{true,0,100};db.Configure(root,cfg,loader,{});db.WaitIdle();db.Configure(root,{false,0,100},loader,{});db.WaitIdle();check(!fs::exists(root/L"mdb.sqlite"),"MDB OFF deletes managed database");
    db.Configure(root,cfg,loader,{});db.WaitIdle();auto r=db.Read(source.wstring());auto first=reads.load();auto cached=db.Read(source.wstring());check(reads==first&&cached.text==L"source"&&db.Status().hits>0,"MDB unchanged input uses persisted metadata");
    auto timestamp=fs::last_write_time(source);{std::ofstream f(source,std::ios::trunc);f<<"SOURCE";}fs::last_write_time(source,timestamp);check(db.Read(source.wstring()).text==L"SOURCE","MDB detects equal-size metadata changes with preserved modification time");{std::ofstream f(source,std::ios::trunc);f<<"source";}db.Read(source.wstring());first=reads;
    {std::ofstream f(source,std::ios::app);f<<" changed";}check(db.Read(source.wstring()).text==L"source changed"&&reads==first+1,"MDB external byte revision invalidates cache");
    check(db.Values(0)==std::vector<std::wstring>{L"only once"},"MDB retains a tag present in only one loaded photo");
    auto backup=parent/L"AB_backup.png";{std::ofstream f(backup);f<<"backup";}auto count=db.Status().entries;check(db.Read(backup.wstring()).backup&&db.Status().entries==count,"MDB backup image excluded from database");
    db.Stop();{MetadataDb reopened;reopened.Configure(root,cfg,loader,{});reopened.WaitIdle();first=reads;reopened.Read(source.wstring());check(reads==first,"MDB survives normal shutdown and restart");reopened.Configure(root,{false,0,100},loader,{});reopened.WaitIdle();check(!fs::exists(root/L"mdb.sqlite")&&fs::file_size(source)==14,"MDB deletion leaves original intact");}
    db.Configure(root,cfg,loader,{});db.WaitIdle();check(db.Status().entries==0,"MDB re-enable starts empty database");
    std::vector<fs::path> large;for(int n=0;n<3;++n){auto p=parent/(L"large"+std::to_wstring(n)+L".png");{std::ofstream f(p);f<<"image";}large.push_back(p);db.Read(p.wstring(),n==0?1:0);}
    check(db.Status().entries<=2&&db.Status().bytes<=100000000,"MDB capacity includes DB and journal reserve");first=reads;db.Read(large[0].wstring());check(reads==first,"MDB preferred record survives normal record eviction");
    db.Read(large[0].wstring(),1);db.Read(large[2].wstring(),1);db.Configure(root,cfg,loader,{});db.WaitIdle();auto fresh=parent/L"large-new.png";{std::ofstream f(fresh);f<<"new image";}db.Read(fresh.wstring());first=reads;db.Read(fresh.wstring());check(reads==first,"MDB fixed mode clears automatic priority so new records are admitted");
    fs::remove(large[1]);db.Reconcile(parent.wstring(),{source.wstring(),large[0].wstring(),large[2].wstring(),backup.wstring()});db.WaitIdle();check(db.Status().entries<=2,"MDB successful folder reload reconciles deleted paths");
    auto unrelated=root/L"unrelated.txt";{std::ofstream f(unrelated);f<<"keep";}db.Configure(root,{false,0,100},loader,{});db.WaitIdle();check(fs::exists(unrelated)&&!fs::exists(root/L"mdb.sqlite"),"MDB deletion preserves unrelated files");db.Stop();
    std::atomic<bool> entered{false},release{false};auto delayed=[&](const std::wstring& p){entered=true;while(!release)Sleep(1);return loader(p);};
    db.Configure(root,cfg,delayed,{});db.WaitIdle();db.Invalidate(source.wstring());db.Queue(source.wstring());auto until=GetTickCount64()+3000;while(!entered&&GetTickCount64()<until)Sleep(1);check(entered,"MDB delayed read started");db.Configure(root,{false,0,100},loader,{});release=true;db.WaitIdle();check(!fs::exists(root/L"mdb.sqlite"),"MDB OFF during collection never recreates deleted database");db.Stop();
    {std::ofstream mark(root/L"owner.lock",std::ios::binary);mark<<"KPEMDB01";std::ofstream broken(root/L"mdb.sqlite",std::ios::binary);broken<<"broken database";}
    db.Configure(root,cfg,loader,{});db.WaitIdle();check(db.Read(source.wstring()).text==L"source changed"&&!db.Status().error.empty(),"MDB corrupt DB falls back to source reader");db.Configure(root,{false,0,100},loader,{});db.WaitIdle();db.Stop();
    auto benchParent=directory/L"mdb-benchmark";fs::create_directories(benchParent);MetadataDb bench;auto benchPath=(directory/L"sample-日本語.png").wstring();std::atomic<int> parsed{0};auto read=[&](const std::wstring& p){++parsed;return ReadMdbSource(p);};bench.Configure(benchParent/L"MetadataDB",cfg,read,{});bench.WaitIdle();bench.Configure(benchParent/L"MetadataDB",{false,0,100},read,{});bench.WaitIdle();bench.Configure(benchParent/L"MetadataDB",cfg,read,{});bench.WaitIdle();
    auto start=std::chrono::steady_clock::now();for(int i=0;i<40;++i)ReadMdbSource(benchPath);auto directMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();bench.Read(benchPath);start=std::chrono::steady_clock::now();for(int i=0;i<40;++i)bench.Read(benchPath);auto cachedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();check(parsed==1,"MDB real PNG repeated reads parse source once");report<<"INFO: synthetic PNG x40 direct="<<directMs<<"ms MDB="<<cachedMs<<"ms (not a real-library benchmark)\n";bench.Configure(benchParent/L"MetadataDB",{false,0,100},read,{});bench.WaitIdle();bench.Stop();
    auto foreign=parent/L"foreign"/L"MetadataDB";fs::create_directories(foreign);{std::ofstream f(foreign/L"mdb.sqlite",std::ios::binary|std::ios::trunc);f<<"unrelated database";}MetadataDb unowned;unowned.Configure(foreign,cfg,loader,{});unowned.WaitIdle();unowned.Configure(foreign,{false,0,100},loader,{});unowned.WaitIdle();check(unowned.Status().deletionFailed&&fs::file_size(foreign/L"mdb.sqlite")==18,"MDB refuses deletion of files without ownership marker");unowned.Stop();
    auto real=ReadMdbSource((directory/L"sample-日本語.png").wstring());check(!real.text.empty()&&real.revision==MdbRevision((directory/L"sample-日本語.png").wstring()),"MDB original metadata reader preserves search payload");
}
// Exercise the standard list control's actual press/release path, not only LVM_SETITEMSTATE.
void RunMdbSelectionTests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool v,const char* text){report<<(v?"PASS: ":"FAIL: ")<<"v0.5.7 "<<text<<std::endl;if(!v)throw std::runtime_error(text);};
    auto pump=[&]{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}};
    auto ready=[&]{auto until=GetTickCount64()+10000;while(!app.metadataReady&&GetTickCount64()<until){pump();Sleep(10);}check(app.metadataReady&&!app.previewPixels.data.empty()&&!Text(app.details).empty()&&Text(app.simpleValues[0])==fs::path(app.files[app.selected]).filename().wstring(),"clicked image preview, simple and detailed metadata are ready");};
    std::vector<int> copies;int other=-1;for(int i=0;i<int(app.files.size());++i){auto name=fs::path(app.files[i]).filename();if(name==L"a.png")copies.push_back(i);if(name==L"b.png")other=i;}check(copies.size()==2&&other>=0,"mouse regression fixture has two occurrences and a different image");
    bool visible=IsWindowVisible(app.window);ShowWindow(app.window,SW_SHOWNOACTIVATE);
    auto click=[&](int index,bool ctrl=false,bool shift=false,bool right=false){ListView_EnsureVisible(app.list,index,FALSE);RECT r{};ListView_GetItemRect(app.list,index,&r,LVIR_ICON);auto point=MAKELPARAM((r.left+r.right)/2,(r.top+r.bottom)/2);BYTE saved[256]{},keys[256]{};GetKeyboardState(saved);std::copy(std::begin(saved),std::end(saved),keys);keys[VK_CONTROL]=ctrl?0x80:0;keys[VK_SHIFT]=shift?0x80:0;SetKeyboardState(keys);WPARAM modifiers=(ctrl?MK_CONTROL:0)|(shift?MK_SHIFT:0);auto down=right?WM_RBUTTONDOWN:WM_LBUTTONDOWN,up=right?WM_RBUTTONUP:WM_LBUTTONUP;PostMessageW(app.list,up,modifiers,point);SendMessageW(app.list,down,modifiers|(right?MK_RBUTTON:MK_LBUTTON),point);SendMessageW(app.list,up,modifiers,point);SetKeyboardState(saved);pump();};
    auto selected=[&](int index){return (ListView_GetItemState(app.list,index,LVIS_SELECTED)&LVIS_SELECTED)!=0;};
    ListView_SetItemState(app.list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);FlushSelectionUpdate();
    click(copies[0]);check(selected(copies[0])&&selected(copies[1])&&SelectedImageCount()==1,"normal click release preserves both occurrences as one selected photo");ready();check(app.selected==copies[0],"side view follows the clicked occurrence");
    click(copies[1]);check(selected(copies[0])&&selected(copies[1])&&app.selected==copies[1],"clicking an already-selected duplicate moves focus and keeps selection");ready();
    click(other);check(selected(other)&&!selected(copies[0])&&SelectedImageCount()==1,"normal click on another photo replaces duplicate selection");ready();
    click(copies[0],true);check(selected(other)&&selected(copies[0])&&selected(copies[1])&&SelectedImageCount()==2,"Ctrl click adds a duplicated photo once");
    click(copies[1],true);check(!selected(copies[0])&&!selected(copies[1])&&selected(other)&&SelectedImageCount()==1,"Ctrl click on any duplicate removes that photo");
    click(copies[0]);click(copies[1],false,true);std::set<std::wstring> expected;for(int n=copies[0];n<=copies[1];++n)expected.insert(PathKeyForMdb(app.files[n]));auto range=SelectedFiles();std::set<std::wstring> actual;for(auto& path:range)actual.insert(PathKeyForMdb(path));check(actual==expected,"Shift range includes every intermediate physical photo across groups");
    click(other,false,false,true);check(selected(other)&&app.selected==other,"right click selects the requested photo");ready();
    click(copies[0]);click(other);click(copies[1]);ready();check(app.selected==copies[1]&&SelectedImageCount()==1,"rapid successive clicks retain the newest side view");
    BYTE saved[256]{},keys[256]{};GetKeyboardState(saved);std::copy(std::begin(saved),std::end(saved),keys);keys[VK_CONTROL]=0x80;SetKeyboardState(keys);SendMessageW(app.list,WM_KEYDOWN,VK_SPACE,0);SendMessageW(app.list,WM_KEYUP,VK_SPACE,0);SetKeyboardState(saved);pump();check(!selected(copies[0])&&!selected(copies[1])&&SelectedImageCount()==0,"Ctrl Space clears every occurrence of the focused photo");
    click(copies[0]);SendMessageW(app.list,WM_KEYDOWN,VK_RIGHT,0);SendMessageW(app.list,WM_KEYUP,VK_RIGHT,0);pump();ready();report<<"INFO: arrow selected="<<app.selected<<" focused="<<ListView_GetNextItem(app.list,-1,LVNI_FOCUSED)<<" count="<<SelectedImageCount()<<" pending="<<app.selectionPending<<" busy="<<MdbSelectionBusy()<<std::endl;check(app.selected==ListView_GetNextItem(app.list,-1,LVNI_FOCUSED),"keyboard selection follows focused row");
    click(copies[1]);RECT client{};GetClientRect(app.list,&client);POINT blank{-1,-1};for(int y=client.bottom-10;y>=10&&blank.x<0;y-=15){LVHITTESTINFO hit{};hit.pt={client.right-6,y};if(ListView_HitTest(app.list,&hit)<0&&(hit.flags&LVHT_NOWHERE))blank=hit.pt;}check(blank.x>=0,"blank list space is available");auto point=MAKELPARAM(blank.x,blank.y);PostMessageW(app.list,WM_LBUTTONUP,0,point);SendMessageW(app.list,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(app.list,WM_LBUTTONUP,0,point);pump();check(SelectedImageCount()==0&&app.selected<0&&!MdbSelectionBusy(),"blank click clears selection without leaving an input transaction");
    click(copies[1]);ready();

    for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();CaptureV0412Ui(directory/(dark?L"mdb-selection-dark.png":L"mdb-selection-light.png"),app.window);}app.darkTheme=false;ApplyTheme();
    if(!visible)ShowWindow(app.window,SW_HIDE);
}
void RunMdbUiTests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool v,const char* text){if(!v)throw std::runtime_error(text);report<<"PASS: v0.5.6 "<<text<<'\n';};
    auto wait=[&](auto done){auto until=GetTickCount64()+30000;while(!done()){if(GetTickCount64()>until)throw std::runtime_error("MDB async UI timeout");MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}};
    auto root=directory/L"mdb-ui";fs::create_directories(root);auto source=directory/L"sample-日本語.png";
    std::wstring world=L"wrld_12345678-abcd-1234-abcd-123456789abc",id=L"usr_12345678-abcd-1234-abcd-123456789abc";
    std::vector<fs::path> files{root/L"a.png",root/L"b.png",root/L"c.png",root/L"AB_copy.png"};
    for(int n=0;n<2;++n){MetadataEdit edit;for(auto& action:edit.actions)action=EditAction::Set;edit.values={L"2026-10-18T12:00:00+09:00",L"Photographer",n==0?L"World One":L"World Two",world,L"Model",n==0?L"Red\nBlue":L"Blue"};stage3::Record capture;capture.memberStatus=L"reconstructed";capture.members={{L"Member",id}};stage3::Candidate candidate;candidate.person={L"Candidate",id};capture.candidates={candidate};capture.candidateStatus=L"observed";edit.createCaptureRecord=true;edit.stage3Xml=stage3::RecordXml(capture,true);fs::remove(files[n]);SaveMetadataCopy(source.wstring(),files[n].wstring(),MetadataFingerprint(source.wstring()),edit);}
    MetadataEdit empty;for(auto& action:empty.actions)action=EditAction::Remove;fs::remove(files[2]);SaveMetadataCopy(source.wstring(),files[2].wstring(),MetadataFingerprint(source.wstring()),empty);fs::copy_file(files[0],files[3],fs::copy_options::overwrite_existing);
    app.sortMode=0;app.mdbEnabled=false;app.categoryMode=0;app.mdbMode=0;app.mdbCapacityMB=100;ConfigureMdb();app.mdb.WaitIdle();
    for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();ShowMdbDialog([&](HWND h){check(Text(GetDlgItem(h,2300))==L"MDB：OFF"&&Text(GetDlgItem(h,2302))==L"100"&&SendDlgItemMessageW(h,2301,CB_GETCOUNT,0,0)==3,"MDB dialog default and three capacity modes");CaptureV0412Ui(directory/(dark?L"mdb-dark.png":L"mdb-light.png"),h);SendMessageW(h,WM_COMMAND,2300,0);});check(!app.mdbEnabled,"MDB cancel does not enable collection");}
    app.darkTheme=false;ApplyTheme();ShowMdbDialog([&](HWND h){SendMessageW(h,WM_COMMAND,2300,0);SendMessageW(h,WM_COMMAND,IDOK,0);});wait([]{return !app.searching&&!app.sorting;});check(app.mdbEnabled,"MDB dialog applies ON");
    app.metadataSearch=false;OpenFolder(root.wstring(),true);wait([]{return !app.folderPending&&ThumbnailsReady();});app.mdb.WaitIdle();
    auto names=app.mdb.Values(2);check(std::find(names.begin(),names.end(),L"Candidate")!=names.end()&&std::find(names.begin(),names.end(),L"Photographer")==names.end(),"MDB records candidates without treating photographer as a subject");
    auto all=app.mdb.Values(0);check(std::find(all.begin(),all.end(),L"Red")!=all.end(),"MDB observes loaded thumbnails and stores all tags");
    for(int axis:{1,2,4,5,6}){RequestCategory(axis);wait([]{return !app.searching&&!app.sorting;});SetAllCategories(true);wait([]{return ThumbnailsReady();});check(ListView_GetGroupCount(app.list)>=2,"MDB category creates native nonselectable group headers");check(VisibleImages().size()==4,"MDB category retains every physical image including missing metadata and visible backup");if(axis==1){check(app.files.size()>4&&std::any_of(app.mdbRows.begin(),app.mdbRows.end(),[](auto& r){return r.multiple;}),"MDB multi-tag photo appears in each group with duplicate badge");CaptureV0412Ui(directory/L"mdb-tag-groups.png",app.window);int a=-1;for(int i=0;i<int(app.files.size());++i)if(Same(app.files[i],files[0].wstring())){a=i;break;}ListView_SetItemState(app.list,-1,0,LVIS_SELECTED);ListView_SetItemState(app.list,a,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);FlushSelectionUpdate();check(SelectedFiles().size()==1&&SelectedImageCount()==1,"MDB duplicate selection counts one physical file");ListView_SetItemState(app.list,a,0,LVIS_SELECTED);FlushSelectionUpdate();check(SelectedFiles().empty(),"MDB deselecting one occurrence deselects all occurrences");RunMdbSelectionTests(directory,report);
            for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();for(auto layout:{L"left",L"right",L"bottom",L"none"}){app.layout=layout;Layout();check(ListView_GetGroupCount(app.list)>=2,"MDB groups survive side-view placement changes");}app.layout=L"right";Layout();app.hideFileNames=true;ApplyThumbnailSize(true);wait([]{return ThumbnailsReady();});CaptureV0412Ui(directory/(dark?L"mdb-tile-dark.png":L"mdb-tile-light.png"),app.window);app.hideFileNames=false;ApplyThumbnailSize(true);}app.darkTheme=false;ApplyTheme();}
        if(axis==4)check(std::any_of(app.mdbRows.begin(),app.mdbRows.end(),[&](auto& r){return r.key==world&&r.label==world;}),"MDB conflicting world names use ID as category name");
        if(axis==6)check(std::any_of(app.mdbRows.begin(),app.mdbRows.end(),[](auto& r){return r.label==L"Photographer";}),"MDB photographer is a separate category");}
    app.metadataSearch=true;StartSearch(L"Red");wait([]{return !app.searching&&!app.sorting&&ThumbnailsReady();});check(VisibleImages().size()==2,"MDB search includes excluded backup via direct metadata fallback");
    ShowMdbDialog([&](HWND h){SendMessageW(h,WM_COMMAND,2300,0);SendMessageW(h,WM_COMMAND,IDOK,0);});wait([]{return !app.searching&&!app.sorting;});app.mdb.WaitIdle();check(!app.mdbEnabled&&app.categoryMode==0&&!fs::exists(app.settings.parent_path()/L"MetadataDB"/L"mdb.sqlite"),"MDB OFF deletes DB and restores normal view");app.metadataSearch=false;app.appliedQuery.clear();wait([]{return ThumbnailsReady();});
    {
        auto first=app.wantedFirst,last=app.wantedLast;auto requested=app.thumbnailRequested;
        app.mdbEnabled=true;ConfigureMdb();app.mdb.WaitIdle();
        app.wantedFirst=app.wantedLast=0;app.thumbnailRequested[0]=true;UpdateMdbDisplayPriority();app.mdb.Queue(source.wstring());Sleep(100);
        check(app.mdb.Status().pending>0,"v0511 real UI display requests pause background metadata collection");
        app.thumbnailRequested[0]=false;UpdateMdbDisplayPriority();app.mdb.WaitIdle();
        check(app.mdb.Status().entries>0,"v0511 UI request completion resumes collection without waiting for all images");
        app.wantedFirst=first;app.wantedLast=last;app.thumbnailRequested=std::move(requested);UpdateMdbDisplayPriority();
        app.mdbEnabled=false;ConfigureMdb();app.mdb.WaitIdle();
    }
    app.diskCache=true;app.bookmarkCache=false;ConfigureDiskCache();app.disk.WaitIdle();app.disk.Clear();app.disk.WaitIdle();for(int i=0;i<5;++i)app.disk.Visit(root.wstring());Pixels thumb{16,16,std::vector<BYTE>(16*16*4,80)};app.disk.Put(ThumbnailCache::Key(files[0].wstring())+L"|256x256|v2",files[0].wstring(),thumb);
    app.mdbMode=1;app.mdbEnabled=true;ConfigureMdb();app.mdb.WaitIdle();check(app.mdb.Status().entries==1,"MDB automatic mode registers disk-cached source first without enabling recursion");
    app.categoryMode=1;StartCategory(app.allFiles,L"");SendMessageW(app.window,WM_COMMAND,SearchButton,0);check(!app.sorting&&!app.searching,"MDB category preparation can be cancelled");
    app.mdbEnabled=false;app.categoryMode=0;ConfigureMdb();app.mdb.WaitIdle();app.diskCache=false;ConfigureDiskCache();app.disk.WaitIdle();app.mdbMode=0;SaveSettings();
}
int MdbTest(){auto directory=app.settings.parent_path();std::ofstream report(directory/L"mdb-result.txt");try{RunMdbTests(directory,report);RunMdbUiTests(directory,report);report<<"ALL PASSED\n";return 0;}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}}


void RunV0511Tests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool ok,const char* name){report<<(ok?"PASS: ":"FAIL: ")<<"v0.5.11 "<<name<<std::endl;if(!ok)throw std::runtime_error(name);};
    auto wait=[&](auto ready){auto end=GetTickCount64()+5000;while(!ready()){if(GetTickCount64()>end)throw std::runtime_error("v0511 wait timeout");Sleep(5);}};
    auto base=directory/L"v0511";fs::create_directories(base);auto source=base/L"source.png";
    {std::ofstream out(source,std::ios::binary|std::ios::trunc);out<<"source";}
    auto key=ThumbnailCache::Key(source.wstring())+L"|256x256|v2";
    DiskThumbnailCache cache;DiskThumbnailCache::Config config{true,128ull*1024*1024,30,5,true,{base.wstring()}};
    auto cacheRoot=base/L"ThumbnailDiskCache";cache.Configure(cacheRoot,config);cache.WaitIdle();cache.Clear();cache.WaitIdle();
    Pixels pixels{256,256,std::vector<BYTE>(256*256*4,91)},out;cache.Put(key,source.wstring(),pixels);
    using Presence=DiskThumbnailCache::Presence;
    check(cache.Probe(key)==Presence::Present&&cache.Probe(key+L"changed")==Presence::Missing,"index probe distinguishes held keys without source parsing");
    {
        std::unique_lock<std::mutex> lock(cache.io);std::atomic<int> hint{0};
        std::thread other([&]{hint=int(cache.Probe(key));});other.join();check(hint==int(Presence::Pending),"busy cache lock returns Pending instead of waiting or Missing");
    }
    {auto prior=cache.loadedEpoch;cache.loadedEpoch=UINT64_MAX;check(cache.Probe(key)==Presence::Pending,"index initialization is not mistaken for a cache miss");cache.loadedEpoch=prior;}
    auto dtc=cacheRoot;for(auto& entry:fs::directory_iterator(cacheRoot))if(entry.path().extension()==L".dtc"){dtc=entry.path();break;}
    auto stamp=fs::last_write_time(dtc);HANDLE locked=CreateFileW(dtc.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"exclusive test cache handle acquired");
    auto hinted=cache.Probe(key);CloseHandle(locked);
    check(hinted==Presence::Present&&fs::last_write_time(dtc)==stamp,"index hint works with cache file exclusively locked and leaves LRU timestamp unchanged");
    auto ms=[](auto start){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
    std::vector<double> full,probe;
    for(int pass=0;pass<3;++pass){auto start=std::chrono::steady_clock::now();for(int i=0;i<256;++i)if(!cache.Get(key,out,false))throw std::runtime_error("benchmark Get");full.push_back(ms(start));
        start=std::chrono::steady_clock::now();for(int i=0;i<256;++i)if(cache.Probe(key)!=Presence::Present)throw std::runtime_error("benchmark Probe");probe.push_back(ms(start));}
    std::sort(full.begin(),full.end());std::sort(probe.begin(),probe.end());
    report<<"INFO: v0511 warm single synthetic 256x256 cache x256 median/3 Get="<<full[1]<<"ms Probe="<<probe[1]<<"ms; not application startup timing"<<std::endl;
    {std::fstream file(dtc,std::ios::binary|std::ios::in|std::ios::out);file.seekp(-1,std::ios::end);file.put('x');}
    check(cache.Probe(key)==Presence::Present&&!cache.Get(key,out,false),"priority hint never replaces display pixel digest validation");
    cache.Put(key,source.wstring(),pixels);check(cache.Get(key,out,false)&&out.data==pixels.data,"corrupt cached pixels can still be repaired");
    cache.Clear();cache.WaitIdle();check(cache.Probe(key)==Presence::Missing,"clear removes index hints");
    config.enabled=false;cache.Configure(cacheRoot,config);cache.WaitIdle();check(cache.Probe(key)==Presence::Missing,"disabled cache has no priority hints");cache.Stop();

    MetadataDb db;MdbConfig cfg{true,1,100};auto root=base/L"MetadataDB";
    std::atomic<int> loads{0},probes{0},hint{-1};
    auto loader=[&](const std::wstring& path){++loads;MdbRecord row;row.path=path;row.text=L"metadata";return row;};
    auto priority=[&](const std::wstring&){++probes;return hint.load();};
    db.Configure(root,cfg,loader,priority);db.WaitIdle();db.Configure(root,{false,1,100},loader,priority);db.WaitIdle();db.Configure(root,cfg,loader,priority);db.WaitIdle();
    db.SetDisplayBusy(true);db.QueueCached({source.wstring()});db.Queue(source.wstring());Sleep(120);
    check(loads==0&&probes==0&&db.Status().pending==1,"display demand defers and merges automatic priority and ordinary collection");
    auto direct=db.Read(source.wstring(),0);check(direct.text==L"metadata"&&loads==1,"explicit metadata operation bypasses background display gate");
    db.SetDisplayBusy(false);wait([&]{return probes.load()>0;});auto before=probes.load();Sleep(160);
    check(loads==1&&probes-before<=10,"Pending hint retries with bounded waits and never reparses the source");
    hint=1;db.WaitIdle();check(db.Status().entries==1&&loads==1,"ready priority hint resumes and reuses existing metadata");
    auto resumed=loads.load();db.Stop();db.Configure(root,cfg,loader,priority);db.WaitIdle();db.QueueCached({source.wstring()});db.WaitIdle();
    check(loads==resumed&&db.Status().entries==1,"automatic priority refresh after restart does not reparse unchanged metadata");
    db.SetDisplayBusy(true);auto old=base/L"old.png",next=base/L"next.png";for(auto path:{old,next}){std::ofstream f(path);f<<"source";}
    db.QueueCached({old.wstring()});db.QueueCached({next.wstring()});db.SetDisplayBusy(false);db.WaitIdle();
    check(loads==resumed+1&&db.Status().entries==2,"new folder scan discards old queued priority work");
    // Change the scan while a source read is in flight. Its metadata must not be committed.
    std::atomic<bool> entered{false},release{false};auto slow=[&](const std::wstring& path){entered=true;while(!release)Sleep(1);return loader(path);};
    db.Configure(root,cfg,slow,priority);db.WaitIdle();db.QueueCached({old.wstring()});wait([&]{return entered.load();});
    db.QueueCached({});release=true;db.WaitIdle();check(db.Status().entries==2&&db.Status().error.empty(),"obsolete in-flight priority scan cannot commit metadata or report a false failure");
    db.SetDisplayBusy(true);db.Queue(source.wstring());db.Configure(root,{false,1,100},loader,priority);db.WaitIdle();
    check(!fs::exists(root/L"mdb.sqlite"),"MDB OFF deletion bypasses a paused display gate and clears background work");db.Stop();
    db.Configure(root,cfg,loader,priority);db.WaitIdle();db.Queue(source.wstring());auto start=GetTickCount64();db.Stop();
    check(GetTickCount64()-start<3000,"stop interrupts paused collection without waiting for display completion");
    // All writes above are synthetic and confined to self-test-data.
}


void RunV0512Tests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool ok,const char* name){report<<(ok?"PASS: ":"FAIL: ")<<"v0.5.12 "<<name<<std::endl;if(!ok)throw std::runtime_error(name);};
    auto base=directory/L"v0512-unit";fs::create_directories(base);
    std::vector<MdbRecord> photos;
    const std::vector<std::vector<std::wstring>> members{{L"たぬき"},{L"うさぎ"},{L"たぬき",L"うさぎ"},{L"たぬき",L"かめ"},{L"かめ"}};
    for(int i=0;i<5;++i){MdbRecord row;row.path=(base/(std::wstring(1,wchar_t(L'A'+i))+L".png")).wstring();row.peopleReady=true;for(auto& name:members[i])row.terms.push_back({6,L"name:"+name,name,2});photos.push_back(row);}
    Result result;BuildPersonView(photos,{},L"",result);auto fileNames=[&]{std::wstring names;for(auto& path:result.files)names+=fs::path(path).stem().wstring();return names;};
    check(fileNames()==L"BCDEA"&&result.mdbRows[0].label==L"うさぎ"&&result.mdbRows[2].label==L"かめ","Japanese name order assigns example photos exactly once");
    BuildPersonView(photos,{},L"name:たぬき",result);check(fileNames()==L"ACDBE"&&result.files.size()==5,"priority includes every matching photo and retains nonmatches once");
    std::wstring id=L"usr_12345678-abcd-1234-abcd-123456789abc",other=L"usr_22345678-abcd-1234-abcd-123456789abc";
    photos[0].terms={{6,id,L"同名",4}};photos[1].terms={{6,other,L"同名",4}};photos[2].terms={{6,L"name:同名",L"同名",2}};photos[3].terms={{1,id,L"同室者のみ",1}};photos[4].terms={{6,id,L"別名",4}};
    BuildPersonView(photos,{},L"",result);std::set<std::wstring> keys;for(auto& row:result.mdbRows)keys.insert(row.key);
    check(keys.count(id)&&keys.count(other)&&keys.count(L"name:同名")&&keys.count(L"\uffff1"),"same-name IDs and name-only models stay separate; roommates alone have no candidates");
    check(std::any_of(result.mdbRows.begin(),result.mdbRows.end(),[&](auto& row){return row.key==id&&row.label==id;}),"conflicting names for an ID fall back to the ID");
    BuildPersonView(photos,{{id,L"登録名"}},id,result);check(result.mdbRows[0].key==id&&result.mdbRows[0].label==L"登録名","explicit ID alias overrides recorded names");
    photos[0].terms={{6,id,L"",4}};photos[4].terms.clear();photos[1].failed=true;photos[2].backup=true;
    BuildPersonView(photos,{},L"",result);check(result.mdbRows[0].key==id&&result.mdbRows[0].label==id&&result.mdbRows.back().label==L"バックアップ（MDB対象外）","ID-only candidate, read error and backup have distinct categories");
    std::string xml=R"(<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" xmlns:k="urn:kunivrcphoteditor:metadata:1.0"><rdf:Description><k:CaptureRecord><k:ModelCandidates><rdf:Seq><rdf:li rdf:parseType="Resource"><k:UserId>usr_12345678-abcd-1234-abcd-123456789abc</k:UserId></rdf:li></rdf:Seq></k:ModelCandidates></k:CaptureRecord></rdf:Description></rdf:RDF>)";
    XmpFields fields;std::wstring error;check(ParseXmp(xml,fields,error)&&fields.categoryCandidates.size()==1&&fields.categoryCandidates[0].id==id&&fields.candidatePeople.empty(),"ID-only candidate is retained for categorization without changing editable names");
    for(int old:{2,3,4,5,6}){auto settings=ParseViewerSettings("{\"version\":3,\"mdbEnabled\":true,\"categoryMode\":"+std::to_string(old)+"}");check(settings.categoryMode==(old==3?2:old),"legacy category settings preserve world/group/photographer and merge person modes");}
    auto source=base/L"source.bin";{std::ofstream f(source,std::ios::trunc);f<<"unchanged";}
    int reads=0;MetadataDb db;MdbConfig config{true,0,100};auto oldReader=[&](const std::wstring& path){++reads;MdbRecord row;row.path=path;row.text=L"old metadata";return row;};
    auto root=base/L"MetadataDB";db.Configure(root,config,oldReader,{});db.WaitIdle();db.Configure(root,{false,0,100},oldReader,{});db.WaitIdle();db.Configure(root,config,oldReader,{});db.WaitIdle();db.Read(source.wstring());
    auto currentReader=[&](const std::wstring& path){auto row=oldReader(path);row.peopleReady=true;row.terms={{6,id,L"モデル",4}};return row;};db.Configure(root,config,currentReader,{});db.WaitIdle();int before=reads;
    db.Read(source.wstring());check(reads==before,"old MDB is reusable without a forced global rebuild");
    auto upgraded=db.Read(source.wstring(),-1,false,true);check(reads==before+1&&upgraded.peopleReady&&upgraded.terms.size()==1,"person projection upgrades one legacy entry on demand");
    db.Read(source.wstring(),-1,false,true);check(reads==before+1&&fs::file_size(source)==9,"upgraded projection is reused and source stays unchanged");db.Stop();
    std::atomic<int> hint{0};MetadataDb pending;pending.Configure(root,{true,1,100},oldReader,[&](auto&){return hint.load();});pending.WaitIdle();pending.Invalidate(source.wstring());pending.Read(source.wstring());
    pending.Configure(root,{true,1,100},currentReader,[&](auto&){return hint.load();});pending.WaitIdle();hint=-1;pending.Read(source.wstring(),-1,false,true);hint=1;pending.WaitIdle();
    check(pending.Read(source.wstring(),-1,false,true).peopleReady,"legacy projection upgrade is retained even when the cache probe was Pending");pending.Stop();
}

void RunV0512UiTests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool ok,const char* name){report<<(ok?"PASS: ":"FAIL: ")<<"v0.5.12 "<<name<<std::endl;if(!ok)throw std::runtime_error(name);};
    auto wait=[&](auto ready){auto end=GetTickCount64()+20000;while(!ready()){if(GetTickCount64()>end)throw std::runtime_error("v0512 UI timeout");MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}};
    auto root=directory/L"v0512-ui";fs::create_directories(root);auto source=directory/L"sample-日本語.png";check(fs::exists(source),"existing synthetic PNG fixture available");
    std::vector<fs::path> files;const wchar_t* names[]{L"たぬき",L"うさぎ",L"たぬき\nうさぎ",L"たぬき\nかめ",L"かめ"};
    for(int i=0;i<5;++i){auto path=root/(std::wstring(1,wchar_t(L'A'+i))+L".png");MetadataEdit edit;edit.actions[4]=EditAction::Set;edit.values[4]=names[i];fs::remove(path);SaveMetadataCopy(source.wstring(),path.wstring(),MetadataFingerprint(source.wstring()),edit);files.push_back(path);}
    app.mdbEnabled=true;app.mdbMode=0;app.categoryMode=0;app.sortMode=0;app.appliedQuery.clear();app.metadataSearch=false;report<<"INFO: configuring MDB"<<std::endl;ConfigureMdb();report<<"INFO: opening person fixture"<<std::endl;OpenFolder(root.wstring(),true);wait([]{return !app.folderPending&&ThumbnailsReady();});report<<"INFO: requesting person category"<<std::endl;
    RequestCategory(2);wait([]{return !app.sorting&&!app.searching;});
    check(app.files.size()==5&&ListView_GetGroupCount(app.list)==3&&app.personChoices.size()==3,"unified person categories have five unique photos and dynamic choices");
    auto allCollapsed=[&]{for(int i=0;i<ListView_GetGroupCount(app.list);++i){LVGROUP group{sizeof(group)};group.mask=LVGF_GROUPID|LVGF_STATE;group.stateMask=LVGS_COLLAPSED;ListView_GetGroupInfoByIndex(app.list,i,&group);if(!(group.state&LVGS_COLLAPSED))return false;}return true;};
    check(allCollapsed()&&app.wantedItems.empty(),"initial categories are collapsed with no hidden thumbnail demand");
    auto fingerprint=MetadataFingerprint(source.wstring());auto parsed=app.mdb.Status().parsed;
    auto bar=GetDlgItem(app.window,3050);SendMessageW(bar,WM_COMMAND,MAKEWPARAM(3202,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(bar,3202)));wait([]{return ThumbnailsReady();});check(app.files.size()==5&&fs::path(app.files[0]).stem()==L"A"&&fs::path(app.files[1]).stem()==L"C"&&fs::path(app.files[2]).stem()==L"D"&&!app.wantedItems.empty(),"priority regroups every matching photo first and expands its category");
    check(app.mdb.Status().parsed==parsed,"priority button reuses projection without metadata reparse");
    ApplyPersonPriority(L"");check(allCollapsed(),"clearing priority restores initial collapsed state");SetAllCategories(true);wait([]{return ThumbnailsReady();});check(!app.wantedItems.empty(),"expand all schedules visible thumbnails");
    int chosen=0;ListView_SetItemState(app.list,chosen,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);FlushSelectionUpdate();int count=SelectedImageCount();SetAllCategories(false);check(SelectedImageCount()==count&&count==1,"collapsing categories retains selected physical photos");
    for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();for(auto side:{L"left",L"right",L"bottom",L"none"}){app.layout=side;Layout();check(allCollapsed(),"theme and side placement preserve collapse state");}app.layout=L"right";ApplyPersonPriority(L"name:たぬき");wait([]{return ThumbnailsReady();});check(SelectedImageCount()==1,"priority rebuild preserves selection without stale deselection notifications");CaptureV0412Ui(directory/(dark?L"v0512-dark.png":L"v0512-light.png"),app.window);ApplyPersonPriority(L"");}
    for(int i=0;i<80;++i)app.personRecords[0].terms.push_back({6,L"name:Many"+std::to_wstring(i),L"Many"+std::to_wstring(i),2});
    RefreshMdbIdLabels();auto panel=GetDlgItem(app.window,3050);int controls=0;for(auto child=GetWindow(panel,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))++controls;
    check(app.personChoices.size()>=80&&controls<40,"many dynamic names use a bounded scrollable button pool");SendMessageW(panel,WM_VSCROLL,SB_BOTTOM,0);check(GetScrollPos(panel,SB_VERT)>0,"person button region scrolls to later names");
    std::wstring uid1=L"usr_12345678-abcd-1234-abcd-123456789abc",uid2=L"usr_22345678-abcd-1234-abcd-123456789abc";
    app.personRecords[0].terms={{6,uid1,L"Same",4}};app.personRecords[1].terms={{6,uid2,L"Same",4}};RefreshMdbIdLabels();int identified=0;
    for(int i=0;i<ListView_GetGroupCount(app.list);++i){wchar_t text[512]{};LVGROUP g{sizeof(g)};g.mask=LVGF_HEADER;g.pszHeader=text;g.cchHeader=512;ListView_GetGroupInfoByIndex(app.list,i,&g);if(std::wstring(text).find(L"Same［usr_")!=std::wstring::npos)++identified;}
    check(identified==2,"same-name distinct IDs have disambiguated category headers as well as buttons");
    app.darkTheme=false;app.layout=L"right";ApplyTheme();SetAllCategories(true);OpenFolder(root.wstring(),true);wait([]{return !app.folderPending&&!app.sorting;});check(allCollapsed(),"explicit reload resets all categories to collapsed");
    ApplyPersonPriority(L"name:うさぎ");StartSearch(L"A.png");check(!IsWindowEnabled(GetDlgItem(app.window,3050)),"person controls are disabled while search is pending");wait([]{return !app.searching&&!app.sorting;});
    check(app.priorityPerson.empty()&&app.files.size()==1&&app.personChoices.size()==1,"search removes an unavailable priority without retaining stale choices");
    StartSearch(L"");wait([]{return !app.searching&&!app.sorting;});SetAllCategories(false);
    LVGROUP group{sizeof(group)};group.mask=LVGF_GROUPID;ListView_GetGroupInfoByIndex(app.list,0,&group);RECT header{};ListView_GetGroupRect(app.list,group.iGroupId,LVGGR_HEADER,&header);
    auto point=MAKELPARAM(header.left+12,(header.top+header.bottom)/2);SendMessageW(app.list,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(app.list,WM_LBUTTONUP,0,point);
    check(!(ListView_GetGroupState(app.list,group.iGroupId,LVGS_COLLAPSED)&LVGS_COLLAPSED),"category header click expands its group");
    SendMessageW(app.list,WM_KEYDOWN,VK_LEFT,0);check((ListView_GetGroupState(app.list,group.iGroupId,LVGS_COLLAPSED)&LVGS_COLLAPSED)!=0,"focused category keyboard left collapses the group");
    SendMessageW(app.list,WM_KEYDOWN,VK_RIGHT,0);check(!(ListView_GetGroupState(app.list,group.iGroupId,LVGS_COLLAPSED)&LVGS_COLLAPSED),"focused category keyboard right expands the group");
    check(MetadataFingerprint(source.wstring())==fingerprint,"classification never writes source image");
    app.mdbEnabled=false;app.categoryMode=0;ResetCategoryView();ConfigureMdb();app.mdb.WaitIdle();Layout();
}
int PeopleTest(){auto folder=app.settings.parent_path();std::ofstream report(folder/L"v0512-result.txt");try{RunV0512Tests(folder,report);RunV0512UiTests(folder,report);report<<"ALL PASSED\n";return 0;}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<std::endl;return 1;}}
