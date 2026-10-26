#include <richedit.h>
#include "TestOcr.h"
#include "V048Features.h"
#include "UpdateChecker.h"
#include "LicenseUi.h"
#include "ViewerRuntime.h"
#include "MetadataWriterTests.h"
#include "V031Tests.h"
void RunV0415Tests(const fs::path&,std::ostream&);
void RunV035Tests(const fs::path&,std::ostream&);
void RunV0412Tests(const fs::path&,std::ostream&);
void RunV0420Tests(const fs::path&,std::ostream&);
void RunV0420UiTests(std::ostream&);
void RunV0421RegexTests(std::ostream&);
void RunV0422Tests(const fs::path&,std::ostream&);
void RunV0423Tests(const fs::path&,std::ostream&);
void RunV0423UiTests(const fs::path&,std::ostream&,const std::function<void(const wchar_t*)>&);
void RunV050UiTests(const fs::path&,std::ostream&,const std::function<void(const wchar_t*)>&);
void RunV0412UiTests(const fs::path&,std::ostream&);
void TestSelectItem(HWND list,int index,UINT state,UINT mask) {
    if(index>=0&&(state&LVIS_SELECTED)&&(mask&LVIS_SELECTED)){LVITEMW reset{};reset.stateMask=LVIS_SELECTED|LVIS_FOCUSED;SendMessageW(list,LVM_SETITEMSTATE,-1,reinterpret_cast<LPARAM>(&reset));}
    LVITEMW item{};item.state=state;item.stateMask=mask;SendMessageW(list,LVM_SETITEMSTATE,index,reinterpret_cast<LPARAM>(&item));if(index>=0&&(state&LVIS_SELECTED))ListView_EnsureVisible(list,index,FALSE);FlushSelectionUpdate();
}
int SelfTest() {
    // Unit fixtures exercise normal thumbnails independently of saved display preferences.
    app.hideFileNames=false;
    auto directory = app.settings.parent_path() / L"self-test-data";
    fs::create_directories(directory);
    std::ofstream report(directory / L"result.txt");
    try {
        RunUpdateTests(directory,report);RunV0412Tests(directory,report);
        {
            auto require=[&](bool ok,const char* message){if(!ok)throw std::runtime_error(message);report<<"PASS: "<<message<<'\n';};
            const std::wstring uuid=L"12345678-abcd-1234-abcd-123456789abc";
            stage3::Record record;record.memberStatus=L"partial";record.members={{L"A & <B>",L"usr_"+uuid},{L"A & <B>",L""}};
            XmpFields fields;std::wstring error;require(ParseXmp(stage3::RecordXml(record,true),fields,error)&&fields.people.size()==2&&fields.people[0].id==L"usr_"+uuid&&fields.people[1].id.empty(),"v0.4.16 structural names and IDs stay paired");
            PhotoSummary summary;summary.values[6]=L"wrld_"+uuid;summary.values[8]=L"Other\r\n【グループID】grp_"+uuid;summary.values[9]=L"部分的な記録\r\nA & <B>\r\nA & <B>";summary.people=fields.people;summary.memberOffset=7;
            auto links=SummaryLinks(summary);require(links.size()==3&&links.back().url==L"https://vrchat.com/home/user/usr_"+uuid&&links.back().first==7,"v0.4.16 group world and paired user URLs exclude ID-less duplicate");
            summary.values[6]=L"wrld_bad";summary.people[0].id=L"javascript:bad";require(SummaryLinks(summary).size()==1,"v0.4.16 invalid IDs never become links");
            ViewerSettings settings;require(settings.idLinks&&ParseViewerSettings("{\"version\":3}").idLinks,"v0.4.16 defaults and old settings enable links");settings.idLinks=false;require(!ParseViewerSettings(SerializeViewerSettings(settings)).idLinks,"v0.4.17 button OFF persists");require(!ParseViewerSettings("{\"version\":3,\"idLinks\":false}").idLinks&&ParseViewerSettings("{\"version\":3,\"idLinks\":false,\"pageButtons\":true}").idLinks,"v0.4.17 old link setting migrates and new button setting wins");
        }
        RunV0415Tests(directory,report);
        auto require = [&](bool ok, const char* label) { if (!ok) throw std::runtime_error(label); report << "PASS: " << label << '\n'; };
        ViewerSettings v045;v045.sideWidth=750;v045.sideHeight=420;v045.autoThumbnails=true;auto resized=ParseViewerSettings(SerializeViewerSettings(v045));require(resized.sideWidth==750&&resized.sideHeight==420&&resized.autoThumbnails,"v0.4.5 split size and auto thumbnail settings roundtrip");require(!ParseViewerSettings("{\"version\":3}").autoThumbnails,"v0.4.5 legacy settings keep manual thumbnails");
        int wheel = 0;
        require(WheelThumbnailWidth(128, wheel, 60) == 128 && WheelThumbnailWidth(128, wheel, 60) == 160, "high resolution zoom wheel accumulated");
        require(WheelThumbnailWidth(256, wheel, 240) == 256 && WheelThumbnailWidth(64, wheel, -240) == 64, "thumbnail zoom limits");
        JsonObject legacy;
        require(ReadThumbnailWidth(legacy) == 128, "v0.1.0 settings compatibility");
        legacy.Insert(L"thumbnailWidth", JsonValue::CreateNumberValue(257));
        bool rejected = false; try { ReadThumbnailWidth(legacy); } catch (...) { rejected = true; }
        require(rejected, "invalid thumbnail width rejected");
        require(IsPhotosId(L"Microsoft.Windows.Photos_8wekyb3d8bbwe!App") && !IsPhotosId(L"Other.Photos_8wekyb3d8bbwe!App") && !IsPhotosId(L"Microsoft.Windows.Photos_8wekyb3d8bbwe!"), "Photos app identity filter");
        ViewerSettings noSide;noSide.layout=L"none";require(ParseViewerSettings(SerializeViewerSettings(noSide)).layout==L"none","side view none settings round trip");
        Pixels opaque{1,1,{17,31,91,255}},transparent{1,1,{0,0,0,0}};
        for(bool dark:{false,true}){app.darkTheme=dark;auto background=ThemeBackground();
            auto pixel=[&](const Pixels& source,int x,int y){auto bitmap=Thumbnail(source,64);HDC dc=CreateCompatibleDC(nullptr);auto old=SelectObject(dc,bitmap);auto color=GetPixel(dc,x,y);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return color;};
            require(pixel(opaque,0,0)==background&&pixel(transparent,31,23)==background,"thumbnail padding and transparency use theme background");require(pixel(opaque,31,23)==RGB(91,31,17),"theme composition preserves opaque image color");}app.darkTheme=false;
        app.settings = directory / L"settings.json";
        app.settingsProtected = false;
        app.autoThumbnails=false;app.saveHistory = true; app.cacheMB = 100; app.bookmarks.clear();
        app.last = Normalize(directory.wstring()); app.layout = L"bottom"; app.recent = { app.last }; app.thumbnailWidth = 192;
        SaveSettings(); app.last.clear(); app.recent.clear(); app.layout = L"right";
        // LoadSettings uses the real EXE path, so parse the isolated round-trip file directly.
        std::ifstream input(app.settings, std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(input)), {});
        auto object = JsonObject::Parse(winrt::to_hstring(json));
        require(object.GetNamedNumber(L"version") == 4 && object.GetNamedString(L"sideView") == L"bottom" && object.GetNamedArray(L"history").Size() == 1, "settings JSON round trip");
        require(ReadThumbnailWidth(object) == 192, "thumbnail width persisted");
        auto legacySettings = ParseViewerSettings("{\"version\":1,\"lastFolder\":\"\",\"sideView\":\"right\",\"history\":[]}");
        require(legacySettings.cacheMB == 100 && legacySettings.saveHistory && legacySettings.bookmarks.empty(), "old settings migrate to 100MB history ON");
        ViewerSettings settings; settings.last = app.last; settings.history = app.recent; settings.bookmarks = app.recent; settings.cacheMB = 16;
        auto roundTrip = ParseViewerSettings(SerializeViewerSettings(settings));
        require(roundTrip.cacheMB == 16 && roundTrip.bookmarks == app.recent && roundTrip.history == app.recent, "new settings bookmarks and cache round trip");
        settings.saveHistory = false; auto off = ParseViewerSettings(SerializeViewerSettings(settings));
        require(off.last.empty() && off.history.empty() && off.bookmarks == app.recent && !off.saveHistory, "history OFF omits last folder while bookmarks persist");
        int limit = 100;
        require(ParseCacheLimitInput(L"16",limit) && limit == 16 && ParseCacheLimitInput(L"2048",limit) && limit == 2048 && !ParseCacheLimitInput(L"15",limit) && !ParseCacheLimitInput(L"2049",limit) && !ParseCacheLimitInput(L"1.5",limit) && !ParseCacheLimitInput(L"",limit) && !ParseCacheLimitInput(L"9999999999",limit), "cache input bounds and malformed values");
        bool invalidSetting = false; settings.cacheMB = 2049;
        try { ParseViewerSettings(SerializeViewerSettings(settings)); } catch (...) { invalidSetting = true; }
        require(invalidSetting, "invalid cache setting rejected transactionally");
        app.thumbnailWidth = app.activeThumbnailWidth = 128;
        require(Same(Normalize(directory.wstring() + L"\\"), Normalize(directory.wstring())), "trailing separator normalization");
        ComPtr<IWICImagingFactory> factory;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
        for (auto format : { GUID_ContainerFormatPng, GUID_ContainerFormatJpeg }) {
            auto path = directory / (format == GUID_ContainerFormatPng ? L"sample-日本語.png" : L"sample.jpg");
            {
                ComPtr<IWICStream> stream; winrt::check_hresult(factory->CreateStream(&stream)); winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
                ComPtr<IWICBitmapEncoder> encoder; winrt::check_hresult(factory->CreateEncoder(format, nullptr, &encoder)); winrt::check_hresult(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
                ComPtr<IWICBitmapFrameEncode> frame; winrt::check_hresult(encoder->CreateNewFrame(&frame, nullptr)); winrt::check_hresult(frame->Initialize(nullptr)); winrt::check_hresult(frame->SetSize(320, 160));
                WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR; winrt::check_hresult(frame->SetPixelFormat(&pixelFormat));
                require(pixelFormat == GUID_WICPixelFormat24bppBGR, "fixture pixel format");
                std::vector<BYTE> pixels(320 * 160 * 3, 120);
                winrt::check_hresult(frame->WritePixels(160, 320 * 3, static_cast<UINT>(pixels.size()), pixels.data())); winrt::check_hresult(frame->Commit()); winrt::check_hresult(encoder->Commit());
            }
            ComPtr<IWICBitmapDecoder> decoder; winrt::check_hresult(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder));
            ComPtr<IWICBitmapFrameDecode> frame; winrt::check_hresult(decoder->GetFrame(0, &frame));
            auto pixels = Decode(factory.Get(), frame.Get(), 128, 96);
            require(pixels.width == 128 && pixels.height == 64 && pixels.data.size() == 128 * 64 * 4, "aspect-preserving thumbnail decode");
            auto info = Info(decoder.Get(), frame.Get(), path.wstring());
            require(info.find(L"320 × 160") != std::wstring::npos, "file information and metadata traversal");
            for (int width : { 64, 192, 256 }) {
                auto scaled = Decode(factory.Get(), frame.Get(), width, width * 3 / 4);
                auto bitmap = Thumbnail(scaled, width); BITMAP description{};
                require(bitmap && GetObjectW(bitmap, sizeof(description), &description) && description.bmWidth == width && description.bmHeight == width * 3 / 4, "resized thumbnail canvas");
                DeleteObject(bitmap);
            }
        }
        auto broken = directory / L"broken.png";
        RunMetadataTests(directory, report);
        RunWebPTests(directory, report);
        RunExtendedXmpTests(directory, report);
        RunMetadataWriterTests(directory, report);
        RunV031Tests(directory, report);
        RunV063SettingsTests(report);RunV062StorageTests(directory,report);RunV0512Tests(directory,report);RunV0511Tests(directory,report);RunMdbTests(directory,report);RunIdNameTests(directory,report);RunMdbApplyTests(directory,report);RunV035Tests(directory,report);RunV048Tests(directory,report);stage3::RunTests(directory,report);person_ocr::RunTests(directory,report);RunV0420Tests(directory,report);RunV0421RegexTests(report);RunV0421ThemeTests(report);RunV0422Tests(directory,report);RunV0423Tests(directory,report);
{ std::ofstream output(broken, std::ios::binary); output << "invalid image"; }
        ComPtr<IWICBitmapDecoder> decoder;
        require(FAILED(factory->CreateDecoderFromFilename(broken.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)), "broken image rejected");
        std::wstring detail;
        auto absentApp = [](std::wstring&) -> HRESULT { return HRESULT_FROM_WIN32(ERROR_NOT_FOUND); };
        require(ActivatePhotos((directory / L"sample-日本語.png").wstring(), detail, absentApp) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !detail.empty(), "Photos unavailable failure reported");
        require(FAILED(ActivatePhotos((directory / L"does-not-exist.png").wstring(), detail, absentApp)), "Photos missing file rejected");
        std::wstring photos;
        auto photosHr = FindPhotosAppId(photos);
        report << "INFO: Photos discovery HRESULT=" << static_cast<unsigned>(photosHr) << " app=" << winrt::to_string(photos) << '\n';
        report << "ALL PASSED\n"; return 0;
    } catch (const std::exception& error) { report << "FAIL: " << error.what() << '\n'; }
      catch (const winrt::hresult_error& error) { report << "FAIL HRESULT: " << static_cast<unsigned>(error.code()) << '\n'; }
    return 1;
}
int PhotosTest() {
    auto directory = app.settings.parent_path() / L"self-test-data";
    fs::create_directories(directory);
    std::ofstream report(directory / L"photos-result.txt");
    try {
        std::wstring id;
        auto hr = FindPhotosAppId(id);
        if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) { report << "SKIP: Microsoft Photos is not installed for this user.\n"; return 0; }
        if (FAILED(hr)) { report << "FAIL discovery HRESULT=" << static_cast<unsigned>(hr) << '\n'; return 1; }
        report << "APP: " << winrt::to_string(id) << '\n';
        auto path = directory / L"写真 test & # 100%.png";
        fs::copy_file(directory / L"sample-日本語.png", path, fs::copy_options::overwrite_existing);
        std::wstring detail;
        std::thread launch([&] {
            auto init = RoInitialize(RO_INIT_MULTITHREADED);
            hr = FAILED(init) ? init : ActivatePhotos(path.wstring(), detail);
            if (SUCCEEDED(init)) RoUninitialize();
        });
        launch.join();
        if (FAILED(hr)) { report << "FAIL activation HRESULT=" << static_cast<unsigned>(hr) << '\n'; return 1; }
        report << "PASS: LaunchFileAsync with explicit Photos package accepted Japanese, spaces and special characters.\nVisual confirmation of the opened image remains necessary.\n";
        return 0;
    } catch (...) { report << "FAIL: test fixture or activation error\n"; return 1; }
}
bool ThumbnailsReady() { if(app.categoryMode&&ListView_IsGroupViewEnabled(app.list)){if(app.folderPending)return false;for(int i:app.wantedItems)if(!app.thumbnailDone[i])return false;return true;} if (app.folderPending || app.wantedLast < app.wantedFirst) return false; for (int i=app.wantedFirst; i<=app.wantedLast; ++i) if (!app.thumbnailDone[i]) return false; return true; }
namespace {
LRESULT CALLBACK LegacyComboDraw(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(message==WM_DRAWITEM) { ThemeDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE; }
    return DefSubclassProc(window,message,wp,lp);
}
bool ComboPopupHasText(HWND combo) {
    COMBOBOXINFO info{sizeof(info)};
    if (!GetComboBoxInfo(combo, &info) || !IsWindowVisible(info.hwndList)) return false;
    RECT r{}; GetClientRect(info.hwndList, &r);
    HDC source=GetDC(info.hwndList), dc=CreateCompatibleDC(source);
    HBITMAP bitmap=CreateCompatibleBitmap(source,r.right,r.bottom); auto old=SelectObject(dc,bitmap);
    bool printed=PrintWindow(info.hwndList,dc,PW_CLIENTONLY)!=FALSE;
    int ink=0;
    // Compare against the row background, excluding its border and focus rectangle.
    auto background=GetPixel(dc,r.right-24,11);
    for(int y=5;y<17;++y) for(int x=8;x<r.right-32;++x)
        if(GetPixel(dc,x,y)!=background)++ink;
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(info.hwndList,source);
    return printed && ink>25;
}
LRESULT CALLBACK CountLabelPaint(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    if (message == WM_PAINT) ++*reinterpret_cast<int*>(data);
    return DefSubclassProc(window, message, wp, lp);
}
struct LabelPaintProbe {
    std::array<int, 11> counts{};
    LabelPaintProbe() {
        for (size_t i = 0; i < counts.size(); ++i)
            if (!SetWindowSubclass(app.simpleLabels[i], CountLabelPaint, 32, reinterpret_cast<DWORD_PTR>(&counts[i])))
                throw std::runtime_error("label paint probe setup failed");
    }
    ~LabelPaintProbe() { for (auto label : app.simpleLabels) RemoveWindowSubclass(label, CountLabelPaint, 32); }
};
LRESULT CALLBACK CountImageLabels(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
    if(m==LVM_GETITEMTEXTW)++*reinterpret_cast<size_t*>(data);
    return DefSubclassProc(h,m,w,l);
}
struct ImageLabelProbe {
    size_t reads=0;
    ImageLabelProbe(){if(!SetWindowSubclass(app.list,CountImageLabels,41,reinterpret_cast<DWORD_PTR>(&reads)))throw std::runtime_error("image label probe setup");}
    ~ImageLabelProbe(){RemoveWindowSubclass(app.list,CountImageLabels,41);}
};
void CaptureUiTest(const fs::path& file, HWND target = nullptr, UINT printFlags = 2){bool visible=IsWindowVisible(app.window);ShowWindow(app.window,SW_SHOWNOACTIVATE);RedrawWindow(app.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);if(!target)target=app.window;RECT rect{};GetWindowRect(target,&rect);int width=rect.right-rect.left,height=rect.bottom-rect.top;HDC screen=GetDC(target),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,width,height);auto old=SelectObject(dc,bitmap);if(!PrintWindow(target,dc,printFlags))throw std::runtime_error("UI capture failed");if(!visible)ShowWindow(app.window,SW_HIDE);SelectObject(dc,old);std::vector<BYTE> bytes(size_t(width)*height*4);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;GetDIBits(dc,bitmap,0,height,bytes.data(),&info,DIB_RGB_COLORS);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(target,screen);
    ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;winrt::check_hresult(factory->CreateStream(&stream));winrt::check_hresult(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE));ComPtr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;winrt::check_hresult(encoder->CreateNewFrame(&frame,nullptr));winrt::check_hresult(frame->Initialize(nullptr));winrt::check_hresult(frame->SetSize(width,height));for(size_t i=3;i<bytes.size();i+=4)bytes[i]=255;auto format=GUID_WICPixelFormat32bppBGRA;winrt::check_hresult(frame->SetPixelFormat(&format));winrt::check_hresult(frame->WritePixels(height,width*4,static_cast<UINT>(bytes.size()),bytes.data()));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());
}
}
void CaptureV0412Ui(const fs::path& path,HWND h){CaptureUiTest(path,h,0);}
int UiTest() {
    auto directory = app.settings.parent_path();
    std::ofstream report(directory / L"ui-result.txt");
    auto require = [&](bool ok, const char* label) { if (!ok) throw std::runtime_error(label); report << "PASS: " << label << '\n'; };
    auto wait = [&](auto complete) {
        auto deadline = GetTickCount64() + 15000;
        while (!complete()) {
            if (GetTickCount64() > deadline) throw std::runtime_error("async UI timeout");
            MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { if(!IsDialogMessageW(app.window,&message)){TranslateMessage(&message); DispatchMessageW(&message);} }
        }
    };
    try {
        stage3::RunMonitorUiTests(directory,report,[&](HWND dialog){CaptureUiTest(directory/L"v048-person-settings.png",dialog);});
        RunLicenseUiTests(directory,report,[&](HWND dialog,const wchar_t* name){CaptureUiTest(directory/name,dialog);});
        RunUpdateWorkflowTests(report);
        RunUpdateDialogTests(directory,report,[&](HWND dialog){CaptureUiTest(directory/L"v046-update-dialog.png",dialog);});
        app.autoThumbnails=false;app.thumbnailWidth=128;ApplyThumbnailSize();
        auto testLedger=AppDataFile(directory,L"backups.json");if(fs::exists(testLedger)){auto records=ReadBackupLedger(testLedger);for(auto& e:records)if(Same(e.source,(directory/L"Stage2-ui-saved.jpg").wstring())&&SameFolderAddress(fs::path(e.path).parent_path().wstring(),directory.wstring()))fs::remove(e.path);fs::remove(testLedger);}
        auto source = directory / L"sample.jpg";
        auto longName = L"長いファイル名-" + std::wstring(70, L'界') + L".jpg";
        fs::copy_file(source, directory / longName, fs::copy_options::overwrite_existing);
        require(Text(AppControl( OpenPath)) == L"ロード" && Text(AppControl( Options)).find(L"オプション") == 0 && Text(AppControl( Bookmarks)).find(L"ブックマーク") == 0 && app.cacheMB == 100 && app.saveHistory, "load options bookmarks controls and defaults");
        require(!AppControl(PersonSettings)&&Text(AppControl(RecordToggle))==L"記録開始"&&!stage3::Recording()&&Text(app.simpleLabels[9])==L"同室者"&&Text(app.simpleLabels[10])==L"モデル候補","v0.4.1 recording controls start OFF and participant rows exist");
        for(int width:{760,1000}){SetWindowPos(app.window,nullptr,0,0,width,700,SWP_NOMOVE|SWP_NOZORDER);Layout();RECT placement{},button{},indicator{},gallery{},person{},help{};GetWindowRect(app.placement,&placement);GetWindowRect(AppControl(RecordToggle),&button);GetWindowRect(AppControl(RecordIndicator),&indicator);GetWindowRect(app.list,&gallery);GetWindowRect(AppControl(Options),&person);GetWindowRect(AppControl(Help),&help);require(placement.right<=button.left&&button.right<=indicator.left&&button.bottom<=gallery.top&&person.right<=help.left,"v0.4.1 toolbar wraps without overlapping recording controls");}
        for (int i = 0; i < 650; ++i) fs::copy_file(source, directory / (L"grid-" + std::to_wstring(i) + L".jpg"), fs::copy_options::overwrite_existing);
        OpenFolder(directory.wstring());
        wait([] { return !app.folderPending && !app.files.empty() && ThumbnailsReady(); });
        require(ListView_GetItemCount(app.list) >= 20, "async folder and thumbnail loading");
        require(app.thumbnailWorkers.size()>=1&&app.thumbnailWorkers.size()<=4&&app.thumbnailPeak<=app.thumbnailWorkers.size()&&(app.thumbnailWorkers.size()==1||app.thumbnailPeak>=2),"CPU bounded thumbnail workers execute concurrent image jobs");
        require(app.files.size() > 650 && app.loaded < static_cast<int>(app.files.size()) && app.wantedLast - app.wantedFirst < ThumbnailSlots, "large folder loads viewport before offscreen images");
        require(SendMessageW(app.history,CB_GETCURSEL,0,0)==0 && Same(Text(app.history),app.folder), "history refresh restores current folder selection");
        ShowWindow(app.window,SW_SHOWNOACTIVATE);
        auto selectionClick=[&](int index,bool shift){ListView_EnsureVisible(app.list,index,FALSE);RECT icon{};ListView_GetItemRect(app.list,index,&icon,LVIR_ICON);auto point=MAKELPARAM((icon.left+icon.right)/2,(icon.top+icon.bottom)/2);BYTE saved[256]{},keys[256]{};GetKeyboardState(saved);std::copy(std::begin(saved),std::end(saved),keys);keys[VK_SHIFT]=shift?0x80:0;keys[VK_CONTROL]=0;SetKeyboardState(keys);PostMessageW(app.list,WM_LBUTTONUP,shift?MK_SHIFT:0,point);SendMessageW(app.list,WM_LBUTTONDOWN,MK_LBUTTON|(shift?MK_SHIFT:0),point);SendMessageW(app.list,WM_LBUTTONUP,shift?MK_SHIFT:0,point);SetKeyboardState(saved);};
        for(int amount:{50,250,600}) {
            auto refreshes=app.selectionRefreshes;auto started=GetTickCount64();ListView_SetItemState(app.list,-1,0,LVIS_SELECTED);for(int i=0;i<amount;++i)ListView_SetItemState(app.list,i,LVIS_SELECTED,LVIS_SELECTED);
            require(ListView_GetSelectedCount(app.list)==amount&&app.selectionRefreshes==refreshes,"large native selection notifications do not synchronously refresh viewer");
            wait([&]{return !app.selectionPending;});require(app.selectionRefreshes==refreshes+1,"large selection commits viewer refresh once");
            refreshes=app.selectionRefreshes;selectionClick(3,false);report<<"INFO: click selected="<<ListView_GetSelectedCount(app.list)<<" refresh_delta="<<app.selectionRefreshes-refreshes<<'\n';require(ListView_GetSelectedCount(app.list)==1&&app.selectionRefreshes==refreshes,"single click promptly clears large selection without per-item viewer refresh");wait([&]{return !app.selectionPending;});require(app.selectionRefreshes==refreshes+1&&app.selected==3,"single click refreshes final focused image once");
            report<<"INFO: selection and single click count="<<amount<<" elapsed_ms="<<GetTickCount64()-started<<'\n';
        }
        selectionClick(0,false);wait([]{return !app.selectionPending;});selectionClick(500,true);require(ListView_GetSelectedCount(app.list)==501,"Shift click selects entire range across offscreen anchor");bool completeRange=true;for(int i=0;i<=500;++i)if(!(ListView_GetItemState(app.list,i,LVIS_SELECTED)&LVIS_SELECTED))completeRange=false;require(completeRange,"offscreen Shift range includes every intermediate image");wait([]{return !app.selectionPending;});
        selectionClick(600,false);wait([]{return !app.selectionPending;});selectionClick(100,true);require(ListView_GetSelectedCount(app.list)==501,"reverse Shift click includes offscreen intermediate range");wait([]{return !app.selectionPending;});TestSelectItem(app.list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        ListView_SetItemState(app.list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
        int selectedForEdit=0;for(int i=0;i<static_cast<int>(app.files.size())&&selectedForEdit<80;++i)if(fs::path(app.files[i]).filename().wstring().find(L"grid-")==0){ListView_SetItemState(app.list,i,LVIS_SELECTED,LVIS_SELECTED);++selectedForEdit;}
        FlushSelectionUpdate();BeginMetadataEdit();require(app.batchLoading&&!app.activityText.empty()&&Text(app.status).find(L"0 / 80")!=std::wstring::npos,"80-image edit displays loading immediately");
        auto animation=Text(app.status);SendMessageW(app.window,WM_TIMER,ActivityTimer,0);require(Text(app.status)!=animation,"loading animation advances while UI remains responsive");
        wait([]{return !app.batchLoading;});require(app.editing&&app.batchMetadata.size()==80&&app.activityText.empty(),"80-image editor loads all metadata and stops animation");
        app.editing=false;app.batchFiles.clear();app.batchMetadata.clear();UpdateEditorControls();Layout();TestSelectItem(app.list,0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        SetWindowSubclass(app.history,LegacyComboDraw,39,0);
        SendMessageW(app.history,CB_SHOWDROPDOWN,TRUE,0);
        bool legacyBlank=!ComboPopupHasText(app.history);
        SendMessageW(app.history,CB_SHOWDROPDOWN,FALSE,0);
        RemoveWindowSubclass(app.history,LegacyComboDraw,39);
        require(legacyBlank,"legacy combo draw handler reproduces invisible history text");
        for(bool dark:{false,true}) {
            app.darkTheme=dark; ApplyTheme();
            for(auto combo:{app.history,app.placement}) {
                SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);
                require(ComboPopupHasText(combo),"history and placement dropdowns contain rendered text in both themes");
                COMBOBOXINFO info{sizeof(info)};GetComboBoxInfo(combo,&info);
                CaptureUiTest(directory/(std::wstring(dark?L"combo-night-":L"combo-light-")+(combo==app.history?L"history.png":L"placement.png")),info.hwndList);
                SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);
            }
        }
        app.darkTheme=false;ApplyTheme();ShowWindow(app.window,SW_HIDE);
        auto compactSpacing = ListView_GetItemSpacing(app.list,FALSE);
        report << "INFO: compact spacing=" << LOWORD(compactSpacing) << "x" << HIWORD(compactSpacing) << '\n';
        RECT icon{}, next{}, filenameRect{};
        ListView_GetItemRect(app.list,0,&icon,LVIR_ICON); ListView_GetItemRect(app.list,1,&next,LVIR_ICON);
        require(next.left - icon.left - app.activeThumbnailWidth == 8 && next.top == icon.top, "compact grid leaves 8px between neighboring images");
        ListView_GetItemRect(app.list,0,&filenameRect,LVIR_LABEL);
        int nextRow = -1;
        for (int i=1; i<ListView_GetItemCount(app.list); ++i) { ListView_GetItemRect(app.list,i,&next,LVIR_ICON); if (next.top > icon.top) { nextRow = i; break; } }
        require(nextRow >= 0 && filenameRect.bottom <= next.top && HIWORD(compactSpacing) < 140, "compact row preserves filename space without next-row overlap");
        int longIndex = -1;
        for (int i=0; i<static_cast<int>(app.files.size()); ++i) if (fs::path(app.files[i]).filename() == longName) longIndex = i;
        require(longIndex >= 0,"long Japanese filename fixture listed");
        for (int width : { 64, 128, 256 }) {
            app.thumbnailWidth = width; ApplyThumbnailSize();
            wchar_t label[512]{}; ListView_GetItemText(app.list, longIndex, 0, label, ARRAYSIZE(label));
            HDC dc = GetDC(app.list); auto old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(app.list, WM_GETFONT, 0, 0)));
            SIZE size{}; GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &size); SelectObject(dc, old); ReleaseDC(app.list, dc);
            require(size.cx <= width && std::wstring(label).find(L"…") != std::wstring::npos, "long filename display fits thumbnail width after zoom");
            require(fs::path(app.files[longIndex]).filename() == longName, "short label preserves full processing path");
        }
        app.thumbnailWidth = 128; ApplyThumbnailSize();
        wait([] { return ThumbnailsReady(); });
        {
            ImageLabelProbe probe;
            auto elapsed=[&](bool rebuild){LARGE_INTEGER start{},end{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);for(int i=0;i<10;++i){if(rebuild)app.imageLabelsDirty=true;ArrangeImages();}QueryPerformanceCounter(&end);return 1000.0*(end.QuadPart-start.QuadPart)/frequency.QuadPart;};
            auto full=elapsed(true);require(probe.reads==app.files.size()*10,"v0.4.18 reference recomputes every filename label");
            probe.reads=0;auto reused=elapsed(false);require(probe.reads==0,"v0.4.18 repeated arrangement skips unchanged filename labels");
            Layout();require(probe.reads==0,"v0.4.18 unchanged layout reuses filename labels");
            report<<"INFO: v0.4.18 labels="<<app.files.size()<<" arrangements=10 full_recalculation_ms="<<full<<" reused_ms="<<reused<<'\n';
            auto oldFont=SendMessageW(app.list,WM_GETFONT,0,0);auto font=CreateFontW(22,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");require(font!=nullptr,"v0.4.18 font fixture created");
            SendMessageW(app.list,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);ArrangeImages();require(probe.reads==app.files.size(),"v0.4.18 changed font rebuilds labels");
            SendMessageW(app.list,WM_SETFONT,oldFont,FALSE);ArrangeImages();DeleteObject(font);
        }
        wchar_t tooltip[512]{}; NMLVGETINFOTIPW tip{};
        tip.hdr = {app.list,Images,LVN_GETINFOTIPW}; tip.iItem = longIndex; tip.pszText = tooltip; tip.cchTextMax = ARRAYSIZE(tooltip);
        SendMessageW(app.window,WM_NOTIFY,Images,reinterpret_cast<LPARAM>(&tip));
        require(std::wstring(tooltip) == longName,"infotip contains full Japanese filename and extension");
        auto brokenItem = std::find_if(app.files.begin(), app.files.end(), [](auto& path) { return fs::path(path).filename() == L"broken.png"; }); require(brokenItem != app.files.end(), "broken thumbnail fixture listed"); tip.iItem = static_cast<int>(brokenItem - app.files.begin()); ListView_EnsureVisible(app.list, tip.iItem, FALSE); ScheduleThumbnails(); wait([&] { return app.thumbnailDone[tip.iItem]; }); SendMessageW(app.window,WM_NOTIFY,Images,reinterpret_cast<LPARAM>(&tip));
        require(std::wstring(tooltip).find(L"読込みエラー") != std::wstring::npos,"infotip retains failed-thumbnail status");
        int imageFixtureIndex=int(app.directories.size())+3;TestSelectItem(app.list, imageFixtureIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        wait([] { return !app.previewPixels.data.empty(); });
        wait([] { return Text(app.details).find(L"読み込んでいます") == std::wstring::npos; });
        auto selected = app.files[app.selected]; auto details = Text(app.details);
        auto rowCount = [] {
            POINT origin{}, position{}; ListView_GetItemPosition(app.list, 0, &origin);
            int count = 0;
            for (int i = 0; i < ListView_GetItemCount(app.list); ++i) { ListView_GetItemPosition(app.list, i, &position); if (position.y == origin.y) ++count; }
            return count;
        };
        for (auto layout : { L"right", L"left", L"bottom" }) {
            app.layout = layout;
            SetWindowPos(app.window, nullptr, 0, 0, 820, 680, SWP_NOMOVE | SWP_NOZORDER); Layout();
            RECT smallArea{}; GetClientRect(app.list, &smallArea); auto smallColumns = rowCount();
            SetWindowPos(app.window, nullptr, 0, 0, 1480, 900, SWP_NOMOVE | SWP_NOZORDER); Layout();
            RECT large{}; GetClientRect(app.list, &large); auto largeColumns = rowCount();
            require(large.right > smallArea.right && large.bottom > smallArea.bottom && largeColumns > smallColumns, "list area and columns follow window size");
            require(app.selected == imageFixtureIndex && Same(app.files[app.selected], selected), "selection retained on resize");
        }
        wait([] { return ThumbnailsReady(); });
        auto cacheHits = app.cache.hits.load();
        SendMessageW(app.list, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
        SendMessageW(app.window, WM_TIMER, ZoomTimer, 0);
        wait([] { return ThumbnailsReady(); });
        int width = 0, height = 0; ImageList_GetIconSize(app.thumbs, &width, &height);
        require(width == 160 && height == 120 && app.activeThumbnailWidth == 160, "Ctrl wheel changes thumbnail image list");
        require(app.cache.hits > cacheHits, "Ctrl wheel reuses decoded thumbnail cache");
        require(app.selected == imageFixtureIndex && Text(app.details) == details, "zoom keeps selection and metadata");
        SendMessageW(app.list, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        require(app.thumbnailWidth == 160, "plain wheel does not zoom");
        SendMessageW(app.list, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, -WHEEL_DELTA), 0);
        SendMessageW(app.window, WM_TIMER, ZoomTimer, 0);
        Job stale = ThumbnailJob(0); --stale.thumbnailGeneration;
        require(!CurrentJob(stale), "old thumbnail size result rejected");
        OpenFolder(directory.wstring(), true);
        wait([] { return !app.folderPending && ThumbnailsReady() && !app.previewPixels.data.empty(); });
        require(app.activeThumbnailWidth == 128 && Same(app.files[app.selected], selected), "zoom and reload retain selected image");
        NMITEMACTIVATE blank{}; blank.hdr.hwndFrom = app.list; blank.hdr.code = NM_DBLCLK; blank.iItem = -1;
        SendMessageW(app.window, WM_NOTIFY, Images, reinterpret_cast<LPARAM>(&blank));
        { std::lock_guard<std::mutex> lock(app.mutex);
          require(std::none_of(app.jobs.begin(), app.jobs.end(), [](auto& job) { return job.kind == 3; }), "blank double click does not launch Photos"); }
        require(TabCtrl_GetItemCount(app.metadataTabs) == 3 && TabCtrl_GetCurSel(app.metadataTabs) == 0, "simple/details/OCR tabs created with simple default");
        int xmpIndex = -1;
        for (int i = 0; i < static_cast<int>(app.files.size()); ++i) if (fs::path(app.files[i]).filename() == L"xmp-日本語.png") xmpIndex = i;
        require(xmpIndex >= 0, "XMP UI fixture found");
        TestSelectItem(app.list, xmpIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        wait([] { return !app.previewPixels.data.empty() && app.summary.values[4] == L"撮影者 & テスト"; });
        require(Text(app.simpleValues[0]) == L"xmp-日本語.png" && Text(app.simpleValues[2]) == L"320 × 160 px" && Text(app.simpleValues[3]).find(L"XMP") != std::wstring::npos && Text(app.simpleValues[5]) == L"日本語ワールド" && Text(app.simpleValues[6]) == L"wrld_test" && Text(app.simpleValues[7]).empty(), "simple tab eight fields match selected image");
        auto selectionGeneration = app.selection; auto summary = app.summary; auto detailedText = Text(app.details);
        for (int tab : { 1,0,1 }) {
            TabCtrl_SetCurSel(app.metadataTabs, tab);
            NMHDR notification{ app.metadataTabs, MetadataTabs, TCN_SELCHANGE };
            SendMessageW(app.window, WM_NOTIFY, MetadataTabs, reinterpret_cast<LPARAM>(&notification));
            require(app.selection == selectionGeneration && app.summary.values == summary.values && Text(app.details) == detailedText && ((GetWindowLongPtrW(app.details, GWL_STYLE) & WS_VISIBLE) != 0) == (tab == 1), "tab switch uses same result without rereading");
        }
        auto longSummary = summary;longSummary.values[7]=L"最終モデル行";for(auto& v:longSummary.values)if(v.empty())v=L"検証値"; longSummary.values[5] = std::wstring(3000, L'界');
        ShowSummary(longSummary); TabCtrl_SetCurSel(app.metadataTabs, 0);
        for (auto layout : { L"right", L"left", L"bottom" }) {
            app.layout = layout; Layout();
            RECT start{}; GetWindowRect(app.simpleValues[0], &start);
            bool aligned = true;
            for (auto value : app.simpleValues) { RECT rect{}; GetWindowRect(value, &rect); aligned = aligned && rect.left == start.left; }
            require(aligned && SendMessageW(app.simpleValues[5], EM_GETLINECOUNT, 0, 0) > 1, "simple labels align and long Japanese value wraps in each layout");
            SendMessageW(app.simple, WM_VSCROLL, SB_BOTTOM, 0);
            RECT model{}, page{}; GetWindowRect(app.simpleValues[10], &model); GetWindowRect(app.side, &page);
            require(app.sideScroll > 0 && model.top >= page.top && model.bottom <= page.bottom, "simple pane scroll exposes final visible field");
        }
        ShowSummary(summary);
        // Observe normal WM_PAINT delivery before the screenshot helper forces repainting.
        ShowWindow(app.window, SW_SHOWNOACTIVATE);
        for (bool dark : { false, true }) {
            app.darkTheme = dark; ApplyTheme();
            for (auto placement : { L"left", L"right", L"bottom" }) {
                app.layout = placement; Layout();
                ShowSummary(summary);
                RedrawWindow(app.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                LabelPaintProbe probe;
                ShowSummary({}, L"メタデータを読み込んでいます…");
                wait([&] { RECT pane{}; GetWindowRect(app.simple, &pane); for (size_t i = 0; i < probe.counts.size(); ++i) { RECT label{}, overlap{}; GetWindowRect(app.simpleLabels[i], &label); if ((GetWindowLongPtrW(app.simpleLabels[i],GWL_STYLE)&WS_VISIBLE)&&IntersectRect(&overlap, &pane, &label) && !probe.counts[i]) return false; } return true; });
                probe.counts.fill(0);
                // Labels have unchanged text and positions after a new result arrives.
                ShowSummary(summary);
                wait([&] { RECT pane{}; GetWindowRect(app.simple, &pane); for (size_t i = 0; i < probe.counts.size(); ++i) { RECT label{}, overlap{}; GetWindowRect(app.simpleLabels[i], &label); if ((GetWindowLongPtrW(app.simpleLabels[i],GWL_STYLE)&WS_VISIBLE)&&IntersectRect(&overlap, &pane, &label) && !probe.counts[i]) return false; } return true; });
                require(true, "simple labels repaint after loading and result without forced redraw");
                CaptureUiTest(directory / (std::wstring(dark ? L"simple-night-" : L"simple-light-") + placement + L".png"));
            }
        }
        ShowWindow(app.window, SW_HIDE);
        app.darkTheme = false; ApplyTheme();
        int webpIndex = -1;
        for (int i=0; i<static_cast<int>(app.files.size()); ++i) if (fs::path(app.files[i]).filename() == L"webp-透過.WEBP") webpIndex = i;
        require(webpIndex >= 0, "uppercase WebP included in enumeration");
        TestSelectItem(app.list, webpIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        wait([] { return !app.previewPixels.data.empty() && app.summary.values[4] == L"WebP撮影者"; });
        require(app.summary.values[2] == L"320 × 160 px" && Text(app.details).find(L"WebP") != std::wstring::npos, "WebP selected preview and metadata");
        for (int i=0; i<static_cast<int>(app.files.size()); i+=40) {
            ListView_EnsureVisible(app.list, i, FALSE); ScheduleThumbnails(); wait([] { return ThumbnailsReady(); });
            require(ImageList_GetImageCount(app.thumbs) <= ThumbnailSlots + 1 && app.cache.Bytes() <= ThumbnailCache::Limit, "scroll keeps thumbnail resources bounded");
        }
        int restoredIndex = -1;
        for (int i=0; i<static_cast<int>(app.files.size()); ++i) if (fs::path(app.files[i]).filename() == L"grid-0.jpg") restoredIndex = i;
        require(restoredIndex >= 0, "scroll restoration fixture found");
        ListView_EnsureVisible(app.list, restoredIndex, FALSE); ScheduleThumbnails(); wait([] { return ThumbnailsReady(); });
        require(app.thumbnailDone[restoredIndex] && app.itemSlots[restoredIndex] >= 0, "evicted thumbnail restored when scrolling back");
        { std::lock_guard<std::mutex> lock(app.mutex);
          require(app.results.size() <= 32 && app.jobs.size() <= ThumbnailSlots + 2, "result buffers and pending jobs bounded"); }
        app.thumbnailWidth = 256; ApplyThumbnailSize(); app.thumbnailWidth = 64; ApplyThumbnailSize();
        OpenFolder(directory.wstring(), true); wait([] { return ThumbnailsReady() && !app.previewPixels.data.empty(); });
        require(app.activeThumbnailWidth == 64 && Same(app.files[app.selected], (directory / L"webp-透過.WEBP").wstring()), "rapid zoom and reload discard stale work");
        auto readSettings = [&] {
            std::ifstream input(app.settings, std::ios::binary); return ParseViewerSettings(std::string((std::istreambuf_iterator<char>(input)), {}));
        };
        auto bookmarkFolder = directory / L"bookmark-自動試験 & #"; fs::create_directories(bookmarkFolder);
        auto generation = app.generation;
        require(AddBookmark(bookmarkFolder.wstring()) && !AddBookmark(bookmarkFolder.wstring() + L"\\") && app.generation == generation && app.bookmarks.size() == 1, "bookmark add deduplicates without loading folder");
        auto menu = BuildBookmarkMenu(); wchar_t label[32768]{};
        GetMenuStringW(menu,1000,label,ARRAYSIZE(label),MF_BYCOMMAND);
        require(std::wstring(label).find(L"&&") != std::wstring::npos && GetSubMenu(menu,GetMenuItemCount(menu)-1), "bookmark menu paths escape ampersands and include deletion submenu"); DestroyMenu(menu);
        ToggleHistorySaving(); auto offSettings = readSettings();
        require(!offSettings.saveHistory && offSettings.history.empty() && offSettings.last.empty() && offSettings.bookmarks.size() == 1 && app.recent.size() == 1, "history OFF clears stored history and keeps bookmarks independent");
        menu = BuildOptionsMenu(); require(!(GetMenuState(menu,2,MF_BYCOMMAND) & MF_CHECKED), "options menu reflects history OFF"); DestroyMenu(menu);
        SetWindowTextW(app.path,bookmarkFolder.c_str()); SendMessageW(app.window,WM_COMMAND,OpenPath,0);
        wait([&] { return !app.folderPending && SameFolderAddress(app.folder,bookmarkFolder.wstring()); });
        require(app.files.empty() && readSettings().history.empty(), "load button uses edited address while history OFF");
        LoadAddress(directory.wstring()); wait([] { return ThumbnailsReady(); });
        auto webpPath = (directory / L"webp-透過.WEBP").wstring();
        for (int i=0; i<static_cast<int>(app.files.size()); ++i) if (Same(app.files[i],webpPath)) TestSelectItem(app.list,i,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
        wait([] { return !app.previewPixels.data.empty(); });
        auto selectedPath = app.files[app.selected]; auto cacheEpoch = app.cache.Ticket();
        SetWindowTextW(app.path,directory.c_str()); SendMessageW(app.window,WM_COMMAND,OpenPath,0);
        wait([] { return ThumbnailsReady() && !app.previewPixels.data.empty(); });
        require(Same(app.files[app.selected],selectedPath) && app.cache.Ticket() > cacheEpoch, "same folder load reloads cache and preserves selection");
        generation = app.generation; LoadAddress(L""); LoadAddress((directory / L"not-a-folder").wstring());
        require(app.generation == generation && Same(app.files[app.selected],selectedPath), "invalid or empty load preserves list and selection");
        fs::remove(bookmarkFolder); LoadAddress(app.bookmarks[0]);
        require(app.bookmarks.size() == 1 && app.generation == generation, "unavailable bookmark retained after load error");
        RemoveBookmark(0); require(app.bookmarks.empty() && readSettings().bookmarks.empty(), "bookmark deletion persists");
        ToggleHistorySaving(); require(readSettings().saveHistory && !readSettings().history.empty(), "history ON resumes saving current session");
        menu = BuildOptionsMenu(); require((GetMenuState(menu,2,MF_BYCOMMAND) & MF_CHECKED) != 0, "options menu reflects history ON"); DestroyMenu(menu);
        int dialogMB = 100;
        auto timer = SetTimer(nullptr,0,50,[](HWND,UINT,UINT_PTR id,DWORD) {
            KillTimer(nullptr,id); auto dialog = GetActiveWindow();
            SetDlgItemTextW(dialog,100,L"16"); SendMessageW(dialog,WM_COMMAND,IDOK,0);
        });
        require(timer && ShowCacheLimitDialog(dialogMB) == IDOK && dialogMB == 16, "cache option native dialog accepts entered value");
        dialogMB = 100;
        timer = SetTimer(nullptr,0,50,[](HWND,UINT,UINT_PTR id,DWORD) { KillTimer(nullptr,id); SendMessageW(GetActiveWindow(),WM_COMMAND,IDCANCEL,0); });
        require(timer && ShowCacheLimitDialog(dialogMB) == IDCANCEL && dialogMB == 100, "cache option cancel keeps existing value");
        ChangeCacheLimit(16); app.thumbnailWidth = 256; ApplyThumbnailSize(); wait([] { return ThumbnailsReady(); });
        require(app.cacheMB == 16 && app.slotLimit < ThumbnailSlots && app.cache.Bytes() + ImageListBytes(app.thumbs) <= 16000000 && readSettings().cacheMB == 16 && Same(app.files[app.selected],selectedPath), "cache decrease bounds LRU plus actual ImageList storage and keeps selection");
        SetWindowPos(app.window,nullptr,0,0,2200,1600,SWP_NOMOVE | SWP_NOZORDER); Layout(); ListView_EnsureVisible(app.list,0,FALSE); ScheduleThumbnails(); wait([] { return ThumbnailsReady(); });
        size_t pending = 0; { std::lock_guard<std::mutex> lock(app.mutex); pending = app.jobs.size(); }
        require(app.thumbnailBudgetLimited && pending < ThumbnailSlots && app.cache.Bytes() + ImageListBytes(app.thumbs) <= 16000000, "viewport larger than cache budget finishes without generation loop");
        ChangeCacheLimit(100); app.thumbnailWidth = 128; ApplyThumbnailSize(); wait([] { return ThumbnailsReady(); });
        require(app.cache.Bytes() + ImageListBytes(app.thumbs) <= 100000000 && readSettings().cacheMB == 100, "cache increase and zoom keep 100MB total pixel budget");
        SetWindowPos(app.window,nullptr,0,0,760,520,SWP_NOMOVE | SWP_NOZORDER); Layout();
        RECT history{}, bookmarks{}, options{}, placement{}; GetWindowRect(app.history,&history); GetWindowRect(AppControl(Bookmarks),&bookmarks); GetWindowRect(AppControl(Options),&options); GetWindowRect(app.placement,&placement);
        require(bookmarks.right < options.left && (options.right < placement.left || options.bottom <= placement.top), "top controls do not overlap at minimum window size");
        int extendedIndex = -1;
        for (int i=0; i<static_cast<int>(app.files.size()); ++i) if (fs::path(app.files[i]).filename() == L"extended-日本語.jpg") extendedIndex = i;
        require(extendedIndex >= 0,"Extended XMP UI fixture found");
        TestSelectItem(app.list,extendedIndex,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
        wait([] { return !app.previewPixels.data.empty() && app.summary.values[4] == L"拡張撮影者"; });
        require(Text(app.simpleValues[5]) == L"拡張ワールド" && Text(app.details).find(L"検証成功") != std::wstring::npos,"Extended XMP updates simple and detailed tabs");
        int editableIndex=-1;
        for(int i=0;i<static_cast<int>(app.files.size());++i)if(fs::path(app.files[i]).filename()==L"sample.jpg")editableIndex=i;
        require(editableIndex>=0,"Stage2 editable source found");
        ListView_EnsureVisible(app.list,editableIndex,FALSE);ScheduleThumbnails();wait([]{return ThumbnailsReady();});
        auto displayed=DisplayedThumbnail(editableIndex);require(!displayed.data.empty(),"selected thumbnail exists before metadata read");
        app.metadataDelayMs=500;
        TestSelectItem(app.list,editableIndex,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        require(!app.metadataReady && !app.previewDedicated && !app.previewPixels.data.empty(),"v0.5.0 unpadded cached preview is available before delayed metadata");
        require(!IsWindowEnabled(app.editButton),"edit disabled during metadata loading");
        wait([]{return app.metadataReady;});app.metadataDelayMs=0;
        TabCtrl_SetCurSel(app.metadataTabs,1);Layout();
        SendMessageW(app.window,WM_COMMAND,EditMetadata,0);
        require(app.editing && !IsWindowEnabled(app.saveButton),"detail edit starts without pending changes");
        ShowWindow(app.window,SW_SHOWNOACTIVATE);
        auto editChoice=GetDlgItem(app.editorUi.Window(),1100);
        for(bool dark:{false,true}) {
            app.darkTheme=dark;ApplyTheme();SendMessageW(editChoice,CB_SHOWDROPDOWN,TRUE,0);
            require(ComboPopupHasText(editChoice),"metadata edit dropdown contains rendered text in both themes");
            COMBOBOXINFO info{sizeof(info)};GetComboBoxInfo(editChoice,&info);
            CaptureUiTest(directory/(dark?L"combo-night-edit.png":L"combo-light-edit.png"),info.hwndList);
            SendMessageW(editChoice,CB_SHOWDROPDOWN,FALSE,0);
        }
        app.darkTheme=false;ApplyTheme();ShowWindow(app.window,SW_HIDE);
        auto authorInput=GetDlgItem(app.editorUi.Window(),1001);
        SetWindowTextW(authorInput,L"編集UI & 日本語");
        require(app.editorUi.Read().Changed() && IsWindowEnabled(app.saveButton),"editing an input marks change and enables save");
        auto editedPath=app.files[app.selected];auto editedSelection=app.selected;
        app.confirmChanges=[]{return 3;};
        SendMessageW(app.window,WM_CLOSE,0,0);
        require(!app.stopping && app.editing,"cancel unsaved close preserves running editor");
        TestSelectItem(app.list,editableIndex==0?1:0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        require(app.selected==editedSelection && app.editing && Text(authorInput)==L"編集UI & 日本語","cancel unsaved image switch preserves selected edit");
        OpenFolder(directory.wstring(),true);
        require(app.editing && app.selected==editedSelection && Text(authorInput)==L"編集UI & 日本語" && !app.folderPending,"cancel unsaved folder switch retains edit");
        app.confirmChanges=[]{return 1;};app.chooseSavePath=[]{return std::wstring();};
        OpenFolder(directory.wstring(),true);
        require(app.editing && !app.saving && !app.folderPending,"cancel save destination blocks transition and keeps edit");
        app.chooseSavePath=[&]{return (directory/L"absent-directory"/L"failure.jpg").wstring();};
        OpenFolder(directory.wstring(),true);wait([]{return !app.saving;});
        require(app.editing && app.editorUi.Read().Changed() && !app.folderPending && Text(app.editStatus).find(L"保存失敗")!=std::wstring::npos,"save failure preserves edits and cancels transition");
        auto uiOutput=directory/L"Stage2-ui-saved.jpg";fs::remove(uiOutput);
        app.chooseSavePath=[&]{return uiOutput.wstring();};app.afterSave={};StartSave();
        require(!app.activityText.empty()&&Text(app.status).find(L"保存")!=std::wstring::npos,"single save shows activity immediately");require(app.saving && !GuardTransition([]{}) && !IsWindowEnabled(app.cancelButton),"save blocks transitions and multiple saves");
        wait([&]{return !app.saving && !app.folderPending && app.selected>=0 && Same(app.files[app.selected],uiOutput.wstring()) && app.metadataReady;});
        require(Text(app.simpleValues[4])==L"編集UI & 日本語" && !app.editing,"save reloads and selects completed copy");
        int photosRequests=0;std::wstring photosPath;app.photosRequest=[&](const std::wstring& path){++photosRequests;photosPath=path;};
        SendMessageW(app.preview,WM_LBUTTONDBLCLK,0,0);
        require(photosRequests==1 && Same(photosPath,uiOutput.wstring()),"preview double click submits selected saved file once");
        NMITEMACTIVATE click{};click.hdr={app.list,Images,NM_DBLCLK};click.iItem=app.selected;SendMessageW(app.window,WM_NOTIFY,Images,reinterpret_cast<LPARAM>(&click));
        require(photosRequests==2 && Same(photosPath,uiOutput.wstring()),"list shares same Photos request with preview");app.photosRequest={};
        SendMessageW(app.window,WM_COMMAND,EditMetadata,0);SetWindowTextW(authorInput,L"discard");app.confirmChanges=[]{return 2;};
        OpenFolder(directory.wstring(),true);wait([]{return !app.folderPending;});require(!app.editing,"discard unsaved changes permits reload");
        app.confirmChanges={};app.chooseSavePath={};
        wchar_t comboText[100]{};SendMessageW(app.placement,CB_GETLBTEXT,0,reinterpret_cast<LPARAM>(comboText));require(std::wstring(comboText)==L"サイドビュー: 右","owner-drawn placement retains Japanese item strings");
        require(app.autoBackup&&app.backupLimit==10&&!app.darkTheme&&!app.metadataSearch,"v0.3.1 option defaults");
        require(TestBackupCheckboxes(),"native backup checkboxes exclude unchecked files and disable zero-selection delete");
        auto completeCount=app.files.size();SetWindowTextW(app.searchEdit,L"grid-64");
        require(app.files.size()==completeCount&&!app.searching,"search typing does not filter list");
        SendMessageW(app.window,WM_COMMAND,SearchButton,0);wait([]{return !app.searching;});
        require(!app.files.empty()&&app.files.size()<completeCount&&std::all_of(app.files.begin(),app.files.end(),[](auto& p){return app.folderItems.count(p)||SearchContains(fs::path(p).filename().wstring(),L"grid-64");}),"search button commits filename filter");
        SetWindowTextW(app.searchEdit,L"");SendMessageW(app.window,WM_COMMAND,SearchButton,0);wait([]{return !app.searching;});require(app.files.size()==completeCount,"empty search restores full folder list");
        auto sub=directory/L"search-subfolder";fs::create_directories(sub);fs::copy_file(source,sub/L"nested-only.jpg",fs::copy_options::overwrite_existing);
        StartSearch(L"nested-only");wait([]{return !app.searching;});require(VisibleImages().empty(),"search does not descend into subfolders");
        app.metadataSearch=true;StartSearch(L"拡張撮影者");wait([]{return !app.searching;});require(std::any_of(app.files.begin(),app.files.end(),[](auto& p){return fs::path(p).filename()==L"extended-日本語.jpg";}),"metadata search matches Extended XMP author");
        StartSearch(L"no-match");CancelSearch();StartSearch(L"WebP撮影者");wait([]{return !app.searching;});require(std::any_of(app.files.begin(),app.files.end(),[](auto& p){return fs::path(p).filename()==L"webp-透過.WEBP";})&&app.appliedQuery==L"WebP撮影者","cancelled search cannot replace newer metadata results");
        app.metadataSearch=false;StartSearch(L"");wait([]{return !app.searching;});
        for(int i=0;i<static_cast<int>(app.files.size());++i)if(fs::path(app.files[i]).filename()==L"xmp-日本語.png")TestSelectItem(app.list,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        wait([]{return app.metadataReady;});SetWindowPos(app.window,nullptr,0,0,1180,800,SWP_NOMOVE|SWP_NOZORDER);Layout();
        for(bool dark:{true,false}){app.darkTheme=dark;ApplyTheme();require(ListView_GetBkColor(app.list)==ThemeBackground()&&ListView_GetTextColor(app.list)==ThemeForeground(),"theme updates list background and text");for(auto layout:{L"left",L"right",L"bottom"}){app.layout=layout;Layout();RECT r{};GetWindowRect(app.details,&r);require(r.right>r.left&&r.bottom>r.top,"side pane remains valid after theme and placement changes");CaptureUiTest(directory/(std::wstring(dark?L"night-":L"light-")+layout+L".png"));}}
        StartSearch(L"Stage2-ui-saved");wait([]{return !app.searching;});require(VisibleImages().size()==1,"overwrite UI fixture found through filter");
        TestSelectItem(app.list,int(app.directories.size()),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);wait([]{return app.metadataReady;});
        SendMessageW(app.window,WM_COMMAND,EditMetadata,0);SetWindowTextW(authorInput,L"上書きUI & 検証");app.chooseSaveMode=[]{return 1;};app.confirmChanges=[]{return 3;};
        auto beforeSearch=app.searchGeneration;StartSearch(L"grid");require(app.searchGeneration==beforeSearch&&app.editing,"cancel unsaved search retains edited image");
        auto originalHash=MetadataFingerprint(uiOutput.wstring());StartSave();wait([&]{return !app.saving&&!app.folderPending&&!app.searching&&app.selected>=0&&app.metadataReady&&app.summary.values[4]==L"上書きUI & 検証";});
        require(!app.files.empty()&&Same(app.files[app.selected],uiOutput.wstring())&&!app.editing&&app.appliedQuery==L"Stage2-ui-saved","overwrite reload preserves filtered selected file");
        auto ledger=AppDataFile(directory,L"backups.json");auto backupEntries=ReadBackupLedger(ledger);require(std::any_of(backupEntries.begin(),backupEntries.end(),[&](auto& e){return Same(e.source,uiOutput.wstring())&&e.sourceFingerprint==originalHash&&MetadataFingerprint(e.path)==e.fingerprint&&InspectEditable(e.path).values[5].find(L"【AutoBackup】")!=std::wstring::npos;}),"overwrite UI creates tagged backup before replacing original");
        for(auto& e:backupEntries)fs::remove(e.path);fs::remove(ledger);app.chooseSaveMode={};app.confirmChanges={};StartSearch(L"");wait([]{return !app.searching;});
        auto v3=readSettings();app.autoBackup=false;app.backupLimit=7;app.darkTheme=true;app.metadataSearch=true;SaveSettings();v3=readSettings();require(!v3.autoBackup&&v3.backupLimit==7&&v3.darkTheme&&v3.metadataSearch,"v0.3.1 options persist independently");app.autoBackup=true;app.backupLimit=10;app.darkTheme=false;app.metadataSearch=false;SaveSettings();ApplyTheme();
        auto checked=directory/L"AB_ui-checked.jpg",unchecked=directory/L"AB_ui-unchecked.jpg";fs::copy_file(source,checked,fs::copy_options::overwrite_existing);fs::copy_file(source,unchecked,fs::copy_options::overwrite_existing);OpenFolder(directory.wstring(),true);wait([]{return !app.folderPending&&!app.searching;});
        auto checkedItem=std::find_if(app.files.begin(),app.files.end(),[&](auto& p){return Same(p,checked.wstring());});require(checkedItem!=app.files.end(),"backup deletion fixture listed");
        TestSelectItem(app.list,static_cast<int>(checkedItem-app.files.begin()),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);wait([]{return app.metadataReady;});
        auto candidates=FindBackups(app.folder,AppDataFile(directory,L"backups.json"));
        SendMessageW(app.window,WM_COMMAND,EditMetadata,0);SetWindowTextW(authorInput,L"削除前の未保存変更");app.confirmChanges=[]{return 3;};
        SubmitBackupDeletion(candidates,{checked.wstring()});require(!app.backupBusy&&app.editing&&fs::exists(checked),"cancel unsaved selected-backup deletion preserves original and edit");
        app.confirmChanges=[]{return 2;};SubmitBackupDeletion(candidates,{checked.wstring()});require(app.backupBusy&&app.selected<0&&!app.editing,"selected-backup deletion clears preview after discard confirmation");app.confirmChanges={};wait([]{return !app.backupBusy&&!app.folderPending&&!app.searching;});require(!fs::exists(checked)&&fs::exists(unchecked),"background backup deletion removes checked file and retains unchecked file");wait([&]{std::error_code cleanupError;fs::remove(unchecked,cleanupError);if(cleanupError&&cleanupError.value()!=ERROR_SHARING_VIOLATION)throw std::system_error(cleanupError);return !cleanupError;});fs::remove(AppDataFile(directory,L"backups.json"));
        auto batchUi=directory/L"v035-ui-batch",batchCopies=batchUi/L"copies";fs::create_directories(batchCopies);
        for(auto name:{L"sample.jpg",L"sample-日本語.png",L"v035-source-0.webp"}){fs::copy_file(directory/name,batchUi/name,fs::copy_options::overwrite_existing);auto p=fs::path(name);fs::remove(batchCopies/(p.stem().wstring()+L"_edited"+p.extension().wstring()));}
        OpenFolder(batchUi.wstring());wait([]{return !app.folderPending&&ThumbnailsReady();});
        require(!(GetWindowLongPtrW(app.list,GWL_STYLE)&LVS_SINGLESEL),"list supports Ctrl and Shift multiple selection");
        app.updatingSelection=true;
        for(int i=0;i<3;++i){LVITEMW item{};item.state=LVIS_SELECTED;item.stateMask=LVIS_SELECTED;SendMessageW(app.list,LVM_SETITEMSTATE,i+app.directories.size(),reinterpret_cast<LPARAM>(&item));}
        app.updatingSelection=false;Select(int(app.directories.size()));wait([]{return app.metadataReady;});
        BeginMetadataEdit();wait([]{return !app.batchLoading;});
        require(app.editing&&app.batchFiles.size()==3&&ListView_GetSelectedCount(app.list)==3,"three mixed-format selected images enter batch editor");
        require(!IsWindowEnabled(GetDlgItem(app.editorUi.Window(),1000))&&app.editorUi.Read().actions[0]==EditAction::Keep&&Text(GetDlgItem(app.editorUi.Window(),1001)).empty(),"batch capture date disabled and mixed author is not a literal saved value");
        SetWindowTextW(GetDlgItem(app.editorUi.Window(),1001),L"一括UI撮影者");SetWindowTextW(GetDlgItem(app.editorUi.Window(),1005),L"UI追加タグ");SendMessageW(GetDlgItem(app.editorUi.Window(),1105),CB_SETCURSEL,3,0);
        for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();for(auto layout:{L"left",L"right",L"bottom"}){app.layout=layout;Layout();CaptureUiTest(directory/(std::wstring(dark?L"batch-night-":L"batch-light-")+layout+L".png"));}}
        app.darkTheme=false;app.layout=L"right";ApplyTheme();Layout();
        app.confirmChanges=[]{return 3;};TestSelectItem(app.list,int(app.directories.size()),0,LVIS_SELECTED);require(app.editing&&ListView_GetSelectedCount(app.list)==3,"cancel unsaved batch deselection preserves selected targets");app.confirmChanges={};
        app.chooseSaveMode=[]{return 2;};app.chooseBatchFolder=[&]{return batchCopies.wstring();};app.confirmBatch=[](const Job&){return true;};
        auto batchBefore=app.batchMetadata;StartSave();require(!app.activityText.empty(),"batch save shows preflight activity immediately");require(app.saving&&IsWindowEnabled(app.cancelButton),"batch save enables between-file cancellation");
        wait([]{return !app.saving&&!app.folderPending&&ThumbnailsReady();});require(app.activityText.empty(),"batch save completion stops animation");
        require(!app.editing&&app.files.size()==3&&ListView_GetSelectedCount(app.list)==3,"batch copy reload restores multiple selection");
        for(auto& path:app.files){auto saved=InspectEditable(path);require(saved.values[1]==L"一括UI撮影者"&&saved.values[5].find(L"UI追加タグ")!=std::wstring::npos,"batch UI applies author and appends searchable tags");}
        app.metadataSearch=true;SetWindowTextW(app.searchEdit,L"UI追加タグ");SetFocus(app.searchEdit);
        MSG enter{};enter.hwnd=app.searchEdit;enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;
        require((SendMessageW(app.searchEdit,WM_GETDLGCODE,VK_RETURN,reinterpret_cast<LPARAM>(&enter))&DLGC_WANTMESSAGE)!=0,"search edit requests Enter from dialog manager");
        MSG tab=enter;tab.wParam=VK_TAB;require(!(SendMessageW(app.searchEdit,WM_GETDLGCODE,VK_TAB,reinterpret_cast<LPARAM>(&tab))&DLGC_WANTMESSAGE),"search edit retains normal Tab navigation");
        for(int modifier:{0,VK_CONTROL,VK_SHIFT}) {
            BYTE original[256]{};GetKeyboardState(original);BYTE keys[256]{};std::copy(std::begin(original),std::end(original),keys);if(modifier)keys[modifier]=0x80;SetKeyboardState(keys);
            auto before=app.searchGeneration;bool consumed=IsDialogMessageW(app.window,&enter);if(!consumed){TranslateMessage(&enter);DispatchMessageW(&enter);}SetKeyboardState(original);
            require(app.searchGeneration>before,"Enter Ctrl+Enter Shift+Enter reach search through dialog message path");
            wait([]{return !app.searching;});require(app.files.size()==3&&app.appliedQuery==L"UI追加タグ","Enter searches tag metadata across PNG JPEG WebP");
        }
        auto queuedBefore=app.searchGeneration;PostMessageW(app.searchEdit,WM_KEYDOWN,VK_RETURN,0);wait([&]{return app.searchGeneration>queuedBefore&&!app.searching;});require(app.files.size()==3,"queued Enter searches through normal UI message pump");
        auto searchEpoch=app.searchGeneration;SendMessageW(app.searchEdit,WM_KEYDOWN,VK_RETURN,1LL<<30);require(app.searchGeneration==searchEpoch,"search Enter auto-repeat does not start duplicate job");
        SetWindowTextW(app.searchEdit,L"");SendMessageW(app.searchEdit,WM_KEYDOWN,VK_RETURN,0);wait([]{return !app.searching;});require(app.files.size()==3&&app.appliedQuery.empty(),"empty Enter restores all files");app.metadataSearch=false;
        app.chooseSaveMode={};app.chooseBatchFolder={};app.confirmBatch={};
        ShowWindow(app.window,SW_SHOWNOACTIVATE);auto gallerySelected=app.selected;RECT narrow{},expanded{};GetWindowRect(app.list,&narrow);
        for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();SendMessageW(app.placement,CB_SETCURSEL,3,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(Placement,CBN_SELCHANGE),reinterpret_cast<LPARAM>(app.placement));
            GetWindowRect(app.list,&expanded);require(app.layout==L"none"&&!IsWindowVisible(app.preview)&&!IsWindowVisible(app.metadataTabs)&&!IsWindowVisible(app.simple)&&!IsWindowVisible(app.details)&&expanded.right-expanded.left>narrow.right-narrow.left&&app.selected==gallerySelected,"no side view hides pane and expands gallery while keeping selection");
            wait([]{return ThumbnailsReady();});CaptureUiTest(directory/(dark?L"gallery-night.png":L"gallery-light.png"));require(ParseViewerSettings(SerializeViewerSettings(ViewerSettings{128,100,true,true,dark,false,10,L"",L"none"})).layout==L"none","gallery setting persists");}
        SendMessageW(app.placement,CB_SETCURSEL,0,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(Placement,CBN_SELCHANGE),reinterpret_cast<LPARAM>(app.placement));app.darkTheme=false;ApplyTheme();wait([]{return app.selected>=0&&app.metadataReady&&ThumbnailsReady();});
        BeginMetadataEdit();wait([]{return !app.batchLoading;});SetWindowTextW(GetDlgItem(app.editorUi.Window(),1001),L"非表示前の変更");app.confirmChanges=[]{return 3;};SendMessageW(app.placement,CB_SETCURSEL,3,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(Placement,CBN_SELCHANGE),reinterpret_cast<LPARAM>(app.placement));require(app.layout==L"right"&&app.editing&&IsWindowVisible(app.editorUi.Window()),"cancel hiding sidebar protects unsaved metadata edit");
        app.confirmChanges=[]{return 2;};SendMessageW(app.placement,CB_SETCURSEL,3,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(Placement,CBN_SELCHANGE),reinterpret_cast<LPARAM>(app.placement));require(app.layout==L"none"&&!app.editing&&!IsWindowVisible(app.editorUi.Window()),"discard confirmation permits hiding editor");app.confirmChanges={};
        auto recordingRoot=directory/L"ui-recording";fs::create_directories(recordingRoot/L"logs");fs::create_directories(recordingRoot/L"photos");JsonObject recordingConfig;recordingConfig.Insert(L"version",JsonValue::CreateNumberValue(1));recordingConfig.Insert(L"logFolder",JsonValue::CreateStringValue((recordingRoot/L"logs").wstring()));recordingConfig.Insert(L"photoFolder",JsonValue::CreateStringValue((recordingRoot/L"photos").wstring()));recordingConfig.Insert(L"version",JsonValue::CreateNumberValue(1));WriteSettingsSection(directory,L"recording",winrt::to_string(recordingConfig.Stringify()));
        auto recordingPixel=[&](int id,int x,int y){auto child=AppControl(id);RECT rect{};GetClientRect(child,&rect);HDC screen=GetDC(child),dc=CreateCompatibleDC(screen);auto bitmap=CreateCompatibleBitmap(screen,rect.right,rect.bottom);auto old=SelectObject(dc,bitmap);DRAWITEMSTRUCT draw{};draw.CtlID=id;draw.CtlType=id==RecordToggle?ODT_BUTTON:ODT_STATIC;draw.hwndItem=child;draw.hDC=dc;draw.rcItem=rect;SendMessageW(app.window,WM_DRAWITEM,id,reinterpret_cast<LPARAM>(&draw));auto color=GetPixel(dc,x,y);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(child,screen);return color;};
        SendMessageW(app.window,WM_COMMAND,RecordToggle,0);require(stage3::Recording()&&Text(AppControl(RecordToggle))==L"記録停止","v0.4.1 record button enables recording while waiting for a log");
        for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();auto buttonColor=recordingPixel(RecordToggle,5,13);SendMessageW(app.window,WM_TIMER,RecordTimer,0);auto first=recordingPixel(RecordIndicator,12,13);SendMessageW(app.window,WM_TIMER,RecordTimer,0);auto second=recordingPixel(RecordIndicator,12,13);require(GetRValue(buttonColor)>GetBValue(buttonColor)&&first!=second&&(GetRValue(first)>GetBValue(first)||GetRValue(second)>GetBValue(second)),"v0.4.1 ON button is red and recording dot blinks in both themes");CaptureUiTest(directory/(dark?L"recording-night-on.png":L"recording-light-on.png"));}
        SendMessageW(app.window,WM_COMMAND,RecordToggle,0);wait([]{return !stage3::Recording();});SendMessageW(app.window,WM_TIMER,RecordTimer,0);auto offButton=recordingPixel(RecordToggle,5,13),offDot=recordingPixel(RecordIndicator,12,13);wait([]{return !stage3::Recording();});require(!stage3::Recording()&&Text(AppControl(RecordToggle))==L"記録開始"&&GetBValue(offButton)>GetRValue(offButton)&&GetRValue(offDot)==GetGValue(offDot)&&GetGValue(offDot)==GetBValue(offDot),"v0.4.1 OFF button is blue and dot remains gray");
        require(TabCtrl_GetItemCount(app.metadataTabs)==3,"v0.4.2 OCR tab exists");
        app.layout=L"right";Layout();int oldWidth=app.sideWidth;auto bar=app.splitter;auto point=MAKELPARAM((bar.left+bar.right)/2,(bar.top+bar.bottom)/2);SendMessageW(app.window,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(app.window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(bar.left-100,(bar.top+bar.bottom)/2));SendMessageW(app.window,WM_LBUTTONUP,0,point);require(app.sideWidth>oldWidth&&!app.draggingSplitter,"v0.4.5 right divider drag widens sidebar");app.sideWidth=oldWidth;Layout();NewOptionsAction(9);require(app.autoThumbnails,"v0.4.5 auto thumbnail option enables");SendMessageW(app.window,WM_TIMER,ZoomTimer,0);require(app.activeThumbnailWidth>=64&&app.activeThumbnailWidth<=256,"v0.4.5 automatic thumbnail bounds");SendMessageW(app.list,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);require(!app.autoThumbnails,"v0.4.5 Ctrl wheel switches to manual");app.thumbnailWidth=128;ApplyThumbnailSize();TabCtrl_SetCurSel(app.metadataTabs,2);Layout();require(Text(AppControl(1804))==L"OCR結果：認識","v0.4.5 OCR output toggle present");SendMessageW(app.window,WM_COMMAND,1804,0);require(Text(AppControl(1804))==L"OCR結果：名前","v0.4.5 OCR output toggles names");SendMessageW(app.window,WM_COMMAND,1804,0);SendMessageW(AppControl(1803),CB_SETCURSEL,1,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(1803,CBN_SELCHANGE),0);require(OcrTestSource()&&!stage3::Recording(),"v0.4.5 test source without recording");SendMessageW(AppControl(1803),CB_SETCURSEL,0,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(1803,CBN_SELCHANGE),0);require(!OcrTestSource(),"v0.4.5 test mode returns to recording source");
        OpenFolder(directory.wstring());wait([]{return !app.folderPending&&!app.files.empty();});
        auto imageItem=std::find_if(app.files.begin(),app.files.end(),[](auto& p){return fs::path(p).filename()==L"v045-ocr-image.png";});require(imageItem!=app.files.end(),"v0.4.5 OCR image fixture in gallery");TestSelectItem(app.list,int(imageItem-app.files.begin()),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);wait([]{return app.metadataReady;});SendMessageW(AppControl(1803),CB_SETCURSEL,2,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(1803,CBN_SELCHANGE),0);require(Text(AppControl(1805))==L"画像認識：実行","v0.4.7 selected image starts stopped");require(Text(AppControl(1806))==L"1"&&!IsWindowEnabled(AppControl(1806)),"v0.4.7 image FPS fixed and disabled");auto normalFps=person_ocr::GetSettings().fps;SendMessageW(app.window,WM_COMMAND,1805,0);wait([]{RefreshOcrUi();return OcrTestStatus().find(L"完了")!=std::wstring::npos;});require(OcrTestSource()&&OcrTestPreview(),"v0.4.5 selected original image OCR and preview");CaptureUiTest(directory/L"v047-image-ocr.png");SendMessageW(app.window,WM_COMMAND,1805,0);require(Text(AppControl(1805))==L"画像認識：実行"&&OcrTestPreview(),"v0.4.7 image stop retains preview");RefreshOcrUi();require(Text(AppControl(1805))==L"画像認識：実行","v0.4.7 stopped selection does not restart");require(SendMessageW(app.preview,WM_ERASEBKGND,0,0)==1,"v0.4.7 preview suppresses intermediate background erase");SendMessageW(AppControl(1803),CB_SETCURSEL,0,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(1803,CBN_SELCHANGE),0);require(person_ocr::GetSettings().fps==normalFps&&Text(AppControl(1806))==std::to_wstring(normalFps),"v0.4.7 normal FPS restored without changing settings");
        for(auto placement:{L"right",L"left",L"bottom",L"none"}){app.layout=placement;TabCtrl_SetCurSel(app.metadataTabs,2);Layout();require((IsWindowVisible(AppControl(1801))!=FALSE)==(app.layout!=L"none"),"OCR tab follows sidebar visibility");if(app.layout!=L"none")require(!IsWindowVisible(app.simple)&&!IsWindowVisible(app.details),"OCR tab hides metadata controls");}
        app.layout=L"right";TabCtrl_SetCurSel(app.metadataTabs,2);Layout();auto before=person_ocr::GetSettings();SendMessageW(app.window,WM_COMMAND,1801,0);require(person_ocr::GetSettings().always!=before.always,"OCR mode toggle persists");SendMessageW(app.window,WM_COMMAND,1801,0);require(person_ocr::GetSettings().always==before.always,"OCR mode toggle restores");for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();RECT button{};auto mode=AppControl(1801);GetWindowRect(mode,&button);POINT center{(button.left+button.right)/2,(button.top+button.bottom)/2};ScreenToClient(app.window,&center);require(SideHit(center)==mode,"OCR controls remain above tab page after theme repaint");CaptureUiTest(directory/(dark?L"ocr-night.png":L"ocr-light.png"));}app.darkTheme=false;ApplyTheme();
        {
            const std::wstring uuid=L"12345678-abcd-1234-abcd-123456789abc";
            PhotoSummary sample;sample.values[6]=L"wrld_"+uuid;sample.values[8]=L"Ordinary\r\n【グループID】grp_"+uuid;sample.values[9]=L"A & <B>\r\nA & <B>";sample.people={{L"A & <B>",L"usr_"+uuid},{L"A & <B>",L""}};
            app.idLinks=true;app.layout=L"right";TabCtrl_SetCurSel(app.metadataTabs,0);ShowSummary(sample);Layout();auto links=SummaryLinks(sample);require(links.size()==3,"v0.4.17 three validated page targets");
            auto buttons=[&]{std::vector<HWND> found;for(int id=4000;id<4500;++id){auto h=GetDlgItem(app.simple,id);if(!h)break;if(IsWindowVisible(h))found.push_back(h);}return found;};
            auto visible=buttons();require(visible.size()==3,"v0.4.17 world group and ID-bearing name have separate buttons");
            std::wstring opened;int opens=0;app.summaryLinkLauncher=[&](const std::wstring& url){opened=url;++opens;};for(size_t i=0;i<visible.size();++i){SendMessageW(visible[i],BM_CLICK,0,0);require(opened==links[i].url,"v0.4.17 button opens corresponding validated page");}
            auto h=app.simpleValues[9];CHARRANGE range{0,7};SendMessageW(h,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&range));CHARFORMAT2W format{};format.cbSize=sizeof(format);SendMessageW(h,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));require(!(format.dwEffects&(CFE_LINK|CFE_UNDERLINE))&&Text(h)==sample.values[9],"v0.4.17 names remain plain selectable text");
            auto before=opens;POINTL point{};SendMessageW(h,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),0);SendMessageW(h,WM_LBUTTONUP,0,MAKELPARAM(point.x+2,point.y+2));SendMessageW(h,WM_LBUTTONDBLCLK,MK_LBUTTON,MAKELPARAM(point.x+2,point.y+2));SendMessageW(h,WM_KEYDOWN,VK_RETURN,0);require(opens==before,"v0.4.17 text click double-click and Enter never open pages");
            SetFocus(visible.back());MSG enter{};enter.hwnd=visible.back();enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;IsDialogMessageW(app.window,&enter);require(opens==before+1,"v0.4.17 focused button Enter opens page");before=opens;SendMessageW(visible.back(),WM_KEYDOWN,VK_SPACE,0);SendMessageW(visible.back(),WM_KEYUP,VK_SPACE,0);require(opens==before+1,"v0.4.17 focused button Space opens page");
            app.idLinks=false;RefreshSummaryLinks();require(buttons().empty(),"v0.4.17 OFF immediately hides all page buttons");before=opens;SendMessageW(visible.back(),BM_CLICK,0,0);require(opens==before,"v0.4.17 hidden stale button cannot open page");
            app.idLinks=true;RefreshSummaryLinks();for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();for(auto layout:{L"left",L"right",L"bottom"}){app.layout=layout;Layout();auto current=buttons();require(current.size()==3,"v0.4.17 buttons persist across theme and placement");for(size_t i=0;i<current.size();++i){RECT button{},value{};GetWindowRect(current[i],&button);GetWindowRect(app.simpleValues[links[i].field],&value);POINTL origin{};SendMessageW(app.simpleValues[links[i].field],EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&origin),links[i].first);require(button.right<=value.left&&abs(button.top-(value.top+origin.y+2))<=1,"v0.4.17 buttons stay left of first text line");}CaptureUiTest(directory/(std::wstring(L"v0417-")+(dark?L"night-":L"light-")+layout+L".png"));}}
            PhotoSummary large;large.values[9]=L"";for(int i=0;i<80;++i){auto name=L"Long participant name "+std::to_wstring(i)+L" with wrapping words";if(i)large.values[9]+=L"\r\n";large.values[9]+=name;large.people.push_back({name,L"usr_"+uuid});}app.layout=L"right";ShowSummary(large);Layout();require(buttons().size()<80,"v0.4.17 large member list creates only viewport buttons");app.sideScroll=100000;LayoutSimple();auto last=buttons();require(!last.empty(),"v0.4.17 scrolling exposes later member buttons");for(auto button:last){SendMessageW(button,BM_CLICK,0,0);require(opened==L"https://vrchat.com/home/user/usr_"+uuid,"v0.4.17 scrolled button keeps its target");}ShowSummary({});require(buttons().empty(),"v0.4.17 image change removes old buttons");app.summaryLinkLauncher={};
            app.darkTheme=false;app.layout=L"right";ShowSummary({});ApplyTheme();Layout();
        }
        {auto metadata=InspectEditable((directory/L"v048-record.png").wstring());app.editorUi.Load(metadata);auto reference=GetDlgItem(app.editorUi.Window(),1010);require(Text(reference).find(L"usr_private")!=std::wstring::npos&&(GetWindowLongPtrW(reference,GWL_STYLE)&ES_READONLY),"v0.4.8 editor loads existing automatic record read-only");require(app.editorUi.Read().actions[2]==EditAction::Keep&&app.editorUi.Read().actions[5]==EditAction::Keep,"v0.4.8 displaying existing metadata does not mark edits");auto summary=ReadPhotoSummary((directory/L"v048-record.png").wstring());ShowSummary(summary);require(SummaryLinks(summary).size()==2,"v0.4.17 group and world page targets validated");app.layout=L"right";TabCtrl_SetCurSel(app.metadataTabs,0);Layout();app.sideScroll=100000;LayoutSimple();CaptureUiTest(directory/L"v048-group-link.png");ShowSummary({});require(SummaryLinks(app.summary).empty(),"v0.4.16 empty metadata has no links");}
        app.layout=L"right";TabCtrl_SetCurSel(app.metadataTabs,0);auto participantSummary=ReadPhotoSummary((directory/L"stage3-tests"/L"display-names.png").wstring());ShowSummary(participantSummary);app.sideScroll=100000;Layout();require(Text(app.simpleValues[9])==participantSummary.values[9]&&Text(app.simpleValues[10])==L"候補 & 名前","v0.4.1 sidebar receives structured participants and candidate names");CaptureUiTest(directory/L"participants-night.png");app.darkTheme=false;ApplyTheme();CaptureUiTest(directory/L"participants-light.png");
        {auto button=AppControl(1807);TabCtrl_SetCurSel(app.metadataTabs,2);Layout();require(!IsWindowVisible(button)&&!person_ocr::GetSettings().showHighPrecision,"v0.4.20 high precision hidden by default");SendMessageW(app.window,WM_COMMAND,1807,0);require(!person_ocr::GetSettings().highPrecision,"v0.4.20 hidden high precision command blocked");SetHighPrecisionVisible(true);require(IsWindowVisible(button)&&!person_ocr::GetSettings().highPrecision,"v0.4.20 display ON does not enable high precision");require(Text(button)==L"高精度：OFF"&&!person_ocr::GetSettings().highPrecision,"v0.4.10 high precision defaults OFF");app.confirmHighOcr=[]{return false;};SendMessageW(app.window,WM_COMMAND,1807,0);require(!person_ocr::GetSettings().highPrecision,"v0.4.10 high load warning cancellation keeps OFF");app.confirmHighOcr=[]{return true;};SendMessageW(app.window,WM_COMMAND,1807,0);require(person_ocr::GetSettings().highPrecision&&Text(button)==L"高精度：ON","v0.4.10 high precision toggle enables persisted setting");
        TabCtrl_SetCurSel(app.metadataTabs,2);Layout();SendMessageW(AppControl(1803),CB_SETCURSEL,2,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(1803,CBN_SELCHANGE),0);SendMessageW(app.window,WM_COMMAND,1805,0);wait([]{RefreshOcrUi();return test_ocr::Raw().find(L"TESTMODEL")!=std::wstring::npos;});require(Text(AppControl(1806))==L"1"&&person_ocr::GetSettings().highPrecision,"v0.4.10 selected image uses native tiles at 1 FPS");
        SetHighPrecisionVisible(false);require(!IsWindowVisible(button)&&!person_ocr::GetSettings().highPrecision,"v0.4.20 hiding active high OCR forces normal mode");wait([]{RefreshOcrUi();return test_ocr::Raw().find(L"TESTMODEL")!=std::wstring::npos;});require(test_ocr::Active(),"v0.4.20 image OCR continues in normal mode after hiding");SetHighPrecisionVisible(true);require(!person_ocr::GetSettings().highPrecision,"v0.4.20 redisplay remains normal mode");SendMessageW(app.window,WM_COMMAND,1807,0);wait([]{RefreshOcrUi();return test_ocr::Raw().find(L"TESTMODEL")!=std::wstring::npos;});SendMessageW(app.window,WM_COMMAND,1805,0);for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();require(recordingPixel(1807,2,2)==RGB(255,100,100),"v0.4.10 high precision red in both themes");CaptureUiTest(directory/(dark?L"v0410-high-night.png":L"v0410-high-light.png"));}SendMessageW(app.window,WM_COMMAND,1807,0);require(!person_ocr::GetSettings().highPrecision&&recordingPixel(1807,2,2)==RGB(80,175,255),"v0.4.10 high precision OFF restores blue");SendMessageW(AppControl(1803),CB_SETCURSEL,0,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(1803,CBN_SELCHANGE),0);app.confirmHighOcr={};app.darkTheme=false;ApplyTheme();SetHighPrecisionVisible(false);require(!IsWindowVisible(button)&&!person_ocr::GetSettings().highPrecision,"v0.4.20 display OFF hides high precision");}
        {auto menu=BuildOptionsMenu();require(GetMenuState(menu,8,MF_BYCOMMAND)!=UINT(-1),"v0.4.9 recording settings in options");DestroyMenu(menu);
        std::ifstream file(directory.parent_path().parent_path().parent_path()/L"Stable"/L"readme.md",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});bytes.erase(std::remove(bytes.begin(),bytes.end(),'\r'),bytes.end());auto help=QuickHelpText();help.erase(std::remove(help.begin(),help.end(),L'\r'),help.end());require(winrt::to_string(help)==bytes,"v0.4.9 embedded help matches Stable readme");
        bool helpShown=false;ShowQuickHelp([&](HWND h){auto c=GetDlgItem(h,1901);helpShown=IsWindowVisible(c)&&(GetWindowLongPtrW(c,GWL_STYLE)&ES_READONLY)&&Text(c)==QuickHelpText();CaptureUiTest(directory/L"v049-help.png",h);});require(helpShown,"v0.4.9 quick help dialog is scrollable read-only text");
        app.editable=InspectEditable((directory/L"v048-record.png").wstring());app.editorUi.Load(app.editable);app.editing=true;SetWindowTextW(GetDlgItem(app.editorUi.Window(),1001),L"reset unsaved");app.confirmChanges=[]{return 3;};auto beforeReset=app.generation;ResetViewer();require(app.generation==beforeReset&&app.editing,"v0.4.9 reset cancellation preserves unsaved edit");app.confirmChanges={};app.editing=false;
        app.saving=true;ResetViewer();require(app.generation==beforeReset,"v0.4.9 reset blocked during saving");app.saving=false;app.backupBusy=true;ResetViewer();require(app.generation==beforeReset,"v0.4.9 reset blocked during backup");app.backupBusy=false;
        auto recent=app.recent;auto bookmarks=app.bookmarks;auto generation=app.generation;app.editing=false;app.folderPending=true;Result stale;stale.job={0,generation};stale.files={L"stale.png"};{std::lock_guard<std::mutex> lock(app.mutex);app.results.push_back(stale);}ResetViewer();Results();require(app.files.empty()&&app.allFiles.empty()&&app.folder.empty()&&app.last.empty()&&!app.folderPending&&Text(app.path).empty()&&SendMessageW(app.history,CB_GETCURSEL,0,0)==CB_ERR&&app.recent==recent&&app.bookmarks==bookmarks,"v0.4.9 reset clears view and rejects stale enumeration while preserving history");CaptureUiTest(directory/L"v049-reset.png");}
        {
            auto metadata=InspectEditable((directory/L"stage3-tests"/L"v0414-record-writer-alpha.png").wstring());
            app.editorUi.Load(metadata);app.editing=true;app.layout=L"right";TabCtrl_SetCurSel(app.metadataTabs,1);Layout();ApplyTheme();
            auto list=GetDlgItem(app.editorUi.Window(),1011),manual=GetDlgItem(app.editorUi.Window(),1012);
            require(ListView_GetItemCount(list)==3&&ListView_GetCheckState(list,0)&&ListView_GetCheckState(list,1)&&!ListView_GetCheckState(list,2)&&!app.editorUi.Read().Changed(),"v0.4.14 existing and orphan candidates checked without creating edits");
            ListView_SetCheckState(list,2,TRUE);require(app.editorUi.Read().editCandidates&&app.editorUi.Read().candidates.size()==3&&IsWindowEnabled(app.saveButton),"v0.4.14 checkbox changes notify save controls");ListView_SetCheckState(list,2,FALSE);require(!app.editorUi.Read().Changed(),"v0.4.14 reverting checkbox clears dirty state");
            SetWindowTextW(manual,L"追加候補");require(app.editorUi.Read().additionalCandidates==L"追加候補"&&app.editorUi.Read().editCandidates,"v0.4.14 manual candidate entry marks changes");SetWindowTextW(manual,L"");
            for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();for(auto layout:{L"left",L"right",L"bottom"}){app.layout=layout;Layout();SendMessageW(app.editorUi.Window(),WM_VSCROLL,SB_TOP,0);SendMessageW(app.editorUi.Window(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-9*WHEEL_DELTA)),0);RECT r{};GetWindowRect(list,&r);require(r.right>r.left&&r.bottom>r.top&&ListView_GetTextColor(list)==ThemeForeground(),"v0.4.14 candidate list lays out and follows theme");CaptureUiTest(directory/(std::wstring(L"v0414-")+(dark?L"night-":L"light-")+layout+L".png"));}}
            app.editorUi.LoadBatch({metadata,metadata});require(!IsWindowEnabled(list)&&!IsWindowEnabled(manual)&&!app.editorUi.Read().editCandidates&&!IsWindowVisible(list),"v0.4.14 batch editing hides and disables candidate editing");
            app.editorUi.Load(InspectEditable((directory/L"writer-alpha.png").wstring()));require(ListView_GetItemCount(list)==0&&Text(manual).empty()&&!app.editorUi.Read().Changed(),"v0.4.14 switching image resets candidate choices and manual text");
            app.editing=false;app.darkTheme=false;ApplyTheme();
        }
        {
            auto folder=directory/L"sort-ui";fs::create_directories(folder);auto a=folder/L"a.jpg",b=folder/L"b.jpg",c=folder/L"c.jpg";
            ULONGLONG created=130000000000000000ULL;for(auto path:{a,b,c}){fs::copy_file(directory/L"sample.jpg",path,fs::copy_options::overwrite_existing);HANDLE h=CreateFileW(path.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);FILETIME time{DWORD(created),DWORD(created>>32)};require(h!=INVALID_HANDLE_VALUE&&SetFileTime(h,&time,nullptr,nullptr),"v0.4.15 UI fixture creation time");CloseHandle(h);created+=10000000;}
            app.sortMode=0;app.editing=false;app.layout=L"right";TabCtrl_SetCurSel(app.metadataTabs,1);OpenFolder(folder.wstring());wait([]{return !app.folderPending&&app.files.size()==3;});
            TestSelectItem(app.list,0,LVIS_SELECTED,LVIS_SELECTED);ListView_SetItemState(app.list,2,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);FlushSelectionUpdate();
            RequestSort(1);wait([]{return !app.sorting;});require(app.files==std::vector<std::wstring>{c.wstring(),b.wstring(),a.wstring()}&&SelectedFiles().size()==2&&app.selected==0&&ListView_GetSelectionMark(app.list)==0,"v0.4.15 sort preserves multiple selection, focus and Shift anchor");
            StartSearch(L"a");wait([]{return !app.searching&&app.files.size()==1;});RequestSort(3);wait([]{return !app.sorting;});require(app.files==std::vector<std::wstring>{a.wstring()}&&app.allFiles==std::vector<std::wstring>{c.wstring(),b.wstring(),a.wstring()},"v0.4.15 sort applies to filtered display and full folder");
            StartSearch(L"");wait([]{return !app.searching&&app.files.size()==3;});require(app.files==app.allFiles,"v0.4.15 clearing search retains sort mode");TestSelectItem(app.list,2,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);wait([]{return app.metadataReady;});BeginMetadataEdit();SetWindowTextW(GetDlgItem(app.editorUi.Window(),1001),L"sort guard");app.confirmChanges=[]{return 3;};RequestSort(0);require(!app.sorting&&app.sortMode==3&&app.editing&&app.editorUi.Read().Changed(),"v0.4.15 cancelling sort preserves unsaved edit");
            for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();for(auto placement:{L"left",L"right",L"bottom"}){app.layout=placement;Layout();UpdateEditorControls();RECT rect{};GetWindowRect(app.cancelButton,&rect);POINT point{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};ScreenToClient(app.window,&point);require(SideHit(point)==app.cancelButton&&IsWindowEnabled(app.cancelButton),"v0.4.15 cancel stays above tabs before mouse hover");RECT panel{};GetWindowRect(app.editorUi.Window(),&panel);POINT inside{panel.left+2,panel.top+2};ScreenToClient(app.window,&inside);require(SideHit(inside)==app.editorUi.Window(),"v0.4.15 editor pane stays above tab page in both themes");CaptureUiTest(directory/(std::wstring(L"v0415-")+(dark?L"night-":L"light-")+placement+L".png"));}}
            app.confirmChanges=[]{return 2;};RequestSort(0);wait([]{return !app.sorting;});require(!app.editing&&app.files==std::vector<std::wstring>{a.wstring(),b.wstring(),c.wstring()},"v0.4.15 discard confirmation permits sort");app.confirmChanges={};app.darkTheme=false;ApplyTheme();
            RECT help{},sort{};GetWindowRect(AppControl(Help),&help);GetWindowRect(AppControl(SortMenu),&sort);require(help.right<sort.left&&Text(AppControl(SortMenu))==L"並べ替え ▼","v0.4.15 sort button follows help");
            ResetViewer();app.sortMode=0;SaveSettings();
        }
        RunV0412UiTests(directory,report);RunV0413UiTests(directory,report);RunV0420UiTests(report);RunV0421ThemeTests(report,[&](const wchar_t* name){CaptureUiTest(directory/name);});
        app.layout=L"right";OpenFolder((directory/L"v0422").wstring());wait([]{return !app.folderPending;});int nulIndex=-1;for(int i=0;i<int(app.files.size());++i)if(fs::path(app.files[i]).filename()==L"nul-2.webp")nulIndex=i;require(nulIndex>=0,"v0.4.22 NUL WebP UI fixture found");TestSelectItem(app.list,nulIndex,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);wait([]{return app.metadataReady;});TabCtrl_SetCurSel(app.metadataTabs,0);Layout();
        require(app.summary.values[4]==L"撮影者"&&app.summary.values[9].find(L"同名")!=std::wstring::npos&&Text(app.simpleValues[4])==L"撮影者"&&IsWindowVisible(app.simpleLabels[4]),"v0.4.22 NUL WebP metadata is visible in simple tab");CaptureUiTest(directory/L"v0422-simple.png");TabCtrl_SetCurSel(app.metadataTabs,1);Layout();SendMessageW(app.window,WM_COMMAND,EditMetadata,0);require(app.editing&&app.editorUi.Read().values[1]==L"撮影者","v0.4.22 NUL WebP opens editor with existing author");SendMessageW(app.window,WM_COMMAND,CancelMetadata,0);
        RunV0423UiTests(directory,report,[&](const wchar_t* name){CaptureUiTest(directory/name);});
        RunV060UiTests(directory,report);RunV061UiTests(directory,report);RunV062UiTests(directory,report);RunV063UiTests(directory,report);RunV0516UiTests(directory,report);RunV0515UiTests(directory,report);RunV0514UiTests(directory,report);RunV0513UiTests(directory,report);RunV050UiTests(directory,report,[&](const wchar_t* name){CaptureUiTest(directory/name);});
        RunV053UiTests(directory,report,[&](const wchar_t* name){CaptureUiTest(directory/name);});
        RunV0512UiTests(directory,report);RunMdbUiTests(directory,report);RunIdNameUiTests(directory,report);RunMdbApplyUiTests(directory,report);
        fs::remove(sub/L"nested-only.jpg");fs::remove(sub);
        report << "ALL PASSED\n"; return 0;
    } catch (const std::exception& error) { report << "FAIL: " << error.what() << '\n'; }
    return 1;
}

void RunV0415Tests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: v0.4.15 "<<label<<'\n';};
    auto folder=directory/L"sort-tests";fs::create_directories(folder);auto a=folder/L"a.png",b=folder/L"B.png",c=folder/L"c.png",missing=folder/L"missing.png";fs::remove(missing);
    for(auto path:{a,b,c}){std::ofstream out(path);out<<"sort fixture";}
    auto set=[&](const fs::path& path,ULONGLONG value){HANDLE h=CreateFileW(path.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("sort fixture handle");FILETIME time{DWORD(value),DWORD(value>>32)};bool ok=SetFileTime(h,&time,nullptr,nullptr)!=FALSE;CloseHandle(h);check(ok,"sort fixture creation time");};
    constexpr ULONGLONG base=130000000000000000ULL;set(a,base+20000000);set(b,base);set(c,base);
    std::vector<std::wstring> files{c.wstring(),a.wstring(),missing.wstring(),b.wstring()};SortFiles(files,0);check(files==std::vector<std::wstring>{a.wstring(),b.wstring(),c.wstring(),missing.wstring()},"filename ascending");SortFiles(files,1);check(files==std::vector<std::wstring>{missing.wstring(),c.wstring(),b.wstring(),a.wstring()},"filename descending");SortFiles(files,2);check(files==std::vector<std::wstring>{b.wstring(),c.wstring(),a.wstring(),missing.wstring()},"creation ascending, equal dates use name and missing last");SortFiles(files,3);check(files==std::vector<std::wstring>{a.wstring(),b.wstring(),c.wstring(),missing.wstring()},"creation descending with missing last");auto before=files;SortFiles(files,0,[]{return false;});check(files==before,"cancelled sort does not partially commit");
    for(int mode=0;mode<4;++mode){ViewerSettings value;value.sortMode=mode;check(ParseViewerSettings(SerializeViewerSettings(value)).sortMode==mode,"sort settings round trip");}check(ParseViewerSettings("{\"version\":3}").sortMode==0,"legacy defaults to filename order");
    auto ledger=folder/L"legacy.json";{std::ofstream out(ledger);out<<"{\"version\":1,\"entries\":[{\"path\":\"AB_old.png\",\"source\":\"old.png\",\"fingerprint\":\"original-hash\",\"created\":\"1\"}]}";}auto entries=ReadBackupLedger(ledger);check(entries.size()==1&&entries[0].sourceFingerprint==L"original-hash","legacy backup fingerprints migrate safely");WriteBackupLedger(ledger,entries);check(ReadBackupLedger(ledger)[0].sourceFingerprint==L"original-hash","backup ledger retains source fingerprint");
}

int ComfortTest(){auto folder=app.settings.parent_path();std::ofstream report(folder/L"comfort-result.txt");try{RunV050UiTests(folder,report,[&](const wchar_t* name){CaptureUiTest(folder/name);});report<<"ALL PASSED\n";return 0;}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}}

int UpdateDialogTest(){auto folder=app.settings.parent_path();std::ofstream report(folder/L"update-dialog-result.txt");try{RunUpdateDialogTests(folder,report,[&](HWND dialog){CaptureUiTest(folder/L"v051-update-dialog.png",dialog);});report<<"ALL PASSED\n";return 0;}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}}

int LicenseDialogTest(){auto folder=app.settings.parent_path();std::ofstream report(folder/L"license-dialog-result.txt");try{RunLicenseUiTests(folder,report,[&](HWND dialog,const wchar_t* name){CaptureUiTest(folder/name,dialog);});report<<"ALL PASSED\n";return 0;}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}}

int TileTooltipTest(){auto folder=app.settings.parent_path();std::ofstream report(folder/L"tile-tooltip-result.txt");try{RunV053UiTests(folder,report,[&](const wchar_t* name){CaptureUiTest(folder/name);});report<<"ALL PASSED\n";return 0;}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';return 1;}}
