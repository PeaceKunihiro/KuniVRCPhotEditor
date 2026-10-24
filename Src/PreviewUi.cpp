#include "ViewerRuntime.h"
namespace {
struct CompositeCache {std::shared_ptr<const Pixels> frame;uint64_t revision=UINT64_MAX;COLORREF background=0;bool live=false;std::vector<BYTE> bytes;};
CompositeCache composite;
}
namespace {
bool NeedsOriginalPreview(){
    if(!app.preview||app.closing||app.layout==L"none"||OcrTestSource()||stage3::RecordingOcr()||app.selectionPending||app.folderPending||app.selected<0||app.selected>=int(app.files.size())||IsFolderItem(app.selected))return false;
    RECT r{};GetClientRect(app.preview,&r);
    return r.right>1024||r.bottom>1024;
}
}
void ScheduleOriginalPreview(){
    if(!app.preview)return;
    KillTimer(app.preview,1);
    if(NeedsOriginalPreview()&&app.previewDedicated&&!app.previewOriginalRequested&&!app.previewSourceKey.empty())SetTimer(app.preview,1,180,nullptr);
}
void LoadOriginalPreview(){
    if(!NeedsOriginalPreview()||!app.previewDedicated||app.previewOriginalRequested||app.previewSourceKey.empty())return;
    app.previewOriginalRequested=true;
    Job job{5,app.generation,app.selection,app.selected,app.files[app.selected],L""};job.sourceKey=app.previewSourceKey;
    Queue(std::move(job),true);Counts(L"元画像を読込み中…（プレビュー表示）");
}
void RefreshPreview(){
    if(!app.preview)return;
    bool test=OcrTestSource(),record=stage3::RecordingOcr();auto frame=test?OcrTestPreview():record?person_ocr::Preview():std::shared_ptr<const Pixels>{};
    auto text=(frame&&!frame->data.empty())||(!test&&!record&&!app.previewPixels.data.empty())?L"":test?OcrTestStatus():record?person_ocr::Status():app.selected<0?L"画像プレビュー":app.previewFailed?L"画像読込みエラー":L"サムネイル読込み中…";
    static std::shared_ptr<const Pixels> last;static uint64_t revision=UINT64_MAX;static int source=-1;static std::wstring status;static COLORREF color=0;
    int next=test?(OcrImageSource()?3:2):record?1:0;auto background=ThemeBackground();
    static UINT lastW=0,lastH=0;const Pixels* shape=frame?frame.get():&app.previewPixels;if(shape->width!=lastW||shape->height!=lastH){lastW=shape->width;lastH=shape->height;LayoutSideView();}
    if(!frame&&app.previewPixels.data.empty())DrawBackdropPixels(nullptr,{},{});
    if(last!=frame||revision!=app.previewRevision||source!=next||status!=text||color!=background){last=frame;revision=app.previewRevision;source=next;status=text;color=background;composite={};InvalidateRect(app.preview,nullptr,FALSE);}
}
LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_LBUTTONDBLCLK) { if(!OcrImageSource()&&(stage3::RecordingOcr()||OcrTestSource()))return 0; if(OcrImageSource()){FlushSelectionUpdate();if(SelectedImageCount()!=1||app.selected<0||app.selected>=int(app.files.size())||!fs::is_regular_file(app.files[app.selected]))return 0;} LaunchSelectedPhotos(); return 0; }
    if (message == WM_MOUSEWHEEL) {
        if(app.side)return SendMessageW(app.side,message,wparam,lparam);
        if (app.saving || stage3::RecordingOcr() || OcrTestSource()) return 0;
        if (app.selected < 0 || app.files.empty() || IsFolderItem(app.selected)) return 0;
        app.wheel += GET_WHEEL_DELTA_WPARAM(wparam);
        int index = app.selected;
        while (abs(app.wheel) >= WHEEL_DELTA) { int direction = app.wheel > 0 ? -1 : 1; app.wheel += direction * WHEEL_DELTA; int next=index+direction;while(next>=0&&next<int(app.files.size())&&IsFolderItem(next))next+=direction;if(next>=0&&next<int(app.files.size()))index=next; }
        if (index != app.selected) { ListView_SetItemState(app.list, -1, 0, LVIS_SELECTED); ListView_SetItemState(app.list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); ListView_EnsureVisible(app.list, index, FALSE); }
        return 0;
    }
    if(message==WM_SIZE){ScheduleOriginalPreview();InvalidateRect(window,nullptr,FALSE);return 0;}
    if(message==WM_TIMER&&wparam==1){KillTimer(window,1);LoadOriginalPreview();return 0;}
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT||message==WM_PRINTCLIENT) {
        PAINTSTRUCT paint{}; HDC screen = message==WM_PAINT?BeginPaint(window, &paint):reinterpret_cast<HDC>(wparam); RECT r{}; GetClientRect(window, &r);if(message==WM_PRINTCLIENT)paint.rcPaint=r;
        if(r.right<=0||r.bottom<=0){if(message==WM_PAINT)EndPaint(window,&paint);return 0;}
        RECT clip=paint.rcPaint;if(app.side){RECT view{};GetClientRect(app.side,&view);MapWindowPoints(app.side,window,reinterpret_cast<POINT*>(&view),2);if(!IntersectRect(&clip,&clip,&view)){if(message==WM_PAINT)EndPaint(window,&paint);return 0;}}int cw=std::max(1L,clip.right-clip.left),ch=std::max(1L,clip.bottom-clip.top);
        HDC dc=CreateCompatibleDC(screen);HBITMAP buffer=CreateCompatibleBitmap(screen,cw,ch);
        if(!dc||!buffer){if(buffer)DeleteObject(buffer);if(dc)DeleteDC(dc);if(message==WM_PAINT)EndPaint(window,&paint);return 0;}
        auto old=SelectObject(dc,buffer);SelectObject(dc,UiFont());SetViewportOrgEx(dc,-clip.left,-clip.top,nullptr);PaintBackdrop(window,dc,clip);
        auto live=OcrTestSource()?OcrTestPreview():stage3::RecordingOcr()?person_ocr::Preview():std::shared_ptr<const Pixels>{};
        Pixels empty;auto& p=(stage3::RecordingOcr()||OcrTestSource())?(live?*live:empty):app.previewPixels;
        if (!p.data.empty()) {
            double scale = std::min(double(r.right) / p.width, double(r.bottom) / p.height);
            int width = int(p.width * scale), height = int(p.height * scale);
            if(UsesBackdrop(window)){RECT image{(r.right-width)/2,(r.bottom-height)/2,(r.right+width)/2,(r.bottom+height)/2};DrawBackdropPixels(dc,image,p);composite.frame=live;}
            else {
            // Composite PBGRA against the current UI theme without changing source pixels.
            auto bg=ThemeBackground();bool isLive=bool(live);bool retain=!isLive||p.data.size()<=4*1024*1024;
            bool reusable=retain&&!composite.bytes.empty()&&composite.background==bg&&composite.live==isLive&&(isLive?composite.frame==live:composite.revision==app.previewRevision);
            std::vector<BYTE> scratch;const std::vector<BYTE>* pixels=&composite.bytes;
            if(!reusable){scratch=p.data;int background[]{GetBValue(bg),GetGValue(bg),GetRValue(bg)};
                for(size_t i=0;i<scratch.size();i+=4)for(int c=0;c<3;++c)scratch[i+c]=BYTE(std::min(255,int(scratch[i+c])+background[c]*(255-scratch[i+3])/255));
                if(retain){composite.bytes=std::move(scratch);composite.frame=live;composite.revision=app.previewRevision;composite.background=bg;composite.live=isLive;}
                else {composite={};pixels=&scratch;}
            }
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = p.width; info.bmiHeader.biHeight = -static_cast<LONG>(p.height);
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, HALFTONE); StretchDIBits(dc, (r.right - width) / 2, (r.bottom - height) / 2, width, height, 0, 0, p.width, p.height, pixels->data(), &info, DIB_RGB_COLORS, SRCCOPY);
            }
        } else { SetTextColor(dc,ThemeForeground());SetBkMode(dc, TRANSPARENT); DrawTextW(dc, OcrTestSource()?OcrTestStatus().c_str():stage3::RecordingOcr()?person_ocr::Status().c_str():app.selected<0?L"画像プレビュー":app.previewFailed?L"画像読込みエラー":L"サムネイル読込み中…", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        BitBlt(screen,clip.left,clip.top,cw,ch,dc,clip.left,clip.top,SRCCOPY);SelectObject(dc,old);DeleteObject(buffer);DeleteDC(dc);
        if(message==WM_PAINT)EndPaint(window, &paint); return 0;
    }
    if(message==WM_NCDESTROY){composite={};DrawBackdropPixels(nullptr,{},{});}
    return DefWindowProcW(window, message, wparam, lparam);
}
