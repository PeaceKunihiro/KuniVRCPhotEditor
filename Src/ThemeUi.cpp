#include "ViewerRuntime.h"
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"uxtheme.lib")
namespace {
// Theme helpers and thumbnail compositing are called on the UI thread.
struct ContrastState {bool initialized=false,known=false,high=false;} contrastState;
std::function<bool(bool&)> contrastQuery;
void RefreshContrast(){
    bool high=false;bool success;
    if(contrastQuery)success=contrastQuery(high);
    else {HIGHCONTRASTW contrast{sizeof(contrast)};success=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0)!=FALSE;high=(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;}
    contrastState.initialized=true;
    if(success){contrastState.known=true;contrastState.high=high;}
}
bool SystemTheme(){if(!contrastState.initialized)RefreshContrast();return !contrastState.known||contrastState.high;}
bool Night(){return !SystemTheme()&&app.darkTheme;}
LRESULT CALLBACK ContrastColorProbe(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){if(message==WM_SYSCOLORCHANGE)++*reinterpret_cast<int*>(ref);return DefSubclassProc(h,message,wp,lp);}
}
bool ThemeIsDark(){return Night();}
COLORREF ThemeBackground(){return Night()?RGB(32,32,32):GetSysColor(COLOR_WINDOW);}
COLORREF ThemeForeground(){return Night()?RGB(235,235,235):GetSysColor(COLOR_WINDOWTEXT);}
HBRUSH ThemeBrush(){static HBRUSH dark=CreateSolidBrush(RGB(32,32,32));return Night()?dark:GetSysColorBrush(COLOR_WINDOW);}
void RunV0421ThemeTests(std::ostream& report,const std::function<void(const wchar_t*)>& capture){
    auto require=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: "<<label<<'\n';};
    auto savedQuery=contrastQuery;auto savedState=contrastState;bool savedDark=app.darkTheme;
    struct Restore{std::function<void()> work;~Restore(){work();}} restore{[&]{contrastQuery=savedQuery;contrastState=savedState;app.darkTheme=savedDark;if(app.window)ApplyTheme();}};
    int queries=0;bool success=false,high=false;
    contrastQuery=[&](bool& result){++queries;result=high;return success;};contrastState={};app.darkTheme=true;
    require(ThemeBackground()==GetSysColor(COLOR_WINDOW)&&queries==1,"v0.4.21 initial contrast query failure uses system colors");
    for(int i=0;i<1000;++i){ThemeBackground();ThemeForeground();ThemeBrush();}
    require(queries==1,"v0.4.21 failed initial query is not repeated by drawing");
    success=true;ApplyTheme();require(queries==2&&ThemeBackground()==RGB(32,32,32),"v0.4.21 successful refresh restores requested dark theme");
    high=true;ApplyTheme();require(queries==3&&ThemeBackground()==GetSysColor(COLOR_WINDOW)&&app.darkTheme,"v0.4.21 contrast mode uses system colors without overwriting dark preference");
    success=false;ApplyTheme();require(queries==4&&ThemeBackground()==GetSysColor(COLOR_WINDOW),"v0.4.21 refresh failure retains last known contrast state");
    success=true;high=false;
    if(app.window)SendMessageW(app.window,WM_SETTINGCHANGE,0,0);else ApplyTheme();
    require(queries==5&&ThemeBackground()==RGB(32,32,32),"v0.4.21 setting change refreshes cached contrast state");
    if(capture)capture(L"v0421-theme-dark.png");
    high=true;if(app.window)SendMessageW(app.window,WM_THEMECHANGED,0,0);else ApplyTheme();
    require(queries==6&&ThemeBackground()==GetSysColor(COLOR_WINDOW),"v0.4.21 theme change refreshes cached contrast state");
    if(app.window){require(ListView_GetBkColor(app.list)==GetSysColor(COLOR_WINDOW)&&ListView_GetTextColor(app.list)==GetSysColor(COLOR_WINDOWTEXT),"v0.4.21 simulated contrast updates list colors");if(capture)capture(L"v0421-theme-contrast-simulated.png");}
    high=false;int colorNotifications=0;
    if(app.window){SetWindowSubclass(app.list,ContrastColorProbe,421,reinterpret_cast<DWORD_PTR>(&colorNotifications));SendMessageW(app.window,WM_SYSCOLORCHANGE,0,0);RemoveWindowSubclass(app.list,ContrastColorProbe,421);}else ApplyTheme();
    require(queries==7&&ThemeBackground()==RGB(32,32,32),"v0.4.21 system color change restores cached normal mode");
    if(app.window)require(colorNotifications>0,"v0.4.21 system color change is forwarded to child controls");
    success=false;ApplyTheme();require(queries==8&&ThemeBackground()==RGB(32,32,32),"v0.4.21 refresh failure retains last known normal state");
    success=true;int before=queries;HDC dc=CreateCompatibleDC(nullptr);
    for(int i=0;i<1000;++i){ThemeControlColor(dc);ThemeBackground();ThemeForeground();ThemeBrush();}
    if(app.window)RedrawWindow(app.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    bool darkColors=GetTextColor(dc)==RGB(235,235,235)&&GetBkColor(dc)==RGB(32,32,32);DeleteDC(dc);
    require(queries==before&&darkColors,"v0.4.21 control colors and repaint reuse state without OS queries");
    report<<"INFO: v0.4.21 contrast control-color calls=1000 additional_queries="<<queries-before<<'\n';
    app.darkTheme=false;ApplyTheme();require(ThemeBackground()==GetSysColor(COLOR_WINDOW),"v0.4.21 light theme remains system colored");
    if(capture)capture(L"v0421-theme-light.png");
}
LRESULT ThemeControlColor(HDC dc,HWND child){SetTextColor(dc,ThemeForeground());SetBkColor(dc,ThemeBackground());if(UsesBackdrop(child)){SetBkMode(dc,TRANSPARENT);return reinterpret_cast<LRESULT>(BackdropBrush(child,dc));}SetBkMode(dc,OPAQUE);return reinterpret_cast<LRESULT>(ThemeBrush());}
namespace {
void TabBorder(HDC dc,RECT rect){InflateRect(&rect,-1,-1);auto brush=CreateSolidBrush(SystemTheme()?GetSysColor(COLOR_WINDOWTEXT):Night()?RGB(210,210,210):RGB(60,60,60));FrameRect(dc,&rect,brush);DeleteObject(brush);}
void PaintTabs(HWND h,HDC dc){RECT rect{};GetClientRect(h,&rect);PaintBackdrop(h,dc,rect);auto font=SendMessageW(h,WM_GETFONT,0,0);auto old=font?SelectObject(dc,reinterpret_cast<HGDIOBJ>(font)):nullptr;for(int i=0;i<TabCtrl_GetItemCount(h);++i){RECT tab{};TabCtrl_GetItemRect(h,i,&tab);auto brush=CreateSolidBrush(i==TabCtrl_GetCurSel(h)?(Night()?RGB(58,58,58):GetSysColor(COLOR_BTNFACE)):ThemeBackground());if(UsesBackdrop(h))PaintBackdrop(h,dc,tab);else FillRect(dc,&tab,brush);DeleteObject(brush);wchar_t text[80]{};TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=text;item.cchTextMax=80;TabCtrl_GetItem(h,i,&item);SetTextColor(dc,ThemeForeground());SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text,-1,&tab,DT_CENTER|DT_VCENTER|DT_SINGLELINE);TabBorder(dc,tab);if(UsesBackdrop(h)&&i==TabCtrl_GetCurSel(h)){auto underline=tab;underline.top=underline.bottom-3;auto pen=CreateSolidBrush(ThemeForeground());FillRect(dc,&underline,pen);DeleteObject(pen);}if(i==TabCtrl_GetCurSel(h)&&GetFocus()==h){InflateRect(&tab,-3,-3);DrawFocusRect(dc,&tab);}}if(old)SelectObject(dc,old);}
LRESULT CALLBACK ThemeChild(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){wchar_t type[64]{};GetClassNameW(h,type,64);if(wcscmp(type,WC_TABCONTROLW)==0){if(message==WM_PAINT){PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);PaintTabs(h,dc);EndPaint(h,&paint);return 0;}if(message==WM_PRINTCLIENT){PaintTabs(h,reinterpret_cast<HDC>(wp));return 0;}if(message==WM_ERASEBKGND){RECT rect{};GetClientRect(h,&rect);PaintBackdrop(h,reinterpret_cast<HDC>(wp),rect);return 1;}}if(message==WM_CTLCOLORSTATIC||message==WM_CTLCOLOREDIT||message==WM_CTLCOLORLISTBOX||message==WM_CTLCOLORBTN)return ThemeControlColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp));if(message==WM_ERASEBKGND&&Night()){wchar_t name[64]{};GetClassNameW(h,name,64);if(wcscmp(name,L"KuniMetadataEditor")==0||wcscmp(name,L"KuniSimple")==0){RECT r{};GetClientRect(h,&r);PaintBackdrop(h,reinterpret_cast<HDC>(wp),r);return 1;}}if(message==WM_NOTIFY){auto hdr=reinterpret_cast<NMHDR*>(lp);if(hdr->code==NM_CUSTOMDRAW)return ThemeNotify(hdr);}if(message==WM_DRAWITEM && wcscmp(type,L"ComboBox")!=0 && ThemeDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lp)))return TRUE;if(message==WM_MEASUREITEM && wcscmp(type,L"ComboBox")!=0){auto measure=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(measure->CtlType==ODT_COMBOBOX){measure->itemHeight=UiControlHeight()-6;return TRUE;}}if(message==WM_NCDESTROY)RemoveWindowSubclass(h,ThemeChild,id);return DefSubclassProc(h,message,wp,lp);}
BOOL CALLBACK PaintChild(HWND h,LPARAM){SetWindowSubclass(h,ThemeChild,31,0);wchar_t name[64]{};GetClassNameW(h,name,64);if(wcscmp(name,WC_LISTVIEWW)==0){ListView_SetBkColor(h,UsesBackdrop(h)?CLR_NONE:ThemeBackground());ListView_SetTextBkColor(h,UsesBackdrop(h)?CLR_NONE:ThemeBackground());ListView_SetTextColor(h,ThemeForeground());}if(wcscmp(name,WC_TABCONTROLW)==0){auto style=GetWindowLongPtrW(h,GWL_STYLE);SetWindowLongPtrW(h,GWL_STYLE,style|TCS_OWNERDRAWFIXED);}InvalidateRect(h,nullptr,TRUE);return TRUE;}
}
void ApplyTheme(){RefreshContrast();if(!app.window)return;BOOL dark=Night();DwmSetWindowAttribute(app.window,20,&dark,sizeof(dark));EnumChildWindows(app.window,PaintChild,0);if(app.thumbs){++app.previewRevision;ApplyThumbnailSize(true);if(!app.previewDedicated&&app.selected>=0&&!IsFolderItem(app.selected)){try{Pixels pixels;if(app.cache.Get(ThumbnailCache::Key(app.files[app.selected]),pixels))app.previewPixels=std::move(pixels);else app.previewPixels={};}catch(...){app.previewPixels={};}InvalidateRect(app.preview,nullptr,TRUE);}}RefreshSummaryLinks();SyncBackdrop();RedrawWindow(app.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);}
LRESULT ThemeNotify(NMHDR* hdr){auto draw=reinterpret_cast<NMCUSTOMDRAW*>(hdr);wchar_t name[64]{};GetClassNameW(hdr->hwndFrom,name,64);if(wcscmp(name,L"Button")==0&&(Night()||UsesBackdrop(hdr->hwndFrom))&&draw->dwDrawStage==CDDS_PREPAINT){SelectObject(draw->hdc,UiFont());auto r=draw->rc;auto brush=CreateSolidBrush(draw->uItemState&CDIS_SELECTED?RGB(75,75,75):RGB(50,50,50));if(UsesBackdrop(hdr->hwndFrom))PaintBackdrop(hdr->hwndFrom,draw->hdc,r);else FillRect(draw->hdc,&r,brush);DeleteObject(brush);FrameRect(draw->hdc,&r,GetSysColorBrush(COLOR_GRAYTEXT));SetTextColor(draw->hdc,draw->uItemState&CDIS_DISABLED?RGB(135,135,135):ThemeForeground());SetBkMode(draw->hdc,TRANSPARENT);auto text=Text(hdr->hwndFrom);DrawTextW(draw->hdc,text.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(draw->uItemState&CDIS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(draw->hdc,&r);}return CDRF_SKIPDEFAULT;}if(wcscmp(name,WC_LISTVIEWW)==0){if(reinterpret_cast<NMLVCUSTOMDRAW*>(hdr)->dwItemType==LVCDI_GROUP){if(draw->dwDrawStage==CDDS_ITEMPREPAINT)return CDRF_NOTIFYPOSTPAINT;if(draw->dwDrawStage==CDDS_ITEMPOSTPAINT&&hdr->hwndFrom==app.list)DrawMdbGroupHeader(draw->hdc,int(draw->dwItemSpec));return CDRF_DODEFAULT;}if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;if(draw->dwDrawStage==CDDS_ITEMPOSTPAINT){if(hdr->hwndFrom==app.list){if(IsFolderItem(int(draw->dwItemSpec)))DrawFolderIcon(draw->hdc,int(draw->dwItemSpec));DrawTileSelection(draw->hdc,int(draw->dwItemSpec));DrawMdbBadge(draw->hdc,int(draw->dwItemSpec));}return CDRF_DODEFAULT;}if(draw->dwDrawStage==CDDS_ITEMPREPAINT){if(hdr->hwndFrom==app.list&&(app.categoryMode||app.hideFileNames||IsFolderItem(int(draw->dwItemSpec))))return CDRF_NOTIFYPOSTPAINT;auto list=reinterpret_cast<NMLVCUSTOMDRAW*>(hdr);list->clrText=ThemeForeground();list->clrTextBk=UsesBackdrop(hdr->hwndFrom)?CLR_NONE:ThemeBackground();}}return CDRF_DODEFAULT;}
bool ThemeDrawItem(DRAWITEMSTRUCT* draw){if(draw->CtlType==ODT_COMBOBOX){int saved=SaveDC(draw->hDC);auto font=SendMessageW(draw->hwndItem,WM_GETFONT,0,0);if(font)SelectObject(draw->hDC,reinterpret_cast<HGDIOBJ>(font));bool selected=draw->itemState&ODS_SELECTED;FillRect(draw->hDC,&draw->rcItem,selected?GetSysColorBrush(COLOR_HIGHLIGHT):ThemeBrush());SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,selected?GetSysColor(COLOR_HIGHLIGHTTEXT):ThemeForeground());if(draw->itemID!=static_cast<UINT>(-1)){auto length=SendMessageW(draw->hwndItem,CB_GETLBTEXTLEN,draw->itemID,0);if(length>=0&&length<32768){std::wstring text(length+1,L'\0');SendMessageW(draw->hwndItem,CB_GETLBTEXT,draw->itemID,reinterpret_cast<LPARAM>(text.data()));auto rect=draw->rcItem;rect.left+=4;DrawTextW(draw->hDC,text.c_str(),-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);}}if(draw->itemState&ODS_FOCUS)DrawFocusRect(draw->hDC,&draw->rcItem);RestoreDC(draw->hDC,saved);return true;}if(draw->CtlType!=ODT_TAB)return false;int saved=SaveDC(draw->hDC);SelectObject(draw->hDC,UiFont());wchar_t text[80]{};TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=text;item.cchTextMax=80;TabCtrl_GetItem(draw->hwndItem,draw->itemID,&item);PaintBackdrop(draw->hwndItem,draw->hDC,draw->rcItem);SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,ThemeForeground());DrawTextW(draw->hDC,text,-1,&draw->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);TabBorder(draw->hDC,draw->rcItem);if(draw->itemState&ODS_FOCUS)DrawFocusRect(draw->hDC,&draw->rcItem);RestoreDC(draw->hDC,saved);return true;}
