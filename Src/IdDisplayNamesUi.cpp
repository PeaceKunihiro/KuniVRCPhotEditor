#include "ViewerRuntime.h"
#include "IdDisplayNames.h"
#include <uxtheme.h>
namespace {
constexpr int Table=2400,Import=2401,Export=2402,Notice=2403;
struct Editor {IdNameTable names;std::vector<std::wstring> rows;IdNameUiHooks hooks;bool invalidEdit=false,saved=false;std::exception_ptr failure;};
void Status(HWND h,const std::wstring& text){SetDlgItemTextW(h,Notice,text.c_str());}
void Layout(HWND h){RECT r{};GetClientRect(h,&r);auto list=GetDlgItem(h,Table);if(!list)return;MoveWindow(list,12,12,r.right-24,std::max(60L,r.bottom-132),TRUE);int width=std::max(550L,r.right-46);ListView_SetColumnWidth(list,0,std::max(140,width-400));ListView_SetColumnWidth(list,1,300);ListView_SetColumnWidth(list,2,100);MoveWindow(GetDlgItem(h,Notice),12,r.bottom-108,r.right-24,52,TRUE);for(auto pair:std::array<std::pair<int,int>,4>{{{IDOK,0},{IDCANCEL,1},{Import,2},{Export,3}}})MoveWindow(GetDlgItem(h,pair.first),12+pair.second*112,r.bottom-42,104,28,TRUE);}
void Fill(HWND h,Editor& state){
    std::set<std::wstring> ids;for(auto& entry:state.names)ids.insert(entry.first);for(int axis:{1,3,4})for(auto& raw:app.mdb.Values(axis)){auto id=CanonicalDisplayId(raw);if(!id.empty())ids.insert(id);}state.rows.assign(ids.begin(),ids.end());auto list=GetDlgItem(h,Table);ListView_DeleteAllItems(list);
    for(int i=0;i<int(state.rows.size());++i){auto& id=state.rows[i];auto found=state.names.find(id);auto name=found==state.names.end()?L"":found->second;LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=const_cast<wchar_t*>(name.c_str());ListView_InsertItem(list,&item);ListView_SetItemText(list,i,1,const_cast<wchar_t*>(id.c_str()));ListView_SetItemText(list,i,2,const_cast<wchar_t*>(L"↗"));}
}
bool CommitEdit(HWND h,Editor& state){auto list=GetDlgItem(h,Table);if(!ListView_GetEditControl(list))return true;state.invalidEdit=false;SetFocus(list);return !state.invalidEdit;}
std::wstring Pick(HWND owner,bool save){
    ComPtr<IFileDialog> dialog;HRESULT hr=save?CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)):CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));if(FAILED(hr))throw std::runtime_error("file dialog unavailable");DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|(save?FOS_OVERWRITEPROMPT:FOS_FILEMUSTEXIST));COMDLG_FILTERSPEC types[]{{L"JSON (*.json)",L"*.json"}};dialog->SetFileTypes(1,types);dialog->SetDefaultExtension(L"json");dialog->SetTitle(save?L"対応表をExport":L"対応表をImport");if(save)dialog->SetFileName(L"IdDisplayNames-export.json");hr=dialog->Show(owner);if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return L"";if(FAILED(hr))throw std::runtime_error("file selection failed");ComPtr<IShellItem> item;PWSTR path=nullptr;if(FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&path)))throw std::runtime_error("file path unavailable");std::wstring result=path;CoTaskMemFree(path);return result;
}
void OpenRow(HWND h,Editor& state,int row){if(row<0||row>=int(state.rows.size()))return;auto url=DisplayIdUrl(state.rows[row]);if(url.empty())return;try{if(state.hooks.openUrl)state.hooks.openUrl(url);else if(INT_PTR(ShellExecuteW(h,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("browser launch failed");}catch(...){Status(h,L"ブラウザーを開けませんでした。");}}
LRESULT CALLBACK NameEditKeys(HWND edit,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref){
    if(m==WM_GETDLGCODE&&(wp==VK_RETURN||wp==VK_ESCAPE))return DLGC_WANTALLKEYS;
    if(m==WM_KEYDOWN&&wp==VK_RETURN){SetFocus(reinterpret_cast<HWND>(ref));return 0;}
    if(m==WM_KEYDOWN&&wp==VK_ESCAPE){SendMessageW(reinterpret_cast<HWND>(ref),LVM_CANCELEDITLABEL,0,0);return 0;}
    if(m==WM_NCDESTROY)RemoveWindowSubclass(edit,NameEditKeys,id);return DefSubclassProc(edit,m,wp,lp);
}
LRESULT CALLBACK ListKeys(HWND list,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(m==WM_GETDLGCODE&&(wp==VK_RETURN||wp==VK_F2))return DLGC_WANTALLKEYS;
    if(m==WM_KEYDOWN&&(wp==VK_RETURN||wp==VK_F2)){auto h=GetParent(list);auto state=reinterpret_cast<Editor*>(GetWindowLongPtrW(h,DWLP_USER));auto row=ListView_GetNextItem(list,-1,LVNI_FOCUSED);if(wp==VK_F2&&row>=0)ListView_EditLabel(list,row);else if(state)OpenRow(h,*state,row);return 0;}
    if(m==WM_NCDESTROY)RemoveWindowSubclass(list,ListKeys,id);return DefSubclassProc(list,m,wp,lp);
}
INT_PTR CALLBACK Dialog(HWND h,UINT m,WPARAM wp,LPARAM lp){
    auto state=reinterpret_cast<Editor*>(GetWindowLongPtrW(h,DWLP_USER));
    if(m==WM_INITDIALOG){state=reinterpret_cast<Editor*>(lp);SetWindowLongPtrW(h,DWLP_USER,lp);SetWindowTextW(h,L"ID・表示名の編集");auto add=[&](const wchar_t* type,const wchar_t* text,int id,DWORD style){auto c=CreateWindowExW(id==Table?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return c;};
        auto list=add(WC_LISTVIEWW,L"",Table,WS_TABSTOP|LVS_REPORT|LVS_EDITLABELS|LVS_SINGLESEL|LVS_SHOWSELALWAYS);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);ListView_SetBkColor(list,ThemeBackground());ListView_SetTextBkColor(list,ThemeBackground());ListView_SetTextColor(list,ThemeForeground());SetWindowTheme(list,app.darkTheme?L"DarkMode_Explorer":L"Explorer",nullptr);SetWindowSubclass(list,ListKeys,1,0);
        const wchar_t* columns[]{L"表示名（クリック／F2で入力）",L"ID",L"ショートカット"};for(int i=0;i<3;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t*>(columns[i]);column.cx=250;ListView_InsertColumn(list,i,&column);}Fill(h,*state);
        add(L"BUTTON",L"保存",IDOK,WS_TABSTOP|BS_OWNERDRAW);add(L"BUTTON",L"キャンセル",IDCANCEL,WS_TABSTOP|BS_OWNERDRAW);add(L"BUTTON",L"Import",Import,WS_TABSTOP|BS_OWNERDRAW);add(L"BUTTON",L"Export",Export,WS_TABSTOP|BS_OWNERDRAW);add(L"STATIC",L"",Notice,0);
        auto message=app.idNames.Error();if(message.empty())message=L"写真のメタデータは変更しません。対応表はMDBをOFFにしても残ります。\r\nExportは未保存の入力も含みます。Enterでページを開き、表示名は空欄で解除します。";else EnableWindow(GetDlgItem(h,IDOK),FALSE);Status(h,message);
        RECT size{0,0,850,500};AdjustWindowRectEx(&size,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));SetWindowPos(h,nullptr,0,0,size.right-size.left,size.bottom-size.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);Layout(h);if(state->hooks.inspect)SetTimer(h,1,100,nullptr);return TRUE;
    }
    if(!state)return FALSE;
    if(m==WM_TIMER&&wp==1){KillTimer(h,1);try{state->hooks.inspect(h);}catch(...){state->failure=std::current_exception();}if(IsWindow(h))EndDialog(h,IDCANCEL);return TRUE;}
    if(m==WM_NOTIFY){auto header=reinterpret_cast<NMHDR*>(lp);if(header->idFrom==Table){auto list=GetDlgItem(h,Table);
        if(header->code==LVN_BEGINLABELEDITW){auto edit=ListView_GetEditControl(list);if(edit){SendMessageW(edit,EM_SETLIMITTEXT,1024,0);SetWindowSubclass(edit,NameEditKeys,1,reinterpret_cast<DWORD_PTR>(list));}SetWindowLongPtrW(h,DWLP_MSGRESULT,FALSE);return TRUE;}
        if(header->code==LVN_ENDLABELEDITW){auto info=reinterpret_cast<NMLVDISPINFOW*>(lp);if(info->item.pszText&&info->item.iItem>=0&&info->item.iItem<int(state->rows.size()))try{auto name=ValidateDisplayName(info->item.pszText);auto id=state->rows[info->item.iItem];if(name.empty())state->names.erase(id);else state->names[id]=name;ListView_SetItemText(list,info->item.iItem,0,const_cast<wchar_t*>(name.c_str()));state->invalidEdit=false;Status(h,L"未保存の変更があります。［保存］で確定します。");}catch(...){state->invalidEdit=true;Status(h,L"表示名は1024文字以内の1行で入力してください。制御文字や不正な文字は保存できません。");}SetWindowLongPtrW(h,DWLP_MSGRESULT,FALSE);return TRUE;}
        if(header->code==NM_CLICK){auto click=reinterpret_cast<NMITEMACTIVATE*>(lp);if(click->iItem>=0){if(click->iSubItem==2)OpenRow(h,*state,click->iItem);else if(click->iSubItem==0)ListView_EditLabel(list,click->iItem);}return TRUE;}
        if(header->code==NM_CUSTOMDRAW){auto draw=reinterpret_cast<NMLVCUSTOMDRAW*>(lp);LRESULT result=CDRF_DODEFAULT;if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)result=CDRF_NOTIFYITEMDRAW;else if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)result=CDRF_NOTIFYSUBITEMDRAW;else if(draw->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM)){draw->clrText=ThemeForeground();draw->clrTextBk=ThemeBackground();if(draw->iSubItem==2){RECT r{};ListView_GetSubItemRect(list,int(draw->nmcd.dwItemSpec),2,LVIR_BOUNDS,&r);FillRect(draw->nmcd.hdc,&r,ThemeBrush());InflateRect(&r,-12,-2);auto brush=CreateSolidBrush(ThemeForeground());FrameRect(draw->nmcd.hdc,&r,brush);DeleteObject(brush);SetTextColor(draw->nmcd.hdc,ThemeForeground());SetBkMode(draw->nmcd.hdc,TRANSPARENT);DrawTextW(draw->nmcd.hdc,L"↗",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);result=CDRF_SKIPDEFAULT;}}SetWindowLongPtrW(h,DWLP_MSGRESULT,result);return TRUE;}
    }}
    if(m==WM_COMMAND){if(HIWORD(wp)!=BN_CLICKED)return FALSE;auto id=LOWORD(wp);if(id==IDCANCEL){EndDialog(h,IDCANCEL);return TRUE;}if(id!=IDOK&&id!=Import&&id!=Export)return FALSE;if(!CommitEdit(h,*state))return TRUE;
        try{if(id==IDOK){app.idNames.Save(state->names);state->saved=true;EndDialog(h,IDOK);return TRUE;}
            if(id==Import){auto path=state->hooks.importPath?state->hooks.importPath():Pick(h,false);if(path.empty())return TRUE;auto parsed=ParseIdNames(ReadIdNamesFile(path));auto merged=MergeIdNames(state->names,parsed);Fill(h,*state);Status(h,L"Import: 追加 "+std::to_wstring(merged.added)+L"／既存維持 "+std::to_wstring(merged.kept)+L"／除外 "+std::to_wstring(merged.skipped)+L"。［保存］するまでは未確定です。");}
            if(id==Export){SerializeIdNames(state->names);size_t count=0;bool personal=false;for(auto& pair:state->names)if(!ValidateDisplayName(pair.second).empty()){++count;personal|=DisplayIdType(pair.first)==L"person";}bool consent;if(state->hooks.confirmExport)consent=state->hooks.confirmExport(count,personal);else {auto message=L"現在の編集表から "+std::to_wstring(count)+L"件を書き出します。未保存の入力も含みます。";if(personal)message+=L"\r\nユーザーIDと入力された表示名が含まれます。";consent=MessageBoxW(h,message.c_str(),L"対応表のExport",MB_OKCANCEL|MB_ICONINFORMATION)==IDOK;}if(!consent)return TRUE;auto path=state->hooks.exportPath?state->hooks.exportPath():Pick(h,true);if(path.empty())return TRUE;if(SameFolderAddress(path,(app.settings.parent_path()/L"IdDisplayNames.json").wstring()))throw std::runtime_error("use Save for the internal name table");ExportIdNames(path,state->names);Status(h,L"Export: "+std::to_wstring(count)+L"件。ローカル対応表の［保存］は行っていません。");}
        }catch(const std::exception& e){Status(h,L"処理できませんでした。既存の対応表は変更していません。\r\n"+std::wstring(winrt::to_hstring(e.what())));}catch(...){Status(h,L"処理できませんでした。JSON・UTF-8・形式版・IDを確認してください。既存の対応表は変更していません。");}return TRUE;
    }
    if(m==WM_SIZE){Layout(h);return TRUE;}
    if(m==WM_GETMINMAXINFO){RECT size{0,0,650,300};AdjustWindowRectEx(&size,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={size.right-size.left,size.bottom-size.top};return TRUE;}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT){auto dc=reinterpret_cast<HDC>(wp);SetTextColor(dc,ThemeForeground());SetBkColor(dc,ThemeBackground());return reinterpret_cast<INT_PTR>(ThemeBrush());}
    if(m==WM_ERASEBKGND){RECT r{};GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(wp),&r,ThemeBrush());return TRUE;}
    if(m==WM_DRAWITEM){auto draw=reinterpret_cast<DRAWITEMSTRUCT*>(lp);FillRect(draw->hDC,&draw->rcItem,ThemeBrush());auto brush=CreateSolidBrush(ThemeForeground());FrameRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);SetTextColor(draw->hDC,ThemeForeground());SetBkMode(draw->hDC,TRANSPARENT);wchar_t text[32]{};GetDlgItemTextW(h,draw->CtlID,text,32);DrawTextW(draw->hDC,text,-1,&draw->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(draw->itemState&ODS_FOCUS){auto r=draw->rcItem;InflateRect(&r,-4,-4);DrawFocusRect(draw->hDC,&r);}return TRUE;}
    if(m==WM_CLOSE){EndDialog(h,IDCANCEL);return TRUE;}return FALSE;
}
}
void ShowIdNamesDialog(HWND owner,const IdNameUiHooks& hooks){
    if(!app.mdbEnabled||app.saving||app.backupBusy)return;app.idNames.Load(app.settings.parent_path()/L"IdDisplayNames.json");RefreshMdbIdLabels();Editor state;state.names=app.idNames.Snapshot();state.hooks=hooks;
    struct Template{DLGTEMPLATE d;WORD menu,cls,title;}t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN|DS_MODALFRAME|DS_CENTER,0,0,0,0,420,280},0,0,0};auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.d,owner,Dialog,reinterpret_cast<LPARAM>(&state));
    if(state.saved)RefreshMdbIdLabels();if(state.failure)std::rethrow_exception(state.failure);if(result==-1&&!hooks.inspect)MessageBoxW(owner,L"対応表の編集画面を表示できません。",L"対応表",MB_OK|MB_ICONERROR);
}
