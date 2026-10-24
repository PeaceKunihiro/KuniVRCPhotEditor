#include "ViewerRuntime.h"
#include "LicenseUi.h"

namespace {
constexpr int Tabs=2105, Body=2106;
constexpr std::array<int,4> Resources{2100,2101,2102,2103};
const wchar_t* TabNames[]{L"MIT License",L"BSD 3-Clause (libwebp)",L"Patent Grant (libwebp)"};

std::string ResourceBytes(int id){
    auto module=GetModuleHandleW(nullptr);
    auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
    if(!resource)throw std::runtime_error("License resource missing");
    auto size=SizeofResource(module,resource);
    auto bytes=static_cast<const char*>(LockResource(LoadResource(module,resource)));
    if(!bytes||!size||size>1024*1024)throw std::runtime_error("License resource invalid");
    return {bytes,size};
}
std::wstring DisplayText(const std::string& bytes){
    auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),nullptr,0);
    if(!count)throw std::runtime_error("License UTF-8 decoding failed");
    std::wstring text(count,L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),int(bytes.size()),text.data(),count))throw std::runtime_error("License decoding failed");
    std::wstring result;
    for(auto c:text){if(result.empty()&&c==0xFEFF)continue;if(c==L'\n'&&(result.empty()||result.back()!=L'\r'))result+=L'\r';result+=c;}
    return result;
}
std::array<std::wstring,3> LicenseTexts(){
    std::array<std::wstring,3> text;
    for(int i=0;i<3;++i)try{
        if(i==0)text[i]=L"KuniVRCPhotEditor — MIT License\r\n\r\n"+DisplayText(ResourceBytes(2100));
        else if(i==1)text[i]=L"libwebp 1.6.0 — BSD 3-Clause\r\n\r\n"+DisplayText(ResourceBytes(2101))+L"\r\n\r\n----- COPYING (original text) -----\r\n\r\n"+DisplayText(ResourceBytes(2102));
        else text[i]=L"libwebp — Patent Grant\r\n\r\n"+DisplayText(ResourceBytes(2103));
    }catch(const std::exception&){text[i]=L"ライセンス文書を読み込めませんでした。配布元のライセンス文書を確認してください。";}
    return text;
}
struct LicenseState {
    std::array<std::wstring,3> text;
    std::function<void(HWND)> inspect;
    std::exception_ptr failure;
};
void LayoutLicense(HWND h){
    RECT r{};GetClientRect(h,&r);auto tabs=GetDlgItem(h,Tabs);if(!tabs)return;
    int margin=12,bh=UiControlHeight(),tabH=std::max(30,UiLineHeight()+10);
    MoveWindow(tabs,margin,margin,std::max(1L,r.right-2*margin),tabH,TRUE);
    int top=margin+std::max(1,TabCtrl_GetRowCount(tabs))*tabH+8;
    MoveWindow(tabs,margin,margin,std::max(1L,r.right-2*margin),top-margin-4,TRUE);
    MoveWindow(GetDlgItem(h,Body),margin,top,std::max(1L,r.right-2*margin),std::max(1L,r.bottom-top-bh-24),TRUE);
    int bw=std::max(100,UiTextWidth(L"閉じる")+20);MoveWindow(GetDlgItem(h,IDCANCEL),std::max(12L,r.right-bw-12),r.bottom-bh-12,bw,bh,TRUE);
}
void SelectLicense(HWND h,LicenseState& state){
    int selected=TabCtrl_GetCurSel(GetDlgItem(h,Tabs));
    if(selected<0||selected>=int(state.text.size()))return;
    auto body=GetDlgItem(h,Body);SetWindowTextW(body,state.text[selected].c_str());
    SendMessageW(body,EM_SETSEL,0,0);SendMessageW(body,EM_SCROLL,SB_TOP,0);
}
LRESULT CALLBACK LicenseEditKeys(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(m==WM_KEYDOWN&&wp=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(h,EM_SETSEL,0,-1);return 0;}
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,LicenseEditKeys,id);
    return DefSubclassProc(h,m,wp,lp);
}
LRESULT CALLBACK LicenseTabPaint(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(m==WM_ERASEBKGND)return 1;
    if(m==WM_PAINT){
        PAINTSTRUCT paint{};auto dc=BeginPaint(h,&paint);RECT area{};GetClientRect(h,&area);FillRect(dc,&area,ThemeBrush());
        auto font=reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0));auto old=SelectObject(dc,font?font:UiFont());
        for(int i=0;i<TabCtrl_GetItemCount(h);++i){DRAWITEMSTRUCT draw{};draw.CtlType=ODT_TAB;draw.CtlID=Tabs;draw.itemID=i;draw.itemAction=ODA_DRAWENTIRE;draw.itemState=TabCtrl_GetCurSel(h)==i?ODS_SELECTED:0;if(GetFocus()==h&&TabCtrl_GetCurFocus(h)==i)draw.itemState|=ODS_FOCUS;draw.hwndItem=h;draw.hDC=dc;TabCtrl_GetItemRect(h,i,&draw.rcItem);SendMessageW(GetParent(h),WM_DRAWITEM,Tabs,reinterpret_cast<LPARAM>(&draw));}
        SelectObject(dc,old);EndPaint(h,&paint);return 0;
    }
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,LicenseTabPaint,id);
    return DefSubclassProc(h,m,wp,lp);
}
INT_PTR CALLBACK LicenseDialog(HWND h,UINT m,WPARAM wp,LPARAM lp){
    auto state=reinterpret_cast<LicenseState*>(GetWindowLongPtrW(h,DWLP_USER));
    if(m==WM_INITDIALOG){ApplyUiFont(h);
        state=reinterpret_cast<LicenseState*>(lp);SetWindowLongPtrW(h,DWLP_USER,lp);SetWindowTextW(h,L"ライセンス情報");
        auto create=[&](const wchar_t* type,const wchar_t* text,int id,DWORD style){auto c=CreateWindowExW(id==Body?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),FALSE);return c;};
        auto tabs=create(WC_TABCONTROLW,L"",Tabs,TCS_BUTTONS|TCS_OWNERDRAWFIXED|TCS_MULTILINE);
        SetWindowSubclass(tabs,LicenseTabPaint,1,0);
        for(auto label:TabNames){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(label);TabCtrl_InsertItem(tabs,TabCtrl_GetItemCount(tabs),&item);}
        auto body=create(L"EDIT",L"",Body,WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL);
        SendMessageW(body,EM_SETLIMITTEXT,1024*1024,0);SetWindowSubclass(body,LicenseEditKeys,1,0);
        create(L"BUTTON",L"閉じる",IDCANCEL,BS_OWNERDRAW);
        RECT size{0,0,680,480};AdjustWindowRectEx(&size,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));
        SetWindowPos(h,nullptr,0,0,size.right-size.left,size.bottom-size.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        LayoutLicense(h);SelectLicense(h,*state);SetFocus(tabs);if(state->inspect)SetTimer(h,49,100,nullptr);return FALSE;
    }
    if(!state)return FALSE;
    if(m==WM_TIMER&&wp==49){KillTimer(h,49);try{state->inspect(h);}catch(...){state->failure=std::current_exception();}EndDialog(h,IDOK);return TRUE;}
    if(m==WM_NOTIFY){auto info=reinterpret_cast<NMHDR*>(lp);if(info->idFrom==Tabs&&info->code==TCN_SELCHANGE){SelectLicense(h,*state);return TRUE;}}
    if(m==WM_SIZE){LayoutLicense(h);return TRUE;}
    if(m==WM_GETMINMAXINFO){RECT minimum{0,0,580,300};AdjustWindowRectEx(&minimum,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));auto limits=reinterpret_cast<MINMAXINFO*>(lp);limits->ptMinTrackSize={minimum.right-minimum.left,minimum.bottom-minimum.top};return TRUE;}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT){auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,ThemeForeground());SetBkColor(dc,ThemeBackground());return reinterpret_cast<INT_PTR>(ThemeBrush());}
    if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(wp),&r,ThemeBrush());return TRUE;}
    if(m==WM_DRAWITEM){auto draw=reinterpret_cast<DRAWITEMSTRUCT*>(lp);int saved=SaveDC(draw->hDC);FillRect(draw->hDC,&draw->rcItem,ThemeBrush());SetTextColor(draw->hDC,ThemeForeground());SetBkMode(draw->hDC,TRANSPARENT);auto border=CreateSolidBrush(ThemeForeground());FrameRect(draw->hDC,&draw->rcItem,border);DeleteObject(border);auto text=draw->CtlID==Tabs&&draw->itemID<3?TabNames[draw->itemID]:L"閉じる";RECT label=draw->rcItem;InflateRect(&label,-6,-3);DrawTextW(draw->hDC,text,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);if(draw->itemState&(ODS_FOCUS|ODS_SELECTED))DrawFocusRect(draw->hDC,&label);RestoreDC(draw->hDC,saved);return TRUE;}
    if(m==WM_SETTINGCHANGE||m==WM_THEMECHANGED||m==WM_SYSCOLORCHANGE){RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);return TRUE;}
    if(m==WM_CLOSE||(m==WM_COMMAND&&(LOWORD(wp)==IDCANCEL||LOWORD(wp)==IDOK))){EndDialog(h,IDCANCEL);return TRUE;}
    return FALSE;
}
}

void ShowLicenseInfo(const std::function<void(HWND)>& inspect){
    LicenseState state;state.text=LicenseTexts();state.inspect=inspect;
    struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;} t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN|DS_MODALFRAME|DS_CENTER,0,0,0,0,360,260},0,0,0};
    auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,app.window,LicenseDialog,reinterpret_cast<LPARAM>(&state));
    if(state.failure)std::rethrow_exception(state.failure);
    if(result==-1&&!inspect)MessageBoxW(app.window,L"ライセンス情報を表示できませんでした。",L"ライセンス情報",MB_OK|MB_ICONERROR);
}

void RunLicenseUiTests(const fs::path& folder,std::ostream& report,const std::function<void(HWND,const wchar_t*)>& capture){
    auto require=[&](bool ok,const char* text){if(!ok)throw std::runtime_error(text);report<<"PASS: "<<text<<'\n';};
    auto source=folder.parent_path().parent_path().parent_path();
    const fs::path paths[]{source/L"LICENSE.txt",source/L"Documents/THIRD_PARTY_NOTICES.md",source/L"third_party/libwebp-1.6.0/COPYING",source/L"third_party/libwebp-1.6.0/PATENTS"};
    for(size_t i=0;i<Resources.size();++i){std::ifstream in(paths[i],std::ios::binary);require(bool(in),"v0.5.2 license source exists");std::string bytes((std::istreambuf_iterator<char>(in)),{});require(ResourceBytes(Resources[i])==bytes,"v0.5.2 embedded license matches original bytes");}
    auto texts=LicenseTexts();require(texts[0].find(L"Copyright (c) 2026 PeaceKunihiro")!=std::wstring::npos&&texts[1].find(L"Neither the name of Google")!=std::wstring::npos&&texts[2].find(L"Additional IP Rights Grant")!=std::wstring::npos,"v0.5.2 license notices mapped to correct tabs");
    bool dark=app.darkTheme;
    struct Restore{bool dark;~Restore(){app.darkTheme=dark;}} restore{dark};
    for(bool night:{false,true}){app.darkTheme=night;ShowLicenseInfo([&](HWND h){
        auto tabs=GetDlgItem(h,Tabs),body=GetDlgItem(h,Body);require(TabCtrl_GetItemCount(tabs)==3&&(GetWindowLongPtrW(body,GWL_STYLE)&ES_READONLY),"v0.5.2 three tabs and read-only body");
        for(int i=0;i<3;++i){TabCtrl_SetCurSel(tabs,i);NMHDR notification{tabs,Tabs,TCN_SELCHANGE};SendMessageW(h,WM_NOTIFY,Tabs,reinterpret_cast<LPARAM>(&notification));require(Text(body)==texts[i],"v0.5.2 complete license text shown on tab change");SendMessageW(body,EM_SETSEL,0,-1);DWORD first=0,last=0;SendMessageW(body,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));require(last==texts[i].size(),"v0.5.2 full license can be selected for copying");}
        capture(h,night?L"v052-license-night.png":L"v052-license-light.png");
        for(auto size:std::array<SIZE,2>{{{600,340},{900,620}}}){SetWindowPos(h,nullptr,0,0,size.cx,size.cy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);RECT content{},button{};GetWindowRect(body,&content);GetWindowRect(GetDlgItem(h,IDCANCEL),&button);require(content.bottom<button.top&&content.right>content.left&&content.bottom>content.top,"v0.5.2 resizing keeps license text above close button");}
        TabCtrl_SetCurSel(tabs,0);NMHDR notification{tabs,Tabs,TCN_SELCHANGE};SendMessageW(h,WM_NOTIFY,Tabs,reinterpret_cast<LPARAM>(&notification));require(SendMessageW(body,EM_GETFIRSTVISIBLELINE,0,0)==0,"v0.5.2 tab change resets scroll position");
    });}
}
