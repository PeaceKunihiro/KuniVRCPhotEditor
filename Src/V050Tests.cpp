#include "ViewerRuntime.h"
#include "WebPFixtures.h"
void RunV050UiTests(const fs::path& root,std::ostream& report,const std::function<void(const wchar_t*)>& capture){
    auto require=[&](bool ok,const char* text){if(!ok)throw std::runtime_error(text);report<<"PASS: "<<text<<'\n';};
    auto wait=[&](auto ready){auto until=GetTickCount64()+15000;while(!ready()){if(GetTickCount64()>until)throw std::runtime_error("v0.5.0 async timeout");MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}};
    auto folder=root/L"v050-comfort";fs::create_directories(folder/L"subfolder");
    {ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;winrt::check_hresult(factory->CreateStream(&stream));winrt::check_hresult(stream->InitializeFromFilename((folder/L"wide.png").c_str(),GENERIC_WRITE));ComPtr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;winrt::check_hresult(encoder->CreateNewFrame(&frame,nullptr));winrt::check_hresult(frame->Initialize(nullptr));winrt::check_hresult(frame->SetSize(1200,600));auto format=GUID_WICPixelFormat24bppBGR;winrt::check_hresult(frame->SetPixelFormat(&format));std::vector<BYTE> pixels(1200*600*3,180);winrt::check_hresult(frame->WritePixels(600,3600,UINT(pixels.size()),pixels.data()));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());}
    for(int i=0;i<18;++i)fs::copy_file(folder/L"wide.png",folder/(L"image-"+std::to_wstring(i)+L".png"),fs::copy_options::overwrite_existing);
    app.autoThumbnails=false;app.hideFileNames=false;app.layout=L"right";TabCtrl_SetCurSel(app.metadataTabs,0);OpenFolder(folder.wstring());wait([&]{return !app.folderPending;});
    require(FolderPickerStart()==Normalize(folder.wstring()),"v0.5.0 folder picker starts at address");SetWindowTextW(app.path,(folder/L"subfolder").c_str());require(FolderPickerStart()==Normalize((folder/L"subfolder").wstring()),"v0.5.0 uncommitted address takes priority");SetWindowTextW(app.path,L"Z:\\missing-v050-folder");require(FolderPickerStart()==Normalize(folder.wstring()),"v0.5.0 invalid address falls back to current folder");SetWindowTextW(app.path,folder.c_str());
    int selected=int(app.directories.size());ListView_SetItemState(app.list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(app.list,selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);FlushSelectionUpdate();wait([&]{return app.previewDedicated&&app.metadataReady;});
    require(app.previewPixels.width==1024&&app.previewPixels.height==512,"v0.5.0 selected preview has bounded independent resolution and source aspect");auto pixels=app.previewPixels.data;
    for(int width:{64,256,96}){app.thumbnailWidth=width;ApplyThumbnailSize();wait([&]{return ThumbnailsReady();});require(app.previewDedicated&&app.previewPixels.data==pixels,"v0.5.0 list zoom cannot replace dedicated preview");}
    ToggleFileNames();DWORD compact=DWORD(SendMessageW(app.list,LVM_GETITEMSPACING,FALSE,0));
    require((ListView_GetExtendedListViewStyle(app.list)&LVS_EX_HIDELABELS)&&HIWORD(compact)==app.activeThumbnailWidth+2,"v0.5.3 hidden filenames use compact square tile spacing");
    wchar_t text[128]{};ListView_GetItemText(app.list,selected,0,text,128);require(*text&&app.selected==selected&&(ListView_GetItemState(app.list,selected,LVIS_SELECTED)&LVIS_SELECTED),"v0.5.0 hidden labels preserve accessible text and selection");
    POINT first{},second{};ListView_GetItemPosition(app.list,selected,&first);ListView_GetItemPosition(app.list,selected+1,&second);require(first.x!=second.x||first.y!=second.y,"v0.5.0 hidden labels do not overlap icon positions");
    auto settings=ParseViewerSettings([&]{std::ifstream in(app.settings,std::ios::binary);return std::string((std::istreambuf_iterator<char>(in)),{});}());require(settings.hideFileNames,"v0.5.0 filename option persists");
    for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();Layout();wait([&]{return ThumbnailsReady();});require(app.previewDedicated&&app.previewPixels.data==pixels,"v0.5.0 theme changes preserve dedicated preview");capture(dark?L"v050-night.png":L"v050-light.png");}
    require(FindResourceW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(2004),RT_RCDATA)!=nullptr,"v0.5.0 load icon is embedded");auto button=AppControl(OpenPath);RECT rect{};GetWindowRect(button,&rect);require((GetWindowLongPtrW(button,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW&&rect.right-rect.left==28,"v0.5.0 load button uses compact icon layout");
    ToggleFileNames();require(!(ListView_GetExtendedListViewStyle(app.list)&LVS_EX_HIDELABELS),"v0.5.0 filename display restored");app.darkTheme=false;ApplyTheme();ResetViewer();require(app.previewPixels.data.empty(),"v0.5.0 reset releases preview");
}

void RunV0513UiTests(const fs::path& root,std::ostream& report){
    KillTimer(app.window,RecordTimer);struct RestoreTimer{~RestoreTimer(){SetTimer(app.window,RecordTimer,500,nullptr);}}restoreTimer;
    auto check=[&](bool ok,const char* text){if(!ok)throw std::runtime_error(text);report<<"PASS: "<<text<<std::endl;};
    auto pump=[](){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}};
    auto wait=[&](auto ready){auto until=GetTickCount64()+15000;while(!ready()){pump();if(GetTickCount64()>until)throw std::runtime_error("v0.5.13 async timeout");Sleep(10);}};
    auto folder=root/L"v0513-preview";fs::create_directories(folder);
    ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    auto make=[&](const wchar_t* name,UINT w,UINT h,const GUID& format){
        ComPtr<IWICStream> stream;winrt::check_hresult(factory->CreateStream(&stream));winrt::check_hresult(stream->InitializeFromFilename((folder/name).c_str(),GENERIC_WRITE));
        ComPtr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(format,nullptr,&encoder));winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        ComPtr<IWICBitmapFrameEncode> frame;winrt::check_hresult(encoder->CreateNewFrame(&frame,nullptr));winrt::check_hresult(frame->Initialize(nullptr));winrt::check_hresult(frame->SetSize(w,h));
        auto pixelFormat=GUID_WICPixelFormat24bppBGR;winrt::check_hresult(frame->SetPixelFormat(&pixelFormat));std::vector<BYTE> data(size_t(w)*h*3,180);
        winrt::check_hresult(frame->WritePixels(h,w*3,UINT(data.size()),data.data()));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());
    };
    make(L"wide.png",1800,900,GUID_ContainerFormatPng);make(L"portrait.jpg",900,1800,GUID_ContainerFormatJpeg);
    {std::ofstream file(folder/L"transparent.webp",std::ios::binary);file.write(reinterpret_cast<const char*>(LosslessWebP),sizeof(LosslessWebP));}
    app.layout=L"right";app.categoryMode=0;app.sortMode=0;app.mdbEnabled=false;app.appliedQuery.clear();app.autoThumbnails=false;
    OpenFolder(folder.wstring());wait([&]{return !app.folderPending;});
    auto select=[&](const wchar_t* file){auto i=std::find(app.files.begin(),app.files.end(),(folder/file).wstring());check(i!=app.files.end(),"v0513 fixture in list");Select(int(i-app.files.begin()),false);wait([&]{return app.previewDedicated;});};
    MoveWindow(app.preview,0,0,600,400,FALSE);select(L"wide.png");
    check(app.previewPixels.width==1024&&app.previewPixels.height==512&&!app.previewOriginalRequested,"v0513 small view uses 1024 aspect-preserving preview");
    MoveWindow(app.preview,0,0,1024,700,FALSE);LoadOriginalPreview();check(!app.previewOriginalRequested,"v0513 exactly 1024 does not request original");
    auto revision=app.previewRevision;MoveWindow(app.preview,0,0,1400,800,FALSE);LoadOriginalPreview();
    check(app.previewOriginalRequested&&!app.previewOriginal&&app.previewPixels.width==1024&&app.previewRevision==revision,"v0513 old preview remains until original result is processed");
    wait([&]{return app.previewOriginal;});check(app.previewPixels.width==1800&&app.previewPixels.height==900&&app.metadataReady,"v0513 PNG original replaces preview without losing metadata");
    auto pixels=app.previewPixels.data;auto originalSelection=app.selection;auto sourceKey=app.previewSourceKey;
    for(bool dark:{true,false}){app.darkTheme=dark;ApplyTheme();MoveWindow(app.preview,0,0,700,400,FALSE);RedrawWindow(app.preview,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);LoadOriginalPreview();check(app.previewPixels.data==pixels&&app.selection==originalSelection,"v0513 theme and shrink retain current original without new decode");}
    MoveWindow(app.preview,0,0,600,400,FALSE);select(L"portrait.jpg");check(app.previewPixels.width==512&&app.previewPixels.height==1024&&!app.previewOriginal,"v0513 selection releases previous original and restores portrait preview");
    Result stale;stale.job={5,app.generation,originalSelection,0,(folder/L"wide.png").wstring(),L""};stale.job.sourceKey=sourceKey;stale.pixels={1,1,{0,0,0,255}};
    {std::lock_guard<std::mutex> lock(app.mutex);app.results.push_back(std::move(stale));}Results();check(app.previewPixels.height==1024&&!app.previewOriginal,"v0513 late result for previous selection ignored");
    MoveWindow(app.preview,0,0,700,1300,FALSE);wait([&]{return app.previewOriginal;});check(app.previewPixels.width==900&&app.previewPixels.height==1800,"v0513 height over 1024 automatically loads JPEG original after debounce");
    MoveWindow(app.preview,0,0,600,400,FALSE);select(L"wide.png");pixels=app.previewPixels.data;
    Result failure;failure.job={5,app.generation,app.selection,app.selected,app.files[app.selected],L""};failure.job.sourceKey=app.previewSourceKey;failure.error=L"test decode failure";
    {std::lock_guard<std::mutex> lock(app.mutex);app.results.push_back(std::move(failure));}Results();check(app.previewPixels.data==pixels&&app.metadataReady&&!app.previewOriginal,"v0513 original decode failure preserves fallback and edit state");
    select(L"transparent.webp");check(app.previewPixels.width==320&&app.previewPixels.height==160,"v0513 small WebP preview retains source aspect");
    MoveWindow(app.preview,0,0,1400,900,FALSE);LoadOriginalPreview();wait([&]{return app.previewOriginal;});check(app.previewPixels.width==320&&app.previewPixels.height==160&&app.previewPixels.data[3]==128,"v0513 original WebP decode preserves size and transparency without upsampling pixels");
    MoveWindow(app.preview,0,0,600,400,FALSE);select(L"wide.png");
    app.layout=L"none";MoveWindow(app.preview,0,0,1400,900,FALSE);LoadOriginalPreview();check(!app.previewOriginalRequested,"v0513 hidden side view does not request original");
    app.layout=L"right";LoadOriginalPreview();auto oldJob=Job{5,app.generation,app.selection,app.selected,app.files[app.selected],L""};Select(-1,false);check(!CurrentJob(oldJob)&&app.previewPixels.data.empty()&&!app.previewOriginal,"v0513 deselection cancels old preview work and releases pixels");
    ResetViewer();app.layout=L"right";Layout();report<<"v0513 preview cases complete\n";
}
int PreviewTest(){auto root=app.settings.parent_path();std::ofstream report(root/L"v0513-result.txt");try{RunV0513UiTests(root,report);report<<"ALL PASSED\n";return 0;}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<std::endl;return 1;}}
