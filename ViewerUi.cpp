#include "V048Features.h"
#include "UpdateChecker.h"
#include "ViewerRuntime.h"
#include <windowsx.h>
State app;
namespace {HWND groupButton{};void OpenGroupPage(){auto ids=GroupIds(app.summary.values[8]);if(ids.empty())return;size_t selected=0;if(ids.size()>1){HMENU menu=CreatePopupMenu();for(size_t i=0;i<ids.size();++i)AppendMenuW(menu,MF_STRING,UINT_PTR(i+1),ids[i].c_str());RECT r{};GetWindowRect(groupButton,&r);auto choice=TrackPopupMenuEx(menu,TPM_RETURNCMD,r.left,r.bottom,app.window,nullptr);DestroyMenu(menu);if(choice<1||choice>ids.size())return;selected=choice-1;}auto url=GroupUrl(ids[selected]);if(!url.empty())ShellExecuteW(app.window,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}}
void ShowSummary(const PhotoSummary& summary, const std::wstring& message) {
    app.summary = summary; app.simpleScroll = 0;EnableWindow(groupButton,!GroupIds(summary.values[8]).empty());
    for (size_t i = 0; i < summary.values.size(); ++i) SetWindowTextW(app.simpleValues[i], summary.values[i].c_str());
    SetWindowTextW(app.simpleNote, (message + summary.notes).c_str()); LayoutSimple();
}

std::wstring ErrorText(HRESULT hr) {
    wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, hr, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring value = message ? message : L"処理に失敗しました";
    LocalFree(message);
    wchar_t code[32]; swprintf_s(code, L" (0x%08X)", static_cast<unsigned>(hr));
    return value + code;
}
std::wstring Text(HWND control) {
    std::wstring value(GetWindowTextLengthW(control) + 1, L'\0');
    GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    value.resize(wcslen(value.c_str())); return value;
}
void Status(const std::wstring& message) {
    auto text=app.activityText.empty()?message:std::wstring(1,L"◐◓◑◒"[app.activityFrame%4])+L" "+app.activityText;
    auto monitor=stage3::MonitorStatus();if(!monitor.empty())text+=L" | Stage3: "+monitor;SetWindowTextW(app.status,text.c_str());
}
void SetActivity(const std::wstring& text){app.activityText=text;SetTimer(app.window,ActivityTimer,150,nullptr);Status(text);SetWindowTextW(app.editStatus,text.c_str());}
void StopActivity(){app.activityText.clear();KillTimer(app.window,ActivityTimer);Counts();}
void Counts(const std::wstring& message) {
    Status(std::to_wstring(app.files.size()) + L" / "+std::to_wstring(app.allFiles.size())+L" 画像  |  " +
        (app.selected < 0 ? L"未選択" : std::to_wstring(app.selected + 1) + L" / " + std::to_wstring(app.files.size())) + L"  | 選択: " + std::to_wstring(app.list?ListView_GetSelectedCount(app.list):0) + L"画像  |  " + message);
}
bool Same(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
std::wstring Normalize(const std::wstring& value) {
    return NormalizeFolderAddress(value);
}
void RefreshHistory() {
    SendMessageW(app.history, CB_RESETCONTENT, 0, 0);
    int selected = -1;
    auto current = app.folder.empty() ? app.last : app.folder;
    for (auto& p : app.recent) {
        auto index = SendMessageW(app.history, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.c_str()));
        if (SameFolderAddress(p, current)) selected = static_cast<int>(index);
    }
    SendMessageW(app.history, CB_SETCURSEL, selected, 0);
    InvalidateRect(app.history, nullptr, TRUE);
}
int ReadThumbnailWidth(const JsonObject& object) {
    auto number = object.GetNamedNumber(L"thumbnailWidth", 128);
    if (!std::isfinite(number) || number < ThumbnailMin || number > ThumbnailMax || std::fmod(number - ThumbnailMin, ThumbnailStep) != 0)
        throw std::runtime_error("thumbnail width");
    return static_cast<int>(number);
}
void LoadSettings() {
    wchar_t executable[32768]; GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    app.settings = fs::path(executable).parent_path() / L"settings.json";
    try {
        if (!fs::exists(app.settings)) return;
        if (fs::file_size(app.settings) > 1024 * 1024) throw std::runtime_error("settings size");
        std::ifstream input(app.settings, std::ios::binary);
        if (!input) throw std::runtime_error("read");
        std::string json((std::istreambuf_iterator<char>(input)), {});
        if (json.size() > 1024 * 1024) throw std::runtime_error("size");
        auto settings = ParseViewerSettings(json);
        app.last = settings.last; app.layout = settings.layout; app.recent = std::move(settings.history);
        app.thumbnailWidth = app.activeThumbnailWidth = settings.thumbnailWidth;
        app.autoBackup=settings.autoBackup;app.backupLimit=settings.backupLimit;app.darkTheme=settings.darkTheme;app.metadataSearch=settings.metadataSearch;
        app.autoThumbnails=settings.autoThumbnails;app.sideWidth=settings.sideWidth;app.sideHeight=settings.sideHeight;app.manualThumbnailWidth=settings.thumbnailWidth;
        app.cacheMB = settings.cacheMB; app.saveHistory = settings.saveHistory; app.bookmarks = std::move(settings.bookmarks);
    } catch (...) {
        app.settingsProtected = true;
        app.settingsError = L"設定の読込みに失敗しました。元のsettings.jsonを保全し、今回は設定を保存しません。";
    }
}
void SaveSettings() {
    if (app.settingsProtected) return;
    try {
        ViewerSettings settings;
        settings.autoThumbnails=app.autoThumbnails;settings.sideWidth=app.sideWidth;settings.sideHeight=app.sideHeight;
        settings.thumbnailWidth = app.autoThumbnails?app.manualThumbnailWidth:app.thumbnailWidth; settings.cacheMB = app.cacheMB; settings.saveHistory = app.saveHistory;
        settings.autoBackup=app.autoBackup;settings.backupLimit=app.backupLimit;settings.darkTheme=app.darkTheme;settings.metadataSearch=app.metadataSearch;
        settings.last = app.last; settings.layout = app.layout; settings.history = app.recent; settings.bookmarks = app.bookmarks;
        auto bytes = SerializeViewerSettings(settings);
        auto temp = app.settings; temp += L".tmp";
        { std::ofstream output(temp, std::ios::binary | std::ios::trunc); output.write(bytes.data(), bytes.size()); output.flush(); if (!output) throw std::runtime_error("write"); }
        if (!MoveFileExW(temp.c_str(), app.settings.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("replace");
    } catch (...) {
        MessageBoxW(app.window, L"アプリのフォルダーへsettings.jsonを保存できませんでした。", L"設定保存エラー", MB_OK | MB_ICONWARNING);
    }
}

Pixels Decode(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame, UINT maxWidth, UINT maxHeight) {
    UINT width = 0, height = 0; winrt::check_hresult(frame->GetSize(&width, &height));
    if (!width || !height || uint64_t(width) * height > 64ULL * 1024 * 1024) throw std::runtime_error("invalid dimensions or pixel limit");
    double scale = std::min({ 1.0, double(maxWidth) / width, double(maxHeight) / height });
    Pixels result; result.width = std::max(1U, UINT(width * scale)); result.height = std::max(1U, UINT(height * scale));
    ComPtr<IWICBitmapScaler> scaler; winrt::check_hresult(factory->CreateBitmapScaler(&scaler));
    winrt::check_hresult(scaler->Initialize(frame, result.width, result.height, WICBitmapInterpolationModeFant));
    ComPtr<IWICFormatConverter> converter; winrt::check_hresult(factory->CreateFormatConverter(&converter));
    winrt::check_hresult(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    result.data.resize(size_t(result.width) * result.height * 4);
    winrt::check_hresult(converter->CopyPixels(nullptr, result.width * 4, static_cast<UINT>(result.data.size()), result.data.data()));
    return result;
}
void Metadata(IWICMetadataQueryReader* reader, const std::wstring& prefix, std::wstring& output, int depth, int& count, bool& unsupported, bool& failed) {
    if (depth > 24 || count > 20000) { unsupported = true; return; }
    ComPtr<IEnumString> names;
    if (FAILED(reader->GetEnumerator(&names))) { failed = true; return; }
    LPOLESTR name = nullptr; ULONG fetched = 0;
    HRESULT next;
    while ((next = names->Next(1, &name, &fetched)) == S_OK) {
        std::wstring tag(name); CoTaskMemFree(name); name = nullptr;
        PROPVARIANT value; PropVariantInit(&value);
        auto hr = reader->GetMetadataByName(tag.c_str(), &value);
        if (FAILED(hr)) { failed = true; PropVariantClear(&value); continue; }
        if (value.vt == VT_UNKNOWN && value.punkVal) {
            ComPtr<IWICMetadataQueryReader> child;
            if (SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(&child)))) Metadata(child.Get(), prefix + tag, output, depth + 1, count, unsupported, failed);
            else unsupported = true;
        } else {
            // Binary blobs are deliberately not interpreted as text.
            if (value.vt == VT_BLOB || value.vt == (VT_VECTOR | VT_UI1)) unsupported = true;
            else {
                PWSTR string = nullptr;
                if (SUCCEEDED(PropVariantToStringAlloc(value, &string))) {
                    output += prefix + tag + L"\r\n" + string + L"\r\n\r\n"; CoTaskMemFree(string); ++count;
                } else unsupported = true;
            }
        }
        PropVariantClear(&value);
        if (count > 20000) { unsupported = true; break; }
    }
    if (FAILED(next)) failed = true;
}
std::wstring Info(IWICBitmapDecoder* decoder, IWICBitmapFrameDecode* frame, const std::wstring& path) {
    UINT width = 0, height = 0; frame->GetSize(&width, &height);
    GUID format{}; decoder->GetContainerFormat(&format);
    std::wstring type = format == GUID_ContainerFormatPng ? L"PNG" : format == GUID_ContainerFormatJpeg ? L"JPEG" : L"その他";
    std::wstring output = L"ファイル名: " + fs::path(path).filename().wstring() + L"\r\nフルパス: " + path + L"\r\n形式: " + type +
        L"\r\nサイズ: " + std::to_wstring(fs::file_size(path)) + L" bytes\r\n画像: " + std::to_wstring(width) + L" × " + std::to_wstring(height);
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        FILETIME local{}; SYSTEMTIME time{};
        if (FileTimeToLocalFileTime(&attributes.ftLastWriteTime, &local) && FileTimeToSystemTime(&local, &time)) {
            wchar_t date[64]; swprintf_s(date, L"\r\nファイル更新日時: %04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond); output += date;
        }
    }
    output += L"\r\n\r\nメタデータ（WICで取得できたタグ）\r\n\r\n";
    int count = 0; bool unsupported = false, failed = false;
    ComPtr<IWICMetadataQueryReader> reader;
    auto hr = frame->GetMetadataQueryReader(&reader);
    if (SUCCEEDED(hr)) Metadata(reader.Get(), L"", output, 0, count, unsupported, failed);
    else if (hr != WINCODEC_ERR_UNSUPPORTEDOPERATION) failed = true;
    if (!count && !failed) output += L"WICで取得できるメタデータなし\r\n";
    if (failed) output += L"メタデータ読込みエラー（一部または全部）\r\n";
    if (unsupported) output += L"未対応データがあります。\r\n";
    output += L"\r\nこの版のメタデータ対応は暫定です。PNG圧縮テキスト・独自XMP等の完全対応は検証中です。";
    return output;
}
void Queue(Job job, bool priority) {
    if(job.kind==2){try{job.sourceKey=ThumbnailCache::Key(job.path);}catch(...){}}
    { std::lock_guard<std::mutex> lock(app.mutex); if (priority) app.jobs.push_front(std::move(job)); else app.jobs.push_back(std::move(job)); }
    app.wake.notify_all();
}
void UpdateThumbnailSpacing() {
    HDC dc = GetDC(app.list);
    auto font = reinterpret_cast<HFONT>(SendMessageW(app.list, WM_GETFONT, 0, 0));
    auto previous = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous); ReleaseDC(app.list, dc);
    int labelHeight = std::max(1L, metrics.tmHeight + metrics.tmExternalLeading);
    ListView_SetIconSpacing(app.list, app.activeThumbnailWidth + 8, app.activeThumbnailWidth * 3 / 4 + labelHeight + 8);
}
void ArrangeImages() {
    HDC dc = GetDC(app.list);
    auto font = reinterpret_cast<HFONT>(SendMessageW(app.list, WM_GETFONT, 0, 0));
    auto previous = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    auto fits = [&](const std::wstring& text) { SIZE size{}; GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size); return size.cx <= app.activeThumbnailWidth; };
    for (int i = 0; i < static_cast<int>(app.files.size()); ++i) {
        auto text = fs::path(app.files[i]).filename().wstring();
        if (i < static_cast<int>(app.thumbnailFailed.size()) && app.thumbnailFailed[i]) text += L" [読込みエラー]";
        if (!fits(text)) {
            while (!text.empty() && !fits(text + L"…")) {
                text.pop_back();
                if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF) text.pop_back();
            }
            text += L"…";
        }
        wchar_t current[512]{}; ListView_GetItemText(app.list, i, 0, current, ARRAYSIZE(current));
        if (text != current) ListView_SetItemText(app.list, i, 0, text.data());
    }
    SelectObject(dc, previous); ReleaseDC(app.list, dc);
    ListView_Arrange(app.list, LVA_DEFAULT);
}
void ResetThumbnailTracking() {
    app.thumbnailFailed.assign(app.files.size(), false);
    app.thumbnailDone.assign(app.files.size(), false); app.thumbnailRequested.assign(app.files.size(), false);
    app.itemSlots.assign(app.files.size(), -1); app.slotOwners.clear(); app.wantedFirst = 0; app.wantedLast = -1;
    app.thumbnailKeys.assign(app.files.size(),L"");
}
void ScheduleThumbnails() {
    if (app.folderPending || app.files.empty() || app.closing) return;
    RECT client{}; GetClientRect(app.list, &client);
    const int count = static_cast<int>(app.files.size());
    auto boundary = [&](bool end) {
        int low = 0, high = count;
        while (low < high) {
            int mid = low + (high - low) / 2; RECT r{}; ListView_GetItemRect(app.list, mid, &r, LVIR_BOUNDS);
            if (end ? r.top <= client.bottom : r.bottom < 0) low = mid + 1; else high = mid;
        }
        return low;
    };
    int first = boundary(false), last = boundary(true) - 1;
    app.thumbnailBudgetLimited = last - first + 1 > app.slotLimit;
    // Visible items precede a small prefetch margin. Both UI slots and queued work are bounded.
    int margin = std::min(32, std::max(0, (app.slotLimit - (last - first + 1)) / 2));
    app.wantedFirst = std::max(0, first - margin); app.wantedLast = std::min({ count - 1, last + margin, app.wantedFirst + app.slotLimit - 1 });
    if(app.selected>=first && app.selected<=last && app.selected>app.wantedLast){app.wantedLast=app.selected;app.wantedFirst=std::max(first,app.wantedLast-app.slotLimit+1);}
    std::lock_guard<std::mutex> lock(app.mutex);
    app.jobs.erase(std::remove_if(app.jobs.begin(), app.jobs.end(), [&](const Job& j) {
        if (j.kind != 1) return false;
        if (j.index >= 0 && j.index < count) app.thumbnailRequested[j.index] = false;
        return true;
    }), app.jobs.end());
    auto add = [&](int index) {
        if (index < app.wantedFirst || index > app.wantedLast || app.thumbnailDone[index] || app.thumbnailRequested[index]) return;
        app.thumbnailRequested[index] = true; app.jobs.push_back(ThumbnailJob(index));
    };
    add(app.selected);
    for (int i = first; i <= std::min(last, app.wantedLast); ++i) add(i);
    for (int i = app.wantedFirst; i < first; ++i) add(i);
    for (int i = last + 1; i <= app.wantedLast; ++i) add(i);
    app.wake.notify_all();
}
size_t ImageListBytes(HIMAGELIST images) {
    IMAGEINFO info{};
    if (!ImageList_GetImageInfo(images, 0, &info)) throw std::runtime_error("ImageList info");
    auto bytes = [](HBITMAP bitmap) {
        if (!bitmap) return size_t(0); BITMAP b{};
        if (!GetObjectW(bitmap, sizeof(b), &b)) throw std::runtime_error("ImageList bitmap");
        return size_t(b.bmWidthBytes) * abs(b.bmHeight);
    };
    return bytes(info.hbmImage) + (info.hbmMask != info.hbmImage ? bytes(info.hbmMask) : 0);
}
HIMAGELIST CreateThumbnails(int width, int& slots, size_t& retained) {
    size_t budget = size_t(app.cacheMB) * 1000000, canvas = size_t(width) * (width * 3 / 4) * 4;
    slots = static_cast<int>(std::min(size_t(ThumbnailSlots), budget / 3 / canvas - 5));
    auto images = ImageList_Create(width, width * 3 / 4, ILC_COLOR32, slots + 1, 1);
    auto placeholder = Thumbnail({}, width);
    bool ok = images && placeholder && ImageList_Add(images, placeholder, nullptr) == 0 && ImageList_SetImageCount(images, slots + 1);
    if (placeholder) DeleteObject(placeholder);
    try { if (ok) { retained = ImageListBytes(images); ok = retained < budget; } } catch (...) { ok = false; }
    if (!ok) { if (images) ImageList_Destroy(images); return nullptr; }
    return images;
}
void ApplyThumbnailSize(bool force) {
    KillTimer(app.window, ZoomTimer);
    if (!force && app.thumbnailWidth == app.activeThumbnailWidth) { SaveSettings(); return; }
    int slots = 0; size_t retained = 0; auto images = CreateThumbnails(app.thumbnailWidth, slots, retained);
    if (!images) {
        app.thumbnailWidth = app.activeThumbnailWidth; Counts(L"サムネイルサイズを変更できませんでした。"); return;
    }
    ++app.thumbnailGeneration; app.currentThumbnailGeneration = app.thumbnailGeneration;
    { std::lock_guard<std::mutex> lock(app.mutex); app.jobs.erase(std::remove_if(app.jobs.begin(), app.jobs.end(), [](auto& j) { return j.kind == 1; }), app.jobs.end()); }
    app.activeThumbnailWidth = app.thumbnailWidth;
    auto previous = ListView_SetImageList(app.list, images, LVSIL_NORMAL); app.thumbs = images;
    if (previous) ImageList_Destroy(previous);
    app.slotLimit = slots; app.imageBytes = retained;
    app.cache.SetLimit(size_t(app.cacheMB) * 1000000 - retained);
    UpdateThumbnailSpacing();
    app.loaded = 0; ResetThumbnailTracking();
    SendMessageW(app.list, WM_SETREDRAW, FALSE, 0);
    for (int i = 0; i < static_cast<int>(app.files.size()); ++i) {
        LVITEMW item{}; item.mask = LVIF_IMAGE; item.iItem = i; item.iImage = 0; ListView_SetItem(app.list, &item);
        auto name = fs::path(app.files[i]).filename().wstring(); ListView_SetItemText(app.list, i, 0, name.data());
    }
    SendMessageW(app.list, WM_SETREDRAW, TRUE, 0); ArrangeImages(); InvalidateRect(app.list, nullptr, TRUE);
    ScheduleThumbnails();
    SaveSettings(); Counts(L"サムネイル幅: " + std::to_wstring(app.activeThumbnailWidth) + L"px");
}
int WheelThumbnailWidth(int width, int& remainder, int delta) {
    remainder += delta;
    while (abs(remainder) >= WHEEL_DELTA) {
        int direction = remainder > 0 ? 1 : -1;
        remainder -= direction * WHEEL_DELTA;
        width = std::clamp(width + direction * ThumbnailStep, ThumbnailMin, ThumbnailMax);
    }
    return width;
}
LRESULT CALLBACK ImagesProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR) {
    if (message == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL)) {
        if(app.autoThumbnails){app.autoThumbnails=false;app.thumbnailWidth=std::clamp(((app.activeThumbnailWidth+16)/32)*32,64,256);SaveSettings();}
        int width = WheelThumbnailWidth(app.thumbnailWidth, app.zoomWheel, GET_WHEEL_DELTA_WPARAM(wparam));
        if (width != app.thumbnailWidth) {
            app.thumbnailWidth = width;
            SetTimer(app.window, ZoomTimer, 150, nullptr);
            Counts(L"サムネイル幅: " + std::to_wstring(width) + L"px");
        }
        return 0;
    }
    if (message == WM_MOUSEWHEEL) app.zoomWheel = 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ImagesProc, id);
    auto result = DefSubclassProc(window, message, wparam, lparam);
    if (message == WM_MOUSEWHEEL || message == WM_VSCROLL || message == WM_HSCROLL || message == WM_KEYDOWN || message == WM_SIZE)
        SetTimer(app.window, ThumbnailTimer, 30, nullptr);
    return result;
}
void Results() {
    std::deque<Result> results;
    { std::lock_guard<std::mutex> lock(app.mutex);
      for (int i = 0; i < 2 && !app.results.empty(); ++i) { results.push_back(std::move(app.results.front())); app.results.pop_front(); }
      if (!app.results.empty()) PostMessageW(app.window, WM_RESULTS, 0, 0); }
    app.wake.notify_all();
    for (auto& result : results) {
        auto& job = result.job;
        if(job.kind==6){if(!CurrentJob(job)||job.searchGeneration!=app.currentSearchGeneration)continue;
            if(result.progress){Counts(result.text);continue;}
            app.searching=false;SetWindowTextW(app.searchButton,L"検索");
            if(!result.error.empty()){Counts(L"検索失敗: "+result.error);continue;}
            if(!GuardTransition([query=job.query,preserve=job.preserve]{StartSearch(query,preserve);}))continue;
            app.appliedQuery=job.query;CommitDisplay(std::move(result.files),job.preserve);Counts((!job.preserve.empty()&&std::none_of(app.files.begin(),app.files.end(),[&](auto& file){return Same(file,job.preserve);})?L"選択画像は検索条件に一致しません。 ":L"")+std::wstring(L"検索完了: ")+std::to_wstring(app.files.size())+L"件"+(result.failures?L"（一部のメタデータを取得できませんでした）":L""));continue;
        }
        if(job.kind==4&&result.progress){if(app.saving)SetActivity(result.text);continue;}
        if(HandleBatchResult(result))continue;
        if(job.kind>=7){if(job.kind==7){app.backupBusy=false;UpdateEditorControls();if(CurrentJob(job)&&result.error.empty())ChooseBackupDeletion(result.backups);else Counts(L"バックアップ一覧取得失敗: "+result.error);}
            else{app.backupBusy=false;UpdateEditorControls();app.cache.Clear();OpenFolder(app.folder,true);auto message=result.error.empty()?L"バックアップ整理完了。削除できなかった件数: "+std::to_wstring(result.failures):L"バックアップ整理失敗: "+result.error;if(!result.text.empty())message+=L"\r\n"+result.text;Counts(message);if(result.failures||!result.error.empty()||!result.text.empty())MessageBoxW(app.window,message.c_str(),L"バックアップ整理",MB_OK|MB_ICONWARNING);}continue;
        }
        if (job.kind == 4) {
            StopActivity();app.saving=false; app.editorUi.Enable(true);
            if(!result.error.empty()) {
                app.afterSave={}; SetWindowTextW(app.editStatus,(L"保存失敗: "+result.error).c_str()); UpdateEditorControls();
            } else {
                auto next=std::move(app.afterSave); app.afterSave={}; app.editing=false; app.cache.Clear();
                SetWindowTextW(app.editStatus,(L"保存完了: "+job.destination).c_str());UpdateEditorControls();Layout();
                if(next){app.transitionBypass=true;next();app.transitionBypass=false;if(app.closing)return;}
                else { auto parent=fs::path(job.destination).parent_path().wstring(); OpenFolder(parent,SameFolderAddress(parent,app.folder),job.destination); }
                Counts(L"保存完了: "+job.destination);if(!result.text.empty())MessageBoxW(app.window,result.text.c_str(),L"保存後の確認",MB_OK|MB_ICONWARNING);
            }
            continue;
        }
        if (job.kind == 3) {
            if (!result.error.empty()) {
                Counts(L"Windowsフォト起動エラー");
                MessageBoxW(app.window, (job.path + L"\r\n\r\n" + result.error).c_str(), L"Windowsフォト起動エラー", MB_OK | MB_ICONWARNING);
            } else Counts(L"Windowsフォトに画像を渡しました。");
            continue;
        }
        if (!CurrentJob(job)) continue;
        if((job.kind==1||job.kind==2)&&!job.sourceKey.empty()){
            bool changed=false;
            try{changed=job.sourceKey!=ThumbnailCache::Key(job.path);}catch(...){changed=true;}
            if(changed){result.error=L"ファイルが外部で変更・削除されました。ロードで再読込みしてください。";
                if(job.index==app.selected){app.previewPixels={};app.previewFailed=true;InvalidateRect(app.preview,nullptr,TRUE);}}
        }
        if (job.kind == 0) {
            app.folderPending = false;
            if (!result.error.empty()) {
                // A failed folder change keeps the old list, including unfinished work.
                app.thumbnailRequested.assign(app.files.size(), false); ScheduleThumbnails();
                if (app.selected >= 0 && app.previewPixels.data.empty())
                    Queue({ 2, app.generation, app.selection, app.selected, app.files[app.selected], L"" }, true);
                Counts(L"フォルダーを開けません: " + result.error); continue;
            }
            app.folder=job.path;app.last=job.path;app.allFiles=std::move(result.files);
            app.recent.erase(std::remove_if(app.recent.begin(),app.recent.end(),[&](auto& p){return SameFolderAddress(p,app.folder);}),app.recent.end());
            app.recent.insert(app.recent.begin(),app.folder);if(app.recent.size()>HistoryLimit)app.recent.resize(HistoryLimit);
            RefreshHistory();SetWindowTextW(app.path,app.folder.c_str());SaveSettings();
            if(app.appliedQuery.empty())CommitDisplay(app.allFiles,job.preserve);else StartSearch(app.appliedQuery,job.preserve);
        } else if (job.kind == 1) {
            app.thumbnailRequested[job.index] = false;
            if (job.index < app.wantedFirst || job.index > app.wantedLast) continue;
            if (app.thumbnailDone[job.index]) continue;
            app.thumbnailDone[job.index] = true;
            ++app.loaded;
            if (result.error.empty()) {
                int slot = -1;
                if (app.slotOwners.size() < app.slotLimit) slot = static_cast<int>(app.slotOwners.size());
                else for (int i = 0; i < app.slotLimit; ++i) {
                    int owner = app.slotOwners[i];
                    if (owner < app.wantedFirst || owner > app.wantedLast) {
                        slot = i; break;
                    }
                }
                if (slot < 0) { app.thumbnailDone[job.index] = false; continue; }
                auto bitmap = Thumbnail(result.pixels, app.activeThumbnailWidth);
                if (!bitmap) { Counts(L"サムネイル用メモリを確保できません。"); continue; }
                int image = slot + 1;
                if (image >= ImageList_GetImageCount(app.thumbs)) image = ImageList_Add(app.thumbs, bitmap, nullptr);
                else if (!ImageList_Replace(app.thumbs, image, bitmap, nullptr)) image = -1;
                if (bitmap) DeleteObject(bitmap);
                if (image < 0) { Counts(L"サムネイルを画像一覧に登録できません。"); continue; }
                if (slot == static_cast<int>(app.slotOwners.size())) app.slotOwners.push_back(job.index);
                else {
                    int owner = app.slotOwners[slot];
                    LVITEMW old{}; old.mask = LVIF_IMAGE; old.iItem = owner; old.iImage = 0; ListView_SetItem(app.list, &old);
                    app.itemSlots[owner] = -1; app.thumbnailDone[owner] = false; app.slotOwners[slot] = job.index;
                }
                app.itemSlots[job.index] = slot;
                app.thumbnailKeys[job.index]=job.sourceKey;
                LVITEMW item{}; item.mask = LVIF_IMAGE; item.iItem = job.index; item.iImage = image; ListView_SetItem(app.list, &item);
                if(job.index==app.selected){app.previewPixels=DisplayedThumbnail(job.index);InvalidateRect(app.preview,nullptr,TRUE);UpdateEditorControls();}
            } else {
                if(job.index==app.selected){app.previewFailed=true;app.previewPixels={};app.metadataReady=false;UpdateEditorControls();InvalidateRect(app.preview,nullptr,TRUE);}
                app.thumbnailFailed[job.index] = true;
                ArrangeImages();
            }
            bool ready = true;
            for (int i = app.wantedFirst; i <= app.wantedLast; ++i) ready = ready && app.thumbnailDone[i];
            if (ready) Counts(app.thumbnailBudgetLimited ? L"キャッシュ上限により表示範囲の一部を保持しています。" : L"表示範囲のサムネイル読込み完了");
        } else if (job.selection == app.selection && job.index == app.selected) {
            app.editable=std::move(result.editable); app.editError=result.editError;
            app.metadataReady=result.error.empty() && result.editError.empty() && !app.previewFailed; UpdateEditorControls();
            auto text = result.error.empty() ? result.text : job.path + L"\r\n\r\n読込みエラー: " + result.error;
            if (!result.error.empty()) text += L"\r\n" + result.summary.notes + result.summary.xmpDetail;
            ShowSummary(result.summary, result.error.empty() ? L"" : L"画像読込みエラー。解像度を取得できません。\r\n");
            SetWindowTextW(app.details, text.c_str()); InvalidateRect(app.preview, nullptr, TRUE); Counts(result.error.empty() ? L"表示完了" : L"画像読込みエラー");
        }
    }
}
namespace {
bool recordBlink=false;
void UpdateRecordingControls(bool tick=false){auto on=stage3::Recording();recordBlink=on?(tick?!recordBlink:recordBlink):false;auto button=GetDlgItem(app.window,RecordToggle);auto text=on?L"記録停止":L"記録開始";if(Text(button)!=text)SetWindowTextW(button,text);InvalidateRect(button,nullptr,FALSE);InvalidateRect(GetDlgItem(app.window,RecordIndicator),nullptr,FALSE);}
bool DrawRecording(DRAWITEMSTRUCT* draw){if(draw->CtlID!=RecordToggle&&draw->CtlID!=RecordIndicator)return false;int saved=SaveDC(draw->hDC);bool on=stage3::Recording();auto rect=draw->rcItem;
    if(draw->CtlID==RecordIndicator){FillRect(draw->hDC,&rect,ThemeBrush());auto brush=CreateSolidBrush(on&&recordBlink?RGB(220,35,35):RGB(135,135,135));auto oldBrush=SelectObject(draw->hDC,brush);auto oldPen=SelectObject(draw->hDC,GetStockObject(NULL_PEN));int size=std::min(rect.right-rect.left,rect.bottom-rect.top)-10;int x=(rect.left+rect.right-size)/2,y=(rect.top+rect.bottom-size)/2;Ellipse(draw->hDC,x,y,x+size,y+size);SelectObject(draw->hDC,oldBrush);SelectObject(draw->hDC,oldPen);DeleteObject(brush);}
    else {auto color=on?RGB(185,25,25):RGB(0,90,190);if(draw->itemState&ODS_SELECTED)color=on?RGB(140,15,15):RGB(0,65,140);auto brush=CreateSolidBrush(color);FillRect(draw->hDC,&rect,brush);DeleteObject(brush);FrameRect(draw->hDC,&rect,GetSysColorBrush(COLOR_GRAYTEXT));auto font=SendMessageW(draw->hwndItem,WM_GETFONT,0,0);if(font)SelectObject(draw->hDC,reinterpret_cast<HGDIOBJ>(font));SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,RGB(255,255,255));auto text=Text(draw->hwndItem);DrawTextW(draw->hDC,text.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(draw->itemState&ODS_FOCUS){InflateRect(&rect,-3,-3);DrawFocusRect(draw->hDC,&rect);}}
    RestoreDC(draw->hDC,saved);return true;
}
}
void Layout() {
    RECT r{}; GetClientRect(app.window, &r); int width = r.right, height = r.bottom;
    // Move the toolbar as one batch and discard copied pixels from old positions.
    // Sequential MoveWindow(..., TRUE) can repaint while siblings still overlap.
    struct ToolbarPosition { HWND window; int x, y, width, height; };
    bool wrap=width<700;int recordingY=wrap?110:76,placementX=wrap?8:width-332;int listTop=wrap?144:110;
    const ToolbarPosition toolbar[]{
        { app.path, 8, 8, std::max(80, width - 248), 26 },
        { GetDlgItem(app.window, OpenPath), width - 232, 8, 64, 26 },
        { GetDlgItem(app.window, Browse), width - 160, 8, 80, 26 },
        { GetDlgItem(app.window, ResetView), width-72,8,64,26 },
        { app.history, 8, 42, std::max(120,width/3), 280 },
        { app.searchEdit, 16+std::max(120,width/3),42,std::max(100,width-96-std::max(120,width/3)),26 },
        { app.searchButton,width-72,42,64,26 },
        { GetDlgItem(app.window, Bookmarks),8,76,120,26 },
        { GetDlgItem(app.window, Options),136,76,116,26 },
        { GetDlgItem(app.window, Help),260,76,88,26 },
        { app.placement,placementX,recordingY,172,180 },
        { GetDlgItem(app.window,RecordToggle),placementX+180,recordingY,104,26 },
        { GetDlgItem(app.window,RecordIndicator),placementX+292,recordingY,24,26 }
    };
    auto positions = BeginDeferWindowPos(ARRAYSIZE(toolbar));
    for (const auto& control : toolbar) {
        if (!positions) break;
        positions = DeferWindowPos(positions, control.window, nullptr, control.x, control.y, control.width, control.height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
    if (!positions || !EndDeferWindowPos(positions)) {
        for (const auto& control : toolbar) SetWindowPos(control.window, nullptr, control.x, control.y, control.width, control.height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
    RECT list{ 8, listTop, width - 8, height - 36 }, side = list;
    bool noSide=app.layout==L"none";
    for(auto h:{app.preview,app.metadataTabs})ShowWindow(h,noSide?SW_HIDE:SW_SHOW);
    if (noSide) {}
    else if (app.layout == L"bottom") { int split = std::max(listTop+80,height-36-std::clamp(app.sideHeight,120,std::max(120,height-listTop-116))); list.bottom = split - 4; side.top = split + 4; }
    else {
        int sideWidth = std::clamp(app.sideWidth,280,std::max(280,width-176));
        if (app.layout == L"left") { side.right = 8 + sideWidth; list.left = side.right + 8; }
        else { side.left = width - 8 - sideWidth; list.right = side.left - 8; }
    }
    app.splitter=noSide?RECT{}:app.layout==L"bottom"?RECT{8,list.bottom,width-8,side.top}:app.layout==L"left"?RECT{side.right,listTop,list.left,height-36}:RECT{list.right,listTop,side.left,height-36};
    MoveWindow(app.list, list.left, list.top, std::max(1L, list.right - list.left), std::max(1L, list.bottom - list.top), TRUE);
    if (app.layout == L"bottom") {
        int previewWidth = (side.right - side.left) / 3;
        MoveWindow(app.preview, side.left, side.top, previewWidth, std::max(1L, side.bottom - side.top), TRUE);
        MoveWindow(app.metadataTabs, side.left + previewWidth + 8, side.top, std::max(1L, side.right - side.left - previewWidth - 8), std::max(1L, side.bottom - side.top), TRUE);
    } else {
        int previewHeight = std::max(60L, (side.bottom - side.top) / 3);
        MoveWindow(app.preview, side.left, side.top, side.right - side.left, previewHeight, TRUE);
        MoveWindow(app.metadataTabs, side.left, side.top + previewHeight + 8, side.right - side.left, std::max(1L, side.bottom - side.top - previewHeight - 8), TRUE);
    }
    RECT page{}; GetClientRect(app.metadataTabs, &page); TabCtrl_AdjustRect(app.metadataTabs, FALSE, &page);
    MapWindowPoints(app.metadataTabs, app.window, reinterpret_cast<POINT*>(&page), 2);
    MoveWindow(app.simple,page.left,page.top,std::max(1L,page.right-page.left),std::max(1L,page.bottom-page.top),FALSE);
    bool detailed = TabCtrl_GetCurSel(app.metadataTabs) == 1;
    MoveWindow(app.editButton,page.left,page.top,64,26,TRUE);MoveWindow(app.saveButton,page.left+68,page.top,64,26,TRUE);MoveWindow(app.cancelButton,page.left+136,page.top,80,26,TRUE);
    MoveWindow(app.editStatus,page.left,page.top+29,std::max(1L,page.right-page.left),38,TRUE);
    MoveWindow(app.details,page.left,page.top+68,std::max(1L,page.right-page.left),std::max(1L,page.bottom-page.top-68),TRUE);
    for(auto h:{app.editButton,app.saveButton,app.cancelButton,app.editStatus})ShowWindow(h,detailed?SW_SHOW:SW_HIDE);
    app.editorUi.Place(page.left,page.top+68,page.right-page.left,page.bottom-page.top-68,detailed&&app.editing);
    ShowWindow(app.details, detailed && !app.editing ? SW_SHOW : SW_HIDE); ShowWindow(app.simple, TabCtrl_GetCurSel(app.metadataTabs)==0 ? SW_SHOW : SW_HIDE);
    LayoutOcrUi(page,!noSide&&TabCtrl_GetCurSel(app.metadataTabs)==2);
    LayoutSimple();
    if(noSide){for(auto h:{app.simple,app.details,app.editButton,app.saveButton,app.cancelButton,app.editStatus})ShowWindow(h,SW_HIDE);app.editorUi.Place(page.left,page.top+68,page.right-page.left,page.bottom-page.top-68,false);}
    MoveWindow(app.status, 8, height - 28, width - 16, 24, TRUE);
    if(app.autoThumbnails){int available=std::max(64L,list.right-list.left-GetSystemMetrics(SM_CXVSCROLL)-4);int columns=std::max(1,(available+263)/264);int desired=std::clamp(available/columns-8,64,256);if(desired!=app.thumbnailWidth){app.thumbnailWidth=desired;SetTimer(app.window,ZoomTimer,150,nullptr);}}
    ArrangeImages();
    // Erase the vacated button locations as well as repainting the controls.
    RECT toolbarArea{ 0, 0, width, height-30 };
    RedrawWindow(app.window, &toolbarArea, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}
void LayoutSimple() {
    if (!app.simple) return;
    RECT area{}; GetClientRect(app.simple, &area);
    int labelWidth = 104, valueWidth = std::max(40L, area.right - labelWidth - 24);
    HDC dc = GetDC(app.simple); auto previous = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT)); TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics); SelectObject(dc, previous); ReleaseDC(app.simple, dc);
    int y = 8;
    for (size_t i = 0; i < app.simpleValues.size(); ++i) {
        MoveWindow(app.simpleValues[i], labelWidth + 12, y - app.simpleScroll, valueWidth, 24, FALSE);
        int lines = static_cast<int>(SendMessageW(app.simpleValues[i], EM_GETLINECOUNT, 0, 0));
        int height = std::max(26, lines * static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading) + 8);
        MoveWindow(app.simpleLabels[i], 8, y + 3 - app.simpleScroll, labelWidth, 24, FALSE);
        MoveWindow(app.simpleValues[i], labelWidth + 12, y - app.simpleScroll, valueWidth, height, FALSE);
        y += height + 8;
    }
    MoveWindow(groupButton,8,y-app.simpleScroll,std::max(40L,area.right-16),26,FALSE);y+=34;
    auto note = Text(app.simpleNote);
    MoveWindow(app.simpleNote, 8, y - app.simpleScroll, std::max(40L, area.right - 16), 24, FALSE);
    int lines = static_cast<int>(SendMessageW(app.simpleNote, EM_GETLINECOUNT, 0, 0));
    int noteHeight = note.empty() ? 0 : std::max(26, lines * static_cast<int>(metrics.tmHeight + metrics.tmExternalLeading) + 8);
    MoveWindow(app.simpleNote, 8, y - app.simpleScroll, std::max(40L, area.right - 16), std::max(1, noteHeight), FALSE);
    app.simpleContentHeight = y + noteHeight + 8;
    int position = std::clamp(app.simpleScroll, 0, std::max(0, app.simpleContentHeight - static_cast<int>(area.bottom)));
    SCROLLINFO scroll{ sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS }; scroll.nMax = app.simpleContentHeight - 1; scroll.nPage = area.bottom; scroll.nPos = position; SetScrollInfo(app.simple, SB_VERT, &scroll, TRUE);
    if (position != app.simpleScroll) { app.simpleScroll = position; LayoutSimple(); return; }
    // Repaint unchanged labels as well as values, after all positions are final.
    // The pane clips children, so invalidating only its background is insufficient.
    RedrawWindow(app.simple, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
LRESULT CALLBACK SimpleProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if(message==WM_COMMAND&&LOWORD(wparam)==1810){OpenGroupPage();return 0;}
    if (message == WM_VSCROLL || message == WM_MOUSEWHEEL) {
        RECT area{}; GetClientRect(window, &area); int position = app.simpleScroll;
        if (message == WM_MOUSEWHEEL) {
            app.simpleWheel += GET_WHEEL_DELTA_WPARAM(wparam);
            while (abs(app.simpleWheel) >= WHEEL_DELTA) { int direction = app.simpleWheel > 0 ? -1 : 1; position += direction * 60; app.simpleWheel += direction * WHEEL_DELTA; }
        } else {
            switch (LOWORD(wparam)) {
            case SB_LINEUP: position -= 24; break; case SB_LINEDOWN: position += 24; break;
            case SB_PAGEUP: position -= area.bottom; break; case SB_PAGEDOWN: position += area.bottom; break;
            case SB_TOP: position = 0; break; case SB_BOTTOM: position = app.simpleContentHeight; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: { SCROLLINFO scroll{ sizeof(scroll), SIF_TRACKPOS }; GetScrollInfo(window, SB_VERT, &scroll); position = scroll.nTrackPos; break; }
            }
        }
        app.simpleScroll = std::clamp(position, 0, std::max(0, app.simpleContentHeight - static_cast<int>(area.bottom))); LayoutSimple(); return 0;
    }
    if (message == WM_SIZE) { LayoutSimple(); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
LRESULT CALLBACK SummaryValueProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR) {
    if (message == WM_MOUSEWHEEL) return SendMessageW(app.simple, message, wparam, lparam);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, SummaryValueProc, id);
    return DefSubclassProc(window, message, wparam, lparam);
}
void BrowseFolder() {
    ComPtr<IFileOpenDialog> dialog;
    auto hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) { DWORD options = 0; hr = dialog->GetOptions(&options); if (SUCCEEDED(hr)) hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM); }
    if (SUCCEEDED(hr)) hr = dialog->Show(app.window);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    ComPtr<IShellItem> item; if (SUCCEEDED(hr)) hr = dialog->GetResult(&item);
    PWSTR path = nullptr; if (SUCCEEDED(hr)) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (SUCCEEDED(hr)) { LoadAddress(path); CoTaskMemFree(path); } else Counts(L"フォルダー選択エラー: " + ErrorText(hr));
}
void LoadAddress(const std::wstring& address) {
    try {
        auto path = Normalize(address);
        if (!fs::is_directory(path)) throw std::runtime_error("folder");
        SetWindowTextW(app.path, path.c_str()); OpenFolder(path, SameFolderAddress(path, app.folder));
    } catch (...) { Counts(L"無効なフォルダーパスです。現在の一覧を保持します。"); }
}
bool AddBookmark(const std::wstring& address) {
    try {
        auto path = Normalize(address);
        if (!fs::is_directory(path)) throw std::runtime_error("folder");
        if (std::any_of(app.bookmarks.begin(), app.bookmarks.end(), [&](auto& p) { return SameFolderAddress(p, path); })) { Counts(L"既にブックマークに登録されています。"); return false; }
        if (app.bookmarks.size() >= 1000) { Counts(L"ブックマークの登録上限は1000件です。"); return false; }
        app.bookmarks.push_back(path); SaveSettings(); Counts(L"ブックマークに追加しました。"); return true;
    } catch (...) { Counts(L"ブックマークに追加できません。フォルダーを確認してください。"); return false; }
}
void RemoveBookmark(size_t index) {
    if (index >= app.bookmarks.size()) return;
    app.bookmarks.erase(app.bookmarks.begin() + index); SaveSettings(); Counts(L"ブックマークを削除しました。");
}
INT_PTR CALLBACK CacheDialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(dialog, DWLP_USER, lparam); SetWindowTextW(dialog, L"画像サムネイルキャッシュ上限");
        auto child = [&](const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int width, int height, int id) {
            auto control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return control;
        };
        child(L"STATIC", L"上限（MB、16〜2048の整数）", 0, 12, 12, 350, 24, 0);
        child(L"EDIT", std::to_wstring(*reinterpret_cast<int*>(lparam)).c_str(), WS_BORDER | WS_TABSTOP | ES_NUMBER, 12, 40, 140, 26, 100);
        child(L"STATIC", L"一覧とメモリキャッシュのサムネイル保持分が対象です。", 0, 12, 74, 420, 24, 0);
        child(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 200, 112, 80, 26, IDOK);
        child(L"BUTTON", L"キャンセル", WS_TABSTOP, 290, 112, 100, 26, IDCANCEL);
        SendDlgItemMessageW(dialog, 100, EM_SETLIMITTEXT, 4, 0); SetFocus(GetDlgItem(dialog, 100)); SendDlgItemMessageW(dialog, 100, EM_SETSEL, 0, -1); return FALSE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDOK) {
        auto text = Text(GetDlgItem(dialog, 100));
        int value = 0;
        if (!ParseCacheLimitInput(text, value)) {
            MessageBoxW(dialog, L"16〜2048MBの整数を入力してください。", L"入力エラー", MB_OK | MB_ICONWARNING); return TRUE;
        }
        *reinterpret_cast<int*>(GetWindowLongPtrW(dialog, DWLP_USER)) = value; EndDialog(dialog, IDOK); return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) { EndDialog(dialog, IDCANCEL); return TRUE; }
    return FALSE;
}
void ChangeCacheLimit(int mb) {
    if (mb < 16 || mb > 2048 || mb == app.cacheMB) return;
    int previous = app.cacheMB; auto generation = app.thumbnailGeneration;
    app.cacheMB = mb; ApplyThumbnailSize(true);
    if (generation == app.thumbnailGeneration) app.cacheMB = previous;
    else Counts(L"画像サムネイルキャッシュ上限: " + std::to_wstring(mb) + L" MB");
}
void ToggleHistorySaving() {
    app.saveHistory = !app.saveHistory;
    if (!app.saveHistory) { app.recent.clear(); if (!app.folder.empty()) app.recent.push_back(app.folder); RefreshHistory(); }
    SaveSettings(); Counts(app.saveHistory ? L"フォルダ履歴保存: ON" : L"フォルダ履歴保存: OFF（起動中のみ保持）");
}
INT_PTR ShowCacheLimitDialog(int& mb) {
    struct { DLGTEMPLATE dialog; WORD menu, windowClass, title; } resource{};
    resource.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    resource.dialog.cx = 240; resource.dialog.cy = 96;
    return DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &resource.dialog, app.window, CacheDialog, reinterpret_cast<LPARAM>(&mb));
}
HMENU BuildOptionsMenu() {
    auto menu = CreatePopupMenu();
    auto label = L"画像サムネイルキャッシュ上限: " + std::to_wstring(app.cacheMB) + L" MB…";
    AppendMenuW(menu, MF_STRING, 1, label.c_str());
    AppendMenuW(menu, MF_STRING | (app.saveHistory ? MF_CHECKED : MF_UNCHECKED), 2, app.saveHistory ? L"フォルダ履歴保存: ON" : L"フォルダ履歴保存: OFF");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING|(app.autoBackup?MF_CHECKED:0),3,app.autoBackup?L"自動バックアップ: ON":L"自動バックアップ: OFF");
    auto backup=L"バックアップ作成数上限: "+std::to_wstring(app.backupLimit)+L"…";AppendMenuW(menu,MF_STRING,4,backup.c_str());
    AppendMenuW(menu,MF_STRING,5,L"バックアップ削除…");
    AppendMenuW(menu,MF_STRING|(app.darkTheme?MF_CHECKED:0),6,app.darkTheme?L"ナイトモード: ON（ダーク）":L"ナイトモード: OFF（ライト）");
    AppendMenuW(menu,MF_STRING|(app.metadataSearch?MF_CHECKED:0),7,app.metadataSearch?L"メタデータ検索モード: ON":L"メタデータ検索モード: OFF");

    AppendMenuW(menu,MF_STRING|(app.autoThumbnails?MF_CHECKED:0),9,app.autoThumbnails?L"サムネイルサイズ自動化: ON":L"サムネイルサイズ自動化: OFF");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,8,L"記録設定…");
    return menu;
}
void OptionsMenu() {
    auto menu = BuildOptionsMenu();
    RECT rect{}; GetWindowRect(GetDlgItem(app.window, Options), &rect);
    auto action = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN, rect.left, rect.bottom, app.window, nullptr); DestroyMenu(menu);
    if(action==8){stage3::ShowMonitorDialog();UpdateRecordingControls();}
    else if (action >= 3) NewOptionsAction(static_cast<int>(action));
    else if (action == 1) {
        int mb = app.cacheMB;
        if (ShowCacheLimitDialog(mb) == IDOK) ChangeCacheLimit(mb);
    } else if (action == 2) ToggleHistorySaving();
}
HMENU BuildBookmarkMenu() {
    auto menu = CreatePopupMenu(), remove = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"このアドレスを追加");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    auto escape = [](std::wstring path) { size_t position = 0; while ((position = path.find(L'&', position)) != std::wstring::npos) { path.insert(position, 1, L'&'); position += 2; } return path; };
    for (size_t i = 0; i < app.bookmarks.size(); ++i) {
        auto label = escape(app.bookmarks[i]);
        AppendMenuW(menu, MF_STRING, 1000 + i, label.c_str()); AppendMenuW(remove, MF_STRING, 3000 + i, label.c_str());
    }
    if (app.bookmarks.empty()) { AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"登録なし"); DestroyMenu(remove); }
    else { AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(remove), L"ブックマークを削除"); }
    return menu;
}
void BookmarkMenu() {
    auto menu = BuildBookmarkMenu();
    RECT rect{}; GetWindowRect(GetDlgItem(app.window, Bookmarks), &rect);
    auto action = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN, rect.left, rect.bottom, app.window, nullptr); DestroyMenu(menu);
    if (action == 1) AddBookmark(Text(app.path));
    else if (action >= 1000 && action < 1000 + app.bookmarks.size()) LoadAddress(app.bookmarks[action - 1000]);
    else if (action >= 3000 && action < 3000 + app.bookmarks.size()) RemoveBookmark(action - 3000);
}
HWND Control(LPCWSTR type, LPCWSTR text, DWORD style, int id) {
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return control;
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        app.window = window;
        app.path = Control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, PathEdit);
        Control(L"BUTTON", L"ロード", WS_TABSTOP, OpenPath); Control(L"BUTTON", L"フォルダー", WS_TABSTOP, Browse);
        Control(L"BUTTON", L"オプション ▼", WS_TABSTOP, Options); Control(L"BUTTON", L"ブックマーク ▼", WS_TABSTOP, Bookmarks);
        Control(L"BUTTON",L"リセット",WS_TABSTOP,ResetView);Control(L"BUTTON",L"ヘルプ ▼",WS_TABSTOP,Help);
        Control(L"BUTTON",L"記録開始",WS_TABSTOP|BS_OWNERDRAW,RecordToggle);Control(L"STATIC",L"記録状態",SS_OWNERDRAW,RecordIndicator);SetTimer(window,RecordTimer,500,nullptr);
        app.searchEdit=Control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,SearchEdit);
        SendMessageW(app.searchEdit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"検索文字列（Enter／ボタンで実行）"));
        SetWindowSubclass(app.searchEdit,SearchEditProc,40,0);app.searchButton=Control(L"BUTTON",L"検索",WS_TABSTOP,SearchButton);
        app.history = Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, History);
        app.placement = Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP, Placement);
        for (auto label : { L"サイドビュー: 右", L"サイドビュー: 左", L"サイドビュー: 下", L"サイドビュー: なし" }) SendMessageW(app.placement, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(app.placement, CB_SETCURSEL, app.layout == L"left" ? 1 : app.layout == L"bottom" ? 2 : app.layout == L"none" ? 3 : 0, 0);
        app.list = Control(WC_LISTVIEWW, L"", LVS_ICON | LVS_AUTOARRANGE | LVS_NOLABELWRAP | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, Images);
        SetWindowSubclass(app.list, ImagesProc, 1, 0);
        ListView_SetExtendedListViewStyle(app.list, LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
        UpdateThumbnailSpacing();
        app.thumbs = CreateThumbnails(app.activeThumbnailWidth, app.slotLimit, app.imageBytes);
        if (!app.thumbs) return -1;
        app.cache.SetLimit(size_t(app.cacheMB) * 1000000 - app.imageBytes); ListView_SetImageList(app.list, app.thumbs, LVSIL_NORMAL);
        app.preview = Control(L"KuniPreview", L"", WS_BORDER, 0);
        app.metadataTabs = Control(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, MetadataTabs);
        for (auto label : { L"シンプル", L"詳細", L"OCR" }) { TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<LPWSTR>(label); TabCtrl_InsertItem(app.metadataTabs, TabCtrl_GetItemCount(app.metadataTabs), &item); }
        app.simple = Control(L"KuniSimple", L"", WS_VSCROLL | WS_CLIPCHILDREN, 0);
        SetWindowLongPtrW(app.simple, GWL_EXSTYLE, GetWindowLongPtrW(app.simple, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
        const wchar_t* labels[]{ L"ファイル名", L"容量", L"解像度", L"作成日時", L"撮影者", L"ワールド名", L"ワールドID", L"モデル", L"タグ", L"同室者", L"モデル候補" };
        for (size_t i = 0; i < app.simpleValues.size(); ++i) {
            app.simpleLabels[i] = Control(L"STATIC", labels[i], SS_LEFT, 0); SetParent(app.simpleLabels[i], app.simple);
            app.simpleValues[i] = Control(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_TABSTOP, 0); SetParent(app.simpleValues[i], app.simple);
            SendMessageW(app.simpleValues[i], EM_SETLIMITTEXT, 0x7ffffffe, 0); SetWindowSubclass(app.simpleValues[i], SummaryValueProc, 1, 0);
        }
        groupButton=Control(L"BUTTON",L"グループページを開く",WS_TABSTOP,1810);SetParent(groupButton,app.simple);EnableWindow(groupButton,FALSE);
        app.simpleNote = Control(L"EDIT", L"画像を選択してください。", ES_MULTILINE | ES_READONLY | WS_TABSTOP, 0); SetParent(app.simpleNote, app.simple);
        SendMessageW(app.simpleNote, EM_SETLIMITTEXT, 0x7ffffffe, 0); SetWindowSubclass(app.simpleNote, SummaryValueProc, 1, 0);
        app.details = Control(L"EDIT", L"画像を選択してください。", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP, Details);
        SendMessageW(app.details, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        app.editButton=Control(L"BUTTON",L"編集",WS_TABSTOP,EditMetadata);
        app.saveButton=Control(L"BUTTON",L"保存",WS_TABSTOP,SaveMetadata);
        app.cancelButton=Control(L"BUTTON",L"キャンセル",WS_TABSTOP,CancelMetadata);
        app.editStatus=Control(L"STATIC",L"PNG/JPEG/WebPの上書き・別名コピー保存に対応。",SS_LEFT,0);
        app.editorUi.Create(window);CreateOcrUi();UpdateEditorControls();
        app.status = Control(L"STATIC", L"フォルダーを選択してください。", SS_LEFT, 0);
        RefreshHistory(); ApplyTheme(); Layout(); auto cpus=std::thread::hardware_concurrency();app.thumbnailWorkers.resize(cpus?std::clamp(cpus,1u,4u):2u);app.activeWorkers=static_cast<unsigned>(2+app.thumbnailWorkers.size());app.searchWorker=std::thread(SearchWorker); app.worker = std::thread(Worker, false);
        for (auto& worker : app.thumbnailWorkers) worker = std::thread(Worker, true);
        if (!app.settingsError.empty()) MessageBoxW(window, app.settingsError.c_str(), L"設定読込みエラー", MB_OK | MB_ICONWARNING);
        if (!app.last.empty()) { SetWindowTextW(app.path, app.last.c_str()); OpenFolder(app.last); }
        return 0;
    }
    case WM_ERASEBKGND: {RECT r{};GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(wparam),&r,ThemeBrush());if(app.layout!=L"none"){auto brush=CreateSolidBrush(app.darkTheme?RGB(70,70,70):RGB(210,210,210));FillRect(reinterpret_cast<HDC>(wparam),&app.splitter,brush);DeleteObject(brush);}return 1;}
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: ApplyTheme();return 0;
    case WM_MEASUREITEM: {auto measure=reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);if(measure->CtlType==ODT_COMBOBOX){measure->itemHeight=22;return TRUE;}break;}
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: return ThemeControlColor(reinterpret_cast<HDC>(wparam));
    case WM_DRAWITEM: if(DrawRecording(reinterpret_cast<DRAWITEMSTRUCT*>(lparam)))return TRUE;if(ThemeDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lparam)))return TRUE;break;
    case WM_SETCURSOR: {POINT p{};GetCursorPos(&p);ScreenToClient(window,&p);if(PtInRect(&app.splitter,p)){SetCursor(LoadCursorW(nullptr,app.layout==L"bottom"?IDC_SIZENS:IDC_SIZEWE));return TRUE;}break;}
    case WM_LBUTTONDOWN: {POINT p{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};if(PtInRect(&app.splitter,p)){app.draggingSplitter=true;SetCapture(window);return 0;}break;}
    case WM_MOUSEMOVE: if(app.draggingSplitter){RECT r{};GetClientRect(window,&r);if(app.layout==L"bottom")app.sideHeight=std::clamp(int(r.bottom-36-GET_Y_LPARAM(lparam)),120,10000);else app.sideWidth=std::clamp(app.layout==L"left"?GET_X_LPARAM(lparam)-8:int(r.right-8-GET_X_LPARAM(lparam)),280,std::max(280,int(r.right)-176));Layout();return 0;}break;
    case WM_LBUTTONUP: if(app.draggingSplitter){app.draggingSplitter=false;ReleaseCapture();SaveSettings();return 0;}break;
    case WM_CAPTURECHANGED: if(app.draggingSplitter){app.draggingSplitter=false;SaveSettings();}break;
    case WM_SIZE: if (wparam != SIZE_MINIMIZED) Layout(); return 0;
    case WM_TIMER:
        if(wparam==RecordTimer){stage3::AutoTick();UpdateRecordingControls(true);RefreshOcrUi();InvalidateRect(app.preview,nullptr,FALSE);return 0;}
        if(wparam==ActivityTimer){++app.activityFrame;Status(L"");return 0;}
        if (wparam == ZoomTimer) { ApplyThumbnailSize(); return 0; }
        if (wparam == ThumbnailTimer) { KillTimer(window, ThumbnailTimer); ScheduleThumbnails(); return 0; } break;
    case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = { 760, 520 }; return 0;
    case WM_APP+4: InvalidateRect(app.preview,nullptr,FALSE);return 0;
    case WM_COMMAND:
        if(OcrCommand(LOWORD(wparam),HIWORD(wparam)))return 0;
        if(app.closing)return 0;
        FlushSelectionUpdate();
        switch (LOWORD(wparam)) {
        case 1200:
            if(app.editing && !app.saving && app.editorUi.Read().Validate().empty())SetWindowTextW(app.editStatus,app.editorUi.Read().Changed()?L"変更あり（保存まで画像は変更されません）。":L"編集中（変更なし）。");
            UpdateEditorControls(); break;
        case EditMetadata:
            BeginMetadataEdit();break;
        case SaveMetadata: StartSave(); break;
        case CancelMetadata:
            if(app.saving&&!app.batchFiles.empty()){app.cancelBatch=true;SetWindowTextW(app.editStatus,L"現在の画像の保存完了後に中止します…");}else if(!app.saving){app.editing=false;app.batchLoading=false;app.batchFiles.clear();app.batchMetadata.clear();SetWindowTextW(app.editStatus,L"編集をキャンセルしました。");UpdateEditorControls();Layout();}break;
        case SearchButton: if(app.searching)CancelSearch();else StartSearch(Text(app.searchEdit));break;
        case RecordToggle: stage3::ToggleRecording();UpdateRecordingControls();Counts();break;
        case ResetView: ResetViewer();break;

        case Help: {auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"バージョン…");AppendMenuW(menu,MF_STRING,2,L"アップデートを確認…");AppendMenuW(menu,MF_STRING,3,L"簡単な使い方…");RECT r{};GetWindowRect(GetDlgItem(window,Help),&r);auto action=TrackPopupMenuEx(menu,TPM_RETURNCMD,r.left,r.bottom,window,nullptr);DestroyMenu(menu);if(action==1)ShowVersion();if(action==2)ShowUpdateChecker(window);if(action==3)ShowQuickHelp();break;}
        case OpenPath: LoadAddress(Text(app.path)); break;
        case Browse: BrowseFolder(); break;
        case Options: OptionsMenu(); break;
        case Bookmarks: BookmarkMenu(); break;
        case History: if (HIWORD(wparam) == CBN_SELCHANGE) { auto index = SendMessageW(app.history, CB_GETCURSEL, 0, 0); if (index >= 0 && size_t(index) < app.recent.size()) LoadAddress(app.recent[index]); } break;
        case Placement: if (HIWORD(wparam) == CBN_SELCHANGE) { auto index = SendMessageW(app.placement, CB_GETCURSEL, 0, 0); auto layout=index==1?L"left":index==2?L"bottom":index==3?L"none":L"right";auto apply=[layout]{app.layout=layout;SendMessageW(app.placement,CB_SETCURSEL,app.layout==L"left"?1:app.layout==L"bottom"?2:app.layout==L"none"?3:0,0);Layout();SaveSettings();};if(GuardTransition(apply))apply();else SendMessageW(app.placement,CB_SETCURSEL,app.layout==L"left"?1:app.layout==L"bottom"?2:app.layout==L"none"?3:0,0); } break;
        } return 0;
    case WM_NOTIFY:
        if(reinterpret_cast<NMHDR*>(lparam)->code==NM_CUSTOMDRAW)return ThemeNotify(reinterpret_cast<NMHDR*>(lparam));
        if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom == app.metadataTabs && reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE) { Layout(); return 0; }
        if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom == app.list) {
            auto header = reinterpret_cast<NMHDR*>(lparam);
            if (header->code == LVN_GETINFOTIPW) {
                auto tip = reinterpret_cast<NMLVGETINFOTIPW*>(lparam);
                if (tip->iItem >= 0 && tip->iItem < static_cast<int>(app.files.size()) && tip->pszText && tip->cchTextMax > 0) {
                    auto name = fs::path(app.files[tip->iItem]).filename().wstring();
                    if (app.thumbnailFailed[tip->iItem]) name += L" [読込みエラー]";
                    wcsncpy_s(tip->pszText, static_cast<size_t>(tip->cchTextMax), name.c_str(), _TRUNCATE);
                }
            } else if (header->code == LVN_ITEMCHANGING) {
                auto changed=reinterpret_cast<NMLISTVIEW*>(lparam);
                if((app.saving || app.backupBusy) && !app.transitionBypass && (changed->uChanged&LVIF_STATE) && ((changed->uNewState^changed->uOldState)&LVIS_SELECTED))return TRUE;
                if(!app.updatingSelection && (changed->uChanged&LVIF_STATE) && ((changed->uNewState^changed->uOldState)&LVIS_SELECTED)){
                    int target=(changed->uNewState&LVIS_SELECTED)?changed->iItem:-1;
                    if(!GuardTransition([index=changed->iItem,state=changed->uNewState]{ListView_SetItemState(app.list,index,state,LVIS_SELECTED|LVIS_FOCUSED);})){
                        app.transitionBypass=true;if(app.selected>=0)ListView_SetItemState(app.list,app.selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);app.transitionBypass=false;return TRUE;
                    }
                }
            } else if (header->code == LVN_ITEMCHANGED) {
                auto changed = reinterpret_cast<NMLISTVIEW*>(lparam);
                if(!app.updatingSelection && (changed->uChanged&LVIF_STATE) && ((changed->uNewState^changed->uOldState)&LVIS_SELECTED))ScheduleSelectionUpdate();
            } else if (header->code == NM_DBLCLK) {
                int index = reinterpret_cast<NMITEMACTIVATE*>(lparam)->iItem;
                if (index >= 0 && index < static_cast<int>(app.files.size())) {
                    RequestPhotos(index);
                }
            }
        } return 0;
    case WM_APP+3: if(!app.closing)Counts();return 0;
    case WM_SELECTION: if(!app.closing)FlushSelectionUpdate();return 0;
    case WM_RESULTS: if(!app.closing)Results(); return 0;
    case WM_CLOSE: {
        FlushSelectionUpdate();
        if(app.closing)return 0;
        if(!GuardTransition([window]{SendMessageW(window,WM_CLOSE,0,0);}))return 0;
        KillTimer(window, ZoomTimer);
        KillTimer(window, ThumbnailTimer);StopActivity();StopOcrUi();stage3::Shutdown(); app.closing = true;
        if (app.thumbnailWidth != app.activeThumbnailWidth) SaveSettings();
        CancelSearch();{std::lock_guard<std::mutex> lock(app.mutex);app.stopping=true;}app.wake.notify_all();
        // COM teardown may call back into the UI STA. Keep it pumping until workers finish.
        while(app.activeWorkers){MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);MSG pending{};while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)){if(pending.message!=WM_QUIT){TranslateMessage(&pending);DispatchMessageW(&pending);}}}
        if (app.worker.joinable()) app.worker.join();
        for (auto& worker : app.thumbnailWorkers) if (worker.joinable()) worker.join();
        if(app.searchWorker.joinable())app.searchWorker.join();
        DestroyWindow(window); return 0; }
    case WM_DESTROY: ImageList_Destroy(app.thumbs); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void UpdateEditorControls() {
    auto edit=app.editing?app.editorUi.Read():MetadataEdit{};
    EnableWindow(app.editButton,app.metadataReady && !app.previewPixels.data.empty() && !app.editing && !app.saving && !app.backupBusy && !app.batchLoading && app.selected>=0);
    EnableWindow(app.saveButton,app.editing && !app.saving && !app.backupBusy && edit.Changed() && edit.Validate().empty());
    EnableWindow(app.cancelButton,app.editing && (!app.saving||!app.batchFiles.empty()) && !app.backupBusy);SetWindowTextW(app.cancelButton,app.saving&&!app.batchFiles.empty()?L"一括保存中止":L"キャンセル");
    for(int id:{OpenPath,Browse,ResetView,History,Bookmarks,SearchButton,Options})EnableWindow(GetDlgItem(app.window,id),!app.saving&&!app.backupBusy);
    if(app.editing && !app.saving){auto error=edit.Validate();if(!error.empty())SetWindowTextW(app.editStatus,error.c_str());}
    else if(!app.editError.empty() && !app.editing)SetWindowTextW(app.editStatus,(L"編集不可: "+app.editError).c_str());
}

std::wstring QuickHelpText(){
    auto module=GetModuleHandleW(nullptr);auto resource=FindResourceW(module,MAKEINTRESOURCEW(1900),RT_RCDATA);
    if(!resource)return L"説明文を読み込めませんでした。";
    auto data=static_cast<const char*>(LockResource(LoadResource(module,resource)));auto size=SizeofResource(module,resource);
    auto length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data,size,nullptr,0);std::wstring text(length,L'\0');
    if(length)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data,size,text.data(),length);
    std::wstring output;for(auto c:text){if(c==L'\n'&&(output.empty()||output.back()!=L'\r'))output+=L'\r';output+=c;}return output;
}
namespace {
INT_PTR CALLBACK QuickHelpDialog(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_INITDIALOG){SetWindowLongPtrW(h,DWLP_USER,lp);if(lp)SetTimer(h,49,100,nullptr);SetWindowTextW(h,L"簡単な使い方");RECT area{};GetClientRect(h,&area);auto text=QuickHelpText();auto c=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",text.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,8,8,std::max(1L,area.right-16),std::max(1L,area.bottom-16),h,reinterpret_cast<HMENU>(1901),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);SetFocus(c);SendMessageW(c,EM_SETSEL,0,0);return FALSE;}
    if(m==WM_TIMER&&wp==49){KillTimer(h,49);auto inspect=reinterpret_cast<const std::function<void(HWND)>*>(GetWindowLongPtrW(h,DWLP_USER));RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);(*inspect)(h);EndDialog(h,IDOK);return TRUE;}
    if(m==WM_SIZE){RECT r{};GetClientRect(h,&r);MoveWindow(GetDlgItem(h,1901),8,8,std::max(1L,r.right-16),std::max(1L,r.bottom-16),TRUE);return TRUE;}
    if(m==WM_CLOSE||(m==WM_COMMAND&&(LOWORD(wp)==IDCANCEL||LOWORD(wp)==IDOK))){EndDialog(h,IDCANCEL);return TRUE;}return FALSE;
}
}
void ShowQuickHelp(const std::function<void(HWND)>& inspect){struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;} t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|DS_MODALFRAME|DS_CENTER,0,0,0,0,360,300},0,0,0};DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,app.window,QuickHelpDialog,inspect?reinterpret_cast<LPARAM>(&inspect):0);}
