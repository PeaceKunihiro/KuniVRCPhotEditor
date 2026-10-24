#include "ViewerRuntime.h"
namespace {
void WriteTileFixture(const fs::path& path,UINT width,UINT height){
    ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream;winrt::check_hresult(factory->CreateStream(&stream));winrt::check_hresult(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame;winrt::check_hresult(encoder->CreateNewFrame(&frame,nullptr));winrt::check_hresult(frame->Initialize(nullptr));winrt::check_hresult(frame->SetSize(width,height));auto format=GUID_WICPixelFormat24bppBGR;winrt::check_hresult(frame->SetPixelFormat(&format));
    std::vector<BYTE> pixels(size_t(width)*height*3);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){auto p=&pixels[(size_t(y)*width+x)*3];p[0]=BYTE(x*255/width);p[1]=BYTE(y*255/height);p[2]=150;}
    winrt::check_hresult(frame->WritePixels(height,width*3,UINT(pixels.size()),pixels.data()));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());
}
}
void RunV053UiTests(const fs::path& root,std::ostream& report,const std::function<void(const wchar_t*)>& capture){
    report<<std::unitbuf;
    auto require=[&](bool ok,const char* text){if(!ok)throw std::runtime_error(text);report<<"PASS: "<<text<<'\n';};
    auto wait=[&](auto ready){auto until=GetTickCount64()+15000;while(!ready()){if(GetTickCount64()>until)throw std::runtime_error("v0.5.3 async timeout");MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}};
    app.sortMode=0;app.autoThumbnails=false;app.hideFileNames=true;app.darkTheme=false;app.layout=L"right";app.thumbnailWidth=128;ApplyThumbnailSize(true);
    auto pixel=[&](const Pixels& source,int x,int y){auto bitmap=Thumbnail(source,64);require(bitmap!=nullptr,"v0.5.3 tile bitmap allocated");BITMAP size{};GetObjectW(bitmap,sizeof(size),&size);require(size.bmWidth==64&&size.bmHeight==64,"v0.5.3 tile canvas is square");auto dc=CreateCompatibleDC(nullptr);auto old=SelectObject(dc,bitmap);auto color=GetPixel(dc,x,y);SelectObject(dc,old);DeleteDC(dc);DeleteObject(bitmap);return color;};
    for(auto size:std::array<SIZE,3>{{{8,4},{4,8},{4,4}}}){Pixels source;source.width=size.cx;source.height=size.cy;source.data.resize(size_t(size.cx)*size.cy*4);for(int y=0;y<size.cy;++y)for(int x=0;x<size.cx;++x){auto p=&source.data[(size_t(y)*size.cx+x)*4];bool middle=(size.cx>size.cy?x>=2&&x<6:size.cy>size.cx?y>=2&&y<6:true);p[0]=0;p[1]=middle?255:0;p[2]=middle?0:255;p[3]=255;}require(pixel(source,8,8)==RGB(0,255,0)&&pixel(source,55,55)==RGB(0,255,0),"v0.5.3 landscape portrait and square use centered fill without padding");}
    auto folder=root/L"v053-tiles";fs::create_directories(folder/L"subfolder");
    for(int i=0;i<24;++i)WriteTileFixture(folder/(L"photo-"+std::to_wstring(i)+L".png"),i%3==0?640:320,i%3==1?640:320);
    OpenFolder(folder.wstring());wait([&]{return !app.folderPending&&ThumbnailsReady();});
    int selected=int(app.directories.size());ListView_SetItemState(app.list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(app.list,selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);FlushSelectionUpdate();wait([&]{return app.previewDedicated&&app.metadataReady;});
    require(app.previewPixels.width==640&&app.previewPixels.height==320,"v0.5.3 side preview retains uncropped landscape aspect");
    auto preview=app.previewPixels.data;auto path=app.files[selected],fingerprint=MetadataFingerprint(path);int w=0,h=0;ImageList_GetIconSize(app.thumbs,&w,&h);require(w==128&&h==128,"v0.5.3 ImageList uses square cells");
    auto spacing=DWORD(SendMessageW(app.list,LVM_GETITEMSPACING,FALSE,0));require(LOWORD(spacing)==130&&HIWORD(spacing)==130,"v0.5.3 square cell spacing has two pixel gap");
    POINT a{},b{};ListView_GetItemPosition(app.list,selected,&a);ListView_GetItemPosition(app.list,selected+1,&b);require(a.y==b.y&&b.x-a.x==130,"v0.5.3 neighboring tiles follow compact grid");
    RECT iconA{},iconB{};ListView_GetItemRect(app.list,selected,&iconA,LVIR_ICON);ListView_GetItemRect(app.list,selected+1,&iconB,LVIR_ICON);report<<"INFO: v0.5.3 icon rectangles gap="<<iconB.left-iconA.right<<'\n';
    app.editing=true;auto state=app.selection;ToggleFileNames();wait([&]{return ThumbnailsReady();});ImageList_GetIconSize(app.thumbs,&w,&h);require(w==128&&h==96&&app.editing&&app.selection==state&&app.selected==selected,"v0.5.3 normal view restored without ending edit or selection");app.editing=false;
    ToggleFileNames();wait([&]{return ThumbnailsReady();});require(app.previewDedicated&&app.previewPixels.data==preview&&MetadataFingerprint(path)==fingerprint,"v0.5.3 tile toggle preserves full preview and original file");
    Job old=ThumbnailJob(selected);ToggleFileNames();require(!CurrentJob(old),"v0.5.3 old display generation is rejected");ToggleFileNames();wait([&]{return ThumbnailsReady();});
    for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();Layout();wait([&]{return ThumbnailsReady();});capture(dark?L"v053-tiles-night.png":L"v053-tiles-light.png");}
    require(SendMessageW(app.recordTooltip,TTM_GETDELAYTIME,TTDT_INITIAL,0)==3000&&SendMessageW(app.recordTooltip,TTM_GETDELAYTIME,TTDT_RESHOW,0)==3000,"v0.5.3 initial and repeated recording tooltip delay are three seconds");
    auto text=[&]{NMTTDISPINFOW info{};info.hdr.hwndFrom=app.recordTooltip;info.hdr.code=TTN_GETDISPINFOW;SendMessageW(app.window,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&info));return info.lpszText?std::wstring(info.lpszText):L"";};
    UpdateRecordingTooltip(false,true);require(text()==L"写真へ追加情報の記録を開始します","v0.5.3 stopped button tooltip describes starting");UpdateRecordingTooltip(true,true);require(text()==L"写真へ追加情報の記録を停止します","v0.5.3 recording button tooltip describes stopping");UpdateRecordingTooltip(true,false);require(text().empty(),"v0.5.3 unavailable recording action suppresses tooltip");UpdateRecordingTooltip(stage3::Recording(),!stage3::Stopping());
    for(auto layout:{L"left",L"bottom",L"none",L"right"}){app.layout=layout;Layout();wait([&]{return ThumbnailsReady();});require(app.previewPixels.data==preview&&app.selected==selected,"v0.5.3 tile placement changes preserve preview and selection");}
    app.autoThumbnails=true;Layout();wait([&]{return app.thumbnailWidth==app.activeThumbnailWidth&&ThumbnailsReady();});require(ThumbnailHeight(app.activeThumbnailWidth)==app.activeThumbnailWidth,"v0.5.3 automatic sizing keeps square tiles");app.autoThumbnails=false;
    {DiskThumbnailCache cache;cache.Configure(root/L"v053-disk-cache",{true,128ull*1024*1024,30,2});cache.WaitIdle();cache.Visit(folder.wstring());cache.Visit(folder.wstring());Pixels square{256,256,std::vector<BYTE>(256*256*4,150)},loaded;auto key=ThumbnailCache::Key(path)+L"|256x256|v2";cache.Put(key,path,square);require(cache.Get(key,loaded)&&loaded.width==256&&loaded.height==256&&loaded.data==square.data,"v0.5.3 disk cache accepts full 256 square base without truncation");require(!cache.Get(ThumbnailCache::Key(path)+L"|256x192|v1",loaded),"v0.5.3 old disk cache key is not reused as square base");}
    ChangeCacheLimit(16);app.thumbnailWidth=256;ApplyThumbnailSize();wait([&]{return ThumbnailsReady();});require(app.imageBytes<size_t(app.cacheMB)*1000000&&ImageListBytes(app.thumbs)==app.imageBytes,"v0.5.3 square ImageList respects small cache budget");
    ChangeCacheLimit(100);if(app.hideFileNames)ToggleFileNames();app.darkTheme=false;ApplyTheme();ResetViewer();
}
