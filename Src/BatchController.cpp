#include "ViewerRuntime.h"
#include <imm.h>
#include <future>
#include <set>
#pragma comment(lib,"imm32.lib")
std::vector<std::wstring> SelectedFiles() {
    std::vector<std::wstring> files;int index=-1;
    while((index=ListView_GetNextItem(app.list,index,LVNI_SELECTED))>=0)
        if(index<static_cast<int>(app.files.size()))files.push_back(app.files[index]);
    return files;
}
void BeginMetadataEdit() {
    FlushSelectionUpdate();
    if(app.saving||app.backupBusy||app.batchLoading||!GuardTransition([]{BeginMetadataEdit();}))return;
    auto files=SelectedFiles();if(files.empty())return;
    app.batchFiles.clear();app.batchMetadata.clear();app.batchCompleted.clear();
    if(files.size()==1) {
        if(!app.metadataReady)return;app.editing=true;app.editorUi.Load(app.editable);
        SetWindowTextW(app.editStatus,L"編集中（原本は保存まで変更しません）。");UpdateEditorControls();Layout();return;
    }
    CancelSearch();app.batchLoading=true;Job job{10,app.generation,app.selection};job.files=files;
    SetActivity(L"編集用メタデータ読込み中 0 / "+std::to_wstring(files.size())+L"件");Queue(std::move(job));UpdateEditorControls();
}
void QueueProgress(Result result) {
    if(!app.window)return;
    auto matches=[&](const Result& prior){return prior.progress&&prior.job.kind==result.job.kind&&prior.job.generation==result.job.generation&&prior.job.selection==result.job.selection&&prior.job.searchGeneration==result.job.searchGeneration;};
    std::unique_lock<std::mutex> lock(app.mutex);
    app.wake.wait(lock,[&]{return app.stopping||app.results.size()<32||std::any_of(app.results.begin(),app.results.end(),matches);});
    if(app.stopping)return;
    bool notify=app.results.empty();
    auto prior=std::find_if(app.results.begin(),app.results.end(),matches);
    if(prior!=app.results.end())*prior=std::move(result);
    else app.results.push_back(std::move(result));
    if(notify)PostMessageW(app.window,WM_RESULTS,0,0);
}
void BatchProgress(const Job& job,const std::wstring& text) {
    Result progress;progress.job.kind=job.kind;progress.job.generation=job.generation;progress.job.selection=job.selection;progress.job.searchGeneration=job.searchGeneration;progress.progress=true;progress.text=text;
    QueueProgress(std::move(progress));
}

namespace {
void CheckSource(const std::wstring& path) {
    auto attr=GetFileAttributesW(path.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES||(attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))throw std::runtime_error("Missing image or link; remove it from selection");
}
}
void RunBatchJob(const Job& job,Result& result,const std::function<void(size_t)>& afterFile,const std::function<void(size_t)>& beforePrepare) {
    if(job.kind==10) {
        result.metadata.resize(job.files.size());
        std::atomic<size_t> next{0},done{0};std::atomic<bool> failed{false};
        std::vector<std::exception_ptr> errors(job.files.size());std::vector<std::future<void>> workers;
        for(size_t worker=0;worker<std::min(size_t(2),job.files.size());++worker)workers.push_back(std::async(std::launch::async,[&]{
            auto hr=RoInitialize(RO_INIT_MULTITHREADED);
            while(!app.stopping&&!failed&&job.selection==app.currentSelection&&job.generation==app.currentGeneration){
                auto i=next.fetch_add(1);if(i>=job.files.size())break;
                struct Busy{Busy(){auto n=++app.batchReadActive;auto p=app.batchReadPeak.load();while(p<n&&!app.batchReadPeak.compare_exchange_weak(p,n)){}}~Busy(){--app.batchReadActive;}} busy;
                try{winrt::check_hresult(hr);if(beforePrepare)beforePrepare(i);CheckSource(job.files[i]);result.metadata[i]=InspectEditable(job.files[i]);
                    auto count=++done;if(count%4==0||count==job.files.size())BatchProgress(job,L"編集用メタデータ読込み中 "+std::to_wstring(count)+L" / "+std::to_wstring(job.files.size())+L"件");
                }catch(...){errors[i]=std::current_exception();failed=true;}
            }
            if(SUCCEEDED(hr))RoUninitialize();
        }));
        for(auto& worker:workers)worker.get();
        for(size_t i=0;i<errors.size();++i)if(errors[i]){result.metadata.clear();try{std::rethrow_exception(errors[i]);}catch(const std::exception& e){throw std::runtime_error(winrt::to_string(fs::path(job.files[i]).filename().wstring())+": "+e.what());}}
        if(done!=job.files.size())result.metadata.clear();
        return;
    }
    if(job.edit.editCandidates)throw std::runtime_error("Model candidates can only be edited for a single image");
    if(job.edit.actions[0]!=EditAction::Keep)throw std::runtime_error("Batch editing cannot change capture date");
    if(job.files.size()!=job.metadata.size()||job.files.size()!=job.destinations.size())throw std::runtime_error("Invalid batch snapshot");
    auto validation=job.edit.Validate();if(!validation.empty())throw std::runtime_error(winrt::to_string(validation));
    // Validate every input and destination before writing the first image.
    struct DestinationLess{bool operator()(const std::wstring& a,const std::wstring& b)const{return _wcsicmp(a.c_str(),b.c_str())<0;}};
    std::set<std::wstring,DestinationLess> destinations;
    for(size_t i=0;i<job.files.size();++i) {
        if(app.cancelBatch||app.stopping){result.text=L"書込み前に中止しました。";return;}
        BatchProgress(job,L"保存前確認 "+std::to_wstring(i)+L" / "+std::to_wstring(job.files.size())+L"件");auto& path=job.files[i];CheckSource(path);auto current=InspectEditable(path);
        if(current.fingerprint!=job.metadata[i].fingerprint)throw std::runtime_error(winrt::to_string(path)+": Source changed; reload before saving");
        if(job.overwrite && GetFileAttributesW(path.c_str())&FILE_ATTRIBUTE_READONLY)throw std::runtime_error(winrt::to_string(path)+": Original is read only");
        if(!job.overwrite&&fs::exists(job.destinations[i]))throw std::runtime_error(winrt::to_string(job.destinations[i])+": Destination exists");
        if(!job.overwrite)ValidateMetadataDestination(current,job.destinations[i]);
        if(!destinations.insert(job.destinations[i]).second)throw std::runtime_error("Duplicate batch destinations");
    }
    struct Prepared {std::wstring path,fingerprint;std::exception_ptr error;std::wstring* warnings;explicit Prepared(std::wstring* output):warnings(output){}~Prepared(){if(!path.empty()&&!DeleteFileW(path.c_str())&&GetLastError()!=ERROR_FILE_NOT_FOUND)*warnings+=L"未確定コピーを削除できませんでした: "+path+L"\r\n";}};
    for(size_t first=0;first<job.files.size()&&result.error.empty();first+=2) {
        if(app.cancelBatch||app.stopping){result.text+=L"保存を中止しました。";break;}
        BatchProgress(job,L"保存用コピー準備 "+std::to_wstring(first)+L" / "+std::to_wstring(job.files.size())+L"件");
        std::vector<std::shared_ptr<Prepared>> ready;std::vector<std::future<void>> pending;
        for(size_t i=first;i<std::min(first+2,job.files.size());++i) {
            auto prepared=std::make_shared<Prepared>(&result.text);ready.push_back(prepared);
            pending.push_back(std::async(std::launch::async,[&,i,prepared]{
                auto hr=RoInitialize(RO_INIT_MULTITHREADED);
                try {winrt::check_hresult(hr);if(beforePrepare)beforePrepare(i);GUID id{};winrt::check_hresult(CoCreateGuid(&id));wchar_t name[40]{};StringFromGUID2(id,name,40);
                    auto destination=fs::path(job.destinations[i]);prepared->path=(destination.parent_path()/(L".Kuni-batch-"+std::wstring(name)+destination.extension().wstring())).wstring();
                    SaveMetadataCopy(job.files[i],prepared->path,job.metadata[i].fingerprint,job.edit);prepared->fingerprint=MetadataFingerprint(prepared->path);
                }catch(...){prepared->error=std::current_exception();}
                if(SUCCEEDED(hr))RoUninitialize();
            }));
        }
        for(auto& worker:pending)worker.get(); // At most two image buffers; all tasks finish before scope cleanup.
        for(size_t offset=0;offset<ready.size();++offset) {
            size_t i=first+offset;auto& prepared=*ready[offset];
            if(app.cancelBatch||app.stopping){result.text+=L"保存を中止しました。";break;}
            try {
                if(prepared.error)std::rethrow_exception(prepared.error);
                if(job.overwrite)result.text+=SaveMetadataOverwrite(job.files[i],job.metadata[i].fingerprint,job.edit,job.autoBackup,job.backupLimit,job.destination,prepared.path,prepared.fingerprint,true);
                else {CheckSource(job.files[i]);if(MetadataFingerprint(job.files[i])!=job.metadata[i].fingerprint)throw std::runtime_error("Source changed; reload before saving");
                    if(MetadataFingerprint(prepared.path)!=prepared.fingerprint)throw std::runtime_error("Prepared copy changed");
                    if(!MoveFileExW(prepared.path.c_str(),job.destinations[i].c_str(),MOVEFILE_WRITE_THROUGH))winrt::throw_last_error();}
                prepared.path.clear();result.files.push_back(job.destinations[i]);if(afterFile)afterFile(i);
                BatchProgress(job,L"一括保存済み: "+std::to_wstring(result.files.size())+L" / "+std::to_wstring(job.files.size()));
            }catch(const winrt::hresult_error& e){result.error=ErrorText(e.code());break;}
            catch(const std::exception& error){auto detail=std::wstring(winrt::to_hstring(error.what()).c_str());if(detail.find(L"復旧用原本")!=std::wstring::npos)result.text+=detail;result.error=L"保存できませんでした。入力の変更・アクセス権・空き容量を確認してください。";break;}
        }
        if(app.cancelBatch||app.stopping)break;
    }
    if(job.overwrite&&job.autoBackup&&!result.files.empty()) {
        BatchProgress(job,L"バックアップ整理中…");
        try{result.text+=PruneBackups(fs::path(job.files.front()).parent_path().wstring(),job.backupLimit,job.destination,job.files);}
        catch(...){result.text+=L"バックアップ整理を確認してください。";}
    }
}
void StartBatchSave() {
    auto edit=app.editorUi.Read();if(!edit.Changed()||!edit.Validate().empty()||app.batchFiles.empty())return;
    int mode=2;
    if(app.chooseSaveMode)mode=app.chooseSaveMode();
    else {TASKDIALOG_BUTTON buttons[]{{1,L"上書き保存"},{2,L"別名コピーとして保存"},{3,L"キャンセル"}};TASKDIALOGCONFIG c{sizeof(c)};c.hwndParent=app.window;c.pszWindowTitle=L"一括保存方法";c.pszMainInstruction=L"選択画像の保存方法を選択してください。";c.cButtons=3;c.pButtons=buttons;c.nDefaultButton=2;c.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION;TaskDialogIndirect(&c,&mode,nullptr,nullptr);}
    if(mode!=1&&mode!=2){app.afterSave={};return;}
    std::wstring folder;
    if(mode==2) {
        if(app.chooseBatchFolder)folder=app.chooseBatchFolder();
        else {ComPtr<IFileOpenDialog> dialog;auto hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));
            if(SUCCEEDED(hr)){DWORD options{};dialog->GetOptions(&options);dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);hr=dialog->Show(app.window);}
            if(SUCCEEDED(hr)){ComPtr<IShellItem> item;hr=dialog->GetResult(&item);PWSTR path{};if(SUCCEEDED(hr))hr=item->GetDisplayName(SIGDN_FILESYSPATH,&path);if(SUCCEEDED(hr)){folder=path;CoTaskMemFree(path);}}
            if(FAILED(hr)&&hr!=HRESULT_FROM_WIN32(ERROR_CANCELLED))SetWindowTextW(app.editStatus,ErrorText(hr).c_str());}
        if(folder.empty()){app.afterSave={};return;}
    }
    Job job{11,app.generation,app.selection};job.files=app.batchFiles;job.metadata=app.batchMetadata;job.edit=edit;
    job.overwrite=mode==1;job.autoBackup=app.autoBackup;job.backupLimit=app.backupLimit;job.destination=(app.settings.parent_path()/L"backups.json").wstring();
    std::wstring names;
    for(auto& source:job.files){auto p=fs::path(source);job.destinations.push_back(job.overwrite?source:(fs::path(folder)/(p.stem().wstring()+L"_edited"+p.extension().wstring())).wstring());names+=p.filename().wstring()+L"\r\n";}
    const wchar_t* fields[]{L"日時",L"撮影者",L"ワールド名",L"ワールドID",L"モデル",L"タグ"};std::wstring changes;
    for(size_t i=1;i<edit.actions.size();++i)if(edit.actions[i]!=EditAction::Keep){changes+=fields[i];changes+=edit.actions[i]==EditAction::Remove?L"：削除\r\n":edit.actions[i]==EditAction::Append?L"：追加\r\n":L"：設定\r\n";}
    bool confirmed=false;
    if(app.confirmBatch)confirmed=app.confirmBatch(job);
    else {TASKDIALOGCONFIG c{sizeof(c)};c.hwndParent=app.window;c.pszWindowTitle=L"一括保存の確認";auto title=std::to_wstring(job.files.size())+L"画像へ変更を適用しますか？";c.pszMainInstruction=title.c_str();auto text=changes+(job.overwrite?L"上書き保存":L"別名コピー保存")+L"\r\n"+(job.overwrite?(job.autoBackup?L"自動バックアップ：ON":L"自動バックアップ：OFF"):L"保存先："+folder)+L"\r\n途中で失敗した場合、成功済みの画像は保存されたままです。";c.pszContent=text.c_str();c.pszExpandedInformation=names.c_str();c.dwCommonButtons=TDCBF_YES_BUTTON|TDCBF_NO_BUTTON;c.nDefaultButton=IDNO;int answer=IDNO;TaskDialogIndirect(&c,&answer,nullptr,nullptr);confirmed=answer==IDYES;}
    if(!confirmed){app.afterSave={};return;}
    CancelSearch();SetActivity(L"保存前確認 0 / "+std::to_wstring(job.files.size())+L"件");app.cancelBatch=false;app.saving=true;app.editorUi.Enable(false);UpdateEditorControls();Queue(std::move(job));
}
std::wstring BatchResultCounts(size_t total,size_t success,bool failed){size_t failures=failed&&success<total?1:0;return L"対象: "+std::to_wstring(total)+L"件\r\n成功: "+std::to_wstring(success)+L"件\r\n失敗: "+std::to_wstring(failures)+L"件\r\n未処理: "+std::to_wstring(total-success-failures)+L"件";}
bool HandleBatchResult(Result& result) {
    auto& job=result.job;if(job.kind!=10&&job.kind!=11)return false;
    if(job.kind==10) {
        if(job.selection!=app.selection||job.generation!=app.generation)return true;
        if(result.progress){if(app.batchLoading)SetActivity(result.text);return true;}
        StopActivity();app.batchLoading=false;
        if(result.error.empty()&&result.metadata.size()==job.files.size()) {
            app.batchFiles=job.files;app.batchMetadata=std::move(result.metadata);app.editing=true;app.editorUi.LoadBatch(app.batchMetadata);
            SetWindowTextW(app.editStatus,(std::to_wstring(app.batchFiles.size())+L"画像を一括編集中（日時は変更しません）。").c_str());
        }else SetWindowTextW(app.editStatus,(L"一括編集不可："+result.error+L" 対象から外して再実行してください。").c_str());
        UpdateEditorControls();Layout();return true;
    }
    if(result.progress){if(app.saving)SetActivity(result.text);return true;}
    StopActivity();app.saving=false;app.editorUi.Enable(true);
    app.batchCompleted.insert(app.batchCompleted.end(),result.files.begin(),result.files.end());
    auto completed=result.files.size();std::wstring report=BatchResultCounts(job.files.size(),completed,!result.error.empty());
    if(!result.error.empty())report+=L"\r\n保存できませんでした。入力の変更・アクセス権・空き容量を確認してください。";
    if(!result.text.empty())report+=L"\r\n中止またはバックアップに関する通知があります。";
    // Recovery locations must remain available independently of the count-only result.
    if(!result.text.empty()&&!app.confirmBatch)MessageBoxW(app.window,result.text.c_str(),L"保存に関する通知",MB_OK|MB_ICONINFORMATION);
    if(completed<job.files.size()) {
        app.afterSave={};app.batchFiles.assign(job.files.begin()+completed,job.files.end());app.batchMetadata.assign(job.metadata.begin()+completed,job.metadata.end());
        SetWindowTextW(app.editStatus,(L"一括保存停止。残り "+std::to_wstring(app.batchFiles.size())+L"件を再試行できます。").c_str());UpdateEditorControls();
    }else {
        auto next=std::move(app.afterSave);app.afterSave={};app.editing=false;app.batchFiles.clear();app.batchMetadata.clear();
        app.restoreSelection=std::move(app.batchCompleted);app.cache.Clear();
        if(next){app.restoreSelection.clear();app.transitionBypass=true;next();app.transitionBypass=false;}
        else OpenFolder(fs::path(job.destinations.front()).parent_path().wstring(),true,job.destinations.front());
    }
    Counts(report);if(!app.confirmBatch)MessageBoxW(app.window,report.c_str(),L"一括保存結果",MB_OK|(result.error.empty()?MB_ICONINFORMATION:MB_ICONWARNING));return true;
}
LRESULT CALLBACK SearchEditProc(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
    if(message==WM_GETDLGCODE) {
        auto code=DefSubclassProc(h,message,wp,lp);
        auto key=reinterpret_cast<const MSG*>(lp);
        if(key&&key->message==WM_KEYDOWN&&key->wParam==VK_RETURN)code|=DLGC_WANTMESSAGE;
        return code;
    }
    if(message==WM_KEYDOWN&&wp==VK_RETURN) {
        auto context=ImmGetContext(h);bool composing=context&&ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0)>0;if(context)ImmReleaseContext(h,context);
        if(!composing){if(!(lp&(1LL<<30))&&!app.searching&&!app.saving&&!app.backupBusy)StartSearch(Text(h));return 0;}
    }
    if(message==WM_CHAR&&wp==VK_RETURN)return 0;
    if(message==WM_NCDESTROY)RemoveWindowSubclass(h,SearchEditProc,id);
    return DefSubclassProc(h,message,wp,lp);
}
