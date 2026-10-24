#include "ViewerRuntime.h"
#include <set>
namespace {
struct FontOwner {std::map<std::pair<std::wstring,int>,HFONT> fonts;~FontOwner(){for(auto& item:fonts)DeleteObject(item.second);}} owner;
constexpr int Family=2701,Size=2702,Defaults=2703;
const wchar_t* Warning=L"フォントやサイズの変更により、文字の欠けやレイアウトの崩れが生じる場合があります。表示確認は既定フォント・10ptのみです。";
struct DialogState {std::function<void(HWND)> inspect;std::wstring name;int pt=12;std::exception_ptr error;};
int CALLBACK FontFamily(const LOGFONTW* lf,const TEXTMETRICW*,DWORD,LPARAM lp){if(lf->lfFaceName[0]&&lf->lfFaceName[0]!=L'@')reinterpret_cast<std::set<std::wstring>*>(lp)->insert(lf->lfFaceName);return 1;}
INT_PTR CALLBACK FontDialog(HWND h,UINT m,WPARAM wp,LPARAM lp){
    auto state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(h,DWLP_USER));
    if(m==WM_INITDIALOG){state=reinterpret_cast<DialogState*>(lp);SetWindowLongPtrW(h,DWLP_USER,lp);SetWindowTextW(h,L"フォント");RECT bounds{0,0,514,238};AdjustWindowRectEx(&bounds,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));SetWindowPos(h,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER);
        auto control=[&](const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int height,int id){auto child=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,height,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),FALSE);return child;};
        control(L"STATIC",L"アプリ内の文字表示に使用するフォントとサイズ",0,12,12,490,26,0);
        auto fonts=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,12,44,490,320,Family);
        SendMessageW(fonts,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"フォント：既定"));
        LOGFONTW lf{};lf.lfCharSet=DEFAULT_CHARSET;std::set<std::wstring> families;auto dc=GetDC(h);EnumFontFamiliesExW(dc,&lf,FontFamily,reinterpret_cast<LPARAM>(&families),0);ReleaseDC(h,dc);
        int selected=0,i=1;for(auto& name:families){auto label=L"フォント："+name;SendMessageW(fonts,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(_wcsicmp(name.c_str(),state->name.c_str())==0)selected=i;++i;}SendMessageW(fonts,CB_SETCURSEL,selected,0);
        control(L"STATIC",L"フォントサイズ（pt）",0,12,82,180,26,0);
        auto sizes=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,200,80,100,260,Size);for(int n=10;n<=24;n+=2){auto text=std::to_wstring(n);SendMessageW(sizes,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}SendMessageW(sizes,CB_SETCURSEL,(state->pt-10)/2,0);
        control(L"STATIC",Warning,0,12,118,490,64,0);
        control(L"BUTTON",L"既定に戻す",WS_TABSTOP,12,194,130,28,Defaults);control(L"BUTTON",L"適用",WS_TABSTOP|BS_DEFPUSHBUTTON,282,194,100,28,IDOK);control(L"BUTTON",L"キャンセル",WS_TABSTOP,390,194,112,28,IDCANCEL);
        ScaleUiDialog(h);if(state->inspect)SetTimer(h,51,100,nullptr);return TRUE;
    }
    if(m==WM_TIMER&&wp==51){KillTimer(h,51);try{state->inspect(h);}catch(...){state->error=std::current_exception();EndDialog(h,IDCANCEL);}return TRUE;}
    if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(wp),&r,ThemeBrush());return TRUE;}
    if(m==WM_NOTIFY&&reinterpret_cast<NMHDR*>(lp)->code==NM_CUSTOMDRAW){SetWindowLongPtrW(h,DWLP_MSGRESULT,ThemeNotify(reinterpret_cast<NMHDR*>(lp)));return TRUE;}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORLISTBOX||m==WM_CTLCOLORBTN)return ThemeControlColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp));
    if(m==WM_COMMAND){if(LOWORD(wp)==Defaults){SendDlgItemMessageW(h,Family,CB_SETCURSEL,0,0);SendDlgItemMessageW(h,Size,CB_SETCURSEL,1,0);return TRUE;}
        if(LOWORD(wp)==IDCANCEL){EndDialog(h,IDCANCEL);return TRUE;}
        if(LOWORD(wp)==IDOK){int family=int(SendDlgItemMessageW(h,Family,CB_GETCURSEL,0,0)),size=int(SendDlgItemMessageW(h,Size,CB_GETCURSEL,0,0));if(family<0||size<0)return TRUE;
            auto text=Text(GetDlgItem(h,Family));std::wstring name=family==0?L"":text.substr(std::wstring(L"フォント：").size());int pt=10+size*2;
            if((!name.empty()||pt!=12)&&(name!=app.fontName||pt!=app.fontPt)&&MessageBoxW(h,Warning,L"フォント変更の注意",MB_OKCANCEL|MB_ICONWARNING)!=IDOK)return TRUE;
            state->name=name;state->pt=pt;EndDialog(h,IDOK);return TRUE;}}
    if(m==WM_CLOSE){EndDialog(h,IDCANCEL);return TRUE;}return FALSE;
}
}
HFONT UiFont(){
    auto key=std::make_pair(app.fontName,app.fontPt);auto found=owner.fonts.find(key);if(found!=owner.fonts.end())return found->second;
    LOGFONTW lf{};GetObjectW(GetStockObject(DEFAULT_GUI_FONT),sizeof(lf),&lf);auto dc=GetDC(nullptr);lf.lfHeight=-MulDiv(app.fontPt,GetDeviceCaps(dc,LOGPIXELSY),72);ReleaseDC(nullptr,dc);lf.lfWidth=0;
    if(!app.fontName.empty())wcsncpy_s(lf.lfFaceName,app.fontName.c_str(),_TRUNCATE);
    auto font=CreateFontIndirectW(&lf);if(!font)return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));owner.fonts.emplace(std::move(key),font);return font;
}
int UiLineHeight(){auto dc=GetDC(nullptr);auto old=SelectObject(dc,UiFont());TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);SelectObject(dc,old);ReleaseDC(nullptr,dc);return std::max(1L,tm.tmHeight+tm.tmExternalLeading);}
int UiTextWidth(const std::wstring& text){auto dc=GetDC(nullptr);auto old=SelectObject(dc,UiFont());SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),int(text.size()),&size);SelectObject(dc,old);ReleaseDC(nullptr,dc);return int(size.cx);}
int UiScale(int value){
    static int baseline=0;static HFONT measured=nullptr;static int height=0;
    if(!baseline){LOGFONTW lf{};GetObjectW(GetStockObject(DEFAULT_GUI_FONT),sizeof(lf),&lf);auto dc=GetDC(nullptr);lf.lfHeight=-MulDiv(10,GetDeviceCaps(dc,LOGPIXELSY),72);auto font=CreateFontIndirectW(&lf);auto old=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));TEXTMETRICW tm{};GetTextMetricsW(dc,&tm);SelectObject(dc,old);ReleaseDC(nullptr,dc);if(font)DeleteObject(font);baseline=std::max(1L,tm.tmHeight+tm.tmExternalLeading);}
    if(measured!=UiFont()){measured=UiFont();height=std::max(baseline,UiLineHeight());}
    return MulDiv(value,height,baseline);
}
bool UiStackEditorFields(){return app.fontPt>=18;}
int UiControlHeight(){return std::max(26,UiLineHeight()+10);}
namespace {
struct DialogScroll {int width=0,height=0,x=0,y=0;};
void ScrollDialog(HWND h,DialogScroll& state,int x,int y){RECT view{};GetClientRect(h,&view);x=std::clamp(x,0,std::max(0,state.width-int(view.right)));y=std::clamp(y,0,std::max(0,state.height-int(view.bottom)));ScrollWindowEx(h,state.x-x,state.y-y,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);state.x=x;state.y=y;SetScrollPos(h,SB_HORZ,x,TRUE);SetScrollPos(h,SB_VERT,y,TRUE);}
LRESULT CALLBACK DialogScrollProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data){auto state=reinterpret_cast<DialogScroll*>(data);
    if(m==WM_NCDESTROY){RemoveWindowSubclass(h,DialogScrollProc,id);delete state;return DefSubclassProc(h,m,wp,lp);}
    if(m==WM_VSCROLL||m==WM_HSCROLL||m==WM_MOUSEWHEEL){bool horizontal=m==WM_HSCROLL;int next=horizontal?state->x:state->y;RECT view{};GetClientRect(h,&view);int page=horizontal?view.right:view.bottom;SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(h,horizontal?SB_HORZ:SB_VERT,&info);
        if(m==WM_MOUSEWHEEL)next-=short(HIWORD(wp))/WHEEL_DELTA*UiLineHeight()*3;else switch(LOWORD(wp)){case SB_TOP:next=0;break;case SB_BOTTOM:next=horizontal?state->width:state->height;break;case SB_LINEUP:next-=UiLineHeight();break;case SB_LINEDOWN:next+=UiLineHeight();break;case SB_PAGEUP:next-=page;break;case SB_PAGEDOWN:next+=page;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=info.nTrackPos;break;}
        ScrollDialog(h,*state,horizontal?next:state->x,horizontal?state->y:next);return 0;
    }
    auto answer=DefSubclassProc(h,m,wp,lp);
    if(m==WM_COMMAND&&lp&&reinterpret_cast<HWND>(lp)==GetFocus()){RECT child{},view{};GetWindowRect(GetFocus(),&child);MapWindowPoints(nullptr,h,reinterpret_cast<POINT*>(&child),2);GetClientRect(h,&view);int dx=child.left<0?child.left:child.right>view.right?std::min(child.left,child.right-view.right):0;int dy=child.top<0?child.top:child.bottom>view.bottom?std::min(child.top,child.bottom-view.bottom):0;if(dx||dy)ScrollDialog(h,*state,state->x+dx,state->y+dy);}
    return answer;
}
}
void ScaleUiDialog(HWND window){
    // Fixed layouts opt in after initialization; dynamic layouts use UiScale themselves.
    if(!window||UiScale(100)==100)return;RECT client{};GetClientRect(window,&client);
    for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){
        RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);
        wchar_t type[32]{};GetClassNameW(child,type,32);int height=r.bottom-r.top;
        if(_wcsicmp(type,L"ComboBox")==0){RECT drop{};SendMessageW(child,CB_GETDROPPEDCONTROLRECT,0,reinterpret_cast<LPARAM>(&drop));height=std::max(height,int(drop.bottom-drop.top));}
        if(_wcsicmp(type,WC_LISTVIEWW)==0){int columns=Header_GetItemCount(ListView_GetHeader(child));for(int i=0;i<columns;++i)ListView_SetColumnWidth(child,i,UiScale(ListView_GetColumnWidth(child,i)));}
        MoveWindow(child,UiScale(r.left),UiScale(r.top),UiScale(r.right-r.left),UiScale(height),FALSE);
    }
    int width=UiScale(client.right),height=UiScale(client.bottom);RECT bounds{0,0,width,height},work{};AdjustWindowRectEx(&bounds,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)));MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);work=monitor.rcWork;
    int outerW=std::min(int(bounds.right-bounds.left),int(std::max(100L,work.right-work.left-24))),outerH=std::min(int(bounds.bottom-bounds.top),int(std::max(100L,work.bottom-work.top-24)));RECT old{};GetWindowRect(window,&old);
    SetWindowPos(window,nullptr,std::clamp(old.left,work.left,std::max(work.left,work.right-outerW)),std::clamp(old.top,work.top,std::max(work.top,work.bottom-outerH)),outerW,outerH,SWP_NOZORDER|SWP_NOACTIVATE);
    GetClientRect(window,&client);bool horizontal=width>client.right,vertical=height>client.bottom;
    if(horizontal||vertical){auto state=new DialogScroll{width,height};SetWindowSubclass(window,DialogScrollProc,516,reinterpret_cast<DWORD_PTR>(state));ShowScrollBar(window,SB_HORZ,horizontal);ShowScrollBar(window,SB_VERT,vertical);GetClientRect(window,&client);SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMax=width-1;info.nPage=client.right;SetScrollInfo(window,SB_HORZ,&info,TRUE);info.nMax=height-1;info.nPage=client.bottom;SetScrollInfo(window,SB_VERT,&info,TRUE);}
}
void ApplyUiFont(HWND window){if(!window)return;SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),FALSE);EnumChildWindows(window,[](HWND child,LPARAM){SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),FALSE);wchar_t type[32]{};GetClassNameW(child,type,32);if(_wcsicmp(type,L"ComboBox")==0){SendMessageW(child,CB_SETITEMHEIGHT,WPARAM(-1),UiControlHeight()-6);SendMessageW(child,CB_SETITEMHEIGHT,0,UiControlHeight()-6);}return TRUE;},0);}
void ReleaseUiFonts(){for(auto& item:owner.fonts)DeleteObject(item.second);owner.fonts.clear();}
void SetAppFont(const std::wstring& name,int pt,bool persist){
    if(name.size()>=LF_FACESIZE||std::any_of(name.begin(),name.end(),[](wchar_t c){return c<32;})||pt<10||pt>24||pt%2)throw std::runtime_error("Invalid font setting");
    app.fontName=name;app.fontPt=pt;ApplyUiFont(app.window);EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM){wchar_t type[64]{};GetClassNameW(h,type,64);if(wcscmp(type,TOOLTIPS_CLASSW)==0)ApplyUiFont(h);return TRUE;},0);app.imageLabelsDirty=true;
    if(app.window&&app.simple){RefreshSummaryLinks();app.editorUi.Layout();Layout();}
    if(persist)SaveSettings();
}
void ShowFontDialog(const std::function<void(HWND)>& inspect){
    DialogState state{inspect,app.fontName,app.fontPt};struct Template{DLGTEMPLATE d;WORD menu,cls,title;}t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER,0,0,0,0,300,150},0,0,0};
    auto answer=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.d,app.window,FontDialog,reinterpret_cast<LPARAM>(&state));if(state.error)std::rethrow_exception(state.error);if(answer==IDOK)SetAppFont(state.name,state.pt);
}

namespace {
struct MenuLabel {HMENU menu{};UINT position{},originalType{},originalState{};ULONG_PTR originalData{};std::wstring text;bool submenu=false,separator=false;};
struct MenuFontState {std::vector<std::unique_ptr<MenuLabel>> labels;std::vector<std::pair<HMENU,HBRUSH>> backgrounds;};
void FontMenuItems(HMENU menu,MenuFontState& state){
    if(ThemeIsDark()){MENUINFO original{sizeof(original)};original.fMask=MIM_BACKGROUND;if(GetMenuInfo(menu,&original)){state.backgrounds.push_back({menu,original.hbrBack});MENUINFO dark{sizeof(dark)};dark.fMask=MIM_BACKGROUND;dark.hbrBack=ThemeBrush();SetMenuInfo(menu,&dark);}}
    for(int i=0;i<GetMenuItemCount(menu);++i){MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_FTYPE|MIIM_DATA|MIIM_STATE|MIIM_SUBMENU|MIIM_STRING;GetMenuItemInfoW(menu,i,TRUE,&info);if(info.hSubMenu)FontMenuItems(info.hSubMenu,state);if((info.fType&MFT_SEPARATOR)&&!ThemeIsDark())continue;
        auto label=std::make_unique<MenuLabel>();label->menu=menu;label->position=i;label->originalType=info.fType;label->originalState=info.fState;label->separator=(info.fType&MFT_SEPARATOR)!=0;label->originalData=info.dwItemData;label->submenu=info.hSubMenu!=nullptr;label->text.resize(size_t(info.cch)+1);info.dwTypeData=label->text.data();info.cch=UINT(label->text.size());GetMenuItemInfoW(menu,i,TRUE,&info);label->text.resize(wcslen(label->text.c_str()));
        MENUITEMINFOW draw{sizeof(draw)};draw.fMask=MIIM_FTYPE|MIIM_DATA|MIIM_STATE;draw.fType=(info.fType&~MFT_SEPARATOR)|MFT_OWNERDRAW;draw.fState=label->separator?info.fState|MFS_DISABLED:info.fState;draw.dwItemData=reinterpret_cast<ULONG_PTR>(label.get());SetMenuItemInfoW(menu,i,TRUE,&draw);state.labels.push_back(std::move(label));
    }
}
LRESULT CALLBACK MenuFontProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){
    if(m==WM_ENTERIDLE&&wp==MSGF_MENU)RefreshMenuFrame(reinterpret_cast<HWND>(lp));
    auto state=reinterpret_cast<MenuFontState*>(data);MenuLabel* label=nullptr;ULONG_PTR item=0;
    if(m==WM_MEASUREITEM&&reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->CtlType==ODT_MENU)item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemData;
    if(m==WM_DRAWITEM&&reinterpret_cast<DRAWITEMSTRUCT*>(lp)->CtlType==ODT_MENU)item=reinterpret_cast<DRAWITEMSTRUCT*>(lp)->itemData;
    if(item)for(auto& entry:state->labels)if(reinterpret_cast<ULONG_PTR>(entry.get())==item){label=entry.get();break;}
    if(!label)return DefSubclassProc(h,m,wp,lp);
    if(m==WM_MEASUREITEM){auto measure=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);if(label->separator){measure->itemWidth=0;measure->itemHeight=9;return TRUE;}HDC dc=GetDC(h);auto old=SelectObject(dc,UiFont());SIZE size{};GetTextExtentPoint32W(dc,label->text.c_str(),int(label->text.size()),&size);SelectObject(dc,old);ReleaseDC(h,dc);measure->itemWidth=size.cx+48;measure->itemHeight=std::max(22L,size.cy+8);return TRUE;}
    auto draw=reinterpret_cast<DRAWITEMSTRUCT*>(lp);int saved=SaveDC(draw->hDC);auto rect=draw->rcItem;
    if(label->separator){FillRect(draw->hDC,&rect,ThemeBrush());auto pen=CreatePen(PS_SOLID,1,RGB(72,72,72));auto old=SelectObject(draw->hDC,pen);MoveToEx(draw->hDC,rect.left+8,(rect.top+rect.bottom)/2,nullptr);LineTo(draw->hDC,rect.right-8,(rect.top+rect.bottom)/2);SelectObject(draw->hDC,old);DeleteObject(pen);RestoreDC(draw->hDC,saved);return TRUE;}
    bool selected=draw->itemState&ODS_SELECTED,disabled=draw->itemState&(ODS_DISABLED|ODS_GRAYED);FillRect(draw->hDC,&rect,selected?GetSysColorBrush(COLOR_HIGHLIGHT):ThemeBrush());SelectObject(draw->hDC,UiFont());SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,disabled?GetSysColor(COLOR_GRAYTEXT):selected?GetSysColor(COLOR_HIGHLIGHTTEXT):ThemeForeground());auto textRect=rect;textRect.left+=24;textRect.right-=20;DrawTextW(draw->hDC,label->text.c_str(),-1,&textRect,DT_SINGLELINE|DT_VCENTER|DT_LEFT);
    if(draw->itemState&ODS_CHECKED){auto check=rect;check.right=check.left+22;DrawTextW(draw->hDC,L"✓",1,&check,DT_SINGLELINE|DT_VCENTER|DT_CENTER);}if(label->submenu){auto arrow=rect;arrow.left=arrow.right-18;DrawTextW(draw->hDC,L"▶",1,&arrow,DT_SINGLELINE|DT_VCENTER|DT_CENTER);}RestoreDC(draw->hDC,saved);return TRUE;
}
}
UINT TrackUiMenu(HMENU menu,UINT flags,int x,int y,HWND ownerWindow,TPMPARAMS* params){
    MenuFontState state;FontMenuItems(menu,state);SetWindowSubclass(ownerWindow,MenuFontProc,514,reinterpret_cast<DWORD_PTR>(&state));
    MenuThemeScope frameTheme;auto answer=TrackPopupMenuEx(menu,flags,x,y,ownerWindow,params);RemoveWindowSubclass(ownerWindow,MenuFontProc,514);
    for(auto& entry:state.labels){MENUITEMINFOW restore{sizeof(restore)};restore.fMask=MIIM_FTYPE|MIIM_DATA|MIIM_STATE;restore.fType=entry->originalType;restore.fState=entry->originalState;restore.dwItemData=entry->originalData;SetMenuItemInfoW(entry->menu,entry->position,TRUE,&restore);}for(auto& original:state.backgrounds){MENUINFO info{sizeof(info)};info.fMask=MIM_BACKGROUND;info.hbrBack=original.second;SetMenuInfo(original.first,&info);}return UINT(answer);
}
