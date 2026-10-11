#include "ViewerRuntime.h"
LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_LBUTTONDBLCLK) { if(!OcrImageSource()&&(stage3::RecordingOcr()||OcrTestSource()))return 0; if(OcrImageSource()){FlushSelectionUpdate();if(ListView_GetSelectedCount(app.list)!=1||app.selected<0||app.selected>=int(app.files.size())||!fs::is_regular_file(app.files[app.selected]))return 0;} LaunchSelectedPhotos(); return 0; }
    if (message == WM_MOUSEWHEEL) {
        if (app.saving || stage3::RecordingOcr() || OcrTestSource()) return 0;
        if (app.selected < 0 || app.files.empty()) return 0;
        app.wheel += GET_WHEEL_DELTA_WPARAM(wparam);
        int index = app.selected;
        while (abs(app.wheel) >= WHEEL_DELTA) { int direction = app.wheel > 0 ? -1 : 1; app.wheel += direction * WHEEL_DELTA; index = std::clamp(index + direction, 0, static_cast<int>(app.files.size()) - 1); }
        if (index != app.selected) { ListView_SetItemState(app.list, -1, 0, LVIS_SELECTED); ListView_SetItemState(app.list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); ListView_EnsureVisible(app.list, index, FALSE); }
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC screen = BeginPaint(window, &paint); RECT r{}; GetClientRect(window, &r);
        if(r.right<=0||r.bottom<=0){EndPaint(window,&paint);return 0;}
        HDC dc=CreateCompatibleDC(screen);HBITMAP buffer=CreateCompatibleBitmap(screen,r.right,r.bottom);
        if(!dc||!buffer){if(buffer)DeleteObject(buffer);if(dc)DeleteDC(dc);EndPaint(window,&paint);return 0;}
        auto old=SelectObject(dc,buffer);FillRect(dc,&r,ThemeBrush());
        auto live=OcrTestSource()?OcrTestPreview():stage3::RecordingOcr()?person_ocr::Preview():std::shared_ptr<const Pixels>{};
        Pixels empty;auto& p=(stage3::RecordingOcr()||OcrTestSource())?(live?*live:empty):app.previewPixels;
        if (!p.data.empty()) {
            double scale = std::min(double(r.right) / p.width, double(r.bottom) / p.height);
            int width = int(p.width * scale), height = int(p.height * scale);
            // Composite PBGRA against the current UI theme without changing source pixels.
            auto pixels = p.data;auto bg=ThemeBackground();int background[]{GetBValue(bg),GetGValue(bg),GetRValue(bg)};
            for (size_t i = 0; i < pixels.size(); i += 4) for (int c = 0; c < 3; ++c) pixels[i + c] = BYTE(std::min(255, int(pixels[i + c]) + background[c]*(255-pixels[i + 3])/255));
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = p.width; info.bmiHeader.biHeight = -static_cast<LONG>(p.height);
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE); StretchDIBits(dc, (r.right - width) / 2, (r.bottom - height) / 2, width, height, 0, 0, p.width, p.height, pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        } else { SetTextColor(dc,ThemeForeground());SetBkMode(dc, TRANSPARENT); DrawTextW(dc, OcrTestSource()?OcrTestStatus().c_str():stage3::RecordingOcr()?person_ocr::Status().c_str():app.selected<0?L"画像プレビュー":app.previewFailed?L"画像読込みエラー":L"サムネイル読込み中…", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        BitBlt(screen,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(buffer);DeleteDC(dc);
        EndPaint(window, &paint); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
