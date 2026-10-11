#include "ViewerRuntime.h"
void ScheduleSelectionUpdate(){if(app.selectionPending)return;app.selectionPending=true;app.metadataReady=false;if(app.batchLoading)StopActivity();app.batchLoading=false;++app.selection;app.currentSelection=app.selection;PostMessageW(app.window,WM_SELECTION,0,0);}
void FlushSelectionUpdate(){if(!app.selectionPending)return;app.selectionPending=false;int index=ListView_GetNextItem(app.list,-1,LVNI_FOCUSED);if(index<0||!(ListView_GetItemState(app.list,index,LVIS_SELECTED)&LVIS_SELECTED))index=ListView_GetNextItem(app.list,-1,LVNI_SELECTED);Select(index,false);}
void Select(int index,bool reveal) {
    app.selectionPending=false;++app.selectionRefreshes;bool wasEditing=app.editing;
    if(app.batchLoading)StopActivity();app.batchLoading=false;app.batchFiles.clear();app.batchMetadata.clear();app.batchCompleted.clear();
    app.editing=false; app.metadataReady=false; app.previewFailed=false; app.editError.clear(); app.editable={}; SetWindowTextW(app.editStatus,L"PNG/JPEG/WebPの上書き・別名コピー保存に対応。");UpdateEditorControls();if(wasEditing)Layout();
    app.selected = index; app.previewPixels = {}; ++app.selection; app.currentSelection = app.selection;
    InvalidateRect(app.preview, nullptr, TRUE);
    if (index < 0) { SetWindowTextW(app.details, L"画像を選択してください。"); ShowSummary({}, L"画像を選択してください。"); Counts(); return; }
    app.previewPixels=DisplayedThumbnail(index);
    try { if(app.previewPixels.data.empty())app.cache.Get(ThumbnailCache::Key(app.files[index]), app.previewPixels); } catch (...) {}
    InvalidateRect(app.preview,nullptr,TRUE);
    ShowSummary({}, L"メタデータを読み込んでいます…");
    SetWindowTextW(app.details, L"プレビュー・メタデータを読み込んでいます…"); Counts(L"読込み中");
    { std::lock_guard<std::mutex> lock(app.mutex); app.jobs.erase(std::remove_if(app.jobs.begin(), app.jobs.end(), [](auto& j) { return j.kind == 2; }), app.jobs.end()); }
    Queue({ 2, app.generation, app.selection, index, app.files[index], L"" }, true);
    if(reveal)ListView_EnsureVisible(app.list,index,FALSE);ScheduleThumbnails();
}
void OpenFolder(const std::wstring& path, bool reload, const std::wstring& preserveOverride) {
    FlushSelectionUpdate();
    if (!GuardTransition([path,reload,preserveOverride]{OpenFolder(path,reload,preserveOverride);})) return;
    if(app.batchLoading){StopActivity();app.batchLoading=false;}CancelSearch();if(!reload){app.appliedQuery.clear();SetWindowTextW(app.searchEdit,L"");}
    app.editing=false; UpdateEditorControls(); Layout();
    try {
        auto normalized = Normalize(path);
        ++app.generation; app.currentGeneration = app.generation;
        if (reload) app.cache.Clear();
        app.folderPending = true;
        { std::lock_guard<std::mutex> lock(app.mutex); app.jobs.erase(std::remove_if(app.jobs.begin(), app.jobs.end(), [](auto& j) { return j.kind != 3; }), app.jobs.end()); }
        // The visible list is committed only after enumeration succeeds.
        auto preserve = !preserveOverride.empty()?preserveOverride:reload && app.selected >= 0 ? app.files[app.selected] : L"";
        Queue({ 0, app.generation, 0, -1, normalized, preserve }, true); Counts(L"フォルダーを読み込んでいます…");
    } catch (...) { Counts(L"無効なフォルダーパスです。現在の一覧を保持します。"); }
}

void StartSave() {
    FlushSelectionUpdate();
    if(!app.editing||app.saving||app.backupBusy||app.selected<0)return;
    if(!app.batchFiles.empty()){StartBatchSave();return;}
    auto edit=app.editorUi.Read();if(!edit.Changed())return;auto error=edit.Validate();if(!error.empty()){SetWindowTextW(app.editStatus,error.c_str());return;}
    std::wstring destination;
    int mode=2;
    if(app.chooseSaveMode)mode=app.chooseSaveMode();
    else if(!app.chooseSavePath){TASKDIALOG_BUTTON buttons[]{{1,L"上書き保存"},{2,L"別名コピーとして保存"},{3,L"キャンセル"}};TASKDIALOGCONFIG c{sizeof(c)};c.hwndParent=app.window;c.pszWindowTitle=L"保存方法";c.pszMainInstruction=L"画像の保存方法を選択してください。";c.pszContent=app.autoBackup?L"上書き前の画像をAB_バックアップへ保存します。":L"自動バックアップはOFFです。上書きでは復元用コピーを作成しません。";c.cButtons=3;c.pButtons=buttons;c.nDefaultButton=2;c.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION;TaskDialogIndirect(&c,&mode,nullptr,nullptr);}
    if(mode!=1&&mode!=2){app.afterSave={};return;}
    bool overwrite=mode==1;
    ComPtr<IFileSaveDialog> dialog;auto hr=S_OK;
    auto source=app.files[app.selected];auto ext=fs::path(source).extension().wstring();auto name=fs::path(source).stem().wstring()+L"_edited"+ext;
    if(overwrite)destination=source;
    else if(app.chooseSavePath){destination=app.chooseSavePath();if(destination.empty()){app.afterSave={};return;}}
    else {
    hr=CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
    if(SUCCEEDED(hr)){DWORD options=0;dialog->GetOptions(&options);dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);dialog->SetFileName(name.c_str());dialog->SetDefaultExtension(ext.c_str()+1);
        ComPtr<IShellItem> folder;if(SUCCEEDED(SHCreateItemFromParsingName(fs::path(source).parent_path().c_str(),nullptr,IID_PPV_ARGS(&folder))))dialog->SetFolder(folder.Get());hr=dialog->Show(app.window);}
    if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED)){app.afterSave={};return;}
    ComPtr<IShellItem> item;if(SUCCEEDED(hr))hr=dialog->GetResult(&item);PWSTR path=nullptr;if(SUCCEEDED(hr))hr=item->GetDisplayName(SIGDN_FILESYSPATH,&path);
    if(FAILED(hr)){app.afterSave={};SetWindowTextW(app.editStatus,ErrorText(hr).c_str());return;}
    destination=path;CoTaskMemFree(path);
    }
    std::error_code destinationError;bool exists=fs::exists(destination,destinationError);
    if(destinationError){app.afterSave={};SetWindowTextW(app.editStatus,L"保存先を確認できません。アクセス権とパスを確認してください。");return;}
    if(exists&&!overwrite){app.afterSave={};SetWindowTextW(app.editStatus,L"既存ファイルには上書きできません。別の名前を指定してください。");return;}
    Job job{4,app.generation,app.selection,app.selected,source,L""};job.edit=edit;job.fingerprint=app.editable.fingerprint;job.destination=destination;job.overwrite=overwrite;job.autoBackup=app.autoBackup;job.backupLimit=app.backupLimit;job.preserve=(app.settings.parent_path()/L"backups.json").wstring();
    CancelSearch();SetActivity(L"保存中：コピー検証・上書き処理を実行しています…");app.saving=true;app.editorUi.Enable(false);SetWindowTextW(app.editStatus,L"保存中…コピーを検証しています。");UpdateEditorControls();Queue(std::move(job),true);
}
bool GuardTransition(std::function<void()> action) {
    if(app.transitionBypass)return true;if(app.saving||app.backupBusy)return false;
    if(!app.editing || !app.editorUi.Read().Changed())return true;
    TASKDIALOG_BUTTON buttons[]{{1,L"保存"},{2,L"破棄"},{3,L"キャンセル"}};
    TASKDIALOGCONFIG config{sizeof(config)};config.hwndParent=app.window;config.pszWindowTitle=L"未保存の変更";config.pszMainInstruction=L"編集内容を保存しますか？";config.cButtons=3;config.pButtons=buttons;config.nDefaultButton=3;config.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION;
    int choice=3;if(app.confirmChanges)choice=app.confirmChanges();else TaskDialogIndirect(&config,&choice,nullptr,nullptr);
    if(choice==2){app.editing=false;UpdateEditorControls();Layout();return true;}
    if(choice==1){app.afterSave=std::move(action);StartSave();if(!app.saving)app.afterSave={};}
    return false;
}

void ResetViewer() {
    if(app.saving||app.backupBusy)return;
    FlushSelectionUpdate();
    if(!GuardTransition([]{ResetViewer();}))return;
    CancelSearch();StopActivity();
    ++app.generation;app.currentGeneration=app.generation;
    {std::lock_guard<std::mutex> lock(app.mutex);app.jobs.erase(std::remove_if(app.jobs.begin(),app.jobs.end(),[](auto& j){return j.kind!=3;}),app.jobs.end());}
    app.wake.notify_all();app.folderPending=false;app.folder.clear();app.last.clear();app.allFiles.clear();app.restoreSelection.clear();app.appliedQuery.clear();
    SetWindowTextW(app.path,L"");SetWindowTextW(app.searchEdit,L"");SendMessageW(app.history,CB_SETCURSEL,-1,0);
    CommitDisplay({},L"");app.selectionPending=false;app.cache.Clear();
    Layout();SaveSettings();Counts(L"フォルダーを選択してください。");
}
