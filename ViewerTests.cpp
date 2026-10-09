#include "ViewerRuntime.h"
#include "MetadataWriterTests.h"
int SelfTest() {
    auto directory = app.settings.parent_path() / L"self-test-data";
    fs::create_directories(directory);
    std::ofstream report(directory / L"result.txt");
    try {
        auto require = [&](bool ok, const char* label) { if (!ok) throw std::runtime_error(label); report << "PASS: " << label << '\n'; };
        int wheel = 0;
        require(WheelThumbnailWidth(128, wheel, 60) == 128 && WheelThumbnailWidth(128, wheel, 60) == 160, "high resolution zoom wheel accumulated");
        require(WheelThumbnailWidth(256, wheel, 240) == 256 && WheelThumbnailWidth(64, wheel, -240) == 64, "thumbnail zoom limits");
        JsonObject legacy;
        require(ReadThumbnailWidth(legacy) == 128, "v0.1.0 settings compatibility");
        legacy.Insert(L"thumbnailWidth", JsonValue::CreateNumberValue(257));
        bool rejected = false; try { ReadThumbnailWidth(legacy); } catch (...) { rejected = true; }
        require(rejected, "invalid thumbnail width rejected");
        require(IsPhotosId(L"Microsoft.Windows.Photos_8wekyb3d8bbwe!App") && !IsPhotosId(L"Other.Photos_8wekyb3d8bbwe!App") && !IsPhotosId(L"Microsoft.Windows.Photos_8wekyb3d8bbwe!"), "Photos app identity filter");
        app.settings = directory / L"settings.json";
        app.settingsProtected = false;
        app.saveHistory = true; app.cacheMB = 100; app.bookmarks.clear();
        app.last = Normalize(directory.wstring()); app.layout = L"bottom"; app.recent = { app.last }; app.thumbnailWidth = 192;
        SaveSettings(); app.last.clear(); app.recent.clear(); app.layout = L"right";
        // LoadSettings uses the real EXE path, so parse the isolated round-trip file directly.
        std::ifstream input(app.settings, std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(input)), {});
        auto object = JsonObject::Parse(winrt::to_hstring(json));
        require(object.GetNamedNumber(L"version") == 2 && object.GetNamedString(L"sideView") == L"bottom" && object.GetNamedArray(L"history").Size() == 1, "settings JSON round trip");
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
bool ThumbnailsReady() { if (app.folderPending || app.wantedLast < app.wantedFirst) return false; for (int i=app.wantedFirst; i<=app.wantedLast; ++i) if (!app.thumbnailDone[i]) return false; return true; }
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
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
    };
    try {
        auto source = directory / L"sample.jpg";
        auto longName = L"長いファイル名-" + std::wstring(70, L'界') + L".jpg";
        fs::copy_file(source, directory / longName, fs::copy_options::overwrite_existing);
        require(Text(GetDlgItem(app.window, OpenPath)) == L"ロード" && Text(GetDlgItem(app.window, Options)).find(L"オプション") == 0 && Text(GetDlgItem(app.window, Bookmarks)).find(L"ブックマーク") == 0 && app.cacheMB == 100 && app.saveHistory, "load options bookmarks controls and defaults");
        for (int i = 0; i < 650; ++i) fs::copy_file(source, directory / (L"grid-" + std::to_wstring(i) + L".jpg"), fs::copy_options::overwrite_existing);
        OpenFolder(directory.wstring());
        wait([] { return !app.folderPending && !app.files.empty() && ThumbnailsReady(); });
        require(ListView_GetItemCount(app.list) >= 20, "async folder and thumbnail loading");
        require(app.files.size() > 650 && app.loaded < static_cast<int>(app.files.size()) && app.wantedLast - app.wantedFirst < ThumbnailSlots, "large folder loads viewport before offscreen images");
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
        wchar_t tooltip[512]{}; NMLVGETINFOTIPW tip{};
        tip.hdr = {app.list,Images,LVN_GETINFOTIPW}; tip.iItem = longIndex; tip.pszText = tooltip; tip.cchTextMax = ARRAYSIZE(tooltip);
        SendMessageW(app.window,WM_NOTIFY,Images,reinterpret_cast<LPARAM>(&tip));
        require(std::wstring(tooltip) == longName,"infotip contains full Japanese filename and extension");
        tip.iItem = 0; SendMessageW(app.window,WM_NOTIFY,Images,reinterpret_cast<LPARAM>(&tip));
        require(std::wstring(tooltip).find(L"読込みエラー") != std::wstring::npos,"infotip retains failed-thumbnail status");
        ListView_SetItemState(app.list, 3, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
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
            require(app.selected == 3 && Same(app.files[app.selected], selected), "selection retained on resize");
        }
        wait([] { return ThumbnailsReady(); });
        auto cacheHits = app.cache.hits.load();
        SendMessageW(app.list, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), 0);
        SendMessageW(app.window, WM_TIMER, ZoomTimer, 0);
        wait([] { return ThumbnailsReady(); });
        int width = 0, height = 0; ImageList_GetIconSize(app.thumbs, &width, &height);
        require(width == 160 && height == 120 && app.activeThumbnailWidth == 160, "Ctrl wheel changes thumbnail image list");
        require(app.cache.hits > cacheHits, "Ctrl wheel reuses decoded thumbnail cache");
        require(app.selected == 3 && Text(app.details) == details, "zoom keeps selection and metadata");
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
        require(TabCtrl_GetItemCount(app.metadataTabs) == 2 && TabCtrl_GetCurSel(app.metadataTabs) == 0, "simple and detailed tabs created with simple default");
        int xmpIndex = -1;
        for (int i = 0; i < static_cast<int>(app.files.size()); ++i) if (fs::path(app.files[i]).filename() == L"xmp-日本語.png") xmpIndex = i;
        require(xmpIndex >= 0, "XMP UI fixture found");
        ListView_SetItemState(app.list, xmpIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        wait([] { return !app.previewPixels.data.empty() && app.summary.values[4] == L"撮影者 & テスト"; });
        require(Text(app.simpleValues[0]) == L"xmp-日本語.png" && Text(app.simpleValues[2]) == L"320 × 160 px" && Text(app.simpleValues[3]).find(L"XMP") != std::wstring::npos && Text(app.simpleValues[5]) == L"日本語ワールド" && Text(app.simpleValues[6]) == L"wrld_test" && Text(app.simpleValues[7]).empty(), "simple tab eight fields match selected image");
        auto selectionGeneration = app.selection; auto summary = app.summary; auto detailedText = Text(app.details);
        for (int tab : { 1,0,1 }) {
            TabCtrl_SetCurSel(app.metadataTabs, tab);
            NMHDR notification{ app.metadataTabs, MetadataTabs, TCN_SELCHANGE };
            SendMessageW(app.window, WM_NOTIFY, MetadataTabs, reinterpret_cast<LPARAM>(&notification));
            require(app.selection == selectionGeneration && app.summary.values == summary.values && Text(app.details) == detailedText && ((GetWindowLongPtrW(app.details, GWL_STYLE) & WS_VISIBLE) != 0) == (tab == 1), "tab switch uses same result without rereading");
        }
        auto longSummary = summary; longSummary.values[5] = std::wstring(3000, L'界');
        ShowSummary(longSummary); TabCtrl_SetCurSel(app.metadataTabs, 0);
        for (auto layout : { L"right", L"left", L"bottom" }) {
            app.layout = layout; Layout();
            RECT start{}; GetWindowRect(app.simpleValues[0], &start);
            bool aligned = true;
            for (auto value : app.simpleValues) { RECT rect{}; GetWindowRect(value, &rect); aligned = aligned && rect.left == start.left; }
            require(aligned && SendMessageW(app.simpleValues[5], EM_GETLINECOUNT, 0, 0) > 1, "simple labels align and long Japanese value wraps in each layout");
            SendMessageW(app.simple, WM_VSCROLL, SB_BOTTOM, 0);
            RECT model{}, page{}; GetWindowRect(app.simpleValues[7], &model); GetWindowRect(app.simple, &page);
            require(app.simpleScroll > 0 && model.top >= page.top && model.bottom <= page.bottom, "simple pane scroll exposes final reserved model field");
        }
        ShowSummary(summary);
        int webpIndex = -1;
        for (int i=0; i<static_cast<int>(app.files.size()); ++i) if (fs::path(app.files[i]).filename() == L"webp-透過.WEBP") webpIndex = i;
        require(webpIndex >= 0, "uppercase WebP included in enumeration");
        ListView_SetItemState(app.list, webpIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
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
        for (int i=0; i<static_cast<int>(app.files.size()); ++i) if (Same(app.files[i],webpPath)) ListView_SetItemState(app.list,i,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
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
        RECT history{}, bookmarks{}, options{}, placement{}; GetWindowRect(app.history,&history); GetWindowRect(GetDlgItem(app.window,Bookmarks),&bookmarks); GetWindowRect(GetDlgItem(app.window,Options),&options); GetWindowRect(app.placement,&placement);
        require(history.right < bookmarks.left && bookmarks.right < options.left && options.right < placement.left, "top controls do not overlap at minimum window size");
        int extendedIndex = -1;
        for (int i=0; i<static_cast<int>(app.files.size()); ++i) if (fs::path(app.files[i]).filename() == L"extended-日本語.jpg") extendedIndex = i;
        require(extendedIndex >= 0,"Extended XMP UI fixture found");
        ListView_SetItemState(app.list,extendedIndex,LVIS_SELECTED | LVIS_FOCUSED,LVIS_SELECTED | LVIS_FOCUSED);
        wait([] { return !app.previewPixels.data.empty() && app.summary.values[4] == L"拡張撮影者"; });
        require(Text(app.simpleValues[5]) == L"拡張ワールド" && Text(app.details).find(L"検証成功") != std::wstring::npos,"Extended XMP updates simple and detailed tabs");
        int editableIndex=-1;
        for(int i=0;i<static_cast<int>(app.files.size());++i)if(fs::path(app.files[i]).filename()==L"sample.jpg")editableIndex=i;
        require(editableIndex>=0,"Stage2 editable source found");
        ListView_EnsureVisible(app.list,editableIndex,FALSE);ScheduleThumbnails();wait([]{return ThumbnailsReady();});
        auto displayed=DisplayedThumbnail(editableIndex);require(!displayed.data.empty(),"selected thumbnail exists before metadata read");
        app.metadataDelayMs=500;
        ListView_SetItemState(app.list,editableIndex,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        require(!app.metadataReady && app.previewPixels.data==displayed.data,"preview uses exact displayed thumbnail before delayed XMP");
        require(!IsWindowEnabled(app.editButton),"edit disabled during metadata loading");
        wait([]{return app.metadataReady;});app.metadataDelayMs=0;
        TabCtrl_SetCurSel(app.metadataTabs,1);Layout();
        SendMessageW(app.window,WM_COMMAND,EditMetadata,0);
        require(app.editing && !IsWindowEnabled(app.saveButton),"detail edit starts without pending changes");
        auto authorInput=GetDlgItem(app.editorUi.Window(),1001);
        SetWindowTextW(authorInput,L"編集UI & 日本語");
        require(app.editorUi.Read().Changed() && IsWindowEnabled(app.saveButton),"editing an input marks change and enables save");
        auto editedPath=app.files[app.selected];auto editedSelection=app.selected;
        app.confirmChanges=[]{return 3;};
        SendMessageW(app.window,WM_CLOSE,0,0);
        require(!app.stopping && app.editing,"cancel unsaved close preserves running editor");
        ListView_SetItemState(app.list,editableIndex==0?1:0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
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
        require(app.saving && !GuardTransition([]{}) && !IsWindowEnabled(app.cancelButton),"save blocks transitions and multiple saves");
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
        report << "ALL PASSED\n"; return 0;
    } catch (const std::exception& error) { report << "FAIL: " << error.what() << '\n'; }
    return 1;
}
