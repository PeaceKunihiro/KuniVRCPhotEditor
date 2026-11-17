namespace {
constexpr int enabledIds[]{2400,2100,2300};
constexpr int sectionIds[]{2410,2411,2412};
constexpr int usageIds[]{2420,2421,2422};
constexpr const wchar_t* sectionNames[]{L"サムネイル（メモリ）",L"ディスクキャッシュ",L"MDB"};
struct CacheDialogState {
    int section;
    std::function<void(HWND)> inspect;
};
bool Checked(HWND h,int id){return IsDlgButtonChecked(h,id)==BST_CHECKED;}
void SelectSection(HWND h,int section){
    for(int i=0;i<3;++i)
        SendDlgItemMessageW(h,sectionIds[i],BM_SETCHECK,i==section?BST_CHECKED:BST_UNCHECKED,0);
    for(int id:{2102,2103,2105,2106,2109,2110,2111,2112})
        ShowWindow(GetDlgItem(h,id),section==1?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(h,2304),section==2?SW_SHOW:SW_HIDE);
    ShowWindow(GetDlgItem(h,2430),section==0?SW_SHOW:SW_HIDE);
}
std::wstring UsageLabel(uint64_t bytes,uint64_t limit,uint64_t unit,const wchar_t* suffix){
    auto text=std::to_wstring(bytes/unit)+suffix;
    if(limit)text+=L" ("+std::to_wstring(bytes*100/limit)+L"%)";
    return text;
}
void RefreshCacheUsage(HWND h,int section){
    auto disk=app.disk.GetStatus();
    auto mdb=app.mdb.Status();
    SetDlgItemTextW(h,usageIds[0],UsageLabel(app.cache.Bytes()+app.imageBytes,uint64_t(app.cacheMB)*1000000,1000000,L" MB").c_str());
    SetDlgItemTextW(h,usageIds[1],(UsageLabel(disk.bytes,uint64_t(app.diskCacheMiB)*1048576,1048576,L" MiB")+L" / "+std::to_wstring(disk.entries)+L"枚").c_str());
    if(!app.mdbEnabled){
        mdb.bytes=0;
        for(auto name:{L"mdb.sqlite",L"mdb.sqlite-journal",L"mdb.sqlite-wal",L"mdb.sqlite-shm",L"owner.lock"}){
            std::error_code error;
            auto size=fs::file_size(app.settings.parent_path()/L"MetadataDB"/name,error);
            if(!error)mdb.bytes+=size;
        }
    }
    uint64_t limit=MdbConfig{app.mdbEnabled,app.mdbMode,app.mdbCapacityMB}.Limit();
    SetDlgItemTextW(h,usageIds[2],(UsageLabel(mdb.bytes,limit,1000000,L" MB")+(app.mdbEnabled?L" / "+std::to_wstring(mdb.entries)+L"枚":L"")).c_str());
    if(section!=1)return;
    std::vector<DiskThumbnailCache::FolderStatus> folders;
    if(!app.disk.GetFolders(folders))return;
    std::wstring listing;
    for(auto& f:folders)listing+=f.folder+L"  "+std::to_wstring(f.entries)+L"枚 / "+std::to_wstring((f.bytes+1023)/1024)+L" KiB\r\n";
    if(listing.empty())listing=L"保存済みサムネイルはありません。";
    if(Text(GetDlgItem(h,2110))!=listing)SetDlgItemTextW(h,2110,listing.c_str());
}
INT_PTR CALLBACK UnifiedCacheDialogProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
    auto state=reinterpret_cast<CacheDialogState*>(GetWindowLongPtrW(h,DWLP_USER));
    if(msg==WM_INITDIALOG){
        state=reinterpret_cast<CacheDialogState*>(lp);
        SetWindowLongPtrW(h,DWLP_USER,lp);
        ApplyUiFont(h);
        SetWindowTextW(h,L"キャッシュ設定");
        auto add=[&](const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int height,int id){
            auto c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,height,h,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(UiFont()),TRUE);
            return c;
        };
        add(L"STATIC",L"有効",0,12,8,50,22,0);
        add(L"STATIC",L"項目（選択して詳細を表示）",0,64,8,215,22,0);
        add(L"STATIC",L"容量上限",0,290,8,170,22,0);
        add(L"STATIC",L"使用量（適用中の上限比）",0,490,8,200,22,0);
        bool enabled[]{app.memoryCache,app.diskCache,app.mdbEnabled};
        for(int i=0;i<3;++i){
            int y=38+i*42;
            add(L"BUTTON",L"",BS_AUTOCHECKBOX|WS_TABSTOP,18,y,36,28,enabledIds[i]);
            CheckDlgButton(h,enabledIds[i],enabled[i]?BST_CHECKED:BST_UNCHECKED);
            add(L"BUTTON",sectionNames[i],BS_RADIOBUTTON|BS_PUSHLIKE|WS_TABSTOP,64,y,208,28,sectionIds[i]);
            add(L"STATIC",L"",0,490,y+3,200,26,usageIds[i]);
        }
        auto number=[&](int id,int value,int x,int y,int w){
            add(L"EDIT",std::to_wstring(value).c_str(),ES_NUMBER|WS_BORDER|WS_TABSTOP,x,y,w,26,id);
        };
        number(2401,app.cacheMB,290,38,74);
        add(L"STATIC",L"MB",0,370,41,50,22,0);
        auto combo=[&](int id,int x,int y,int width){return add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,x,y,width,160,id);};
        auto disk=combo(2101,290,80,125);
        for(int mb:{128,256,512,1024,2048}){
            auto label=std::to_wstring(mb)+L" MiB";
            auto i=SendMessageW(disk,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(disk,CB_SETITEMDATA,i,mb);
            if(mb==app.diskCacheMiB)SendMessageW(disk,CB_SETCURSEL,i,0);
        }
        auto modes=combo(2301,290,122,86);
        for(auto label:{L"固定",L"自動 4GB",L"無制限"})SendMessageW(modes,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(modes,CB_SETCURSEL,app.mdbMode,0);
        number(2302,app.mdbCapacityMB,379,122,66);
        add(L"STATIC",L"MB",0,449,125,38,22,0);
        EnableWindow(GetDlgItem(h,2302),app.mdbMode==0);
        add(L"STATIC",L"メモリ上限には一覧表示用の領域が含まれます。無効にすると再利用用の領域を解放します。",0,14,187,660,55,2430);
        add(L"BUTTON",L"ブックマーク優先",BS_AUTOCHECKBOX|WS_TABSTOP,14,176,190,28,2106);
        CheckDlgButton(h,2106,app.bookmarkCache?BST_CHECKED:BST_UNCHECKED);
        add(L"STATIC",L"判定期間（分）",0,214,179,105,24,2111);
        number(2102,app.diskCacheMinutes,325,176,65);
        add(L"STATIC",L"ロード回数",0,411,179,96,24,2112);
        number(2103,app.diskCacheVisits,510,176,65);
        add(L"STATIC",L"保存済みフォルダー（サムネイルのみ）",0,14,216,450,24,2109);
        add(L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_BORDER|WS_TABSTOP,14,246,658,104,2110);
        add(L"BUTTON",L"キャッシュ削除…",WS_TABSTOP,14,362,160,28,2105);
        add(L"BUTTON",L"ID・表示名の編集…",WS_TABSTOP,14,178,195,30,2304);
        EnableWindow(GetDlgItem(h,2304),app.mdbEnabled);
        add(L"BUTTON",L"適用",BS_DEFPUSHBUTTON|WS_TABSTOP,452,410,100,30,IDOK);
        add(L"BUTTON",L"キャンセル",WS_TABSTOP,565,410,106,30,IDCANCEL);
        RECT r{0,0,695,455};AdjustWindowRectEx(&r,DWORD(GetWindowLongPtrW(h,GWL_STYLE)),FALSE,0);
        SetWindowPos(h,nullptr,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);
        ScaleUiDialog(h);SelectSection(h,state->section);RefreshCacheUsage(h,state->section);
        SetFocus(GetDlgItem(h,sectionIds[state->section]));SetTimer(h,1,500,nullptr);
        if(state->inspect)SetTimer(h,2,150,nullptr);
        return FALSE;
    }
    if(msg==WM_TIMER&&state){
        if(wp==2){KillTimer(h,2);state->inspect(h);if(IsWindow(h))EndDialog(h,IDCANCEL);}
        else RefreshCacheUsage(h,state->section);
        return TRUE;
    }
    if(msg==WM_CLOSE){EndDialog(h,IDCANCEL);return TRUE;}
    if(msg!=WM_COMMAND||!state)return FALSE;
    int id=LOWORD(wp);
    for(int i=0;i<3;++i)if(id==sectionIds[i]||id==enabledIds[i]){
        state->section=i;SelectSection(h,i);
        if(id==2100&&!Checked(h,2100))CheckDlgButton(h,2106,BST_UNCHECKED);
        EnableWindow(GetDlgItem(h,2304),app.mdbEnabled&&Checked(h,2300));
        return TRUE;
    }
    if(id==2301){EnableWindow(GetDlgItem(h,2302),SendDlgItemMessageW(h,2301,CB_GETCURSEL,0,0)==0);return TRUE;}
    if(id==2106&&Checked(h,2106)){CheckDlgButton(h,2100,BST_CHECKED);return TRUE;}
    if(id==2304){ShowIdNamesDialog(h);return TRUE;}
    if(id==2105){if(MessageBoxW(h,L"保存したディスクサムネイルを削除しますか？",L"キャッシュ削除",MB_YESNO|MB_DEFBUTTON2)==IDYES){++app.diskGeneration;app.disk.Clear();}return TRUE;}
    if(id==IDCANCEL){EndDialog(h,IDCANCEL);return TRUE;}
    if(id!=IDOK)return FALSE;
    int memory=GetDlgItemInt(h,2401,nullptr,FALSE),minutes=GetDlgItemInt(h,2102,nullptr,FALSE),visits=GetDlgItemInt(h,2103,nullptr,FALSE);
    int mode=int(SendDlgItemMessageW(h,2301,CB_GETCURSEL,0,0));
    int capacity=mode==0?GetDlgItemInt(h,2302,nullptr,FALSE):app.mdbCapacityMB;
    if(memory<16||memory>2048||minutes<1||minutes>1440||visits<2||visits>100||capacity<100||capacity>4000){
        MessageBoxW(h,L"メモリは16～2048 MB、MDBは100～4000 MB、判定期間は1～1440分、ロード回数は2～100で指定してください。",L"設定値を確認してください",MB_OK);return TRUE;
    }
    bool mem=Checked(h,2400),disk=Checked(h,2100),mdb=Checked(h,2300),priority=disk&&Checked(h,2106);
    int selected=int(SendDlgItemMessageW(h,2101,CB_GETCURSEL,0,0));
    int diskMB=int(SendDlgItemMessageW(h,2101,CB_GETITEMDATA,selected,0));
    std::wstring disabled;
    bool before[]{app.memoryCache,app.diskCache,app.mdbEnabled},after[]{mem,disk,mdb};
    for(int i=0;i<3;++i)if(before[i]&&!after[i]){if(!disabled.empty())disabled+=L"、";disabled+=sectionNames[i];}
    if(memory!=app.cacheMB){ChangeCacheLimit(memory);if(memory!=app.cacheMB)return TRUE;}
    if(mem!=app.memoryCache){
        app.memoryCache=mem;++app.diskGeneration;
        app.cache.SetLimit(mem?size_t(memory)*1000000-app.imageBytes:0);
    }
    if(disk!=app.diskCache||priority!=app.bookmarkCache||diskMB!=app.diskCacheMiB||minutes!=app.diskCacheMinutes||visits!=app.diskCacheVisits){
        app.diskCache=disk;app.bookmarkCache=priority;app.diskCacheMiB=diskMB;
        app.diskCacheMinutes=minutes;app.diskCacheVisits=visits;ConfigureDiskCache();
    }
    bool refresh=mdb!=app.mdbEnabled||mode!=app.mdbMode||capacity!=app.mdbCapacityMB;
    if(refresh){
        CancelSearch();app.mdbEnabled=mdb;app.mdbMode=mode;app.mdbCapacityMB=capacity;
        if(!mdb)app.categoryMode=0;
        ConfigureMdb();
    }
    SaveSettings();
    if(!disabled.empty()&&!state->inspect)MessageBoxW(h,(disabled+L"を無効にしました。\r\n保存済みのキャッシュは自動削除されません").c_str(),L"キャッシュ設定",MB_OK|MB_ICONINFORMATION);
    EndDialog(h,IDOK);
    if(refresh&&!app.folder.empty())StartSearch(app.appliedQuery);
    return TRUE;
}
}
void ShowCacheSettings(int section,const std::function<void(HWND)>& inspect){
    if(app.saving||app.backupBusy||app.sorting||app.batchLoading)return;
    FlushSelectionUpdate();
    if(!GuardTransition([section,inspect]{ShowCacheSettings(section,inspect);}))return;
    CacheDialogState state{section,inspect};
    struct Template{DLGTEMPLATE d;WORD menu,cls,title;}t{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER,0,0,0,0,370,300},0,0,0};
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.d,app.window,UnifiedCacheDialogProc,reinterpret_cast<LPARAM>(&state));
}
