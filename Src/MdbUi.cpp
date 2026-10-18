#include "ViewerRuntime.h"
#include <map>
#include <set>
#include <future>
#include <windowsx.h>
namespace {
constexpr int Toggle=2300,Mode=2301,Capacity=2302,Info=2303;
const wchar_t* categories[]{L"通常",L"タグ別",L"人物ID別",L"人物名別",L"ワールド別",L"グループ別",L"撮影者別"};
struct DialogState{bool enabled=false;std::function<void(HWND)> inspect;};
void UpdateDialog(HWND h){auto state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(h,DWLP_USER));SetDlgItemTextW(h,Toggle,state->enabled?L"MDB：ON":L"MDB：OFF");auto mode=SendDlgItemMessageW(h,Mode,CB_GETCURSEL,0,0);EnableWindow(GetDlgItem(h,Capacity),mode==0);}
void Usage(HWND h){auto s=app.mdb.Status();auto text=L"使用量: "+std::to_wstring(s.bytes/1000)+L" KB / "+std::to_wstring(s.entries)+L"画像";if(s.working||s.pending)text+=L"　収集中・処理待ち "+std::to_wstring(s.pending)+L"件";if(!s.error.empty())text+=L"\r\n"+s.error;SetDlgItemTextW(h,Info,text.c_str());}
INT_PTR CALLBACK Dialog(HWND h,UINT m,WPARAM wp,LPARAM lp){
    auto state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(h,DWLP_USER));
    if(m==WM_INITDIALOG){state=reinterpret_cast<DialogState*>(lp);SetWindowLongPtrW(h,DWLP_USER,lp);SetWindowTextW(h,L"MDB設定");
        auto add=[&](const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int height,int id){auto c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,height,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return c;};
        add(L"BUTTON",L"",WS_TABSTOP,12,12,140,28,Toggle);
        add(L"STATIC",L"MDB（Metadata DataBase）は、写真のメタデータをこのPC内に収集し、\r\nメタデータ検索とカテゴリ別表示に利用します。\r\n画像の読込みに応じて順次登録し、EXEと同じ場所のMetadataDBへ保存します。\r\nMDB機能が画像・メタデータを外部サーバーへ送信することはありません。\r\nサムネイルや元の写真データ全体を保存する機能ではありません。",0,12,54,606,92,0);
        add(L"STATIC",L"MDB容量",0,12,160,100,24,0);auto modes=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,120,156,300,150,Mode);for(auto label:{L"設定値",L"自動値（ディスクサムネイル優先）",L"無制限"})SendMessageW(modes,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(modes,CB_SETCURSEL,app.mdbMode,0);
        add(L"EDIT",std::to_wstring(app.mdbCapacityMB).c_str(),WS_BORDER|ES_NUMBER|WS_TABSTOP,120,192,100,26,Capacity);add(L"STATIC",L"MB（100～4000、1000MB＝1GB）",0,232,196,370,24,0);
        add(L"STATIC",L"自動値：再読み込み時にディスクサムネイル保持画像を優先登録。\r\n暫定安全上限4GB。無制限はMDB独自の上限を設けません。\r\nバックアップ画像はMDBから除外します。\r\nONからOFFにするとMDBを削除します。元の写真は削除しません。",0,12,234,606,76,0);
        auto location=L"保存先: "+(app.settings.parent_path()/L"MetadataDB").wstring();add(L"EDIT",location.c_str(),WS_BORDER|ES_READONLY|ES_AUTOHSCROLL,12,322,606,26,0);
        add(L"STATIC",L"",0,12,358,606,62,Info);add(L"BUTTON",L"適用",BS_DEFPUSHBUTTON|WS_TABSTOP,398,438,100,28,IDOK);add(L"BUTTON",L"キャンセル",WS_TABSTOP,508,438,110,28,IDCANCEL);
        RECT r{0,0,630,480};AdjustWindowRectEx(&r,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));SetWindowPos(h,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);UpdateDialog(h);Usage(h);SetTimer(h,1,500,nullptr);if(state->inspect)SetTimer(h,2,100,nullptr);return TRUE;
    }
    if(m==WM_TIMER){if(wp==2){KillTimer(h,2);RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);if(state->inspect)state->inspect(h);if(IsWindow(h))EndDialog(h,IDCANCEL);}else Usage(h);return TRUE;}
    if(m==WM_COMMAND){int id=LOWORD(wp);if(id==Toggle){state->enabled=!state->enabled;UpdateDialog(h);return TRUE;}if(id==Mode){UpdateDialog(h);return TRUE;}
        if(id==IDOK){BOOL valid=FALSE;auto n=GetDlgItemInt(h,Capacity,&valid,FALSE);auto mode=int(SendDlgItemMessageW(h,Mode,CB_GETCURSEL,0,0));if(!valid||n<100||n>4000||mode<0||mode>2){MessageBoxW(h,L"MDB容量は100～4000MBで指定してください。",L"入力エラー",MB_OK|MB_ICONWARNING);return TRUE;}
            if(app.mdbEnabled&&!state->enabled&&!state->inspect)MessageBoxW(h,L"MDBをOFFにし、保存したメタデータDBを削除します。\r\n元の写真とサムネイルキャッシュは削除しません。\r\n再度ONにすると、画像の読込みに応じてDBを作り直します。",L"MDBの削除",MB_OK|MB_ICONINFORMATION);
            bool disabled=app.mdbEnabled&&!state->enabled;CancelSearch();app.mdbEnabled=state->enabled;app.mdbMode=mode;app.mdbCapacityMB=int(n);if(!app.mdbEnabled)app.categoryMode=0;ConfigureMdb();SaveSettings();EndDialog(h,IDOK);
            if(!app.folder.empty())StartSearch(app.appliedQuery);if(disabled)Counts(L"MDBを停止し、専用DBを削除しています…");return TRUE;}
        if(id==IDCANCEL){EndDialog(h,IDCANCEL);return TRUE;}
    }if(m==WM_CLOSE){EndDialog(h,IDCANCEL);return TRUE;}return FALSE;
}
}
void ConfigureMdb(){app.mdb.Configure(app.settings.parent_path()/L"MetadataDB",{app.mdbEnabled,app.mdbMode,app.mdbCapacityMB},ReadMdbSource,[](const std::wstring& path){try{Pixels unused;return app.disk.Get(ThumbnailCache::Key(path)+L"|256x256|v2",unused,false);}catch(...){return false;}});if(app.mdbEnabled)app.mdb.QueueCached(app.allFiles);}
void ShowMdbDialog(const std::function<void(HWND)>& inspect){if(app.saving||app.backupBusy||app.sorting||app.batchLoading)return;FlushSelectionUpdate();if(!GuardTransition([inspect]{ShowMdbDialog(inspect);}))return;DialogState state{app.mdbEnabled,inspect};struct Template{DLGTEMPLATE d;WORD menu,cls,title;}t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER,0,0,0,0,370,300},0,0,0};DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.d,app.window,Dialog,reinterpret_cast<LPARAM>(&state));}
void MdbTick(){static std::wstring reported;auto status=app.mdb.Status();if(status.deletionFailed&&!status.error.empty()&&status.error!=reported&&IsWindowEnabled(app.window)&&!IsIconic(app.window)&&!app.saving){reported=status.error;MessageBoxW(app.window,status.error.c_str(),L"MDB削除未完了",MB_OK|MB_ICONWARNING);}if(!status.deletionFailed)reported.clear();}
void RequestCategory(int category){if(!app.mdbEnabled||category<1||category>6||app.folderPending||app.saving||app.backupBusy||app.batchLoading||app.sorting||app.searching)return;FlushSelectionUpdate();if(!GuardTransition([category]{RequestCategory(category);}))return;app.restoreSelection=SelectedFiles();app.categoryMode=category;SaveSettings();StartSearch(app.appliedQuery);}
void StartCategory(const std::vector<std::wstring>& files,const std::wstring& preserve){CancelSearch();Job job{13,app.generation};job.searchGeneration=app.searchGeneration;job.categoryMode=app.categoryMode;job.sortMode=app.sortMode;job.files=files;job.preserve=preserve;app.sorting=true;EnableWindow(app.list,FALSE);UpdateEditorControls();SetWindowTextW(app.searchButton,L"中止");SetActivity(L"MDBでカテゴリを準備しています…");Queue(std::move(job));}
void RunCategory(const Job& job,Result& result){
    auto active=[&]{return !app.stopping&&job.generation==app.currentGeneration&&job.searchGeneration==app.currentSearchGeneration;};
    auto files=job.files;SortFiles(files,job.sortMode,active);std::vector<MdbRecord> records(files.size());std::vector<char> failed(files.size());std::atomic<size_t> next{0},done{0};std::vector<std::future<void>> workers;
    for(int w=0;w<2;++w)workers.push_back(std::async(std::launch::async,[&]{auto init=RoInitialize(RO_INIT_MULTITHREADED);for(;;){auto n=next++;if(n>=files.size()||!active())break;try{records[n]=app.mdb.Read(files[n],-1,false);failed[n]=records[n].failed;std::wstring().swap(records[n].text);}catch(...){failed[n]=true;records[n].path=files[n];}auto count=++done;if(count%25==0){Result p;p.job.kind=13;p.job.generation=job.generation;p.job.searchGeneration=job.searchGeneration;p.progress=true;p.text=L"カテゴリ準備中: "+std::to_wstring(count)+L" / "+std::to_wstring(files.size());QueueProgress(std::move(p));}}if(SUCCEEDED(init))RoUninitialize();}));for(auto& w:workers)w.get();if(!active())return;
    struct Group {std::wstring label;std::vector<std::pair<std::wstring,bool>> files;};std::map<std::wstring,Group> groups;std::map<std::wstring,std::set<std::wstring>> names;int axis=job.categoryMode-1;
    for(auto& row:records)if(!row.backup)for(auto& t:row.terms)if(t.axis==axis)names[t.value].insert(t.label);
    for(auto& item:names)if(axis==1||axis==3||axis==4)for(auto& label:app.mdb.Labels(axis,item.first))item.second.insert(label);
    std::set<std::wstring> seen;
    for(size_t n=0;n<records.size();++n){auto& row=records[n];auto path=files[n];if(!seen.insert(PathKeyForMdb(path)).second)continue;
        std::set<std::wstring> values;if(!row.backup&&!failed[n])for(auto& t:row.terms)if(t.axis==axis)values.insert(t.value);
        if(values.empty()){auto key=row.backup?L"\uffff3":failed[n]?L"\uffff2":L"\uffff1";auto& g=groups[key];g.label=row.backup?L"バックアップ（MDB対象外）":failed[n]?L"読取り失敗":L"情報なし";g.files.push_back({path,false});continue;}
        for(auto& value:values){auto& g=groups[value];g.label=value;if(axis==3||axis==4){auto& labels=names[value];if(labels.size()==1&&!labels.begin()->empty())g.label=*labels.begin();}g.files.push_back({path,values.size()>1});}
    }
    // Labels are presentation only; identity remains the category's exact value/ID.
    std::vector<std::wstring> order;for(auto& g:groups)order.push_back(g.first);std::stable_sort(order.begin(),order.end(),[&](auto& a,auto& b){bool am=a[0]==L'\uffff',bm=b[0]==L'\uffff';if(am!=bm)return !am;if(am&&bm)return a<b;int c=CompareStringOrdinal(groups[a].label.c_str(),-1,groups[b].label.c_str(),-1,TRUE);return c==CSTR_LESS_THAN||(c==CSTR_EQUAL&&a<b);});
    for(auto& key:order)for(auto& file:groups[key].files){result.files.push_back(file.first);result.mdbRows.push_back({key,groups[key].label,file.second});}
}
void SetupMdbGroups(){
    app.displayCategoryMode=app.mdbRows.empty()?0:app.categoryMode;
    ListView_EnableGroupView(app.list,FALSE);ListView_RemoveAllGroups(app.list);if(!app.categoryMode||app.mdbRows.empty())return;
    std::map<std::wstring,int> ids;int next=1;auto add=[&](const std::wstring& key,const std::wstring& text){auto i=ids.find(key);if(i!=ids.end())return i->second;int id=next++;LVGROUP g{sizeof(g)};g.mask=LVGF_GROUPID|LVGF_HEADER|LVGF_ALIGN;g.iGroupId=id;g.pszHeader=const_cast<wchar_t*>(text.c_str());g.uAlign=LVGA_HEADER_LEFT;ListView_InsertGroup(app.list,-1,&g);ids[key]=id;return id;};
    if(!app.directories.empty())add(L"\x1folder",L"フォルダー");
    for(int i=0;i<int(app.files.size());++i){int id=0;if(IsFolderItem(i))id=ids[L"\x1folder"];else {auto n=i-int(app.directories.size());if(n<0||n>=int(app.mdbRows.size()))continue;auto& row=app.mdbRows[n];id=add(row.key,row.label);}LVITEMW item{};item.mask=LVIF_GROUPID;item.iItem=i;item.iGroupId=id;ListView_SetItem(app.list,&item);}
    ListView_EnableGroupView(app.list,TRUE);
}
namespace {
struct SelectionInput {int depth=0,target=-1;bool active=false,mouse=false,toggle=false;} selectionInput;
bool SelectionMessage(UINT m){return m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK||m==WM_LBUTTONUP||m==WM_RBUTTONDOWN||m==WM_RBUTTONDBLCLK||m==WM_RBUTTONUP||m==WM_KEYDOWN||m==WM_KEYUP||m==WM_CAPTURECHANGED||m==WM_CANCELMODE;}
}
bool MdbSelectionBusy(){return selectionInput.active;}
void BeginMdbInput(HWND list,UINT message,WPARAM keys,LPARAM point){
    auto& input=selectionInput;if((!app.categoryMode&&!input.active)||!SelectionMessage(message))return;
    bool down=message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||message==WM_RBUTTONDOWN||message==WM_RBUTTONDBLCLK;
    if(!input.active&&(down||message==WM_KEYDOWN)){
        FlushSelectionUpdate();input.active=true;input.target=-1;input.toggle=false;
        if(down){LVHITTESTINFO hit{};hit.pt={GET_X_LPARAM(point),GET_Y_LPARAM(point)};input.target=ListView_HitTest(list,&hit);input.mouse=true;input.toggle=(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK)&&(keys&MK_CONTROL)&&!(keys&MK_SHIFT);}
        else {input.target=ListView_GetNextItem(list,-1,LVNI_FOCUSED);input.toggle=keys==VK_SPACE&&(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_SHIFT)&0x8000);}
    }
    ++input.depth;
}
void EndMdbInput(HWND list,UINT message){
    auto& input=selectionInput;if(!SelectionMessage(message)||!input.depth)return;
    if(message==WM_LBUTTONUP||message==WM_RBUTTONUP||message==WM_CAPTURECHANGED||message==WM_CANCELMODE)input.mouse=false;
    if(--input.depth||input.mouse||!input.active)return;
    if(!app.categoryMode){input={};app.mdbSelectionChanges.clear();if(app.selectionPending)PostMessageW(app.window,WM_SELECTION,0,0);return;}
    // Standard list input can deselect another occurrence after selecting the clicked row.
    // Reconcile once from the finished native operation; Ctrl toggles use the target row.
    std::set<std::wstring> selected;int i=-1;while((i=ListView_GetNextItem(list,i,LVNI_SELECTED))>=0)if(i<int(app.files.size()))selected.insert(PathKeyForMdb(app.files[i]));
    for(auto& change:app.mdbSelectionChanges)change.second=selected.count(change.first)!=0;
    if(input.toggle&&input.target>=0&&input.target<int(app.files.size()))app.mdbSelectionChanges[PathKeyForMdb(app.files[input.target])]=(ListView_GetItemState(list,input.target,LVIS_SELECTED)&LVIS_SELECTED)!=0;
    input.active=false;input.target=-1;
    if(app.selectionPending)PostMessageW(app.window,WM_SELECTION,0,0);
}
void NoteMdbSelection(int i,UINT state){if(!app.categoryMode||app.updatingSelection||i<0||i>=int(app.files.size()))return;app.mdbSelectionChanges[PathKeyForMdb(app.files[i])]=(state&LVIS_SELECTED)!=0;}
void SyncMdbSelection(){if(!app.categoryMode||app.updatingSelection||app.mdbSelectionChanges.empty())return;auto changes=std::move(app.mdbSelectionChanges);app.mdbSelectionChanges.clear();app.updatingSelection=true;for(int i=0;i<int(app.files.size());++i){auto found=changes.find(PathKeyForMdb(app.files[i]));if(found!=changes.end())ListView_SetItemState(app.list,i,found->second?LVIS_SELECTED:0,LVIS_SELECTED);}app.updatingSelection=false;app.selectionCountsDirty=true;}
std::wstring PathKeyForMdb(const std::wstring& path){auto s=NormalizeFolderAddress(path);CharLowerBuffW(s.data(),DWORD(s.size()));return s;}
bool MdbDuplicateRow(int i){auto n=i-int(app.directories.size());return app.categoryMode&&n>=0&&n<int(app.mdbRows.size())&&app.mdbRows[n].multiple;}

void DrawMdbGroupHeader(HDC dc,int group){RECT r{};if(!ListView_GetGroupRect(app.list,group,LVGGR_HEADER,&r))return;wchar_t text[1024]{};LVGROUP g{sizeof(g)};g.mask=LVGF_HEADER;g.pszHeader=text;g.cchHeader=1024;if(ListView_GetGroupInfo(app.list,group,&g)==-1)return;FillRect(dc,&r,ThemeBrush());int saved=SaveDC(dc);SetTextColor(dc,ThemeForeground());SetBkMode(dc,TRANSPARENT);auto font=reinterpret_cast<HFONT>(SendMessageW(app.list,WM_GETFONT,0,0));if(font)SelectObject(dc,font);auto label=r;label.left+=8;DrawTextW(dc,text,-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);SIZE size{};GetTextExtentPoint32W(dc,text,int(wcslen(text)),&size);int left=std::min(r.right-8,r.left+16+size.cx);auto pen=CreatePen(PS_SOLID,1,ThemeForeground());auto old=SelectObject(dc,pen);MoveToEx(dc,left,(r.top+r.bottom)/2,nullptr);LineTo(dc,r.right-6,(r.top+r.bottom)/2);SelectObject(dc,old);DeleteObject(pen);RestoreDC(dc,saved);}

void PaintMdbHeaders(HWND list,HDC dc){if(!app.categoryMode||!ListView_IsGroupViewEnabled(list))return;RECT client{};GetClientRect(list,&client);int count=int(ListView_GetGroupCount(list));auto rectAt=[&](int index,RECT& r,int& id){LVGROUP g{sizeof(g)};g.mask=LVGF_GROUPID;if(!ListView_GetGroupInfoByIndex(list,index,&g))return false;id=g.iGroupId;return ListView_GetGroupRect(list,id,LVGGR_HEADER,&r)!=FALSE;};int lo=0,hi=count;while(lo<hi){int mid=lo+(hi-lo)/2,id=0;RECT r{};if(rectAt(mid,r,id)&&r.bottom<0)lo=mid+1;else hi=mid;}for(int i=lo;i<count;++i){RECT r{};int id=0;if(!rectAt(i,r,id))continue;if(r.top>client.bottom)break;if(r.bottom>=0)DrawMdbGroupHeader(dc,id);}}
