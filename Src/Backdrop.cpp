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
LRESULT CALLBACK ListParentBackdrop(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,ListParentBackdrop,id);
    if(m==WM_PRINTCLIENT&&UsesBackdrop(h)){RECT r{};GetClientRect(h,&r);PaintBackdrop(h,reinterpret_cast<HDC>(wp),r);return 0;}
    return DefSubclassProc(h,m,wp,lp);
}
void SyncList(HWND h,ListStamp& stamp){
    auto parent=GetParent(h);if(parent&&parent!=app.window)SetWindowSubclass(parent,ListParentBackdrop,601,0);
    bool enabled=UsesBackdrop(h)&&EnsureCanvas();RECT r{};GetClientRect(h,&r);POINT origin{};MapWindowPoints(h,app.window,&origin,1);
    if(stamp.enabled==enabled&&(!enabled||(stamp.serial==canvasSerial&&stamp.origin.x==origin.x&&stamp.origin.y==origin.y&&stamp.size.cx==r.right&&stamp.size.cy==r.bottom)))return;
    // Let ComCtl paint the parent's backdrop into its own double buffer.
    // No list-sized watermark copy and no separate on-screen background erase.
    LVBKIMAGEW image{};ListView_SetBkImage(h,&image);
    ListView_SetExtendedListViewStyleEx(h,LVS_EX_TRANSPARENTBKGND|LVS_EX_DOUBLEBUFFER,
        LVS_EX_DOUBLEBUFFER|(enabled?LVS_EX_TRANSPARENTBKGND:0));
    ListView_SetBkColor(h,enabled?CLR_NONE:ThemeBackground());ListView_SetTextBkColor(h,enabled?CLR_NONE:ThemeBackground());ListView_SetTextColor(h,ThemeForeground());
    stamp={canvasSerial,origin,{r.right,r.bottom},enabled};InvalidateRect(h,nullptr,TRUE);
}
void PaintCombo(HWND h,HDC dc){RECT r{};GetClientRect(h,&r);PaintBackdrop(h,dc,r);FrameRect(dc,&r,GetSysColorBrush(COLOR_GRAYTEXT));auto text=Text(h);auto label=r;label.left+=4;label.right-=22;int saved=SaveDC(dc);SelectObject(dc,UiFont());SetTextColor(dc,IsWindowEnabled(h)?ThemeForeground():GetSysColor(COLOR_GRAYTEXT));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);auto arrow=r;arrow.left=arrow.right-22;DrawTextW(dc,L"▼",1,&arrow,DT_SINGLELINE|DT_VCENTER|DT_CENTER);if(GetFocus()==h){InflateRect(&label,-2,-2);DrawFocusRect(dc,&label);}RestoreDC(dc,saved);}
LRESULT CALLBACK ChildProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    auto stamp=reinterpret_cast<ListStamp*>(data);if(m==WM_NCDESTROY){RemoveWindowSubclass(h,ChildProc,id);delete stamp;return DefSubclassProc(h,m,wp,lp);}
    bool active=UsesBackdrop(h);wchar_t type[64]{};GetClassNameW(h,type,64);bool edit=_wcsicmp(type,L"Edit")==0,combo=_wcsicmp(type,L"ComboBox")==0,rich=_wcsicmp(type,MSFTEDIT_CLASS)==0;
    if(stamp&&m==WM_WINDOWPOSCHANGED)SyncList(h,*stamp);
    if(active&&!stamp&&m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);PaintBackdrop(h,reinterpret_cast<HDC>(wp),r);return 1;}
    if(active&&combo&&(m==WM_PAINT||m==WM_PRINTCLIENT)){PAINTSTRUCT ps{};auto dc=m==WM_PAINT?BeginPaint(h,&ps):reinterpret_cast<HDC>(wp);PaintCombo(h,dc);if(m==WM_PAINT)EndPaint(h,&ps);return 0;}
    if(rich&&m==WM_PRINTCLIENT){RECT r{};GetClientRect(h,&r);PaintBackdrop(h,reinterpret_cast<HDC>(wp),r);}
    if(rich&&m==WM_PAINT){auto region=CreateRectRgn(0,0,0,0);if(region){int update=GetUpdateRgn(h,region,FALSE);if(update!=ERROR&&update!=NULLREGION){auto dc=GetDC(h);SelectClipRgn(dc,region);RECT r{};GetClientRect(h,&r);PaintBackdrop(h,dc,r);ReleaseDC(h,dc);}DeleteObject(region);}}
    auto answer=DefSubclassProc(h,m,wp,lp);
    if(active&&(combo||rich||edit)&&(m==WM_CHAR||m==WM_KEYDOWN||m==WM_HSCROLL||m==WM_SETTEXT||m==CB_SETCURSEL||m==WM_SETFOCUS||m==WM_KILLFOCUS||m==WM_ENABLE))InvalidateRect(h,nullptr,TRUE);

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
Pixels ReadBackdrop(const std::wstring& path,unsigned maxWidth,unsigned maxHeight){auto ext=fs::path(path).extension().wstring();if(!Same(ext,L".png")&&!Same(ext,L".jpg")&&!Same(ext,L".jpeg")&&!Same(ext,L".webp"))throw std::runtime_error("Unsupported background format");if(Same(fs::path(path).extension().wstring(),L".webp")){auto image=ReadWebP(path,maxWidth,maxHeight);return {image.width,image.height,std::move(image.pixels)};}ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICBitmapDecoder> decoder;winrt::check_hresult(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));ComPtr<IWICBitmapFrameDecode> frame;winrt::check_hresult(decoder->GetFrame(0,&frame));return Decode(factory.Get(),frame.Get(),maxWidth,maxHeight);}
void RequestBackdrop(const std::wstring& path,int theme){app.backgroundLoading=true;Job job;job.kind=-1;job.path=fs::absolute(path).lexically_normal().wstring();job.destination=app.settings.parent_path().wstring();job.backdropTheme=theme;job.backdropThemeRevision=app.backdropThemeRevision;job.applyGeneration=++app.backgroundGeneration;Queue(std::move(job),true);Counts(L"背景画像を読込み中…");}
namespace {
bool ManagedBackground(const fs::path& file,const fs::path& folder){auto name=file.stem().wstring();std::wstring prefix=L"KuniVRCPhotEditor_Background_";GUID guid{};auto ext=file.extension().wstring();return file.is_absolute()&&file.parent_path()==folder&&name.size()==prefix.size()+38&&name.compare(0,prefix.size(),prefix)==0&&SUCCEEDED(CLSIDFromString(name.substr(prefix.size()).c_str(),&guid))&&(Same(ext,L".png")||Same(ext,L".jpg")||Same(ext,L".jpeg")||Same(ext,L".webp"));}
bool PlainBackground(const fs::path& file){auto attr=GetFileAttributesW(file.c_str());return attr!=INVALID_FILE_ATTRIBUTES&&!(attr&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));}
void RetainBackgrounds(const std::wstring& current,const std::wstring& fingerprint){
    auto folder=fs::absolute(app.settings.parent_path()).lexically_normal();auto& records=app.backgroundCopies;
    // v0.6.1 used this exact GUID filename convention before a history was kept.
    std::vector<std::pair<fs::file_time_type,fs::path>> legacy;
    for(auto& entry:fs::directory_iterator(folder)){auto file=entry.path();if(ManagedBackground(file,folder)&&PlainBackground(file)&&std::none_of(records.begin(),records.end(),[&](auto& r){return r.first==file.wstring();})){std::error_code ec;auto time=fs::last_write_time(file,ec);if(!ec)legacy.push_back({time,file});}}
    std::sort(legacy.begin(),legacy.end(),[](auto& a,auto& b){return a.first>b.first;});
    for(auto& item:legacy){if(records.size()>=1023)break;try{ReadBackdrop(item.second.wstring());records.push_back({item.second.wstring(),MetadataFingerprint(item.second.wstring())});}catch(...){}}
    records.erase(std::remove_if(records.begin(),records.end(),[&](auto& r){return r.first==current;}),records.end());records.insert(records.begin(),{current,fingerprint});
    SaveSettings(false);
    // Deletion is permitted only after the active path and ownership fingerprints are durable.
    std::ifstream in(app.settings,std::ios::binary);auto saved=ParseViewerSettings(std::string((std::istreambuf_iterator<char>(in)),{}));in.close();std::map<std::wstring,bool> themes;for(auto& record:records){auto theme=app.backgroundThemes.find(record.first);if(theme!=app.backgroundThemes.end())themes.insert(*theme);}if(saved.backgroundPath!=current||saved.backgroundCopies!=records||saved.backgroundThemes!=themes)throw std::runtime_error("background retention settings not saved");
    size_t kept=0;std::vector<std::pair<std::wstring,std::wstring>> remain;
    for(auto& record:records){auto path=fs::path(record.first);if(!ManagedBackground(path,folder)||!PlainBackground(path))continue;
        try{if(MetadataFingerprint(record.first)!=record.second){remain.push_back(record);continue;}}catch(...){remain.push_back(record);continue;}
        if(kept<3||record.first==current){remain.push_back(record);++kept;continue;}
        if(!DeleteFileW(path.c_str()))remain.push_back(record);
    }
    records=std::move(remain);for(auto i=app.backgroundThemes.begin();i!=app.backgroundThemes.end();)if(std::none_of(records.begin(),records.end(),[&](auto& r){return r.first==i->first;}))i=app.backgroundThemes.erase(i);else ++i;SaveSettings(false);
}
}
void AcceptBackdrop(Result& result){if(result.job.applyGeneration!=app.backgroundGeneration)return;app.backgroundLoading=false;if(!result.error.empty()||result.pixels.data.empty()){Counts(L"背景画像を保存・読込みできません。以前の背景を保持します。");return;}source=std::move(result.pixels);app.backgroundPath=result.job.path;if(result.job.backdropTheme>=0&&result.job.backdropThemeRevision==app.backdropThemeRevision)app.darkTheme=result.job.backdropTheme!=0;app.backgroundThemes[app.backgroundPath]=app.darkTheme;if(result.backdropCopy)result.backdropCopy->retained=true;plain.Reset();tinted.Reset();++app.previewRevision;ApplyTheme();Layout();SyncBackdrop();SaveSettings();try{RetainBackgrounds(app.backgroundPath,result.job.fingerprint);}catch(...){Counts(L"背景画像を適用しました。古い背景画像の整理は保留しました。");return;}Counts(L"背景画像を適用しました。");}
void RememberBackdropTheme(){++app.backdropThemeRevision;if(HasBackdrop()&&!app.backgroundPath.empty())app.backgroundThemes[app.backgroundPath]=app.darkTheme;}
void ClearBackdrop(bool persist){app.backgroundLoading=false;++app.backgroundGeneration;source={};foreground.Reset();foregroundKey=nullptr;app.backgroundPath.clear();plain.Reset();tinted.Reset();++canvasSerial;++app.previewRevision;if(app.window){ApplyTheme();Layout();SyncBackdrop();if(persist)SaveSettings();Counts(L"背景画像を解除しました。");}}
std::wstring PickBackdrop(HWND owner){wchar_t path[32768]{};OPENFILENAMEW picker{sizeof(picker)};picker.hwndOwner=owner;picker.lpstrFilter=L"背景画像（PNG / JPEG / WebP）\0*.png;*.jpg;*.jpeg;*.webp\0\0";picker.lpstrFile=path;picker.nMaxFile=32768;picker.lpstrTitle=L"背景画像を選択";picker.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;return GetOpenFileNameW(&picker)?std::wstring(path):L"";}

void LoadBackdropJob(Result& result){
    const auto original=fs::absolute(result.job.path).lexically_normal();
    const auto directory=fs::absolute(result.job.destination).lexically_normal();
    auto name=original.stem().wstring();const std::wstring prefix=L"KuniVRCPhotEditor_Background_";
    GUID parsed{};bool managed=original.parent_path()==directory&&name.size()==prefix.size()+38&&name.compare(0,prefix.size(),prefix)==0&&SUCCEEDED(CLSIDFromString(name.substr(prefix.size()).c_str(),&parsed));
    if(!managed){
        auto copy=std::make_shared<BackdropCopy>();GUID guid{};winrt::check_hresult(CoCreateGuid(&guid));wchar_t id[40]{};StringFromGUID2(guid,id,40);
        copy->path=directory/(prefix+id+original.extension().wstring());
        // Exclusive creation also gives us ownership of partial files on I/O failure.
        struct File {HANDLE h=INVALID_HANDLE_VALUE;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
        {
            File input{CreateFileW(original.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
            if(input.h==INVALID_HANDLE_VALUE){auto error=GetLastError();copy->path.clear();winrt::throw_hresult(HRESULT_FROM_WIN32(error));}
            File output{CreateFileW(copy->path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
            if(output.h==INVALID_HANDLE_VALUE){auto error=GetLastError();copy->path.clear();winrt::throw_hresult(HRESULT_FROM_WIN32(error));}
            BYTE buffer[65536];DWORD read=0;
            for(;;){if(!ReadFile(input.h,buffer,sizeof(buffer),&read,nullptr))winrt::throw_last_error();if(!read)break;
                DWORD total=0;while(total<read){DWORD written=0;if(!WriteFile(output.h,buffer+total,read-total,&written,nullptr))winrt::throw_last_error();if(!written)throw std::runtime_error("background copy write failed");total+=written;}}
            if(!FlushFileBuffers(output.h))winrt::throw_last_error();
        }
        result.backdropCopy=copy;result.job.path=copy->path.wstring();
    }
    result.pixels=ReadBackdrop(result.job.path);result.job.fingerprint=MetadataFingerprint(result.job.path);
}
namespace {
constexpr int BackgroundPick=2801,BackgroundClear=2802,BackgroundPath=2803,BackgroundPreview=2804,BackgroundTheme=2805,HistoryFirst=2810;
struct BackgroundHistoryItem {std::wstring path;Pixels thumbnail;int theme=-1;bool available=false;};
struct BackgroundDialogState {std::function<void(HWND)> inspect;std::function<std::wstring(HWND)> choose;std::wstring pending;Pixels preview;std::vector<BackgroundHistoryItem> history;int pendingTheme=-1;bool clear=false;std::exception_ptr error;};
std::wstring ThemeLabel(int theme){return theme<0?L"テーマ未記録":theme?L"ナイト":L"ライト";}
void RefreshBackgroundDialog(HWND h,BackgroundDialogState& state){
    SetDlgItemTextW(h,BackgroundPath,state.clear||state.pending.empty()?L"背景画像なし":state.pending.c_str());
    auto theme=state.clear?L"解除後も現在のテーマを維持します。":state.pendingTheme<0?L"適用テーマ：現在（"+ThemeLabel(app.darkTheme?1:0)+L"）":L"復元テーマ："+ThemeLabel(state.pendingTheme);
    SetDlgItemTextW(h,BackgroundTheme,theme.c_str());InvalidateRect(GetDlgItem(h,BackgroundPreview),nullptr,FALSE);
    for(size_t i=0;i<state.history.size();++i)InvalidateRect(GetDlgItem(h,HistoryFirst+int(i)),nullptr,FALSE);
}
void DrawHistoryItem(const DRAWITEMSTRUCT& draw,const BackgroundDialogState& state){
    auto index=int(draw.CtlID)-HistoryFirst;if(index<0||index>=int(state.history.size()))return;auto& item=state.history[index];auto dc=draw.hDC;auto r=draw.rcItem;int saved=SaveDC(dc);FillRect(dc,&r,ThemeBrush());
    bool selected=!state.clear&&state.pending==item.path;auto frame=CreateSolidBrush(selected?GetSysColor(COLOR_HIGHLIGHT):GetSysColor(COLOR_GRAYTEXT));FrameRect(dc,&r,frame);if(selected){auto inner=r;InflateRect(&inner,-1,-1);FrameRect(dc,&inner,frame);}DeleteObject(frame);
    SelectObject(dc,UiFont());SetBkMode(dc,TRANSPARENT);SetTextColor(dc,item.available?ThemeForeground():GetSysColor(COLOR_GRAYTEXT));auto caption=r;caption.top=caption.bottom-UiLineHeight()-6;caption.left+=4;caption.right-=4;
    auto label=(item.path==app.backgroundPath?L"使用中":L"履歴"+std::to_wstring(index+1))+std::wstring(L" · ")+ThemeLabel(item.theme);if(!item.available)label=L"読込不可 · "+ThemeLabel(item.theme);DrawTextW(dc,label.c_str(),-1,&caption,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    RECT image{r.left+5,r.top+5,r.right-5,caption.top-3};auto& pixels=item.thumbnail;
    if(!pixels.data.empty()){double scale=std::min(double(image.right-image.left)/pixels.width,double(image.bottom-image.top)/pixels.height);int w=std::max(1,int(pixels.width*scale)),h=std::max(1,int(pixels.height*scale));Surface input;if(input.Create(pixels.width,pixels.height)){memcpy(input.bytes,pixels.data.data(),pixels.data.size());HDC sourceDc=CreateCompatibleDC(dc);auto old=SelectObject(sourceDc,input.bitmap);BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};AlphaBlend(dc,(image.left+image.right-w)/2,(image.top+image.bottom-h)/2,w,h,sourceDc,0,0,pixels.width,pixels.height,blend);SelectObject(sourceDc,old);DeleteDC(sourceDc);}}
    else DrawTextW(dc,L"画像を読み込めません",-1,&image,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(draw.itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(dc,&r);}RestoreDC(dc,saved);
}
LRESULT CALLBACK BackgroundPreviewProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){
    if(m==WM_NCDESTROY){RemoveWindowSubclass(h,BackgroundPreviewProc,id);return DefSubclassProc(h,m,wp,lp);}
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT ps{};HDC target=m==WM_PAINT?BeginPaint(h,&ps):reinterpret_cast<HDC>(wp);RECT r{};GetClientRect(h,&r);
        HDC dc=CreateCompatibleDC(target);auto bitmap=CreateCompatibleBitmap(target,std::max(1L,r.right),std::max(1L,r.bottom));
        if(dc&&bitmap){auto old=SelectObject(dc,bitmap);FillRect(dc,&r,ThemeBrush());auto state=reinterpret_cast<BackgroundDialogState*>(data);auto& p=state->preview;
            if(!state->clear&&!p.data.empty()){Surface input;if(input.Create(p.width,p.height)){memcpy(input.bytes,p.data.data(),p.data.size());HDC image=CreateCompatibleDC(dc);auto previous=SelectObject(image,input.bitmap);double scale=std::max(double(r.right)/p.width,double(r.bottom)/p.height);int sw=std::max(1,int(r.right/scale)),sh=std::max(1,int(r.bottom/scale));BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};AlphaBlend(dc,0,0,r.right,r.bottom,image,(p.width-sw)/2,(p.height-sh)/2,sw,sh,blend);SelectObject(image,previous);DeleteDC(image);}}
            BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old);}
        if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(m==WM_PAINT)EndPaint(h,&ps);return 0;
    }
    return DefSubclassProc(h,m,wp,lp);
}
INT_PTR CALLBACK BackgroundDialog(HWND h,UINT m,WPARAM wp,LPARAM lp){
    auto state=reinterpret_cast<BackgroundDialogState*>(GetWindowLongPtrW(h,DWLP_USER));
    if(m==WM_INITDIALOG){state=reinterpret_cast<BackgroundDialogState*>(lp);SetWindowLongPtrW(h,DWLP_USER,lp);SetWindowTextW(h,L"背景画像");
        RECT bounds{0,0,560,516};AdjustWindowRectEx(&bounds,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));SetWindowPos(h,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER);
        auto add=[&](const wchar_t* cls,const std::wstring& text,DWORD style,int x,int y,int w,int height,int id){auto c=CreateWindowExW(0,cls,text.c_str(),WS_CHILD|WS_VISIBLE|style,x,y,w,height,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),FALSE);return c;};
        auto preview=add(L"STATIC",L"",WS_BORDER,12,12,536,168,BackgroundPreview);SetWindowSubclass(preview,BackgroundPreviewProc,1,reinterpret_cast<DWORD_PTR>(state));
        add(L"EDIT",state->pending.empty()?L"背景画像なし":state->pending,ES_READONLY|ES_AUTOHSCROLL|WS_BORDER,12,188,536,26,BackgroundPath);
        add(L"STATIC",L"",0,12,222,536,24,BackgroundTheme);add(L"STATIC",L"最近使った背景（使用中を含む3件）",0,12,252,536,24,2806);
        for(size_t i=0;i<state->history.size();++i){auto& item=state->history[i];auto text=L"背景履歴"+std::to_wstring(i+1)+L"："+ThemeLabel(item.theme)+L" "+item.path;auto button=add(L"BUTTON",text,BS_OWNERDRAW|WS_TABSTOP,12+int(i)*180,280,176,110,HistoryFirst+int(i));EnableWindow(button,item.available);}
        if(state->history.empty())add(L"STATIC",L"履歴はありません。画像を選択して適用すると登録されます。",0,12,310,536,50,2807);
        add(L"STATIC",L"履歴を選び［適用］で背景と記録したテーマを復元します。\r\n新規画像は現在のテーマを記録します。画像はアプリ側へ複製します。\r\n使用中を含む直近3件を保持し、古い管理画像は自動削除します。",0,12,402,536,60,2808);
        add(L"BUTTON",L"画像を選択…",WS_TABSTOP,12,474,128,28,BackgroundPick);add(L"BUTTON",L"解除",WS_TABSTOP,148,474,88,28,BackgroundClear);add(L"BUTTON",L"適用",WS_TABSTOP|BS_DEFPUSHBUTTON,320,474,100,28,IDOK);add(L"BUTTON",L"キャンセル",WS_TABSTOP,428,474,120,28,IDCANCEL);
        RefreshBackgroundDialog(h,*state);
        ScaleUiDialog(h);if(state->inspect)SetTimer(h,61,100,nullptr);return TRUE;
    }
    if(m==WM_TIMER&&wp==61){KillTimer(h,61);try{state->inspect(h);}catch(...){state->error=std::current_exception();EndDialog(h,IDCANCEL);}return TRUE;}
    if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(wp),&r,ThemeBrush());return TRUE;}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORBTN)return ThemeControlColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp));
    if(m==WM_NOTIFY&&reinterpret_cast<NMHDR*>(lp)->code==NM_CUSTOMDRAW){SetWindowLongPtrW(h,DWLP_MSGRESULT,ThemeNotify(reinterpret_cast<NMHDR*>(lp)));return TRUE;}
    if(m==WM_DRAWITEM){auto draw=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(draw->CtlID>=HistoryFirst&&draw->CtlID<HistoryFirst+3){DrawHistoryItem(*draw,*state);return TRUE;}}
    if(m==WM_COMMAND&&LOWORD(wp)>=HistoryFirst&&LOWORD(wp)<HistoryFirst+3){auto index=int(LOWORD(wp))-HistoryFirst;if(index>=int(state->history.size())||!state->history[index].available)return TRUE;auto& item=state->history[index];try{auto pixels=ReadBackdrop(item.path);state->preview=std::move(pixels);state->pending=item.path;state->pendingTheme=item.theme;state->clear=false;RefreshBackgroundDialog(h,*state);}catch(...){item.available=false;EnableWindow(GetDlgItem(h,HistoryFirst+index),FALSE);SetDlgItemTextW(h,BackgroundTheme,L"履歴画像を読み込めません。現在の選択を保持します。");InvalidateRect(GetDlgItem(h,HistoryFirst+index),nullptr,FALSE);}return TRUE;}
    if(m==WM_COMMAND){switch(LOWORD(wp)){
        case BackgroundPick:{auto path=state->choose?state->choose(h):PickBackdrop(h);if(path.empty())return TRUE;try{auto pixels=ReadBackdrop(path);state->preview=std::move(pixels);state->pending=path;state->pendingTheme=-1;state->clear=false;RefreshBackgroundDialog(h,*state);}catch(...){MessageBoxW(h,L"画像を読み込めません。選択内容を保持します。",L"背景画像",MB_OK|MB_ICONWARNING);}return TRUE;}
        case BackgroundClear:state->clear=true;state->pending.clear();state->pendingTheme=-1;state->preview={};RefreshBackgroundDialog(h,*state);return TRUE;
        case IDOK:if(state->clear)ClearBackdrop();else if(!state->pending.empty())RequestBackdrop(state->pending,state->pendingTheme);EndDialog(h,IDOK);return TRUE;
        case IDCANCEL:EndDialog(h,IDCANCEL);return TRUE;
    }}
    if(m==WM_CLOSE){EndDialog(h,IDCANCEL);return TRUE;}return FALSE;
}
}
void ShowBackdropDialog(const std::function<void(HWND)>& inspect,const std::function<std::wstring(HWND)>& choose){
    BackgroundDialogState state;state.inspect=inspect;state.choose=choose;state.pending=app.backgroundPath;state.preview=source;
    auto records=app.backgroundCopies;if(!app.backgroundPath.empty()&&std::none_of(records.begin(),records.end(),[](auto& row){return row.first==app.backgroundPath;}))records.insert(records.begin(),{app.backgroundPath,L""});
    for(auto& record:records){if(state.history.size()>=3)break;BackgroundHistoryItem item;item.path=record.first;auto theme=app.backgroundThemes.find(item.path);if(theme!=app.backgroundThemes.end())item.theme=theme->second?1:0;
        try{if(PlainBackground(item.path)){if(!record.second.empty()&&MetadataFingerprint(item.path)!=record.second)continue;item.thumbnail=ReadBackdrop(item.path,160,76);item.available=true;}}catch(...){}state.history.push_back(std::move(item));}
    struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;} t{};t.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER;t.dialog.cx=340;t.dialog.cy=230;
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,app.window,BackgroundDialog,reinterpret_cast<LPARAM>(&state));if(state.error)std::rethrow_exception(state.error);
}
void ChooseBackdrop(){ShowBackdropDialog();}
