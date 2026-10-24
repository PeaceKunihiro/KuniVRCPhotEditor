#include "ViewerRuntime.h"
#include "MdbApply.h"
namespace {
constexpr int Run=2451,Description=2450,Note=2452,Output=2453;
HWND pane=nullptr,confirmation=nullptr;int scroll=0,contentHeight=0;
std::wstring message;MdbApplyHooks hooks;
std::atomic<unsigned> token{0};
struct Pending {bool active=false;unsigned ticket=0,generation=0,selection=0;std::wstring path,revision;} pending;
void PaintButton(DRAWITEMSTRUCT* d){int saved=SaveDC(d->hDC);SelectObject(d->hDC,UiFont());PaintBackdrop(d->hwndItem,d->hDC,d->rcItem);auto brush=CreateSolidBrush(ThemeForeground());FrameRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);SetTextColor(d->hDC,IsWindowEnabled(d->hwndItem)?ThemeForeground():GetSysColor(COLOR_GRAYTEXT));SetBkMode(d->hDC,TRANSPARENT);wchar_t text[32]{};GetWindowTextW(d->hwndItem,text,32);DrawTextW(d->hDC,text,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(d->itemState&ODS_FOCUS){auto r=d->rcItem;InflateRect(&r,-4,-4);DrawFocusRect(d->hDC,&r);}RestoreDC(d->hDC,saved);}
std::wstring Reason(){
    if(!app.mdbEnabled)return L"MDBがOFFです。";if(app.closing||app.stopping)return L"終了しています。";
    if(app.folderPending||app.searching||app.sorting)return L"一覧の読込み・検索・分類が完了するまでお待ちください。";
    if(app.editing||app.saving||app.backupBusy||app.batchLoading)return L"画像の編集・保存などを完了してから実行してください。";
    if(app.selectionPending||SelectedImageCount()!=1||app.selectedFolderCount||app.selected<0||app.selected>=int(app.files.size())||IsFolderItem(app.selected))return L"画像を1枚選択してください。同じ写真の重複表示は1枚と扱います。";
    if(!app.metadataReady||app.summary.metadataFailed)return L"画像のメタデータ読込みが完了していないか、読込みに失敗しています。";return L"";
}
bool SameContext(){return pending.active&&pending.ticket==token&&pending.generation==app.currentGeneration&&pending.selection==app.currentSelection&&Reason().empty()&&PathKeyForMdb(app.files[app.selected])==PathKeyForMdb(pending.path);}
void ArrangePane(){if(!pane)return;RECT r{};GetClientRect(pane,&r);int height=std::max(0L,r.bottom),width=std::max(1L,r.right-16),top=UiScale(144),resultHeight=std::max(100,height-UiScale(152));contentHeight=top+resultHeight+8;
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMax=contentHeight-1;info.nPage=height;info.nPos=scroll;SetScrollInfo(pane,SB_VERT,&info,TRUE);info.fMask=SIF_POS;GetScrollInfo(pane,SB_VERT,&info);scroll=info.nPos;
    MoveWindow(GetDlgItem(pane,Description),8,8-scroll,width,UiScale(36),TRUE);MoveWindow(GetDlgItem(pane,Note),8,UiScale(48)-scroll,width,UiScale(52),TRUE);MoveWindow(GetDlgItem(pane,Run),8,UiScale(108)-scroll,UiScale(100),UiControlHeight(),TRUE);MoveWindow(GetDlgItem(pane,Output),8,top-scroll,width,resultHeight,TRUE);
}
LRESULT CALLBACK PaneProc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_SIZE){ArrangePane();return 0;}
    if(m==WM_VSCROLL||m==WM_MOUSEWHEEL){SCROLLINFO i{sizeof(i),SIF_ALL};GetScrollInfo(h,SB_VERT,&i);int next=i.nPos;if(m==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*60;else switch(LOWORD(wp)){case SB_TOP:next=0;break;case SB_BOTTOM:next=i.nMax;break;case SB_LINEUP:next-=20;break;case SB_LINEDOWN:next+=20;break;case SB_PAGEUP:next-=int(i.nPage);break;case SB_PAGEDOWN:next+=int(i.nPage);break;case SB_THUMBPOSITION:case SB_THUMBTRACK:next=i.nTrackPos;break;default:break;}scroll=std::clamp(next,0,std::max(0,i.nMax-int(i.nPage)+1));ArrangePane();return 0;}
    if(m==WM_COMMAND&&LOWORD(wp)==Run&&HIWORD(wp)==BN_CLICKED){StartMdbApply();return 0;}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT)return ThemeControlColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp));
    if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(wp),&r,ThemeBrush());return 1;}
    if(m==WM_DRAWITEM){PaintButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}
    return DefWindowProcW(h,m,wp,lp);
}
struct ConfirmState{const PhotoNamePlan* plan;std::function<void(HWND)> inspect;std::exception_ptr failure;};
void ArrangeConfirmation(HWND h){RECT r{};GetClientRect(h,&r);if(!GetDlgItem(h,2460))return;int bh=UiControlHeight(),note=UiScale(48),summary=UiScale(80),tableTop=note+summary+22;MoveWindow(GetDlgItem(h,2460),12,10,r.right-24,note,TRUE);MoveWindow(GetDlgItem(h,2461),12,note+14,r.right-24,summary,TRUE);MoveWindow(GetDlgItem(h,2462),12,tableTop,r.right-24,std::max(60L,r.bottom-tableTop-bh-24),TRUE);int width=std::max(300L,r.right-46),idW=std::min(UiScale(285),width/2);ListView_SetColumnWidth(GetDlgItem(h,2462),0,idW);ListView_SetColumnWidth(GetDlgItem(h,2462),1,(width-idW)/2);ListView_SetColumnWidth(GetDlgItem(h,2462),2,(width-idW)/2);int bw=std::max(108,UiTextWidth(L"キャンセル")+20);MoveWindow(GetDlgItem(h,IDOK),r.right-2*bw-20,r.bottom-bh-12,bw,bh,TRUE);MoveWindow(GetDlgItem(h,IDCANCEL),r.right-bw-12,r.bottom-bh-12,bw,bh,TRUE);}
INT_PTR CALLBACK ConfirmDialog(HWND h,UINT m,WPARAM wp,LPARAM lp){
    auto state=reinterpret_cast<ConfirmState*>(GetWindowLongPtrW(h,DWLP_USER));
    if(m==WM_INITDIALOG){ApplyUiFont(h);state=reinterpret_cast<ConfirmState*>(lp);SetWindowLongPtrW(h,DWLP_USER,lp);confirmation=h;SetWindowTextW(h,L"画像から対応表への反映を確認");auto add=[&](const wchar_t* type,const wchar_t* text,int id,DWORD style){auto c=CreateWindowExW(id==2462?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),TRUE);return c;};
        add(L"STATIC",L"画像の名前で、既存の手動登録名も上書きします。\r\n対応表のバックアップは作成しません。画像は変更しません。",2460,0);add(L"EDIT",PhotoNamePlanSummary(*state->plan).c_str(),2461,WS_TABSTOP|ES_READONLY|ES_MULTILINE|WS_VSCROLL);
        auto list=add(WC_LISTVIEWW,L"",2462,LVS_REPORT|LVS_SHOWSELALWAYS|WS_TABSTOP);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);ListView_SetBkColor(list,ThemeBackground());ListView_SetTextBkColor(list,ThemeBackground());ListView_SetTextColor(list,ThemeForeground());const wchar_t* columns[]{L"ID",L"現在の表示名",L"画像の名前（保存後）"};for(int i=0;i<3;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(columns[i]);c.cx=200;ListView_InsertColumn(list,i,&c);}int index=0;for(auto& change:state->plan->changes){LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=const_cast<wchar_t*>(change.id.c_str());ListView_InsertItem(list,&item);ListView_SetItemText(list,index,1,const_cast<wchar_t*>(change.before.empty()?L"（未登録）":change.before.c_str()));ListView_SetItemText(list,index,2,const_cast<wchar_t*>(change.after.c_str()));++index;}
        add(L"BUTTON",L"保存する",IDOK,BS_OWNERDRAW|WS_TABSTOP);add(L"BUTTON",L"キャンセル",IDCANCEL,BS_OWNERDRAW|WS_TABSTOP);RECT size{0,0,840,500};AdjustWindowRectEx(&size,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));SetWindowPos(h,nullptr,0,0,size.right-size.left,size.bottom-size.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ArrangeConfirmation(h);SetFocus(GetDlgItem(h,IDCANCEL));SendMessageW(h,DM_SETDEFID,IDCANCEL,0);if(state->inspect)SetTimer(h,1,100,nullptr);return FALSE;
    }
    if(!state)return FALSE;
    if(m==WM_TIMER&&wp==1){KillTimer(h,1);try{state->inspect(h);}catch(...){state->failure=std::current_exception();}if(IsWindow(h))EndDialog(h,IDCANCEL);return TRUE;}
    if(m==WM_COMMAND&&HIWORD(wp)==BN_CLICKED&&(LOWORD(wp)==IDOK||LOWORD(wp)==IDCANCEL)){EndDialog(h,LOWORD(wp));return TRUE;}
    if(m==WM_CLOSE){EndDialog(h,IDCANCEL);return TRUE;}
    if(m==WM_SIZE){ArrangeConfirmation(h);return TRUE;}
    if(m==WM_GETMINMAXINFO){RECT size{0,0,650,340};AdjustWindowRectEx(&size,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={size.right-size.left,size.bottom-size.top};return TRUE;}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT)return ThemeControlColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp));
    if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(wp),&r,ThemeBrush());return TRUE;}
    if(m==WM_DRAWITEM){PaintButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;}return FALSE;
}
}
void SetMdbApplyHooks(const MdbApplyHooks& value){hooks=value;}
bool ShowMdbApplyConfirmation(const PhotoNamePlan& plan,const std::function<void(HWND)>& inspect){ConfirmState state{&plan,inspect};struct Template{DLGTEMPLATE d;WORD menu,cls,title;}t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN|DS_MODALFRAME|DS_CENTER,0,0,0,0,440,280},0,0,0};auto answer=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.d,app.window,ConfirmDialog,reinterpret_cast<LPARAM>(&state));confirmation=nullptr;if(state.failure)std::rethrow_exception(state.failure);if(answer==-1)throw std::runtime_error("confirmation dialog unavailable");return answer==IDOK;}
void CreateMdbApplyUi(){
    WNDCLASSW type{};type.lpfnWndProc=PaneProc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"KuniMdbPage";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&type);
    pane=CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|WS_VSCROLL,0,0,1,1,app.side,reinterpret_cast<HMENU>(2454),type.hInstance,nullptr);
    auto add=[&](const wchar_t* cls,const wchar_t* text,int id,DWORD style){auto c=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,pane,reinterpret_cast<HMENU>(INT_PTR(id)),type.hInstance,nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),TRUE);};
    add(L"STATIC",L"画像のメタデータをMDBに反映する",Description,0);add(L"STATIC",L"選択画像のIDと名前の対応を保存します。\r\n画像は変更しません。上書き前に内容を確認します。",Note,0);add(L"BUTTON",L"実行",Run,WS_TABSTOP|BS_OWNERDRAW);add(L"EDIT",L"",Output,ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP);RefreshMdbApplyUi();
}
void LayoutMdbApplyUi(RECT page,bool visible){if(!pane)return;MoveWindow(pane,page.left,page.top,std::max(1L,page.right-page.left),std::max(1L,page.bottom-page.top),TRUE);ShowWindow(pane,visible&&app.mdbEnabled?SW_SHOW:SW_HIDE);if(visible)SetWindowPos(pane,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);ArrangePane();}
bool MdbApplyBusy(){return pending.active;}
bool MdbApplyCurrent(const Job& job){return !app.stopping&&job.applyGeneration==token&&job.generation==app.currentGeneration&&job.selection==app.currentSelection;}
void CancelMdbApply(const std::wstring& reason){if(!pending.active)return;pending.active=false;++token;{std::lock_guard<std::mutex> lock(app.mutex);app.jobs.erase(std::remove_if(app.jobs.begin(),app.jobs.end(),[](auto& j){return j.kind==14;}),app.jobs.end());}if(!reason.empty())message=reason;if(confirmation)EndDialog(confirmation,IDCANCEL);app.wake.notify_all();}
void RefreshMdbApplyUi(){
    if(!app.metadataTabs)return;int count=TabCtrl_GetItemCount(app.metadataTabs);if(app.mdbEnabled&&count==3){TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<wchar_t*>(L"MDB");TabCtrl_InsertItem(app.metadataTabs,3,&tab);}else if(!app.mdbEnabled&&count>3){if(TabCtrl_GetCurSel(app.metadataTabs)==3)TabCtrl_SetCurSel(app.metadataTabs,0);TabCtrl_DeleteItem(app.metadataTabs,3);}
    if(pending.active&&!SameContext())CancelMdbApply(L"画像選択・読込み状態またはMDB設定が変わったため中止しました。");
    if(!pane)return;auto reason=Reason();EnableWindow(GetDlgItem(pane,Run),!pending.active&&reason.empty());auto text=pending.active?L"メタデータを読み込み、対応を確認しています…\r\n"+fs::path(pending.path).filename().wstring():!reason.empty()?reason:message.empty()?L"［実行］すると保存内容を確認します。":message;if(Text(GetDlgItem(pane,Output))!=text)SetDlgItemTextW(pane,Output,text.c_str());
}
void StartMdbApply(){
    FlushSelectionUpdate();RefreshMdbApplyUi();if(pending.active||!Reason().empty())return;
    try{Job job{14,app.generation,app.selection,app.selected,app.files[app.selected]};job.sourceKey=MdbRevision(job.path);if(job.sourceKey.empty())throw std::runtime_error("image revision unavailable");job.applyGeneration=++token;job.applyDelayMs=hooks.delayMs;pending={true,job.applyGeneration,job.generation,job.selection,job.path,job.sourceKey};message.clear();Queue(std::move(job));RefreshMdbApplyUi();}catch(...){message=L"画像の変更情報を取得できません。再読み込みしてから実行してください。";RefreshMdbApplyUi();}
}
void ReadMdbApply(const Job& job,Result& result){
    for(unsigned elapsed=0;elapsed<job.applyDelayMs&&MdbApplyCurrent(job);elapsed+=10)Sleep(10);if(!MdbApplyCurrent(job))return;
    if(job.sourceKey!=MdbRevision(job.path))throw std::runtime_error("image changed before extraction");auto cached=app.mdb.Read(job.path,-1,false);if(cached.failed)throw std::runtime_error("metadata read failed");result.mdbRecord=PhotoNameInput(ReadPhotoSummary(job.path));result.mdbRecord.path=job.path;result.mdbRecord.revision=job.sourceKey;if(job.sourceKey!=MdbRevision(job.path))throw std::runtime_error("image changed during extraction");
}
bool HandleMdbApplyResult(Result& result){
    if(result.job.kind!=14)return false;auto& job=result.job;if(!pending.active||job.applyGeneration!=pending.ticket)return true;
    if(!MdbApplyCurrent(job)||!SameContext()){CancelMdbApply(L"選択が変更されたため中止しました。");RefreshMdbApplyUi();return true;}
    try{if(!result.error.empty())throw std::runtime_error(winrt::to_string(result.error));if(job.sourceKey!=MdbRevision(job.path))throw std::runtime_error("image changed; reload and retry");app.idNames.Load(app.settings.parent_path()/L"IdDisplayNames.json");if(!app.idNames.Error().empty())throw std::runtime_error("name table is protected after a read failure");auto plan=BuildPhotoNamePlan(result.mdbRecord,app.idNames.Snapshot());
        if(plan.changes.empty()){message=L"対応表の変更は不要です。\r\n"+PhotoNamePlanSummary(plan);pending.active=false;RefreshMdbApplyUi();return true;}
        bool accepted=hooks.confirm?hooks.confirm(plan):ShowMdbApplyConfirmation(plan,hooks.inspectConfirmation);
        if(!accepted){if(pending.active)message=L"キャンセルしました。対応表は変更していません。\r\n"+PhotoNamePlanSummary(plan);pending.active=false;RefreshMdbApplyUi();return true;}
        if(!SameContext()||!MdbApplyCurrent(job)||job.sourceKey!=MdbRevision(job.path))throw std::runtime_error("selection, MDB state or image changed before save");
        app.idNames.Save(plan.after,&plan.before);RefreshMdbIdLabels();message=L"対応表を保存しました（バックアップなし）。\r\n"+PhotoNamePlanSummary(plan);pending.active=false;
    }catch(const std::exception& e){pending.active=false;message=L"保存しませんでした。既存の対応表は保全しています。\r\n"+std::wstring(winrt::to_hstring(e.what()));}catch(...){pending.active=false;message=L"処理できませんでした。画像と対応表を確認し、再実行してください。";}
    RefreshMdbApplyUi();return true;
}
