#include "ViewerRuntime.h"
#include <richedit.h>
#include <windowsx.h>
#pragma comment(lib,"msimg32.lib")
namespace {
struct Surface {
    HBITMAP bitmap=nullptr;HBRUSH brush=nullptr;BYTE* bytes=nullptr;int width=0,height=0;
    ~Surface(){Reset();}
    void Reset(){if(brush)DeleteObject(brush);if(bitmap)DeleteObject(bitmap);brush=nullptr;bitmap=nullptr;bytes=nullptr;width=height=0;}
    bool Create(int w,int h){Reset();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* bits=nullptr;bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(!bitmap)return false;bytes=static_cast<BYTE*>(bits);width=w;height=h;return true;}
};
Pixels source;Surface plain,tinted,foreground;const BYTE* foregroundKey=nullptr;uint64_t foregroundRevision=UINT64_MAX;COLORREF canvasColor=CLR_INVALID;unsigned canvasSerial=0;bool building=false;
struct ListStamp {unsigned serial=0;POINT origin{};SIZE size{};bool enabled=false;};
bool InSide(HWND h){return app.side&&(h==app.side||IsChild(app.side,h));}
bool EnsureCanvas(){
    if(source.data.empty()||!app.window)return false;RECT r{};GetClientRect(app.window,&r);int w=r.right,h=r.bottom;auto color=ThemeBackground();
    if(plain.bitmap&&plain.width==w&&plain.height==h&&canvasColor==color)return true;
    if(building||w<1||h<1||uint64_t(w)*h>32ULL*1024*1024)return false;
    building=true;plain.Reset();tinted.Reset();bool ok=plain.Create(w,h)&&tinted.Create(w,h);
    if(ok){int bg[]{GetBValue(color),GetGValue(color),GetRValue(color)};for(size_t i=0;i<size_t(w)*h*4;i+=4){for(int c=0;c<3;++c)plain.bytes[i+c]=BYTE(bg[c]);plain.bytes[i+3]=255;}
        HDC dc=CreateCompatibleDC(nullptr);auto old=SelectObject(dc,plain.bitmap);RECT area{0,0,w,h};
        // Cover the entire client area, keeping aspect and cropping centrally.
        double scale=std::max(double(w)/source.width,double(h)/source.height);int sw=std::max(1,int(w/scale)),sh=std::max(1,int(h/scale));
        Surface input;if(input.Create(source.width,source.height)){memcpy(input.bytes,source.data.data(),source.data.size());HDC image=CreateCompatibleDC(nullptr);auto previous=SelectObject(image,input.bitmap);BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};AlphaBlend(dc,0,0,w,h,image,(source.width-sw)/2,(source.height-sh)/2,sw,sh,blend);SelectObject(image,previous);DeleteDC(image);}else ok=false;
        SelectObject(dc,old);DeleteDC(dc);
        if(ok){GdiFlush();for(size_t i=0;i<size_t(w)*h*4;i+=4){for(int c=0;c<3;++c)tinted.bytes[i+c]=BYTE((int(plain.bytes[i+c])*127+bg[c]*128+127)/255);tinted.bytes[i+3]=plain.bytes[i+3]=255;}plain.brush=CreatePatternBrush(plain.bitmap);tinted.brush=CreatePatternBrush(tinted.bitmap);ok=plain.brush&&tinted.brush;}
    }
    building=false;if(!ok){plain.Reset();tinted.Reset();return false;}canvasColor=color;++canvasSerial;return true;
}
void SyncList(HWND h,ListStamp& stamp){
    bool enabled=UsesBackdrop(h)&&EnsureCanvas();RECT r{};GetClientRect(h,&r);POINT origin{};MapWindowPoints(h,app.window,&origin,1);
    if(stamp.enabled==enabled&&(!enabled||(stamp.serial==canvasSerial&&stamp.origin.x==origin.x&&stamp.origin.y==origin.y&&stamp.size.cx==r.right&&stamp.size.cy==r.bottom)))return;
    LVBKIMAGEW image{};image.ulFlags=LVBKIF_TYPE_WATERMARK;HBITMAP bitmap=nullptr;
    if(enabled&&r.right>0&&r.bottom>0){Surface copy;if(copy.Create(r.right,r.bottom)){HDC dc=CreateCompatibleDC(nullptr);auto old=SelectObject(dc,copy.bitmap);PaintBackdrop(h,dc,r);SelectObject(dc,old);DeleteDC(dc);bitmap=copy.bitmap;copy.bitmap=nullptr;image.ulFlags=LVBKIF_SOURCE_HBITMAP|LVBKIF_TYPE_WATERMARK;image.hbm=bitmap;}}
    if(enabled&&!bitmap)enabled=false;
    // A successful LVM_SETBKIMAGE transfers bitmap ownership to the native list.
    if(!ListView_SetBkImage(h,&image)&&bitmap)DeleteObject(bitmap);
    ListView_SetBkColor(h,enabled?CLR_NONE:ThemeBackground());ListView_SetTextBkColor(h,enabled?CLR_NONE:ThemeBackground());ListView_SetTextColor(h,ThemeForeground());
    stamp={canvasSerial,origin,{r.right,r.bottom},enabled};InvalidateRect(h,nullptr,TRUE);
}
void PaintCombo(HWND h,HDC dc){RECT r{};GetClientRect(h,&r);PaintBackdrop(h,dc,r);FrameRect(dc,&r,GetSysColorBrush(COLOR_GRAYTEXT));auto text=Text(h);auto label=r;label.left+=4;label.right-=22;int saved=SaveDC(dc);SelectObject(dc,UiFont());SetTextColor(dc,IsWindowEnabled(h)?ThemeForeground():GetSysColor(COLOR_GRAYTEXT));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);auto arrow=r;arrow.left=arrow.right-22;DrawTextW(dc,L"▼",1,&arrow,DT_SINGLELINE|DT_VCENTER|DT_CENTER);if(GetFocus()==h){InflateRect(&label,-2,-2);DrawFocusRect(dc,&label);}RestoreDC(dc,saved);}
LRESULT CALLBACK ChildProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    auto stamp=reinterpret_cast<ListStamp*>(data);if(m==WM_NCDESTROY){RemoveWindowSubclass(h,ChildProc,id);delete stamp;return DefSubclassProc(h,m,wp,lp);}
    bool active=UsesBackdrop(h);wchar_t type[64]{};GetClassNameW(h,type,64);bool edit=_wcsicmp(type,L"Edit")==0,combo=_wcsicmp(type,L"ComboBox")==0,rich=_wcsicmp(type,MSFTEDIT_CLASS)==0;
    if(stamp&&m==WM_WINDOWPOSCHANGED)SyncList(h,*stamp);
    if(active&&m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);PaintBackdrop(h,reinterpret_cast<HDC>(wp),r);return 1;}
    if(active&&combo&&(m==WM_PAINT||m==WM_PRINTCLIENT)){PAINTSTRUCT ps{};auto dc=m==WM_PAINT?BeginPaint(h,&ps):reinterpret_cast<HDC>(wp);PaintCombo(h,dc);if(m==WM_PAINT)EndPaint(h,&ps);return 0;}
    if(rich&&m==WM_PRINTCLIENT){RECT r{};GetClientRect(h,&r);PaintBackdrop(h,reinterpret_cast<HDC>(wp),r);}
    if(rich&&m==WM_PAINT){auto region=CreateRectRgn(0,0,0,0);if(region){int update=GetUpdateRgn(h,region,FALSE);if(update!=ERROR&&update!=NULLREGION){auto dc=GetDC(h);SelectClipRgn(dc,region);RECT r{};GetClientRect(h,&r);PaintBackdrop(h,dc,r);ReleaseDC(h,dc);}DeleteObject(region);}}
    auto answer=DefSubclassProc(h,m,wp,lp);
    if(active&&(combo||rich||edit)&&(m==WM_CHAR||m==WM_KEYDOWN||m==WM_HSCROLL||m==WM_SETTEXT||m==CB_SETCURSEL||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_ENABLE))InvalidateRect(h,nullptr,TRUE);
    if(active&&stamp&&(m==WM_VSCROLL||m==WM_HSCROLL||m==WM_MOUSEWHEEL))InvalidateRect(h,nullptr,TRUE);
    return answer;
}
}
bool HasBackdrop(){return !source.data.empty();}
bool UsesBackdrop(HWND h){return HasBackdrop()&&h&&app.window&&(h==app.window||IsChild(app.window,h));}
HBRUSH BackdropBrush(HWND h,HDC dc){if(!UsesBackdrop(h)||!EnsureCanvas())return ThemeBrush();POINT point{};MapWindowPoints(h,app.window,&point,1);POINT device{};LPtoDP(dc,&device,1);SetBrushOrgEx(dc,device.x-point.x,device.y-point.y,nullptr);return InSide(h)?tinted.brush:plain.brush;}
void PaintBackdrop(HWND h,HDC dc,const RECT& rect){int saved=SaveDC(dc);FillRect(dc,&rect,BackdropBrush(h,dc));RestoreDC(dc,saved);}
void DrawBackdropPixels(HDC target,const RECT& rect,const Pixels& pixels){
    if(pixels.data.empty()){foreground.Reset();foregroundKey=nullptr;return;}if(!target||rect.right<=rect.left||rect.bottom<=rect.top)return;
    if(!foreground.bitmap||foregroundKey!=pixels.data.data()||foregroundRevision!=app.previewRevision||foreground.width!=int(pixels.width)||foreground.height!=int(pixels.height)){
        if(!foreground.Create(pixels.width,pixels.height))return;memcpy(foreground.bytes,pixels.data.data(),pixels.data.size());foregroundKey=pixels.data.data();foregroundRevision=app.previewRevision;
    }
    HDC dc=CreateCompatibleDC(target);auto old=SelectObject(dc,foreground.bitmap);BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};AlphaBlend(target,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,dc,0,0,pixels.width,pixels.height,blend);SelectObject(dc,old);DeleteDC(dc);
}
void InstallBackdropControls(){if(!app.window)return;EnumChildWindows(app.window,[](HWND h,LPARAM){DWORD_PTR existing=0;if(!GetWindowSubclass(h,ChildProc,600,&existing)){wchar_t type[64]{};GetClassNameW(h,type,64);ListStamp* stamp=_wcsicmp(type,WC_LISTVIEWW)==0?new ListStamp:nullptr;SetWindowSubclass(h,ChildProc,600,reinterpret_cast<DWORD_PTR>(stamp));}return TRUE;},0);}
void SyncBackdrop(){if(!app.window)return;EnsureCanvas();InstallBackdropControls();EnumChildWindows(app.window,[](HWND h,LPARAM){DWORD_PTR stamp=0;if(GetWindowSubclass(h,ChildProc,600,&stamp)&&stamp)SyncList(h,*reinterpret_cast<ListStamp*>(stamp));return TRUE;},0);}
Pixels ReadBackdrop(const std::wstring& path){auto ext=fs::path(path).extension().wstring();if(!Same(ext,L".png")&&!Same(ext,L".jpg")&&!Same(ext,L".jpeg")&&!Same(ext,L".webp"))throw std::runtime_error("Unsupported background format");if(Same(fs::path(path).extension().wstring(),L".webp")){auto image=ReadWebP(path,4096,4096);return {image.width,image.height,std::move(image.pixels)};}ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICBitmapDecoder> decoder;winrt::check_hresult(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));ComPtr<IWICBitmapFrameDecode> frame;winrt::check_hresult(decoder->GetFrame(0,&frame));return Decode(factory.Get(),frame.Get(),4096,4096);}
void RequestBackdrop(const std::wstring& path){app.backgroundLoading=true;Job job;job.kind=-1;job.path=fs::absolute(path).lexically_normal().wstring();job.applyGeneration=++app.backgroundGeneration;Queue(std::move(job),true);Counts(L"背景画像を読込み中…");}
void AcceptBackdrop(Result& result){if(result.job.applyGeneration!=app.backgroundGeneration)return;app.backgroundLoading=false;if(!result.error.empty()||result.pixels.data.empty()){Counts(L"背景画像を読み込めません。以前の背景を保持します。");return;}source=std::move(result.pixels);app.backgroundPath=result.job.path;plain.Reset();tinted.Reset();++app.previewRevision;ApplyTheme();Layout();SyncBackdrop();SaveSettings();Counts(L"背景画像を適用しました。");}
void ClearBackdrop(bool persist){app.backgroundLoading=false;++app.backgroundGeneration;source={};foreground.Reset();foregroundKey=nullptr;app.backgroundPath.clear();plain.Reset();tinted.Reset();++canvasSerial;++app.previewRevision;if(app.window){ApplyTheme();Layout();SyncBackdrop();if(persist)SaveSettings();Counts(L"背景画像を解除しました。");}}
void ChooseBackdrop(){wchar_t path[32768]{};OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=app.window;picker.lpstrFilter=L"背景画像（PNG / JPEG / WebP）\0*.png;*.jpg;*.jpeg;*.webp\0\0";picker.lpstrFile=path;picker.nMaxFile=32768;picker.lpstrTitle=L"背景画像を選択";picker.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&picker))RequestBackdrop(path);}
