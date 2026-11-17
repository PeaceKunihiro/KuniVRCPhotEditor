#include "ViewerRuntime.h"
#include "CacheSettingsDialog.inl"
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
namespace {
void CompleteManualCache(bool cancelled){
    auto count=app.manualCacheCursor;
    auto text=(cancelled?std::wstring(L"キャッシュ追加を中止しました。"):std::wstring(L"キャッシュ追加が完了しました。"))+
        L"\r\n処理: "+std::to_wstring(count)+L" / "+std::to_wstring(app.manualCacheFiles.size())+
        L"枚";
    if(app.memoryCache)text+=L"\r\nメモリ: "+std::to_wstring(app.manualMemoryCount)+L"枚";
    if(app.manualCacheDisk)text+=L"\r\nディスク: "+std::to_wstring(app.manualDiskCount)+L"枚（未保持 "+std::to_wstring(count-app.manualDiskCount)+L"枚）";
    if(app.manualCacheMdb)text+=L"\r\nMDB: "+std::to_wstring(app.manualMdbCount)+L"枚（未保持 "+std::to_wstring(count-app.manualMdbCount)+L"枚、バックアップ画像は対象外）";
    text+=L"\r\n各件数は既存の保持分を含みます。";
    bool skipped=(app.memoryCache&&app.manualMemoryCount<count)||(app.manualCacheDisk&&app.manualDiskCount<count)||(app.manualCacheMdb&&app.manualMdbCount<count);
    if(skipped)text+=L"\r\n容量上限・優先対象・対象外画像・読取り／保存失敗により、保持できない画像があります。既存項目の追い出しは行いません。";
    if(app.manualCacheErrors)text+=L"\r\n読取り／処理失敗: "+std::to_wstring(app.manualCacheErrors)+L"枚";
    app.manualCacheActive=false;app.manualCacheWaiting=false;app.manualCacheFiles.clear();
    Counts(text);
    if(app.manualCacheNotify&&!cancelled&&!app.closing)MessageBoxW(app.window,text.c_str(),L"キャッシュする",MB_OK|(skipped?MB_ICONWARNING:MB_ICONINFORMATION));
}
}
void StartManualCache(bool notify){
    if(app.manualCacheActive||app.folder.empty()||app.folderPending||app.saving||app.backupBusy||app.sorting)return;
    if(!app.memoryCache&&!app.diskCache&&!app.mdbEnabled){Counts(L"キャッシュ設定で対象を有効にしてください。");return;}
    auto files=app.allFiles;
    if(files.empty()){MessageBoxW(app.window,L"表示中のフォルダーに対象画像がありません。",L"キャッシュする",MB_OK|MB_ICONINFORMATION);return;}
    SortFiles(files,0);
    std::wstring text=L"表示中フォルダー直下の画像をファイル名順にキャッシュへ追加します。サブフォルダーは含みません。\r\n対象: ";
    if(app.memoryCache)text+=L"メモリ";
    if(app.diskCache)text+=L"・ディスクサムネイル";
    if(app.mdbEnabled)text+=L"・MDB";
    text+=L"\r\nOFFのキャッシュ設定は変更しません。既存の保持分を残し、設定容量の空きに収まる画像のみ追加します。";
    if(app.bookmarkCache)text+=L"\r\nブックマーク優先は維持し、既存のブックマーク対象キャッシュも削除しません。";
    if(notify)MessageBoxW(app.window,text.c_str(),L"キャッシュする",MB_OK|MB_ICONINFORMATION);
    app.manualCacheFiles=std::move(files);app.manualCacheCursor=app.manualMemoryCount=app.manualDiskCount=app.manualMdbCount=app.manualCacheErrors=0;
    app.manualCacheNotify=notify;app.manualCacheThumbnailGeneration=app.thumbnailGeneration;
    app.manualCacheGeneration=app.generation;app.manualCacheDiskGeneration=app.diskGeneration;
    app.manualCacheDisk=app.diskCache;app.manualCacheMdb=app.mdbEnabled;
    app.manualCacheActive=true;app.manualCacheWaiting=false;
    Counts(L"キャッシュ追加を開始: "+std::to_wstring(app.manualCacheFiles.size())+L"枚");
    ScheduleBookmarkPrefetch();
}
void FinishManualCache(const Result& result){
    if(!app.manualCacheActive||result.job.generation!=app.manualCacheGeneration||result.job.diskGeneration!=app.manualCacheDiskGeneration)return;
    app.manualCacheWaiting=false;++app.manualCacheCursor;
    app.manualMemoryCount+=result.cacheMemory;app.manualDiskCount+=result.cacheDisk;app.manualMdbCount+=result.cacheMdb;
    if(!result.error.empty())++app.manualCacheErrors;
    Counts(L"キャッシュ追加中: "+std::to_wstring(app.manualCacheCursor)+L" / "+std::to_wstring(app.manualCacheFiles.size())+L"枚");
    ScheduleBookmarkPrefetch();
}
void ScheduleBookmarkPrefetch(){
    if(app.manualCacheActive){
        if(app.generation!=app.manualCacheGeneration||app.diskGeneration!=app.manualCacheDiskGeneration||app.thumbnailGeneration!=app.manualCacheThumbnailGeneration){CompleteManualCache(true);}
        else if(app.manualCacheCursor>=app.manualCacheFiles.size()){CompleteManualCache(false);return;}
        else {
            if(app.manualCacheWaiting||app.closing||app.stopping||app.saving||app.backupBusy||app.sorting)return;
            std::lock_guard<std::mutex> lock(app.mutex);
            if(app.thumbnailActive||std::any_of(app.jobs.begin(),app.jobs.end(),[](auto& j){return j.kind==1;}))return;
            Job job{1,app.generation,0,-1,app.manualCacheFiles[app.manualCacheCursor],L"",app.thumbnailGeneration,256};
            job.diskPrefetch=true;job.manualCache=true;job.diskGeneration=app.diskGeneration;
            job.manualDisk=app.manualCacheDisk;job.manualMdb=app.manualCacheMdb;
            app.manualCacheWaiting=true;app.jobs.push_back(std::move(job));app.wake.notify_all();return;
        }
    }
    if(app.closing||app.stopping||app.folderPending||app.saving||app.backupBusy||app.sorting||app.folder.empty())return;
    size_t limit=app.disk.PrefetchLimit(app.folder);if(!limit||app.diskPrefetchCount>=limit||app.diskPrefetchCursor>=app.allFiles.size())return;
    std::lock_guard<std::mutex> lock(app.mutex);
    if(app.thumbnailActive||std::any_of(app.jobs.begin(),app.jobs.end(),[](auto& j){return j.kind==1;}))return;
    Job job{1,app.generation,0,-1,app.allFiles[app.diskPrefetchCursor++],L"",app.thumbnailGeneration,256};job.diskPrefetch=true;job.diskGeneration=app.diskGeneration;
    ++app.diskPrefetchCount;app.jobs.push_back(std::move(job));app.wake.notify_all();
}
void ShowDiskCacheDialog(const std::function<void(HWND)>& inspect){ShowCacheSettings(1,inspect);}
