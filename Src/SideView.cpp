#include "ViewerRuntime.h"
#include <windowsx.h>
namespace {
constexpr UINT TabBase=2600;
void ScrollSide(int next){app.sideScroll=std::clamp(next,0,std::max(0,app.sideContent));LayoutSideView();}
void PaintVerticalTab(DRAWITEMSTRUCT* draw){
    int selected=TabCtrl_GetCurSel(app.metadataTabs),index=int(draw->CtlID)-TabBase;
    auto r=draw->rcItem;auto brush=CreateSolidBrush(index==selected?GetSysColor(COLOR_BTNFACE):ThemeBackground());
    FillRect(draw->hDC,&r,brush);DeleteObject(brush);auto border=CreateSolidBrush(ThemeForeground());FrameRect(draw->hDC,&r,border);DeleteObject(border);
    int saved=SaveDC(draw->hDC);SelectObject(draw->hDC,UiFont());SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,index==selected?GetSysColor(COLOR_BTNTEXT):ThemeForeground());
    auto text=Text(draw->hwndItem);DrawTextW(draw->hDC,text.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);if(draw->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(draw->hDC,&r);}RestoreDC(draw->hDC,saved);
}
LRESULT CALLBACK SideProc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_SIZE){if(!UiRepaintActive())LayoutSideView();return 0;}
    if(m==WM_MOUSEWHEEL){app.sideWheel+=GET_WHEEL_DELTA_WPARAM(wp);int delta=app.sideWheel/WHEEL_DELTA;app.sideWheel%=WHEEL_DELTA;ScrollSide(app.sideScroll-delta*std::max(36,UiLineHeight()*3));return 0;}
    if(m==WM_VSCROLL){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(h,SB_VERT,&info);int next=app.sideScroll;switch(LOWORD(wp)){case SB_TOP:next=0;break;case SB_BOTTOM:next=app.sideContent;break;case SB_LINEUP:next-=UiLineHeight()+4;break;case SB_LINEDOWN:next+=UiLineHeight()+4;break;case SB_PAGEUP:next-=int(info.nPage);break;case SB_PAGEDOWN:next+=int(info.nPage);break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=info.nTrackPos;break;}ScrollSide(next);return 0;}
    if(m==WM_APP+16){EnsureSideVisible(GetFocus());return 0;}
    if(m==WM_COMMAND&&LOWORD(wp)>=TabBase&&LOWORD(wp)<TabBase+4&&HIWORD(wp)==BN_CLICKED){int index=LOWORD(wp)-TabBase;if(index<TabCtrl_GetItemCount(app.metadataTabs)){TabCtrl_SetCurSel(app.metadataTabs,index);LayoutSideView();}return 0;}
    if(m==WM_DRAWITEM){auto d=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(d->CtlID>=TabBase&&d->CtlID<TabBase+4){PaintVerticalTab(d);return TRUE;}}
    if(m==WM_COMMAND||m==WM_NOTIFY||m==WM_DRAWITEM||m==WM_MEASUREITEM)return SendMessageW(app.window,m,wp,lp);
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORLISTBOX||m==WM_CTLCOLORBTN)return ThemeControlColor(reinterpret_cast<HDC>(wp));
    if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(wp),&r,ThemeBrush());return 1;}
    return DefWindowProcW(h,m,wp,lp);
}
int ImageHeight(int width){
    auto frame=OcrTestSource()?OcrTestPreview():stage3::RecordingOcr()?person_ocr::Preview():std::shared_ptr<const Pixels>{};
    auto& p=frame?*frame:app.previewPixels;
    return p.width&&p.height?int(std::clamp(double(width)*p.height/p.width,1.0,double(INT_MAX/4))):std::max(60,width*9/16);
}
}
HWND AppControl(int id){auto h=GetDlgItem(app.window,id);return h?h:GetDlgItem(app.side,id);}
HWND SideHit(POINT point){MapWindowPoints(app.window,app.side,&point,1);return ChildWindowFromPointEx(app.side,point,CWP_SKIPINVISIBLE);}
void CreateSideView(){
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"KuniSideView";wc.lpfnWndProc=SideProc;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    app.side=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,app.window,nullptr,wc.hInstance,nullptr);
    for(int i=0;i<4;++i){app.verticalTabs[i]=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,1,1,app.side,reinterpret_cast<HMENU>(INT_PTR(TabBase+i)),wc.hInstance,nullptr);SendMessageW(app.verticalTabs[i],WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),FALSE);}
}
void ResetSideScroll(){app.sideScroll=app.sideWheel=0;}
bool SideWheel(HWND hovered,WPARAM wp,LPARAM lp){
    if(!app.side||!(hovered==app.side||IsChild(app.side,hovered)))return false;
    // Let open dropdown lists consume their own wheel instead of moving the pane.
    wchar_t type[64]{};GetClassNameW(hovered,type,64);if(wcscmp(type,L"ComboLBox")==0||SendMessageW(hovered,CB_GETDROPPEDSTATE,0,0)==TRUE&&wcscmp(type,L"ComboBox")==0)return false;
    SendMessageW(app.side,WM_MOUSEWHEEL,wp,lp);return true;
}
void EnsureSideVisible(HWND h){
    if(!h||!app.side||!IsChild(app.side,h))return;RECT child{},view{};GetWindowRect(h,&child);GetWindowRect(app.side,&view);
    int delta=child.top<view.top?child.top-view.top:child.bottom>view.bottom?std::min(child.top-view.top,child.bottom-view.bottom):0;if(delta)ScrollSide(app.sideScroll+delta);
}
void LayoutSideView(){
    if(!app.side||!app.preview||!app.metadataTabs||!app.simple||!app.details||app.sideLayout)return;
    UiRepaintBatch paintBatch(app.side);app.sideLayout=true;
    RECT viewport{};GetClientRect(app.side,&viewport);int w=std::max(1L,viewport.right),h=std::max(1L,viewport.bottom);bool bottom=app.layout==L"bottom",none=app.layout==L"none";
    // Reserve a stable scrollbar gutter so width/line-wrapping cannot oscillate.
    ShowScrollBar(app.side,SB_VERT,TRUE);GetClientRect(app.side,&viewport);w=std::max(1L,viewport.right);h=std::max(1L,viewport.bottom);
    int imageW=bottom?std::max(1,w/3):w,imageH=ImageHeight(imageW),gap=4;
    if(bottom)imageH=std::min(imageH,h);
    int tabX=bottom?imageW+gap:0,tabY=bottom?0:imageH+gap,tabW=std::max(1,w-tabX),line=UiLineHeight();
    int index=TabCtrl_GetCurSel(app.metadataTabs);if(index<0)index=0;
    int header=std::max(24,line+8),nav=bottom?std::min(std::max(60,tabW/2),std::max(88,UiTextWidth(L"シンプル")+12)):0;
    RECT page{tabX+(bottom?nav+gap:4),tabY+(bottom?0:header),w-4,0};
    int pageW=std::max(1L,page.right-page.left);
    // Line wrapping needs the width only. Never move the measuring pane over the image.
    static int measuredWidth=0;static HFONT measuredFont=nullptr;
    if(app.simpleLayoutDirty||measuredWidth!=pageW||measuredFont!=UiFont()){
        RECT current{};GetClientRect(app.simple,&current);
        app.sideMeasuring=true;app.simpleScroll=0;SetWindowPos(app.simple,nullptr,0,0,pageW,std::max(1L,current.bottom),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);LayoutSimple();app.sideMeasuring=false;
        measuredWidth=pageW;measuredFont=UiFont();
    }
    int editHeader=UiControlHeight()+std::max(32,line*2+4)+4,editX=0;
    for(auto control:{app.editButton,app.saveButton,app.cancelButton}){int bw=std::min(pageW,std::max(control==app.cancelButton?88:64,UiTextWidth(Text(control))+16));if(editX&&editX+bw>pageW){editX=0;editHeader+=UiControlHeight()+4;}editX+=bw+4;}
    int body=index==0?std::max(40,app.simpleContentHeight):index==1?(app.editing?app.editorUi.DesiredHeight()+editHeader:std::max(240,h-header)):index==2?std::max(UiScale(400),h-header):std::max(UiScale(280),h-header);
    int tabH=(bottom?0:header)+body,total=std::max(imageH,tabY+tabH);
    if(bottom)total=std::max(total,int(TabCtrl_GetItemCount(app.metadataTabs))*(header+2));
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS|SIF_DISABLENOSCROLL};info.nMax=std::max(0,total-1);info.nPage=h;info.nPos=app.sideScroll;SetScrollInfo(app.side,SB_VERT,&info,TRUE);info.fMask=SIF_POS;GetScrollInfo(app.side,SB_VERT,&info);app.sideScroll=info.nPos;app.sideContent=std::max(0,total-h);
    int yoff=app.sideScroll;MoveWindow(app.preview,0,-yoff,imageW,imageH,FALSE);ShowWindow(app.preview,none?SW_HIDE:SW_SHOW);
    MoveWindow(app.metadataTabs,tabX,tabY-yoff,tabW,tabH,FALSE);ShowWindow(app.metadataTabs,!bottom&&!none?SW_SHOW:SW_HIDE);
    for(int i=0;i<4;++i){auto button=app.verticalTabs[i];bool show=bottom&&!none&&i<TabCtrl_GetItemCount(app.metadataTabs);ShowWindow(button,show?SW_SHOW:SW_HIDE);if(show){wchar_t text[64]{};TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=text;item.cchTextMax=64;TabCtrl_GetItem(app.metadataTabs,i,&item);SetWindowTextW(button,text);MoveWindow(button,tabX,i*(header+2)-yoff,nav,header,FALSE);InvalidateRect(button,nullptr,FALSE);}}
    page.top-=yoff;page.bottom=page.top+body;
    MoveWindow(app.simple,page.left,page.top,pageW,body,FALSE);ShowWindow(app.simple,index==0&&!none?SW_SHOW:SW_HIDE);LayoutSimple();
    bool detailed=index==1&&!none;int buttonH=UiControlHeight(),statusH=std::max(32,line*2+4),bx=0,by=page.top;
    for(auto control:{app.editButton,app.saveButton,app.cancelButton}){int bw=std::min(pageW,std::max(control==app.cancelButton?88:64,UiTextWidth(Text(control))+16));if(bx&&bx+bw>pageW){bx=0;by+=buttonH+4;}MoveWindow(control,page.left+bx,by,bw,buttonH,FALSE);bx+=bw+4;}
    int editTop=by+buttonH+statusH+4;MoveWindow(app.editStatus,page.left,by+buttonH+2,pageW,statusH,FALSE);
    MoveWindow(app.details,page.left,editTop,pageW,std::max(1L,page.bottom-editTop),FALSE);
    for(auto control:{app.editButton,app.saveButton,app.cancelButton,app.editStatus})ShowWindow(control,detailed?SW_SHOW:SW_HIDE);
    app.editorUi.Place(page.left,editTop,pageW,std::max(1L,page.bottom-editTop),detailed&&app.editing);ShowWindow(app.details,detailed&&!app.editing?SW_SHOW:SW_HIDE);
    LayoutOcrUi(page,index==2&&!none);LayoutMdbApplyUi(page,index==3&&!none);
    for(auto control:{app.simple,app.details,app.editorUi.Window(),app.editButton,app.saveButton,app.cancelButton,app.editStatus})SetWindowPos(control,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    app.sideLayout=false;
}
