#include "ViewerRuntime.h"
#include "BackupManager.h"
namespace {
struct BackupDialogState {std::vector<BackupCandidate> candidates;std::vector<std::wstring> selected;HWND list{};bool test=false, testPassed=false,bulk=false;};
INT_PTR CALLBACK BackupDialog(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto state=reinterpret_cast<BackupDialogState*>(GetWindowLongPtrW(window,DWLP_USER));
    if(message==WM_INITDIALOG){state=reinterpret_cast<BackupDialogState*>(lp);SetWindowLongPtrW(window,DWLP_USER,lp);state->bulk=true;SetWindowTextW(window,L"バックアップ削除 — チェックされた画像のみ");
        auto child=[&](const wchar_t* type,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id){auto control=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return control;};
        child(L"STATIC",L"チェックを外した画像は削除しません。管理対象外は初期OFFです。",0,10,8,670,24,0);
        state->list=child(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SHOWSELALWAYS|WS_BORDER|WS_TABSTOP,10,36,670,290,100);
        ListView_SetExtendedListViewStyle(state->list,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        const wchar_t* headers[]{L"ファイル名",L"日時（更新）",L"容量",L"管理"};int widths[]{350,160,85,60};for(int i=0;i<4;++i){LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(headers[i]);c.cx=widths[i];ListView_InsertColumn(state->list,i,&c);}
        for(size_t i=0;i<state->candidates.size();++i){auto& e=state->candidates[i];auto name=fs::path(e.path).filename().wstring();LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(i);item.pszText=name.data();ListView_InsertItem(state->list,&item);FILETIME local{};SYSTEMTIME t{};FileTimeToLocalFileTime(&e.modified,&local);FileTimeToSystemTime(&local,&t);wchar_t date[40]{};swprintf_s(date,L"%04u-%02u-%02u %02u:%02u:%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);ListView_SetItemText(state->list,static_cast<int>(i),1,date);auto size=std::to_wstring(e.bytes/1000)+L" KB";ListView_SetItemText(state->list,static_cast<int>(i),2,size.data());ListView_SetItemText(state->list,static_cast<int>(i),3,const_cast<wchar_t*>(e.managed?L"対象":L"対象外"));ListView_SetCheckState(state->list,static_cast<int>(i),e.managed);}
        child(L"BUTTON",L"すべて選択",WS_TABSTOP,10,338,110,28,101);child(L"BUTTON",L"すべて解除",WS_TABSTOP,130,338,110,28,102);child(L"BUTTON",L"ごみ箱へ削除",WS_TABSTOP,440,338,130,28,IDOK);child(L"BUTTON",L"キャンセル",WS_TABSTOP,580,338,100,28,IDCANCEL);state->bulk=false;
    }
    if(message==WM_INITDIALOG&&state->test)PostMessageW(window,WM_APP+31,0,0);
    if(message==WM_APP+31&&state&&state->test){bool defaults=ListView_GetCheckState(state->list,0)&&!ListView_GetCheckState(state->list,1);SendMessageW(window,WM_COMMAND,102,0);bool zero=!IsWindowEnabled(GetDlgItem(window,IDOK));SendMessageW(window,WM_COMMAND,101,0);ListView_SetCheckState(state->list,0,FALSE);state->selected.clear();for(size_t i=0;i<state->candidates.size();++i)if(ListView_GetCheckState(state->list,static_cast<int>(i)))state->selected.push_back(state->candidates[i].path);state->testPassed=defaults&&zero&&state->selected.size()==1&&state->selected[0]==state->candidates[1].path;EndDialog(window,IDCANCEL);return TRUE;}
    if(state&&(message==WM_INITDIALOG||message==WM_NOTIFY)){if(state->bulk)return TRUE;int checked=0;for(int i=0;i<ListView_GetItemCount(state->list);++i)if(ListView_GetCheckState(state->list,i))++checked;EnableWindow(GetDlgItem(window,IDOK),checked>0);return TRUE;}
    if(message==WM_COMMAND&&state){int id=LOWORD(wp);if(id==101||id==102){state->bulk=true;for(int i=0;i<ListView_GetItemCount(state->list);++i)ListView_SetCheckState(state->list,i,id==101);state->bulk=false;SendMessageW(window,WM_NOTIFY,0,0);return TRUE;}if(id==IDOK){std::wstring text;state->selected.clear();for(size_t i=0;i<state->candidates.size();++i)if(ListView_GetCheckState(state->list,static_cast<int>(i))){state->selected.push_back(state->candidates[i].path);text+=fs::path(state->candidates[i].path).filename().wstring()+L"\r\n";}
            if(state->selected.empty())return TRUE;TASKDIALOGCONFIG c{sizeof(c)};c.hwndParent=window;c.pszWindowTitle=L"削除の確認";auto title=std::to_wstring(state->selected.size())+L"件をごみ箱へ移動しますか？";c.pszMainInstruction=title.c_str();c.pszExpandedInformation=text.c_str();c.pszContent=L"チェックを外した画像は残します。";c.dwCommonButtons=TDCBF_YES_BUTTON|TDCBF_NO_BUTTON;c.nDefaultButton=IDNO;int result=IDNO;TaskDialogIndirect(&c,&result,nullptr,nullptr);if(result==IDYES)EndDialog(window,IDOK);return TRUE;}
        if(id==IDCANCEL){EndDialog(window,IDCANCEL);return TRUE;}}
    if(message==WM_CLOSE){EndDialog(window,IDCANCEL);return TRUE;}return FALSE;
}
}
void ShowBackupDeletion(){if(app.saving||app.backupBusy||app.folder.empty())return;CancelSearch();app.backupBusy=true;UpdateEditorControls();Job job{7,app.generation};job.path=app.folder;job.destination=(app.settings.parent_path()/L"backups.json").wstring();Queue(std::move(job));Counts(L"バックアップ一覧を確認しています…");}
void ChooseBackupDeletion(const std::vector<BackupCandidate>& candidates){if(app.saving||candidates.empty()){Counts(L"削除候補のバックアップはありません。");return;}BackupDialogState state;state.candidates=candidates;
    struct DialogTemplate {DLGTEMPLATE dialog;WORD menu,windowClass,title;} dialog{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME,0,0,0,0,470,252},0,0,0};
    if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&dialog.dialog,app.window,BackupDialog,reinterpret_cast<LPARAM>(&state))!=IDOK)return;
    SubmitBackupDeletion(candidates, state.selected);
}

void SubmitBackupDeletion(const std::vector<BackupCandidate>& candidates, const std::vector<std::wstring>& selected) {
    if (selected.empty() || !GuardTransition([candidates, selected] { SubmitBackupDeletion(candidates, selected); })) return;
    CancelSearch();
    auto preserve = app.selected >= 0 ? app.files[app.selected] : L"";
    if (std::any_of(selected.begin(), selected.end(), [&](auto& path) { return Same(path, preserve); })) {
        app.transitionBypass = true;
        ListView_SetItemState(app.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        app.transitionBypass = false;
        Select(-1); preserve.clear();
        std::lock_guard<std::mutex> lock(app.mutex);
        app.jobs.erase(std::remove_if(app.jobs.begin(), app.jobs.end(), [](auto& job) { return job.kind == 2; }), app.jobs.end());
    }
    Job job{8,app.generation}; job.path=app.folder; job.files=selected; job.preserve=preserve; job.backups=candidates;
    job.destination=(app.settings.parent_path()/L"backups.json").wstring(); app.backupBusy=true; UpdateEditorControls();
    Queue(std::move(job)); Counts(L"バックアップを削除しています…");
}

bool TestBackupCheckboxes(){BackupDialogState state;state.test=true;BackupCandidate a,b;a.path=L"AB_managed.png";a.managed=true;b.path=L"AB_unmanaged.png";state.candidates={a,b};struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;} t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME,0,0,0,0,470,252},0,0,0};DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,app.window,BackupDialog,reinterpret_cast<LPARAM>(&state));return state.testPassed;}
