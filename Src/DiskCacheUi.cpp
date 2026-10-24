#include "ViewerRuntime.h"
namespace {
constexpr int Enable=2100,Capacity=2101,Minutes=2102,Visits=2103,Usage=2104,Delete=2105,Priority=2106,Folders=2110;
struct DialogState {std::function<void(HWND)> inspect;};
void RefreshUsage(HWND h){auto s=app.disk.GetStatus();auto text=L"使用量: "+std::to_wstring(s.bytes/1024)+L" KiB / "+std::to_wstring(s.entries)+L"枚"+(s.pending?L"（確認・整理中）":L"");if(!s.error.empty())text+=L"\r\n"+s.error;SetDlgItemTextW(h,Usage,text.c_str());std::vector<DiskThumbnailCache::FolderStatus> folders;if(app.disk.GetFolders(folders)){std::wstring listing;for(auto& folder:folders){listing+=(folder.folder.empty()?L"元フォルダー不明":folder.folder)+L"\r\n  "+std::to_wstring(folder.entries)+L"枚 / "+std::to_wstring((folder.bytes+1023)/1024)+L" KiB\r\n";}if(listing.empty())listing=L"保持中のサムネイルはありません。";if(Text(GetDlgItem(h,Folders))!=listing)SetDlgItemTextW(h,Folders,listing.c_str());}}
INT_PTR CALLBACK DiskDialog(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_INITDIALOG){ApplyUiFont(h);SetWindowLongPtrW(h,DWLP_USER,lp);SetWindowTextW(h,L"ディスクキャッシュ");
        auto add=[&](const wchar_t* type,const std::wstring& text,DWORD style,int x,int y,int w,int height,int id){auto c=CreateWindowExW(0,type,text.c_str(),WS_CHILD|WS_VISIBLE|style,x,y,w,height,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),TRUE);return c;};
        add(L"BUTTON",L"ディスクキャッシュを有効にする",BS_AUTOCHECKBOX|WS_TABSTOP,12,12,400,26,Enable);CheckDlgButton(h,Enable,app.diskCache?BST_CHECKED:BST_UNCHECKED);
        add(L"BUTTON",L"ブックマーク優先",BS_AUTOCHECKBOX|WS_TABSTOP,12,44,400,26,Priority);CheckDlgButton(h,Priority,app.bookmarkCache?BST_CHECKED:BST_UNCHECKED);
        add(L"STATIC",L"容量上限",0,12,50,110,24,0);auto combo=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,130,47,180,180,Capacity);
        int i=0;for(auto size:{128,256,512,1024,2048}){auto text=size<1024?std::to_wstring(size)+L" MiB":std::to_wstring(size/1024)+L" GiB";SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));SendMessageW(combo,CB_SETITEMDATA,i,size);if(size==app.diskCacheMiB)SendMessageW(combo,CB_SETCURSEL,i,0);++i;}
        add(L"STATIC",L"判定期間（分、1～1440）",0,12,86,230,24,0);add(L"EDIT",std::to_wstring(app.diskCacheMinutes),ES_NUMBER|WS_BORDER|WS_TABSTOP,260,82,90,26,Minutes);
        add(L"STATIC",L"正常ロード回数（2～100）",0,12,120,230,24,0);add(L"EDIT",std::to_wstring(app.diskCacheVisits),ES_NUMBER|WS_BORDER|WS_TABSTOP,260,116,90,26,Visits);
        add(L"STATIC",L"同じフォルダーの明示ロードを数えます。画像選択・スクロールは除外。",0,12,156,585,26,0);
        add(L"EDIT",L"128MiB   約682枚       最小設定に推奨\r\n256MiB   約1,365枚     小規模フォルダー向け\r\n512MiB   約2,730枚     初期値に推奨\r\n1GiB     約5,461枚     大規模フォルダー向け\r\n2GiB     約10,922枚    大量の写真向け",ES_MULTILINE|ES_READONLY|WS_BORDER,12,186,585,124,2107);
        add(L"STATIC",L"1枚約192KiBの概算。寸法・管理情報によって枚数は変わります。",0,12,318,585,24,0);
        auto location=(app.settings.parent_path()/L"ThumbnailDiskCache").wstring();add(L"EDIT",L"保存先: "+location,ES_READONLY|ES_AUTOHSCROLL|WS_BORDER,12,348,585,26,0);
        add(L"STATIC",L"",0,12,382,585,46,Usage);add(L"BUTTON",L"キャッシュ削除…",WS_TABSTOP,12,444,150,28,Delete);
        add(L"BUTTON",L"適用",BS_DEFPUSHBUTTON|WS_TABSTOP,378,444,100,28,IDOK);add(L"BUTTON",L"キャンセル",WS_TABSTOP,488,444,110,28,IDCANCEL);
        for(HWND c=GetWindow(h,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)){if(GetDlgCtrlID(c)==Enable||GetDlgCtrlID(c)==Priority)continue;RECT r{};GetWindowRect(c,&r);MapWindowPoints(nullptr,h,reinterpret_cast<POINT*>(&r),2);SetWindowPos(c,nullptr,r.left,r.top+32,0,0,SWP_NOSIZE|SWP_NOZORDER);}
        add(L"STATIC",L"優先OFFでは期間・回数から対象を自動判定。OFF切替で既存分は削除しません。",0,12,70,585,22,2108);
        // Reserve another row for the explanation above the capacity controls.
        for(HWND c=GetWindow(h,GW_CHILD);c;c=GetWindow(c,GW_HWNDNEXT)){auto id=GetDlgCtrlID(c);if(id==Enable||id==Priority||id==2108)continue;RECT r{};GetWindowRect(c,&r);MapWindowPoints(nullptr,h,reinterpret_cast<POINT*>(&r),2);SetWindowPos(c,nullptr,r.left,r.top+28,0,0,SWP_NOSIZE|SWP_NOZORDER);}
        add(L"STATIC",L"キャッシュするのは画像のサムネイルのみです。\r\n元の写真データ全体を保存するものではありません。",0,12,494,585,40,2109);
        for(int id:{Delete,IDOK,IDCANCEL}){auto c=GetDlgItem(h,id);SetWindowPos(c,nullptr,id==Delete?12:id==IDOK?378:488,698,0,0,SWP_NOSIZE|SWP_NOZORDER);}
        add(L"STATIC",L"保持中の画像フォルダー（保存済みの枚数・容量）",0,12,546,585,24,2111);add(L"EDIT",L"確認中…",ES_MULTILINE|ES_READONLY|ES_AUTOHSCROLL|WS_HSCROLL|WS_VSCROLL|WS_BORDER|WS_TABSTOP,12,574,585,112,Folders);
        RECT size{0,0,610,738};AdjustWindowRectEx(&size,static_cast<DWORD>(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(h,GWL_EXSTYLE)));SetWindowPos(h,nullptr,0,0,size.right-size.left,size.bottom-size.top,SWP_NOMOVE|SWP_NOZORDER);ScaleUiDialog(h);RefreshUsage(h);SetTimer(h,1,500,nullptr);auto state=reinterpret_cast<DialogState*>(lp);if(state&&state->inspect)SetTimer(h,2,500,nullptr);return TRUE;
    }
    if(m==WM_TIMER){if(wp==2){KillTimer(h,2);auto state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(h,DWLP_USER));if(state&&state->inspect){RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);state->inspect(h);}EndDialog(h,IDCANCEL);}else RefreshUsage(h);return TRUE;}
    if(m==WM_COMMAND){auto id=LOWORD(wp);
        if(id==Priority&&HIWORD(wp)==BN_CLICKED){if(IsDlgButtonChecked(h,Priority)==BST_CHECKED)CheckDlgButton(h,Enable,BST_CHECKED);return TRUE;}
        if(id==Enable&&HIWORD(wp)==BN_CLICKED){if(IsDlgButtonChecked(h,Enable)!=BST_CHECKED)CheckDlgButton(h,Priority,BST_UNCHECKED);return TRUE;}
        if(id==IDOK){BOOL a=FALSE,b=FALSE;auto minutes=GetDlgItemInt(h,Minutes,&a,FALSE),visits=GetDlgItemInt(h,Visits,&b,FALSE);auto sel=SendDlgItemMessageW(h,Capacity,CB_GETCURSEL,0,0);
            if(!a||!b||minutes<1||minutes>1440||visits<2||visits>100||sel==CB_ERR){MessageBoxW(h,L"判定期間は1～1440分、回数は2～100を指定してください。",L"入力エラー",MB_OK|MB_ICONWARNING);return TRUE;}
            bool enabled=IsDlgButtonChecked(h,Enable)==BST_CHECKED,priority=enabled&&IsDlgButtonChecked(h,Priority)==BST_CHECKED;
            auto state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(h,DWLP_USER));
            if(priority!=app.bookmarkCache&&!(state&&state->inspect))MessageBoxW(h,priority?L"ディスクキャッシュを有効にし、ブックマーク配下の画像サムネイルを優先して保存します。容量上限内で古いキャッシュが整理されます。":L"ブックマーク優先をOFFにします。ディスクキャッシュが有効な場合、対象は利用頻度から自動判定します。保存済みキャッシュはこの切替では削除しません。以後の容量整理や明示削除の対象にはなります。",L"キャッシュ設定",MB_OK|MB_ICONINFORMATION);
            app.bookmarkCache=priority;app.diskCache=enabled;app.diskCacheMiB=static_cast<int>(SendDlgItemMessageW(h,Capacity,CB_GETITEMDATA,sel,0));app.diskCacheMinutes=minutes;app.diskCacheVisits=visits;ConfigureDiskCache();SaveSettings();EndDialog(h,IDOK);return TRUE;
        }
        if(id==Delete){if(MessageBoxW(h,L"保存したサムネイルキャッシュを削除しますか？\n元の写真は削除しません。",L"ディスクキャッシュ削除",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES){app.disk.Clear();RefreshUsage(h);}return TRUE;}
        if(id==IDCANCEL){EndDialog(h,IDCANCEL);return TRUE;}
    }
    if(m==WM_CLOSE){EndDialog(h,IDCANCEL);return TRUE;}return FALSE;
}
}
void ConfigureDiskCache(){
    if(!app.diskCache)app.bookmarkCache=false;
    ++app.diskGeneration;app.diskPrefetchCursor=app.diskPrefetchCount=0;
    app.disk.Configure(app.settings.parent_path()/L"ThumbnailDiskCache",{app.diskCache,size_t(app.diskCacheMiB)*1024*1024,app.diskCacheMinutes,app.diskCacheVisits,app.bookmarkCache,app.bookmarks});
    {std::lock_guard<std::mutex> lock(app.mutex);app.jobs.erase(std::remove_if(app.jobs.begin(),app.jobs.end(),[](auto& j){return j.diskPrefetch;}),app.jobs.end());}
    if(!app.folder.empty()&&app.disk.PriorityFolder(app.folder))app.disk.Visit(app.folder);
    ScheduleBookmarkPrefetch();
}
bool SetBookmarkCache(bool enabled,bool notify){
    if(notify)MessageBoxW(app.window,enabled?L"ディスクキャッシュを有効にし、ブックマーク配下の画像サムネイルを優先して保存します。容量上限内で古いキャッシュが整理されます。":L"キャッシュ対象は利用頻度から自動判定します。保存済みキャッシュはこの切替では削除しません。以後の容量整理や明示削除の対象にはなります。",L"ブックマークキャッシュ",MB_OK|MB_ICONINFORMATION);
    app.bookmarkCache=enabled;if(enabled)app.diskCache=true;ConfigureDiskCache();SaveSettings();return true;
}
void ScheduleBookmarkPrefetch(){
    if(app.closing||app.stopping||app.folderPending||app.saving||app.backupBusy||app.sorting||app.folder.empty())return;
    size_t limit=app.disk.PrefetchLimit(app.folder);if(!limit||app.diskPrefetchCount>=limit||app.diskPrefetchCursor>=app.allFiles.size())return;
    std::lock_guard<std::mutex> lock(app.mutex);
    if(app.thumbnailActive||std::any_of(app.jobs.begin(),app.jobs.end(),[](auto& j){return j.kind==1;}))return;
    Job job{1,app.generation,0,-1,app.allFiles[app.diskPrefetchCursor++],L"",app.thumbnailGeneration,256};job.diskPrefetch=true;job.diskGeneration=app.diskGeneration;
    ++app.diskPrefetchCount;app.jobs.push_back(std::move(job));app.wake.notify_all();
}
void ShowDiskCacheDialog(const std::function<void(HWND)>& inspect){DialogState state{inspect};struct Template{DLGTEMPLATE dialog;WORD menu,cls,title;}t{};t.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER;t.dialog.cx=365;t.dialog.cy=320;DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,app.window,DiskDialog,reinterpret_cast<LPARAM>(&state));}
