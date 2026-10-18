#include "ViewerRuntime.h"
#include <unordered_set>
namespace {
bool Active(const Job& j){return !app.stopping&&j.generation==app.currentGeneration&&((j.kind!=6&&j.kind!=12&&j.kind!=13)||j.searchGeneration==app.currentSearchGeneration)&&(j.kind!=10||j.selection==app.currentSelection);}
void Deliver(Result result){if(!Active(result.job))return;std::unique_lock<std::mutex> lock(app.mutex);app.wake.wait(lock,[&]{return app.stopping||app.results.size()<32;});if(app.stopping)return;bool notify=app.results.empty();app.results.push_back(std::move(result));if(notify)PostMessageW(app.window,WM_RESULTS,0,0);}
}
bool SearchContains(const std::wstring& text,const std::wstring& query){if(query.empty())return true;return FindNLSStringEx(LOCALE_NAME_INVARIANT,FIND_FROMSTART|NORM_IGNORECASE,text.c_str(),static_cast<int>(text.size()),query.c_str(),static_cast<int>(query.size()),nullptr,nullptr,nullptr,0)>=0;}
void CancelSearch(){if(app.sorting){app.sorting=false;app.categoryMode=app.mdbEnabled?app.displayCategoryMode:0;SaveSettings(false);StopActivity();EnableWindow(app.list,TRUE);UpdateEditorControls();}++app.searchGeneration;app.currentSearchGeneration=app.searchGeneration;app.searching=false;{std::lock_guard<std::mutex> lock(app.mutex);app.jobs.erase(std::remove_if(app.jobs.begin(),app.jobs.end(),[](auto& j){return j.kind==6||j.kind==12||j.kind==13;}),app.jobs.end());}SetWindowTextW(app.searchButton,L"検索");app.wake.notify_all();}
void StartSearch(const std::wstring& query,const std::wstring& preserve){FlushSelectionUpdate();if(app.folder.empty()||app.folderPending||app.saving||app.backupBusy)return;if(!GuardTransition([query,preserve]{StartSearch(query,preserve);}))return;CancelSearch();Job job{6,app.generation};job.query=query;job.metadataSearch=app.metadataSearch;job.useMdb=app.mdbEnabled;for(auto& file:app.allFiles)if(ShowImage(file))job.files.push_back(file);job.searchGeneration=app.searchGeneration;job.preserve=!preserve.empty()?preserve:app.selected>=0?app.files[app.selected]:L"";app.searchQuery=query;app.searching=true;SetWindowTextW(app.searchButton,L"中止");Queue(std::move(job));Counts(L"検索中…");}
void CommitDisplay(std::vector<std::wstring> files,const std::wstring& preserve){
    if(app.categoryMode&&app.mdbEnabled&&!app.categoryCommitting&&!files.empty()){app.categoryCommitting=true;app.mdbRows.clear();CommitDisplay(files,preserve);app.categoryCommitting=false;app.restoreSelection=SelectedFiles();StartCategory(files,preserve);return;}
    if(!app.categoryCommitting)app.mdbRows.clear();app.mdbSelectionChanges.clear();
    files.erase(std::remove_if(files.begin(),files.end(),[](auto& path){return app.folderItems.count(path)||!ShowImage(path);}),files.end());
    if(!app.folder.empty())files.insert(files.begin(),app.directories.begin(),app.directories.end());
    ++app.thumbnailGeneration;app.currentThumbnailGeneration=app.thumbnailGeneration;
    ListView_EnableGroupView(app.list,FALSE);ListView_RemoveAllGroups(app.list);ListView_DeleteAllItems(app.list);Select(-1);app.files=std::move(files);app.loaded=0;ResetThumbnailTracking();
    auto placeholder=Thumbnail({},app.activeThumbnailWidth);ImageList_Replace(app.thumbs,0,placeholder,nullptr);DeleteObject(placeholder);
    int selected=-1;SendMessageW(app.list,WM_SETREDRAW,FALSE,0);
    for(int i=0;i<static_cast<int>(app.files.size());++i){auto name=fs::path(app.files[i]).filename().wstring();LVITEMW item{};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=i;item.iImage=0;item.pszText=name.data();ListView_InsertItem(app.list,&item);if(Same(app.files[i],preserve))selected=i;}
    SetupMdbGroups();SendMessageW(app.list,WM_SETREDRAW,TRUE,0);ArrangeImages();InvalidateRect(app.list,nullptr,TRUE);Counts(app.files.empty()?L"該当する画像がありません":L"サムネイル読込み中");if(selected>=0){ListView_SetItemState(app.list,selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);ListView_EnsureVisible(app.list,selected,FALSE);}if(!app.restoreSelection.empty()){auto restore=std::move(app.restoreSelection);app.restoreSelection.clear();app.updatingSelection=true;int focus=-1;auto key=[](std::wstring path){if(!path.empty())CharLowerBuffW(path.data(),static_cast<DWORD>(path.size()));return path;};std::unordered_set<std::wstring> selectedPaths;for(auto& path:restore)selectedPaths.insert(key(path));for(int i=0;i<static_cast<int>(app.files.size());++i)if(selectedPaths.count(key(app.files[i]))){ListView_SetItemState(app.list,i,LVIS_SELECTED,LVIS_SELECTED);if(focus<0||Same(app.files[i],preserve))focus=i;}app.updatingSelection=false;if(focus>=0){ListView_SetItemState(app.list,focus,LVIS_FOCUSED,LVIS_FOCUSED);ListView_SetSelectionMark(app.list,focus);Select(focus);}}ScheduleThumbnails();
}
void SearchWorker(){WorkerLifetime lifetime;auto initialized=RoInitialize(RO_INIT_MULTITHREADED);ComPtr<IWICImagingFactory> factory;CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    while(!app.stopping){Job job;{std::unique_lock<std::mutex> lock(app.mutex);auto next=[&]{return std::find_if(app.jobs.begin(),app.jobs.end(),[](auto& j){return j.kind>=6;});};app.wake.wait(lock,[&]{return app.stopping||next()!=app.jobs.end();});if(app.stopping)break;auto it=next();job=std::move(*it);app.jobs.erase(it);}if(!Active(job))continue;Result result;result.job=job;
        try{if(job.kind==10||job.kind==11){RunBatchJob(job,result);}
        else if(job.kind==13){RunCategory(job,result);}
        else if(job.kind==12){result.files=job.files;SortFiles(result.files,job.sortMode,[&]{return Active(job);});}
        else if(job.kind==6){int examined=0;for(auto& file:job.files){if(!Active(job))break;bool hit=SearchContains(fs::path(file).filename().wstring(),job.query);if(!hit&&job.metadataSearch){try{if(job.useMdb){hit=SearchContains(app.mdb.Read(file).text,job.query);}else{auto revision=ThumbnailCache::Key(file);auto summary=ReadPhotoSummary(file);std::wstring metadata=summary.xmpDetail;for(size_t i=3;i<summary.values.size();++i)metadata+=L"\n"+summary.values[i];
                    if(factory&&!Same(fs::path(file).extension().wstring(),L".webp")){ComPtr<IWICBitmapDecoder> decoder;if(SUCCEEDED(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))){ComPtr<IWICBitmapFrameDecode> frame;if(SUCCEEDED(decoder->GetFrame(0,&frame))){ComPtr<IWICMetadataQueryReader> reader;if(SUCCEEDED(frame->GetMetadataQueryReader(&reader))){int count=0;bool unsupported=false,failed=false;Metadata(reader.Get(),L"",metadata,0,count,unsupported,failed);}}}}
                    if(revision!=ThumbnailCache::Key(file))throw std::runtime_error("Search input changed");hit=SearchContains(metadata,job.query);}}catch(...){++result.failures;}}
                if(job.useMdb)app.mdb.Queue(file);if(hit)result.files.push_back(file);if(++examined%25==0){Result progress;progress.job.kind=job.kind;progress.job.generation=job.generation;progress.job.selection=job.selection;progress.job.searchGeneration=job.searchGeneration;progress.progress=true;progress.text=L"検索中: "+std::to_wstring(examined)+L" / "+std::to_wstring(job.files.size());if(Active(job))QueueProgress(std::move(progress));}}
                SortFiles(result.files,job.sortMode,[&]{return Active(job);});
            }else if(job.kind==7)result.backups=FindBackups(job.path,job.destination);
            else if(job.kind==8){auto records=ReadBackupLedger(job.destination);for(auto& path:job.files){if(app.stopping)break;auto candidate=std::find_if(job.backups.begin(),job.backups.end(),[&](auto& c){return c.path==path;});bool unchanged=false;try{unchanged=candidate!=job.backups.end()&&candidate->sourceKey==ThumbnailCache::Key(path)&&SameFolderAddress(fs::path(path).parent_path().wstring(),job.path);}catch(...){}
                    std::wstring reason; if(unchanged&&RecycleBackup(path, &reason)){result.files.push_back(path);records.erase(std::remove_if(records.begin(),records.end(),[&](auto& e){return Same(e.path,path);}),records.end());}else { ++result.failures; result.text += fs::path(path).filename().wstring() + L"：" + (unchanged ? reason : L"一覧取得後に変更・削除されたか、削除対象フォルダーと一致しません。") + L"\r\n"; }}WriteBackupLedger(job.destination,records);
            }else if(job.kind==9)result.text=PruneBackups(job.path,job.backupLimit,job.destination);
        }catch(const winrt::hresult_error& e){result.error=ErrorText(e.code());}catch(const std::exception& e){result.error=winrt::to_hstring(e.what()).c_str();}Deliver(std::move(result));
    }factory.Reset();if(SUCCEEDED(initialized))RoUninitialize();
}

void SortFiles(std::vector<std::wstring>& files,int mode,const std::function<bool()>& active){
    if(mode<0||mode>3)throw std::runtime_error("Invalid sort mode");
    struct Entry{std::wstring path,name;ULONGLONG created=0;bool valid=false;};std::vector<Entry> entries;entries.reserve(files.size());
    for(auto& file:files){if(active&&!active())return;Entry e;e.path=file;e.name=fs::path(file).filename().wstring();if(mode>=2){WIN32_FILE_ATTRIBUTE_DATA data{};e.valid=GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&data)!=FALSE;if(e.valid)e.created=(ULONGLONG(data.ftCreationTime.dwHighDateTime)<<32)|data.ftCreationTime.dwLowDateTime;}entries.push_back(std::move(e));}
    auto name=[](const Entry& a,const Entry& b){int n=_wcsicmp(a.name.c_str(),b.name.c_str());if(!n)n=wcscmp(a.name.c_str(),b.name.c_str());if(!n)n=wcscmp(a.path.c_str(),b.path.c_str());return n;};
    std::stable_sort(entries.begin(),entries.end(),[&](auto& a,auto& b){if(mode<2)return mode==0?name(a,b)<0:name(a,b)>0;if(a.valid!=b.valid)return a.valid;if(a.valid&&a.created!=b.created)return mode==2?a.created<b.created:a.created>b.created;return name(a,b)<0;});
    if(active&&!active())return;for(size_t i=0;i<entries.size();++i)files[i]=std::move(entries[i].path);
}
void RequestSort(int mode){
    if(mode<0||mode>3||app.saving||app.backupBusy||app.sorting||app.folderPending||app.batchLoading)return;
    if(app.searching){Counts(L"検索完了後に並べ替えてください。");return;}
    FlushSelectionUpdate();if(!GuardTransition([mode]{RequestSort(mode);}))return;
    app.editing=false;UpdateEditorControls();Layout();
    if(app.allFiles.empty()){app.categoryMode=0;app.sortMode=mode;SaveSettings();return;}
    CancelSearch();Job job{12,app.generation};job.sortMode=mode;job.searchGeneration=app.searchGeneration;job.files=app.allFiles;job.destinations=app.files;
    app.sorting=true;EnableWindow(app.list,FALSE);UpdateEditorControls();SetActivity(L"並べ替え中…");Queue(std::move(job));
}
