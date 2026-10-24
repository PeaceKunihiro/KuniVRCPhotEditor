#include "ViewerRuntime.h"
#include <map>
#include <set>
#include <future>
#include <windowsx.h>
namespace {
constexpr int Toggle=2300,Mode=2301,Capacity=2302,Info=2303,Names=2304;
std::map<int,std::wstring> headerIds;
const wchar_t* categories[]{L"通常",L"タグ別",L"人物別",L"人物別",L"ワールド別",L"グループ別",L"撮影者別"};
struct DialogState{bool enabled=false;std::function<void(HWND)> inspect;};
void UpdateDialog(HWND h){auto state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(h,DWLP_USER));SetDlgItemTextW(h,Toggle,state->enabled?L"MDB：ON":L"MDB：OFF");auto mode=SendDlgItemMessageW(h,Mode,CB_GETCURSEL,0,0);EnableWindow(GetDlgItem(h,Capacity),mode==0);EnableWindow(GetDlgItem(h,Names),state->enabled&&app.mdbEnabled);}
void Usage(HWND h){auto s=app.mdb.Status();auto text=L"使用量: "+std::to_wstring(s.bytes/1000)+L" KB / "+std::to_wstring(s.entries)+L"画像";if(s.working||s.pending)text+=L"　収集中・処理待ち "+std::to_wstring(s.pending)+L"件";if(!s.error.empty())text+=L"\r\n"+s.error;SetDlgItemTextW(h,Info,text.c_str());}
INT_PTR CALLBACK Dialog(HWND h,UINT m,WPARAM wp,LPARAM lp){
    auto state=reinterpret_cast<DialogState*>(GetWindowLongPtrW(h,DWLP_USER));
    if(m==WM_INITDIALOG){ApplyUiFont(h);state=reinterpret_cast<DialogState*>(lp);SetWindowLongPtrW(h,DWLP_USER,lp);SetWindowTextW(h,L"MDB設定");
        auto add=[&](const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int height,int id){auto c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,height,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),TRUE);return c;};
        add(L"BUTTON",L"",WS_TABSTOP,12,12,140,28,Toggle);add(L"BUTTON",L"ID・表示名の編集…",WS_TABSTOP,164,12,230,28,Names);
        add(L"STATIC",L"MDB（Metadata DataBase）は、写真のメタデータをこのPC内に収集し、\r\nメタデータ検索とカテゴリ別表示に利用します。\r\n画像の読込みに応じて順次登録し、EXEと同じ場所のMetadataDBへ保存します。\r\nMDB機能が画像・メタデータを外部サーバーへ送信することはありません。\r\nサムネイルや元の写真データ全体を保存する機能ではありません。",0,12,54,606,92,0);
        add(L"STATIC",L"MDB容量",0,12,160,100,24,0);auto modes=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,120,156,300,150,Mode);for(auto label:{L"設定値",L"自動値（ディスクサムネイル優先）",L"無制限"})SendMessageW(modes,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(modes,CB_SETCURSEL,app.mdbMode,0);
        add(L"EDIT",std::to_wstring(app.mdbCapacityMB).c_str(),WS_BORDER|ES_NUMBER|WS_TABSTOP,120,192,100,26,Capacity);add(L"STATIC",L"MB（100～4000、1000MB＝1GB）",0,232,196,370,24,0);
        add(L"STATIC",L"自動値：再読み込み時にディスクサムネイル保持画像を優先登録。\r\n暫定安全上限4GB。無制限はMDB独自の上限を設けません。\r\nバックアップ画像はMDBから除外します。\r\nONからOFFにするとMDBを削除します。元の写真は削除しません。",0,12,234,606,76,0);
        auto location=L"保存先: "+(app.settings.parent_path()/L"MetadataDB").wstring();add(L"EDIT",location.c_str(),WS_BORDER|ES_READONLY|ES_AUTOHSCROLL,12,322,606,26,0);
        add(L"STATIC",L"",0,12,358,606,62,Info);add(L"BUTTON",L"適用",BS_DEFPUSHBUTTON|WS_TABSTOP,398,438,100,28,IDOK);add(L"BUTTON",L"キャンセル",WS_TABSTOP,508,438,110,28,IDCANCEL);
        RECT r{0,0,630,480};AdjustWindowRectEx(&r,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(h,GWL_EXSTYLE)));SetWindowPos(h,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);ScaleUiDialog(h);UpdateDialog(h);Usage(h);SetTimer(h,1,500,nullptr);if(state->inspect)SetTimer(h,2,100,nullptr);return TRUE;
    }
    if(m==WM_TIMER){if(wp==2){KillTimer(h,2);RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);if(state->inspect)state->inspect(h);if(IsWindow(h))EndDialog(h,IDCANCEL);}else Usage(h);return TRUE;}
    if(m==WM_COMMAND){int id=LOWORD(wp);if(id==Names){ShowIdNamesDialog(h);return TRUE;}if(id==Toggle){state->enabled=!state->enabled;UpdateDialog(h);return TRUE;}if(id==Mode){UpdateDialog(h);return TRUE;}
        if(id==IDOK){BOOL valid=FALSE;auto n=GetDlgItemInt(h,Capacity,&valid,FALSE);auto mode=int(SendDlgItemMessageW(h,Mode,CB_GETCURSEL,0,0));if(!valid||n<100||n>4000||mode<0||mode>2){MessageBoxW(h,L"MDB容量は100～4000MBで指定してください。",L"入力エラー",MB_OK|MB_ICONWARNING);return TRUE;}
            if(app.mdbEnabled&&!state->enabled&&!state->inspect)MessageBoxW(h,L"MDBをOFFにし、保存したメタデータDBを削除します。\r\n元の写真とサムネイルキャッシュは削除しません。\r\n再度ONにすると、画像の読込みに応じてDBを作り直します。",L"MDBの削除",MB_OK|MB_ICONINFORMATION);
            bool disabled=app.mdbEnabled&&!state->enabled;CancelSearch();app.mdbEnabled=state->enabled;app.mdbMode=mode;app.mdbCapacityMB=int(n);if(!app.mdbEnabled)app.categoryMode=0;ConfigureMdb();SaveSettings();EndDialog(h,IDOK);
            if(!app.folder.empty())StartSearch(app.appliedQuery);if(disabled)Counts(L"MDBを停止し、専用DBを削除しています…");return TRUE;}
        if(id==IDCANCEL){EndDialog(h,IDCANCEL);return TRUE;}
    }if(m==WM_CLOSE){EndDialog(h,IDCANCEL);return TRUE;}return FALSE;
}
}
void ConfigureMdb(){CancelMdbApply(L"MDB設定の変更により中止しました。");app.idNames.Load(AppDataFile(app.settings.parent_path(),L"IdDisplayNames.json"));app.mdb.Configure(app.settings.parent_path()/L"MetadataDB",{app.mdbEnabled,app.mdbMode,app.mdbCapacityMB},ReadMdbSource,[](const std::wstring& path){try{return static_cast<int>(app.disk.Probe(ThumbnailCache::Key(path)+L"|256x256|v2"));}catch(...){return -2;}});if(app.mdbEnabled)app.mdb.QueueCached(app.allFiles);RefreshMdbApplyUi();if(app.metadataTabs)Layout();}
void ShowMdbDialog(const std::function<void(HWND)>& inspect){if(app.saving||app.backupBusy||app.sorting||app.batchLoading)return;FlushSelectionUpdate();if(!GuardTransition([inspect]{ShowMdbDialog(inspect);}))return;DialogState state{app.mdbEnabled,inspect};struct Template{DLGTEMPLATE d;WORD menu,cls,title;}t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER,0,0,0,0,370,300},0,0,0};DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.d,app.window,Dialog,reinterpret_cast<LPARAM>(&state));}
void MdbTick(){static std::wstring reportedNames;auto nameError=app.idNames.Error();if(app.mdbEnabled&&!nameError.empty()&&nameError!=reportedNames&&IsWindowEnabled(app.window)&&!IsIconic(app.window)&&!app.saving){reportedNames=nameError;MessageBoxW(app.window,nameError.c_str(),L"対応表の読込み",MB_OK|MB_ICONWARNING);}if(nameError.empty())reportedNames.clear();static std::wstring reported;auto status=app.mdb.Status();if(status.deletionFailed&&!status.error.empty()&&status.error!=reported&&IsWindowEnabled(app.window)&&!IsIconic(app.window)&&!app.saving){reported=status.error;MessageBoxW(app.window,status.error.c_str(),L"MDB削除未完了",MB_OK|MB_ICONWARNING);}if(!status.deletionFailed)reported.clear();}
void RequestCategory(int category){if(!app.mdbEnabled||category<1||category>6||app.folderPending||app.saving||app.backupBusy||app.batchLoading||app.sorting||app.searching)return;FlushSelectionUpdate();if(!GuardTransition([category]{RequestCategory(category);}))return;ResetCategoryView();app.restoreSelection=SelectedFiles();app.categoryMode=category==3?2:category;SaveSettings();StartSearch(app.appliedQuery);}
void StartCategory(const std::vector<std::wstring>& files,const std::wstring& preserve){CancelSearch();Job job{13,app.generation};job.searchGeneration=app.searchGeneration;job.categoryMode=app.categoryMode;job.sortMode=app.sortMode;job.files=files;job.preserve=preserve;app.sorting=true;EnableWindow(app.list,FALSE);UpdateEditorControls();SetWindowTextW(app.searchButton,L"中止");SetActivity(L"MDBでカテゴリを準備しています…");Queue(std::move(job));}
void RunCategory(const Job& job,Result& result){
    auto active=[&]{return !app.stopping&&job.generation==app.currentGeneration&&job.searchGeneration==app.currentSearchGeneration;};
    auto files=job.files;SortFiles(files,job.sortMode,active);std::vector<MdbRecord> records(files.size());std::vector<char> failed(files.size());std::atomic<size_t> next{0},done{0};std::vector<std::future<void>> workers;
    for(int w=0;w<2;++w)workers.push_back(std::async(std::launch::async,[&]{auto init=RoInitialize(RO_INIT_MULTITHREADED);for(;;){auto n=next++;if(n>=files.size()||!active())break;try{records[n]=app.mdb.Read(files[n],-1,false,job.categoryMode==2||job.categoryMode==3);failed[n]=records[n].failed;std::wstring().swap(records[n].text);}catch(...){failed[n]=true;records[n].failed=true;records[n].path=files[n];}auto count=++done;if(count%25==0){Result p;p.job.kind=13;p.job.generation=job.generation;p.job.searchGeneration=job.searchGeneration;p.progress=true;p.text=L"カテゴリ準備中: "+std::to_wstring(count)+L" / "+std::to_wstring(files.size());QueueProgress(std::move(p));}}if(SUCCEEDED(init))RoUninitialize();}));for(auto& w:workers)w.get();if(!active())return;
    if(job.categoryMode==2||job.categoryMode==3){BuildPersonView(records,app.idNames.Snapshot(),L"",result);result.personRecords=std::move(records);return;}
    struct Group {std::wstring label;std::vector<std::pair<std::wstring,bool>> files;};std::map<std::wstring,Group> groups;int axis=job.categoryMode-1;bool idAxis=axis==1||axis==3||axis==4;auto aliases=app.idNames.Snapshot();
    std::set<std::wstring> seen;
    for(size_t n=0;n<records.size();++n){auto& row=records[n];auto path=files[n];if(!seen.insert(PathKeyForMdb(path)).second)continue;
        std::set<std::wstring> values;if(!row.backup&&!failed[n])for(auto& t:row.terms)if(t.axis==axis){auto key=idAxis?CanonicalDisplayId(t.value):t.value;if(idAxis){auto type=DisplayIdType(key);if((axis==1&&type!=L"person")||(axis==3&&type!=L"world")||(axis==4&&type!=L"group"))key.clear();}if(!key.empty())values.insert(key);}
        if(values.empty()){auto key=row.backup?L"\uffff3":failed[n]?L"\uffff2":L"\uffff1";auto& g=groups[key];g.label=row.backup?L"バックアップ（MDB対象外）":failed[n]?L"読取り失敗":L"情報なし";g.files.push_back({path,false});continue;}
        for(auto& value:values){auto& g=groups[value];auto found=aliases.find(value);g.label=idAxis&&found!=aliases.end()?found->second:value;g.files.push_back({path,values.size()>1});}
    }
    // Labels are presentation only; identity remains the category's exact value/ID.
    std::vector<std::wstring> order;for(auto& g:groups)order.push_back(g.first);std::stable_sort(order.begin(),order.end(),[&](auto& a,auto& b){bool am=a[0]==L'\uffff',bm=b[0]==L'\uffff';if(am!=bm)return !am;if(am&&bm)return a<b;int c=CompareStringOrdinal(groups[a].label.c_str(),-1,groups[b].label.c_str(),-1,TRUE);return c==CSTR_LESS_THAN||(c==CSTR_EQUAL&&a<b);});
    for(auto& key:order)for(auto& file:groups[key].files){result.files.push_back(file.first);result.mdbRows.push_back({key,groups[key].label,file.second});}
}
namespace {
struct CategorySpan{int id,first,last;std::wstring key;};
std::vector<CategorySpan> spans;std::map<std::wstring,bool> expanded,savedExpanded;
HWND categoryBar{},categoryTips{};int personScroll=0;
std::vector<HWND> personPool;std::vector<int> personPoolIndex;std::vector<std::wstring> personTips;
std::map<std::wstring,std::wstring> PersonLabels(const std::vector<MdbRecord>& records,const IdNameTable& aliases){
    std::map<std::wstring,std::set<std::wstring>> labels;
    for(auto& record:records)if(!record.backup&&!record.failed)for(auto& term:record.terms)if(term.axis==6)labels[term.value].insert(term.label);
    std::map<std::wstring,std::wstring> result;
    for(auto& item:labels){auto key=item.first;auto alias=aliases.find(key);
        if(key.rfind(L"name:",0)==0)result[key]=key.substr(5);
        else if(alias!=aliases.end())result[key]=alias->second;
        else {auto names=item.second;names.erase(L"");result[key]=names.size()==1?*names.begin():key;}}
    return result;
}
std::vector<std::wstring> PersonOrder(const std::map<std::wstring,std::wstring>& labels){
    std::vector<std::wstring> keys;for(auto& item:labels)keys.push_back(item.first);
    std::sort(keys.begin(),keys.end(),[&](auto& a,auto& b){int order=CompareStringEx(L"ja-JP",NORM_IGNORECASE,labels.at(a).c_str(),-1,labels.at(b).c_str(),-1,nullptr,nullptr,0);return order==CSTR_LESS_THAN||(order!=CSTR_GREATER_THAN&&order!=CSTR_LESS_THAN&&a<b);});return keys;
}
bool GroupExpanded(int id){return !(ListView_GetGroupState(app.list,id,LVGS_COLLAPSED)&LVGS_COLLAPSED);}
}
void BuildPersonView(const std::vector<MdbRecord>& records,const IdNameTable& aliases,const std::wstring& priority,Result& result){
    auto labels=PersonLabels(records,aliases);auto order=PersonOrder(labels);
    auto selected=std::find(order.begin(),order.end(),priority);if(selected!=order.end())std::rotate(order.begin(),selected,selected+1);
    std::map<std::wstring,size_t> rank;for(size_t i=0;i<order.size();++i)rank[order[i]]=i;
    std::map<std::wstring,std::vector<const MdbRecord*>> assigned;std::set<std::wstring> seen;
    for(auto& record:records){if(!seen.insert(PathKeyForMdb(record.path)).second)continue;std::wstring key;
        if(record.backup)key=L"\uffff3";else if(record.failed)key=L"\uffff2";
        else {size_t best=order.size();for(auto& term:record.terms)if(term.axis==6){auto found=rank.find(term.value);if(found!=rank.end()&&found->second<best){best=found->second;key=term.value;}}if(key.empty())key=L"\uffff1";}
        assigned[key].push_back(&record);
    }
    labels[L"\uffff1"]=L"モデル候補なし";labels[L"\uffff2"]=L"読取り失敗";labels[L"\uffff3"]=L"バックアップ（MDB対象外）";
    order.insert(order.end(),{L"\uffff1",L"\uffff2",L"\uffff3"});result.files.clear();result.mdbRows.clear();
    for(auto& key:order)for(auto record:assigned[key]){result.files.push_back(record->path);result.mdbRows.push_back({key,labels[key],false});}
}
void CaptureCategoryState(){if(app.collapseReset||app.categoryCommitting||!app.list)return;for(auto& span:spans)expanded[span.key]=GroupExpanded(span.id);}
void ResetCategoryView(){app.priorityPerson.clear();app.personRecords.clear();app.personChoices.clear();expanded.clear();savedExpanded.clear();app.collapseReset=true;personScroll=0;}
void SetupMdbGroups(){
    app.displayCategoryMode=app.mdbRows.empty()?0:app.categoryMode;headerIds.clear();spans.clear();
    auto aliases=app.idNames.Snapshot();auto people=app.categoryMode==2?PersonLabels(app.personRecords,aliases):std::map<std::wstring,std::wstring>{};
    for(auto& row:app.mdbRows){if(app.categoryMode==2){auto found=people.find(row.key);if(found!=people.end())row.label=found->second;}
        else if(app.categoryMode==4||app.categoryMode==5){auto id=CanonicalDisplayId(row.key);if(!id.empty()){auto found=aliases.find(id);row.label=found==aliases.end()?id:found->second;}}}
    ListView_EnableGroupView(app.list,FALSE);ListView_RemoveAllGroups(app.list);
    RefreshCategoryBar();if(!app.categoryMode||app.mdbRows.empty())return;
    if(app.collapseReset){expanded.clear();app.collapseReset=false;}
    std::map<std::wstring,std::wstring> captions;for(auto& person:app.personChoices)captions.insert(person);
    std::map<std::wstring,int> ids;int next=1;
    auto add=[&](const std::wstring& key,const std::wstring& label,int index){auto found=ids.find(key);if(found!=ids.end()){spans[found->second-1].last=index;return found->second;}
        int id=next++;LVGROUP g{sizeof(g)};g.mask=LVGF_GROUPID|LVGF_HEADER|LVGF_ALIGN|LVGF_STATE;g.iGroupId=id;
        g.pszHeader=const_cast<wchar_t*>(label.c_str());g.uAlign=LVGA_HEADER_LEFT;g.stateMask=LVGS_COLLAPSIBLE|LVGS_COLLAPSED;g.state=LVGS_COLLAPSIBLE|(expanded[key]?0:LVGS_COLLAPSED);
        ListView_InsertGroup(app.list,-1,&g);ids[key]=id;spans.push_back({id,index,index,key});if(!CanonicalDisplayId(key).empty())headerIds[id]=key;return id;};
    for(int i=0;i<int(app.files.size());++i){auto n=i-int(app.directories.size());auto key=IsFolderItem(i)?L"\x1folder":app.mdbRows[n].key;auto label=IsFolderItem(i)?L"フォルダー":app.mdbRows[n].label;if(app.categoryMode==2){auto found=captions.find(key);if(found!=captions.end())label=found->second;}
        int id=add(key,label,i);LVITEMW item{};item.mask=LVIF_GROUPID;item.iItem=i;item.iGroupId=id;ListView_SetItem(app.list,&item);}
    for(auto& span:spans){LVGROUP g{sizeof(g)};g.mask=LVGF_HEADER;std::wstring label=span.first<int(app.directories.size())?L"フォルダー":app.mdbRows[span.first-int(app.directories.size())].label;if(app.categoryMode==2){auto found=captions.find(span.key);if(found!=captions.end())label=found->second;}
        auto text=label+L"（"+std::to_wstring(span.last-span.first+1)+L"）";g.pszHeader=text.data();ListView_SetGroupInfo(app.list,span.id,&g);}
    ListView_EnableGroupView(app.list,TRUE);LRESULT unused=0;MdbHeaderTooltip(app.list,WM_SIZE,0,unused);
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

void DrawMdbGroupHeader(HDC dc,int group){RECT r{};if(!ListView_GetGroupRect(app.list,group,LVGGR_HEADER,&r))return;wchar_t text[1024]{};LVGROUP g{sizeof(g)};g.mask=LVGF_HEADER;g.pszHeader=text;g.cchHeader=1024;if(ListView_GetGroupInfo(app.list,group,&g)==-1)return;PaintBackdrop(app.list,dc,r);int saved=SaveDC(dc);SetTextColor(dc,ThemeForeground());SetBkMode(dc,TRANSPARENT);auto font=reinterpret_cast<HFONT>(SendMessageW(app.list,WM_GETFONT,0,0));if(font)SelectObject(dc,font);auto label=r;label.left+=28;auto marker=GroupExpanded(group)?L"▼":L"▶";auto glyph=r;glyph.right=glyph.left+24;DrawTextW(dc,marker,-1,&glyph,DT_SINGLELINE|DT_VCENTER|DT_CENTER);DrawTextW(dc,text,-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);SIZE size{};GetTextExtentPoint32W(dc,text,int(wcslen(text)),&size);int left=std::min(r.right-8,r.left+36+size.cx);auto pen=CreatePen(PS_SOLID,1,ThemeForeground());auto old=SelectObject(dc,pen);MoveToEx(dc,left,(r.top+r.bottom)/2,nullptr);LineTo(dc,r.right-6,(r.top+r.bottom)/2);SelectObject(dc,old);DeleteObject(pen);RestoreDC(dc,saved);}

void PaintMdbHeaders(HWND list,HDC dc){if(!app.categoryMode||!ListView_IsGroupViewEnabled(list))return;RECT client{};GetClientRect(list,&client);int count=int(ListView_GetGroupCount(list));auto rectAt=[&](int index,RECT& r,int& id){LVGROUP g{sizeof(g)};g.mask=LVGF_GROUPID;if(!ListView_GetGroupInfoByIndex(list,index,&g))return false;id=g.iGroupId;return ListView_GetGroupRect(list,id,LVGGR_HEADER,&r)!=FALSE;};int lo=0,hi=count;while(lo<hi){int mid=lo+(hi-lo)/2,id=0;RECT r{};if(rectAt(mid,r,id)&&r.bottom<0)lo=mid+1;else hi=mid;}for(int i=lo;i<count;++i){RECT r{};int id=0;if(!rectAt(i,r,id))continue;if(r.top>client.bottom)break;if(r.bottom>=0)DrawMdbGroupHeader(dc,id);}}

void RefreshMdbIdLabels(){
    if(!app.mdbEnabled)return;
    if(app.displayCategoryMode==2){auto selected=app.priorityPerson;auto saved=savedExpanded;app.priorityPerson.clear();ApplyPersonPriority(selected);if(!selected.empty())savedExpanded=std::move(saved);return;}
    if(app.displayCategoryMode!=4&&app.displayCategoryMode!=5)return;
    CaptureCategoryState();auto names=app.idNames.Snapshot();for(auto& row:app.mdbRows){auto id=CanonicalDisplayId(row.key);if(!id.empty()){auto found=names.find(id);row.label=found==names.end()?id:found->second;}}
    SetupMdbGroups();ScheduleThumbnails();InvalidateRect(app.list,nullptr,TRUE);
}

std::wstring MdbHeaderIdAt(POINT point){if(app.displayCategoryMode==2||app.displayCategoryMode==4||app.displayCategoryMode==5)for(auto& pair:headerIds){RECT r{};if(ListView_GetGroupRect(app.list,pair.first,LVGGR_HEADER,&r)&&PtInRect(&r,point))return pair.second;}return L"";}
bool MdbHeaderTooltip(HWND list,UINT message,LPARAM lp,LRESULT& result){
    static HWND tooltip=nullptr;static std::wstring text;
    if(message==WM_NCDESTROY){if(tooltip)DestroyWindow(tooltip);tooltip=nullptr;return false;}
    if(message==WM_SIZE){if(!tooltip){tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,list,nullptr,GetModuleHandleW(nullptr),nullptr);TOOLINFOW tool{sizeof(tool)};tool.hwnd=list;tool.uId=1;tool.uFlags=TTF_SUBCLASS;GetClientRect(list,&tool.rect);tool.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));}else {TOOLINFOW tool{sizeof(tool)};tool.hwnd=list;tool.uId=1;GetClientRect(list,&tool.rect);SendMessageW(tooltip,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&tool));}}
    if(message==WM_NOTIFY&&reinterpret_cast<NMHDR*>(lp)->hwndFrom==tooltip&&reinterpret_cast<NMHDR*>(lp)->code==TTN_GETDISPINFOW){text.clear();POINT p{};GetCursorPos(&p);ScreenToClient(list,&p);text=MdbHeaderIdAt(p);reinterpret_cast<NMTTDISPINFOW*>(lp)->lpszText=const_cast<wchar_t*>(text.c_str());result=0;return true;}return false;
}


namespace {
int CategoryHeaderHeight(int width,bool place){bool people=app.categoryMode==2&&app.mdbEnabled;int x=2,y=2,bh=UiControlHeight();
    for(int id:{3100,3101,3102}){auto button=GetDlgItem(categoryBar,id);bool show=id!=3100||people;if(place)ShowWindow(button,show?SW_SHOW:SW_HIDE);if(!show)continue;int bw=std::min(std::max(1,width-4),std::max(UiScale(id==3100?70:id==3101?86:102),UiTextWidth(Text(button))+16));if(x>2&&x+bw>width-2){x=2;y+=bh+6;}if(place)MoveWindow(button,x,y,bw,bh,TRUE);x+=bw+4;}
    return y+bh+4;
}
void PositionPersonButtons(){
    static bool arranging=false;if(!categoryBar||arranging)return;struct Guard{bool& flag;Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(arranging);
    RECT rc{};GetClientRect(categoryBar,&rc);int width=rc.right;
    bool people=app.categoryMode==2&&app.mdbEnabled;int top=CategoryHeaderHeight(width,true),step=UiControlHeight()+2;
    int cell=std::min(UiScale(180),std::max(40,width-20)),columns=std::max(1,(width-16)/cell),rows=std::max(1,(int(rc.bottom)-top)/step);
    int total=people?int(app.personChoices.size()):0,totalRows=(total+columns-1)/columns;
    personScroll=std::clamp(personScroll,0,std::max(0,totalRows-rows));SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,totalRows-1),UINT(rows),personScroll,0};SetScrollInfo(categoryBar,SB_VERT,&scroll,TRUE);
    size_t slots=people?size_t(columns*rows):0;
    while(personPool.size()<slots){int id=3200+int(personPool.size());auto button=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_TABSTOP|BS_AUTOCHECKBOX|BS_PUSHLIKE,0,0,1,1,categoryBar,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),TRUE);personPool.push_back(button);
        TOOLINFOW tool{sizeof(tool)};tool.hwnd=categoryBar;tool.uId=reinterpret_cast<UINT_PTR>(button);tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(categoryTips,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
    personPoolIndex.assign(personPool.size(),-1);personTips.resize(personPool.size());
    for(size_t i=0;i<personPool.size();++i){int index=personScroll*columns+int(i);bool show=i<slots&&index<total;if(show){personPoolIndex[i]=index;auto& choice=app.personChoices[index];bool selected=choice.first==app.priorityPerson;
            auto text=(selected?L"● ":L"")+choice.second;SetWindowTextW(personPool[i],text.c_str());SendMessageW(personPool[i],BM_SETCHECK,selected?BST_CHECKED:BST_UNCHECKED,0);
            personTips[i]=choice.second+L"\r\n"+(choice.first.rfind(L"name:",0)==0?L"IDなし":choice.first);
            MoveWindow(personPool[i],2+int(i)%columns*cell,top+int(i)/columns*step,cell-4,UiControlHeight(),TRUE);}
        ShowWindow(personPool[i],show?SW_SHOW:SW_HIDE);}
}
LRESULT CALLBACK CategoryBarProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(m==WM_SIZE){PositionPersonButtons();return 0;}
    if(m==WM_VSCROLL||m==WM_MOUSEWHEEL){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(h,SB_VERT,&info);
        int next=personScroll;if(m==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA;
        else {switch(LOWORD(wp)){case SB_TOP:next=0;break;case SB_BOTTOM:next=info.nMax;break;case SB_LINEUP:--next;break;case SB_LINEDOWN:++next;break;case SB_PAGEUP:next-=int(info.nPage);break;case SB_PAGEDOWN:next+=int(info.nPage);break;case SB_THUMBTRACK:case SB_THUMBPOSITION:next=info.nTrackPos;break;}}
        personScroll=std::clamp(next,0,std::max(0,info.nMax-int(info.nPage)+1));PositionPersonButtons();return 0;}
    if(m==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){int id=LOWORD(wp);if(id==3100)ApplyPersonPriority(L"");else if(id==3101)SetAllCategories(true);else if(id==3102)SetAllCategories(false);
        else if(id>=3200&&id<3200+int(personPoolIndex.size())){int n=personPoolIndex[id-3200];if(n>=0&&n<int(app.personChoices.size())){auto key=app.personChoices[n].first;ApplyPersonPriority(key);}}return 0;}
    if(m==WM_NOTIFY){auto hdr=reinterpret_cast<NMHDR*>(lp);if(hdr->code==NM_CUSTOMDRAW)return ThemeNotify(hdr);
        if(hdr->hwndFrom==categoryTips&&hdr->code==TTN_GETDISPINFOW){auto tip=reinterpret_cast<NMTTDISPINFOW*>(lp);int n=GetDlgCtrlID(reinterpret_cast<HWND>(hdr->idFrom))-3200;if(n>=0&&n<int(personTips.size()))tip->lpszText=personTips[n].data();return 0;}}
    if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORBTN)return ThemeControlColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp));
    if(m==WM_ERASEBKGND){RECT rc{};GetClientRect(h,&rc);PaintBackdrop(h,reinterpret_cast<HDC>(wp),rc);return 1;}
    if(m==WM_NCDESTROY){personPool.clear();personPoolIndex.clear();personTips.clear();categoryBar=nullptr;categoryTips=nullptr;}
    return DefSubclassProc(h,m,wp,lp);
}
}
void CreateCategoryBar(){
    categoryBar=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"",WS_CHILD|WS_CLIPCHILDREN|WS_VSCROLL,0,0,1,1,app.window,reinterpret_cast<HMENU>(3050),GetModuleHandleW(nullptr),nullptr);
    SetWindowSubclass(categoryBar,CategoryBarProc,512,0);categoryTips=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,categoryBar,nullptr,GetModuleHandleW(nullptr),nullptr);
    const wchar_t* labels[]{L"優先解除",L"すべて展開",L"すべて折り畳む"};for(int i=0;i<3;++i){auto button=CreateWindowExW(0,L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP,0,0,1,1,categoryBar,reinterpret_cast<HMENU>(INT_PTR(3100+i)),GetModuleHandleW(nullptr),nullptr);SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),TRUE);}
}
void RefreshCategoryBar(){
    app.personChoices.clear();if(app.categoryMode==2){auto labels=PersonLabels(app.personRecords,app.idNames.Snapshot());auto order=PersonOrder(labels);std::map<std::wstring,int> counts;for(auto& entry:labels)++counts[entry.second];
        for(auto& key:order){auto name=labels[key];if(counts[name]>1)name+=key.rfind(L"name:",0)==0?L"［IDなし］":L"［"+key+L"］";app.personChoices.push_back({key,name});}
        if(!app.priorityPerson.empty()&&!labels.count(app.priorityPerson)){app.priorityPerson.clear();expanded=savedExpanded;savedExpanded.clear();}}
    if(categoryBar){PositionPersonButtons();Layout();}
}
void LayoutCategoryBar(RECT& list){
    if(!categoryBar)return;bool show=app.mdbEnabled&&app.categoryMode;ShowWindow(categoryBar,show?SW_SHOW:SW_HIDE);if(!show)return;
    int height=CategoryHeaderHeight(std::max(1L,list.right-list.left-GetSystemMetrics(SM_CXVSCROLL)),false)+(app.categoryMode==2?2*(UiControlHeight()+2)+8:0);MoveWindow(categoryBar,list.left,list.top,std::max(1L,list.right-list.left),height,TRUE);list.top+=height;
}
void SetAllCategories(bool open){
    if(!app.list)return;for(auto& span:spans){ListView_SetGroupState(app.list,span.id,LVGS_COLLAPSED,open?0:LVGS_COLLAPSED);expanded[span.key]=open;}
    ScheduleThumbnails();InvalidateRect(app.list,nullptr,FALSE);
}
void ApplyPersonPriority(const std::wstring& key){
    if(app.categoryMode!=2||!app.mdbEnabled||app.sorting||app.searching||app.saving||app.backupBusy)return;
    if(!GuardTransition([key]{ApplyPersonPriority(key);}))return;FlushSelectionUpdate();CaptureCategoryState();
    auto chosen=key==app.priorityPerson?L"":key;auto labels=PersonLabels(app.personRecords,app.idNames.Snapshot());if(!chosen.empty()&&!labels.count(chosen))return;
    if(app.priorityPerson.empty()&&!chosen.empty())savedExpanded=expanded;
    if(!app.priorityPerson.empty())expanded=savedExpanded;
    app.priorityPerson=chosen;if(!chosen.empty())expanded[chosen]=true;else savedExpanded.clear();
    Result result;BuildPersonView(app.personRecords,app.idNames.Snapshot(),chosen,result);app.restoreSelection=SelectedFiles();auto preserve=app.selected>=0?app.files[app.selected]:L"";
    app.categoryCommitting=true;app.mdbRows=std::move(result.mdbRows);CommitDisplay(std::move(result.files),preserve);app.categoryCommitting=false;
    if(!chosen.empty())ListView_Scroll(app.list,0,-GetScrollPos(app.list,SB_VERT));Counts(chosen.empty()?L"人物の優先表示を解除":L"人物を優先表示");
}
bool MdbGroupInput(HWND list,UINT message,WPARAM wp,LPARAM point){
    if(!app.categoryMode||(message!=WM_LBUTTONDOWN&&message!=WM_LBUTTONDBLCLK&&message!=WM_KEYDOWN))return false;
    if(!ListView_IsGroupViewEnabled(list))return false;
    if(message==WM_KEYDOWN&&(wp==VK_LEFT||wp==VK_RIGHT)){int focused=int(SendMessageW(list,LVM_GETFOCUSEDGROUP,0,0));if(focused>=0&&focused<int(spans.size())){auto& span=spans[focused];bool open=wp==VK_RIGHT;expanded[span.key]=open;ListView_SetGroupState(list,span.id,LVGS_COLLAPSED,open?0:LVGS_COLLAPSED);ScheduleThumbnails();InvalidateRect(list,nullptr,FALSE);return true;}}
    if(message!=WM_LBUTTONDOWN&&message!=WM_LBUTTONDBLCLK)return false;POINT p{GET_X_LPARAM(point),GET_Y_LPARAM(point)};
    for(auto& span:spans){RECT rc{};if(ListView_GetGroupRect(list,span.id,LVGGR_HEADER,&rc)&&PtInRect(&rc,p)){
        if(message==WM_LBUTTONDOWN){bool open=!GroupExpanded(span.id);expanded[span.key]=open;ListView_SetGroupState(list,span.id,LVGS_COLLAPSED,open?0:LVGS_COLLAPSED);ListView_SetGroupState(list,span.id,LVGS_FOCUSED,LVGS_FOCUSED);SetFocus(list);ScheduleThumbnails();InvalidateRect(list,nullptr,FALSE);}return true;}}
    return false;
}
std::vector<int> MdbVisibleItems(int limit,bool& limited){
    std::vector<int> result;RECT client{};GetClientRect(app.list,&client);limited=false;
    for(auto& span:spans){if(!GroupExpanded(span.id))continue;RECT group{};if(!ListView_GetGroupRect(app.list,span.id,LVGGR_GROUP,&group)||group.bottom<0||group.top>client.bottom)continue;
        auto boundary=[&](bool end){int lo=span.first,hi=span.last+1;while(lo<hi){int mid=lo+(hi-lo)/2;RECT rc{};if(!ListView_GetItemRect(app.list,mid,&rc,LVIR_BOUNDS)){hi=mid;continue;}if(end?rc.top<=client.bottom:rc.bottom<0)lo=mid+1;else hi=mid;}return lo;};
        int first=boundary(false),last=boundary(true)-1;
        for(int i=first;i<=last;++i)if(!IsFolderItem(i)){if(int(result.size())>=limit){limited=true;break;}result.push_back(i);}
        if(limited)break;
    }return result;
}
bool WantedThumbnail(int index){return app.categoryMode&&ListView_IsGroupViewEnabled(app.list)?std::binary_search(app.wantedItems.begin(),app.wantedItems.end(),index):index>=app.wantedFirst&&index<=app.wantedLast;}

bool CategoryWheel(HWND hovered,WPARAM wp,LPARAM lp){if(categoryBar&&(hovered==categoryBar||IsChild(categoryBar,hovered))){SendMessageW(categoryBar,WM_MOUSEWHEEL,wp,lp);return true;}return false;}
