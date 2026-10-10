#include "ViewerRuntime.h"
LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_LBUTTONDBLCLK) { LaunchSelectedPhotos(); return 0; }
    if (message == WM_MOUSEWHEEL) {
        if (app.saving) return 0;
        if (app.selected < 0 || app.files.empty()) return 0;
        app.wheel += GET_WHEEL_DELTA_WPARAM(wparam);
        int index = app.selected;
        while (abs(app.wheel) >= WHEEL_DELTA) { int direction = app.wheel > 0 ? -1 : 1; app.wheel += direction * WHEEL_DELTA; index = std::clamp(index + direction, 0, static_cast<int>(app.files.size()) - 1); }
        if (index != app.selected) { ListView_SetItemState(app.list, -1, 0, LVIS_SELECTED); ListView_SetItemState(app.list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); ListView_EnsureVisible(app.list, index, FALSE); }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint); RECT r{}; GetClientRect(window, &r); FillRect(dc, &r, ThemeBrush());
        auto& p = app.previewPixels;
        if (!p.data.empty()) {
            double scale = std::min(double(r.right) / p.width, double(r.bottom) / p.height);
            int width = int(p.width * scale), height = int(p.height * scale);
            // Composite PBGRA against the current UI theme without changing source pixels.
            auto pixels = p.data;auto bg=ThemeBackground();int background[]{GetBValue(bg),GetGValue(bg),GetRValue(bg)};
            for (size_t i = 0; i < pixels.size(); i += 4) for (int c = 0; c < 3; ++c) pixels[i + c] = BYTE(std::min(255, int(pixels[i + c]) + background[c]*(255-pixels[i + 3])/255));
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = p.width; info.bmiHeader.biHeight = -static_cast<LONG>(p.height);
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE); StretchDIBits(dc, (r.right - width) / 2, (r.bottom - height) / 2, width, height, 0, 0, p.width, p.height, pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        } else { SetTextColor(dc,ThemeForeground());SetBkMode(dc, TRANSPARENT); DrawTextW(dc, app.selected<0?L"画像プレビュー":app.previewFailed?L"画像読込みエラー":L"サムネイル読込み中…", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        EndPaint(window, &paint); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
