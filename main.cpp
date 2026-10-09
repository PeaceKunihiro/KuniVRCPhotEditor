#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <sstream>
#include <vector>
#include <memory>
#include <cmath>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using namespace winrt::Windows::Data::Json;
constexpr UINT WM_RESULTS = WM_APP + 1;
constexpr size_t HistoryLimit = 10;
enum { PathEdit = 101, OpenPath, Browse, History, Reload, Placement, Images, Details };
struct Pixels { UINT width = 0, height = 0; std::vector<BYTE> data; };
struct Job { int kind = 0; unsigned generation = 0, selection = 0; int index = -1; std::wstring path, preserve; };
struct Result { Job job; std::vector<std::wstring> files; Pixels pixels; std::wstring text, error; };
struct State {
    HWND window{}, path{}, history{}, list{}, preview{}, details{}, status{}, placement{};
    HIMAGELIST thumbs{};
    std::vector<std::wstring> files, recent;
    std::vector<bool> thumbnailDone;
    std::wstring folder, last, layout = L"right", settingsError;
    fs::path settings;
    bool settingsProtected = false, closing = false;
    unsigned generation = 0, selection = 0;
    int selected = -1, wheel = 0, loaded = 0;
    Pixels previewPixels;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::deque<Result> results;
    std::thread worker;
    std::atomic<unsigned> currentGeneration{0}, currentSelection{0};
    std::atomic<bool> stopping{false};
} app;

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
void Status(const std::wstring& message) { SetWindowTextW(app.status, message.c_str()); }
void Counts(const std::wstring& message = L"") {
    Status(std::to_wstring(app.files.size()) + L" 画像  |  " +
        (app.selected < 0 ? L"未選択" : std::to_wstring(app.selected + 1) + L" / " + std::to_wstring(app.files.size())) + L"  |  " + message);
}
bool Same(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
std::wstring Normalize(const std::wstring& value) {
    if (value.empty()) throw std::runtime_error("empty path");
    auto p = fs::absolute(fs::path(value)).lexically_normal();
    auto s = p.wstring();
    while (s.size() > p.root_path().wstring().size() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}
void RefreshHistory() {
    SendMessageW(app.history, CB_RESETCONTENT, 0, 0);
    for (auto& p : app.recent) SendMessageW(app.history, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.c_str()));
}
void LoadSettings() {
    wchar_t executable[32768]; GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    app.settings = fs::path(executable).parent_path() / L"settings.json";
    try {
        if (!fs::exists(app.settings)) return;
        std::ifstream input(app.settings, std::ios::binary);
        if (!input) throw std::runtime_error("read");
        std::string json((std::istreambuf_iterator<char>(input)), {});
        if (json.size() > 1024 * 1024) throw std::runtime_error("size");
        auto object = JsonObject::Parse(winrt::to_hstring(json));
        if (object.GetNamedNumber(L"version") != 1) throw std::runtime_error("version");
        std::wstring last(object.GetNamedString(L"lastFolder"));
        std::wstring layout(object.GetNamedString(L"sideView"));
        if (layout != L"left" && layout != L"right" && layout != L"bottom") throw std::runtime_error("layout");
        std::vector<std::wstring> recent;
        for (auto item : object.GetNamedArray(L"history")) {
            auto path = Normalize(std::wstring(item.GetString()));
            if (std::none_of(recent.begin(), recent.end(), [&](auto& p) { return Same(p, path); }) && recent.size() < HistoryLimit) recent.push_back(path);
        }
        app.last = last; app.layout = layout; app.recent = std::move(recent);
    } catch (...) {
        app.settingsProtected = true;
        app.settingsError = L"設定の読込みに失敗しました。元のsettings.jsonを保全し、今回は設定を保存しません。";
    }
}
void SaveSettings() {
    if (app.settingsProtected) return;
    try {
        JsonObject object; object.Insert(L"version", JsonValue::CreateNumberValue(1));
        object.Insert(L"lastFolder", JsonValue::CreateStringValue(app.last));
        object.Insert(L"sideView", JsonValue::CreateStringValue(app.layout));
        JsonArray history; for (auto& path : app.recent) history.Append(JsonValue::CreateStringValue(path));
        object.Insert(L"history", history);
        auto bytes = winrt::to_string(object.Stringify());
        auto temp = app.settings; temp += L".tmp";
        { std::ofstream output(temp, std::ios::binary | std::ios::trunc); output.write(bytes.data(), bytes.size()); output.flush(); if (!output) throw std::runtime_error("write"); }
        if (!MoveFileExW(temp.c_str(), app.settings.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("replace");
    } catch (...) {
        MessageBoxW(app.window, L"アプリのフォルダーへsettings.jsonを保存できませんでした。", L"設定保存エラー", MB_OK | MB_ICONWARNING);
    }
}

Pixels Decode(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame, UINT maxWidth, UINT maxHeight) {
    UINT width = 0, height = 0; winrt::check_hresult(frame->GetSize(&width, &height));
    if (!width || !height) throw std::runtime_error("invalid dimensions");
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
void Queue(Job job, bool priority = false) {
    { std::lock_guard<std::mutex> lock(app.mutex); if (priority) app.jobs.push_front(std::move(job)); else app.jobs.push_back(std::move(job)); }
    app.wake.notify_one();
}
void Worker() {
    auto init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> factory;
    auto factoryHr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    while (!app.stopping) {
        Job job;
        { std::unique_lock<std::mutex> lock(app.mutex); app.wake.wait(lock, [] { return app.stopping || !app.jobs.empty(); });
          if (app.stopping) break; job = std::move(app.jobs.front()); app.jobs.pop_front(); }
        if (job.generation != app.currentGeneration || (job.kind == 2 && job.selection != app.currentSelection)) continue;
        Result result; result.job = job;
        try {
            if (job.kind == 0) {
                if (!fs::is_directory(job.path)) throw std::runtime_error("not directory");
                for (auto& entry : fs::directory_iterator(job.path)) {
                    if (app.stopping || job.generation != app.currentGeneration) break;
                    std::error_code error;
                    if (!entry.is_regular_file(error)) continue;
                    auto extension = entry.path().extension().wstring();
                    if (Same(extension, L".png") || Same(extension, L".jpg") || Same(extension, L".jpeg")) result.files.push_back(entry.path().wstring());
                }
                std::sort(result.files.begin(), result.files.end(), [](auto& a, auto& b) {
                    auto left = fs::path(a).filename().wstring(), right = fs::path(b).filename().wstring();
                    int comparison = _wcsicmp(left.c_str(), right.c_str()); return comparison ? comparison < 0 : left < right;
                });
            } else {
                winrt::check_hresult(factoryHr);
                ComPtr<IWICBitmapDecoder> decoder;
                winrt::check_hresult(factory->CreateDecoderFromFilename(job.path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder));
                ComPtr<IWICBitmapFrameDecode> frame; winrt::check_hresult(decoder->GetFrame(0, &frame));
                result.pixels = Decode(factory.Get(), frame.Get(), job.kind == 1 ? 128 : 1200, job.kind == 1 ? 96 : 900);
                if (job.kind == 2) result.text = Info(decoder.Get(), frame.Get(), job.path);
            }
        } catch (const winrt::hresult_error& error) { result.error = ErrorText(error.code()); }
          catch (const fs::filesystem_error& error) { result.error = ErrorText(HRESULT_FROM_WIN32(error.code().value())); }
          catch (...) { result.error = L"読込みエラー: パスまたはファイルを確認してください。"; }
        if (app.stopping || job.generation != app.currentGeneration) continue;
        { std::lock_guard<std::mutex> lock(app.mutex); app.results.push_back(std::move(result)); }
        PostMessageW(app.window, WM_RESULTS, 0, 0);
    }
    factory.Reset(); if (SUCCEEDED(init)) CoUninitialize();
}
void Select(int index) {
    app.selected = index; app.previewPixels = {}; ++app.selection; app.currentSelection = app.selection;
    InvalidateRect(app.preview, nullptr, TRUE);
    if (index < 0) { SetWindowTextW(app.details, L"画像を選択してください。"); Counts(); return; }
    SetWindowTextW(app.details, L"プレビュー・メタデータを読み込んでいます…"); Counts(L"読込み中");
    { std::lock_guard<std::mutex> lock(app.mutex); app.jobs.erase(std::remove_if(app.jobs.begin(), app.jobs.end(), [](auto& j) { return j.kind == 2; }), app.jobs.end()); }
    Queue({ 2, app.generation, app.selection, index, app.files[index], L"" }, true);
}
void OpenFolder(const std::wstring& path, bool reload = false) {
    try {
        auto normalized = Normalize(path);
        ++app.generation; app.currentGeneration = app.generation;
        { std::lock_guard<std::mutex> lock(app.mutex); app.jobs.clear(); }
        // The visible list is committed only after enumeration succeeds.
        auto preserve = reload && app.selected >= 0 ? app.files[app.selected] : L"";
        Queue({ 0, app.generation, 0, -1, normalized, preserve }, true); Counts(L"フォルダーを読み込んでいます…");
    } catch (...) { Counts(L"無効なフォルダーパスです。現在の一覧を保持します。"); }
}
HBITMAP Thumbnail(const Pixels& pixels) {
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = 128; info.bmiHeader.biHeight = -96;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr; HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return nullptr;
    auto target = static_cast<BYTE*>(bits); memset(target, 230, 128 * 96 * 4);
    for (int i = 0; i < 128 * 96; ++i) target[i * 4 + 3] = 255;
    auto x = (128 - pixels.width) / 2, y = (96 - pixels.height) / 2;
    for (UINT row = 0; row < pixels.height; ++row) for (UINT col = 0; col < pixels.width; ++col) {
        auto source = &pixels.data[(size_t(row) * pixels.width + col) * 4];
        auto dest = target + ((row + y) * 128 + col + x) * 4;
        for (int c = 0; c < 3; ++c) dest[c] = BYTE(std::min(255, int(source[c]) + (230 * (255 - source[3]) / 255)));
    }
    return bitmap;
}
void Results() {
    std::deque<Result> results;
    { std::lock_guard<std::mutex> lock(app.mutex); results.swap(app.results); }
    for (auto& result : results) {
        auto& job = result.job; if (job.generation != app.generation) continue;
        if (job.kind == 0) {
            if (!result.error.empty()) {
                // A failed folder change keeps the old list, including unfinished work.
                for (int i = 0; i < static_cast<int>(app.files.size()); ++i)
                    if (!app.thumbnailDone[i]) Queue({ 1, app.generation, 0, i, app.files[i], L"" });
                if (app.selected >= 0 && app.previewPixels.data.empty())
                    Queue({ 2, app.generation, app.selection, app.selected, app.files[app.selected], L"" }, true);
                Counts(L"フォルダーを開けません: " + result.error); continue;
            }
            // Clear old selection before replacing the file vector.
            ListView_DeleteAllItems(app.list); Select(-1);
            app.folder = job.path; app.last = job.path; app.files = std::move(result.files); app.loaded = 0;
            app.thumbnailDone.assign(app.files.size(), false);
            app.recent.erase(std::remove_if(app.recent.begin(), app.recent.end(), [&](auto& p) { return Same(p, app.folder); }), app.recent.end());
            app.recent.insert(app.recent.begin(), app.folder); if (app.recent.size() > HistoryLimit) app.recent.resize(HistoryLimit);
            RefreshHistory(); SetWindowTextW(app.path, app.folder.c_str()); SaveSettings();
            ImageList_RemoveAll(app.thumbs); auto placeholder = Thumbnail({}); ImageList_Add(app.thumbs, placeholder, nullptr); DeleteObject(placeholder);
            int preserveIndex = -1;
            SendMessageW(app.list, WM_SETREDRAW, FALSE, 0);
            for (int i = 0; i < static_cast<int>(app.files.size()); ++i) {
                auto name = fs::path(app.files[i]).filename().wstring();
                LVITEMW item{}; item.mask = LVIF_TEXT | LVIF_IMAGE; item.iItem = i; item.iImage = 0; item.pszText = name.data(); ListView_InsertItem(app.list, &item);
                Queue({ 1, app.generation, 0, i, app.files[i], L"" }); if (Same(app.files[i], job.preserve)) preserveIndex = i;
            }
            SendMessageW(app.list, WM_SETREDRAW, TRUE, 0); InvalidateRect(app.list, nullptr, TRUE);
            Counts(app.files.empty() ? L"対応する画像がありません" : L"サムネイル読込み中");
            if (preserveIndex >= 0) { ListView_SetItemState(app.list, preserveIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); ListView_EnsureVisible(app.list, preserveIndex, FALSE); }
        } else if (job.kind == 1) {
            if (app.thumbnailDone[job.index]) continue;
            app.thumbnailDone[job.index] = true;
            ++app.loaded;
            if (result.error.empty()) {
                auto bitmap = Thumbnail(result.pixels); int image = ImageList_Add(app.thumbs, bitmap, nullptr); DeleteObject(bitmap);
                LVITEMW item{}; item.mask = LVIF_IMAGE; item.iItem = job.index; item.iImage = image; ListView_SetItem(app.list, &item);
            } else {
                auto name = fs::path(job.path).filename().wstring() + L" [読込みエラー]";
                ListView_SetItemText(app.list, job.index, 0, name.data());
            }
            if (app.loaded == static_cast<int>(app.files.size())) Counts(L"サムネイル読込み完了");
        } else if (job.selection == app.selection && job.index == app.selected) {
            app.previewPixels = std::move(result.pixels);
            auto text = result.error.empty() ? result.text : job.path + L"\r\n\r\n読込みエラー: " + result.error;
            SetWindowTextW(app.details, text.c_str()); InvalidateRect(app.preview, nullptr, TRUE); Counts(result.error.empty() ? L"表示完了" : L"画像読込みエラー");
        }
    }
}
void Layout() {
    RECT r{}; GetClientRect(app.window, &r); int width = r.right, height = r.bottom;
    MoveWindow(app.path, 8, 8, std::max(80, width - 270), 26, TRUE);
    MoveWindow(GetDlgItem(app.window, OpenPath), width - 254, 8, 64, 26, TRUE);
    MoveWindow(GetDlgItem(app.window, Browse), width - 182, 8, 84, 26, TRUE);
    MoveWindow(GetDlgItem(app.window, Reload), width - 90, 8, 82, 26, TRUE);
    MoveWindow(app.history, 8, 42, std::max(80, width - 196), 280, TRUE);
    MoveWindow(app.placement, width - 180, 42, 172, 180, TRUE);
    RECT list{ 8, 78, width - 8, height - 36 }, side = list;
    if (app.layout == L"bottom") { int split = 78 + std::max(80, (height - 114) / 2); list.bottom = split - 4; side.top = split + 4; }
    else {
        int sideWidth = std::clamp(width * 2 / 5, 280, 540);
        if (app.layout == L"left") { side.right = 8 + sideWidth; list.left = side.right + 8; }
        else { side.left = width - 8 - sideWidth; list.right = side.left - 8; }
    }
    MoveWindow(app.list, list.left, list.top, std::max(1L, list.right - list.left), std::max(1L, list.bottom - list.top), TRUE);
    if (app.layout == L"bottom") {
        int previewWidth = (side.right - side.left) / 3;
        MoveWindow(app.preview, side.left, side.top, previewWidth, std::max(1L, side.bottom - side.top), TRUE);
        MoveWindow(app.details, side.left + previewWidth + 8, side.top, std::max(1L, side.right - side.left - previewWidth - 8), std::max(1L, side.bottom - side.top), TRUE);
    } else {
        int previewHeight = std::max(60L, (side.bottom - side.top) / 3);
        MoveWindow(app.preview, side.left, side.top, side.right - side.left, previewHeight, TRUE);
        MoveWindow(app.details, side.left, side.top + previewHeight + 8, side.right - side.left, std::max(1L, side.bottom - side.top - previewHeight - 8), TRUE);
    }
    MoveWindow(app.status, 8, height - 28, width - 16, 24, TRUE);
}
LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_MOUSEWHEEL) {
        if (app.selected < 0 || app.files.empty()) return 0;
        app.wheel += GET_WHEEL_DELTA_WPARAM(wparam);
        int index = app.selected;
        while (abs(app.wheel) >= WHEEL_DELTA) { int direction = app.wheel > 0 ? -1 : 1; app.wheel += direction * WHEEL_DELTA; index = std::clamp(index + direction, 0, static_cast<int>(app.files.size()) - 1); }
        if (index != app.selected) { ListView_SetItemState(app.list, -1, 0, LVIS_SELECTED); ListView_SetItemState(app.list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); ListView_EnsureVisible(app.list, index, FALSE); }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT r{}; GetClientRect(window, &r); FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
        auto& p = app.previewPixels;
        if (!p.data.empty()) {
            double scale = std::min(double(r.right) / p.width, double(r.bottom) / p.height);
            int width = int(p.width * scale), height = int(p.height * scale);
            // PBGRA is composited against white for the GDI preview.
            auto pixels = p.data;
            for (size_t i = 0; i < pixels.size(); i += 4) for (int c = 0; c < 3; ++c) pixels[i + c] = BYTE(std::min(255, int(pixels[i + c]) + 255 - pixels[i + 3]));
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = p.width; info.bmiHeader.biHeight = -static_cast<LONG>(p.height);
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE); StretchDIBits(dc, (r.right - width) / 2, (r.bottom - height) / 2, width, height, 0, 0, p.width, p.height, pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        } else { SetBkMode(dc, TRANSPARENT); DrawTextW(dc, L"画像プレビュー", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        EndPaint(window, &paint); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
void BrowseFolder() {
    ComPtr<IFileOpenDialog> dialog;
    auto hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) { DWORD options = 0; hr = dialog->GetOptions(&options); if (SUCCEEDED(hr)) hr = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM); }
    if (SUCCEEDED(hr)) hr = dialog->Show(app.window);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
    ComPtr<IShellItem> item; if (SUCCEEDED(hr)) hr = dialog->GetResult(&item);
    PWSTR path = nullptr; if (SUCCEEDED(hr)) hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (SUCCEEDED(hr)) { OpenFolder(path); CoTaskMemFree(path); } else Counts(L"フォルダー選択エラー: " + ErrorText(hr));
}
HWND Control(LPCWSTR type, LPCWSTR text, DWORD style, int id) {
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, app.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); return control;
}
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        app.window = window;
        app.path = Control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, PathEdit);
        Control(L"BUTTON", L"開く", WS_TABSTOP, OpenPath); Control(L"BUTTON", L"フォルダー", WS_TABSTOP, Browse); Control(L"BUTTON", L"再読込み", WS_TABSTOP, Reload);
        app.history = Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, History);
        app.placement = Control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, Placement);
        for (auto label : { L"サイドビュー: 右", L"サイドビュー: 左", L"サイドビュー: 下" }) SendMessageW(app.placement, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(app.placement, CB_SETCURSEL, app.layout == L"left" ? 1 : app.layout == L"bottom" ? 2 : 0, 0);
        app.list = Control(WC_LISTVIEWW, L"", LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, Images);
        ListView_SetExtendedListViewStyle(app.list, LVS_EX_DOUBLEBUFFER); ListView_SetIconSpacing(app.list, 164, 140);
        app.thumbs = ImageList_Create(128, 96, ILC_COLOR32, 1, 64); ListView_SetImageList(app.list, app.thumbs, LVSIL_NORMAL);
        app.preview = Control(L"KuniPreview", L"", WS_BORDER, 0);
        app.details = Control(L"EDIT", L"画像を選択してください。", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER | WS_TABSTOP, Details);
        SendMessageW(app.details, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        app.status = Control(L"STATIC", L"フォルダーを選択してください。", SS_LEFT, 0);
        RefreshHistory(); Layout(); app.worker = std::thread(Worker);
        if (!app.settingsError.empty()) MessageBoxW(window, app.settingsError.c_str(), L"設定読込みエラー", MB_OK | MB_ICONWARNING);
        if (!app.last.empty()) { SetWindowTextW(app.path, app.last.c_str()); OpenFolder(app.last); }
        return 0;
    case WM_SIZE: Layout(); return 0;
    case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lparam)->ptMinTrackSize = { 760, 520 }; return 0;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case OpenPath: OpenFolder(Text(app.path)); break;
        case Browse: BrowseFolder(); break;
        case Reload: if (!app.folder.empty()) OpenFolder(app.folder, true); break;
        case History: if (HIWORD(wparam) == CBN_SELCHANGE) { auto index = SendMessageW(app.history, CB_GETCURSEL, 0, 0); if (index >= 0 && size_t(index) < app.recent.size()) OpenFolder(app.recent[index]); } break;
        case Placement: if (HIWORD(wparam) == CBN_SELCHANGE) { auto index = SendMessageW(app.placement, CB_GETCURSEL, 0, 0); app.layout = index == 1 ? L"left" : index == 2 ? L"bottom" : L"right"; Layout(); SaveSettings(); } break;
        } return 0;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom == app.list) {
            auto header = reinterpret_cast<NMHDR*>(lparam);
            if (header->code == LVN_ITEMCHANGED) {
                auto changed = reinterpret_cast<NMLISTVIEW*>(lparam);
                if ((changed->uChanged & LVIF_STATE) && (changed->uNewState & LVIS_SELECTED) && !(changed->uOldState & LVIS_SELECTED)) Select(changed->iItem);
            } else if (header->code == NM_DBLCLK && app.selected >= 0) {
                MessageBoxW(window, L"Windowsフォトの明示起動は実機検証後に実装します。現在の版はアプリ内プレビューを使用してください。", L"未実装の機能", MB_OK | MB_ICONINFORMATION);
            }
        } return 0;
    case WM_RESULTS: Results(); return 0;
    case WM_CLOSE:
        app.stopping = true; app.wake.notify_all(); if (app.worker.joinable()) app.worker.join(); DestroyWindow(window); return 0;
    case WM_DESTROY: ImageList_Destroy(app.thumbs); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
int SelfTest() {
    auto directory = app.settings.parent_path() / L"self-test-data";
    fs::create_directories(directory);
    std::ofstream report(directory / L"result.txt");
    try {
        auto require = [&](bool ok, const char* label) { if (!ok) throw std::runtime_error(label); report << "PASS: " << label << '\n'; };
        app.settings = directory / L"settings.json";
        app.settingsProtected = false;
        app.last = Normalize(directory.wstring()); app.layout = L"bottom"; app.recent = { app.last };
        SaveSettings(); app.last.clear(); app.recent.clear(); app.layout = L"right";
        // LoadSettings uses the real EXE path, so parse the isolated round-trip file directly.
        std::ifstream input(app.settings, std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(input)), {});
        auto object = JsonObject::Parse(winrt::to_hstring(json));
        require(object.GetNamedNumber(L"version") == 1 && object.GetNamedString(L"sideView") == L"bottom" && object.GetNamedArray(L"history").Size() == 1, "settings JSON round trip");
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
        }
        auto broken = directory / L"broken.png";
        { std::ofstream output(broken, std::ios::binary); output << "invalid image"; }
        ComPtr<IWICBitmapDecoder> decoder;
        require(FAILED(factory->CreateDecoderFromFilename(broken.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)), "broken image rejected");
        report << "ALL PASSED\n"; return 0;
    } catch (const std::exception& error) { report << "FAIL: " << error.what() << '\n'; }
      catch (const winrt::hresult_error& error) { report << "FAIL HRESULT: " << static_cast<unsigned>(error.code()) << '\n'; }
    return 1;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES }; InitCommonControlsEx(&controls);
    LoadSettings();
    if (std::wstring(command) == L"--self-test") { auto result = SelfTest(); winrt::uninit_apartment(); return result; }
    WNDCLASSW preview{}; preview.lpfnWndProc = PreviewProc; preview.hInstance = instance; preview.lpszClassName = L"KuniPreview"; preview.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&preview);
    WNDCLASSW main{}; main.lpfnWndProc = WindowProc; main.hInstance = instance; main.lpszClassName = L"KuniVRCPhotEditor"; main.hCursor = LoadCursorW(nullptr, IDC_ARROW); main.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); RegisterClassW(&main);
    HWND window = CreateWindowExW(0, main.lpszClassName, L"KuniVRCPhotEditor — 画像メタデータビューアー（開発版）", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 800, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && message.hwnd == app.path) { OpenFolder(Text(app.path)); continue; }
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    winrt::uninit_apartment(); return static_cast<int>(message.wParam);
}
