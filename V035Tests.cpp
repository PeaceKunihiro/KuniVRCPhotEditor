#include "ViewerRuntime.h"
#include "WebPMetadata.h"
#include "WebPFixtures.h"
void RunV035Tests(const fs::path& directory,std::ostream& report) {
    auto require=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: "<<label<<'\n';};
    auto read=[](const fs::path& path){std::ifstream file(path,std::ios::binary);return std::string((std::istreambuf_iterator<char>(file)),{});};
    auto write=[](const fs::path& path,const std::string& data){std::ofstream file(path,std::ios::binary);file.write(data.data(),data.size());};
    auto chunks=[](const std::string& bytes){std::vector<std::string> parts,packets;std::vector<bool> flags;InspectWebPChunks(bytes,parts,flags,packets);std::string kept;for(size_t i=1;i<parts.size();++i)if(!flags[i]&&parts[i].substr(0,4)!="VP8X")kept+=parts[i];return kept;};
    require(NormalizeTags(L" 夜景 \n\n夜景\r\n集合 写真\n#VRChat ")==L"夜景\r\n集合 写真\r\n#VRChat","tags normalize whitespace and duplicates without splitting spaces or hash");
    MetadataEdit edit;edit.actions[1]=EditAction::Set;edit.values[1]=L"WebP編集 & < >";edit.actions[5]=EditAction::Set;edit.values[5]=L"夜景\n集合 写真\n夜景";
    std::vector<std::string> samples{
        std::string(reinterpret_cast<const char*>(LosslessWebP),sizeof(LosslessWebP)),
        std::string(reinterpret_cast<const char*>(LossyWebP),sizeof(LossyWebP)),
        std::string(reinterpret_cast<const char*>(AnimatedWebP),sizeof(AnimatedWebP))};
    std::vector<std::string> parts,packets;std::vector<bool> flags;InspectWebPChunks(samples[0],parts,flags,packets);
    std::string simple=samples[0].substr(0,12);for(size_t i=1;i<parts.size();++i)if(!flags[i]&&parts[i].substr(0,4)!="VP8X")simple+=parts[i];WebPPut(simple,4,static_cast<uint32_t>(simple.size()-8));samples.push_back(simple);
    auto extras=samples[0];extras[20]=char(static_cast<unsigned char>(extras[20])|0x28);extras.insert(30,WebPChunk("ICCP","synthetic-profile"));extras+=WebPChunk("EXIF","synthetic-exif")+WebPChunk("KNWN","unknown-payload");WebPPut(extras,4,static_cast<uint32_t>(extras.size()-8));samples.push_back(extras);
    for(size_t i=0;i<samples.size();++i) {
        auto source=directory/(L"v035-source-"+std::to_wstring(i)+L".webp"),output=directory/(L"v035-copy-"+std::to_wstring(i)+L".webp");write(source,samples[i]);fs::remove(output);
        auto before=InspectEditable(source.wstring());auto pixels=ReadWebP(source.wstring(),0,0);
        SaveMetadataCopy(source.wstring(),output.wstring(),before.fingerprint,edit);
        auto actual=InspectEditable(output.wstring());auto after=ReadWebP(output.wstring(),0,0);
        require(actual.values[1]==edit.values[1]&&actual.values[5]==L"夜景\r\n集合 写真"&&ReadPhotoSummary(output.wstring()).values[8]==actual.values[5],"WebP XMP and tags round trip including simple and animated images");
        require(read(source)==samples[i]&&chunks(read(output))==chunks(samples[i])&&pixels.pixels==after.pixels&&pixels.animated==after.animated,"WebP original encoded frames alpha ICC EXIF unknown chunks and pixels preserved");
        auto ledger=directory/L"v035-ledger.json";if(fs::exists(ledger))fs::remove(ledger);
        MetadataEdit append;append.actions[5]=EditAction::Append;append.values[5]=L"追加\n夜景";
        SaveMetadataOverwrite(output.wstring(),actual.fingerprint,append,true,10,ledger);
        require(InspectEditable(output.wstring()).values[5]==L"夜景\r\n集合 写真\r\n追加","WebP overwrite appends tags without losing original tags");
        auto backups=ReadBackupLedger(ledger);require(backups.size()==1&&MetadataFingerprint(backups[0].path)==actual.fingerprint,"WebP overwrite creates exact pre-edit backup");for(auto& b:backups)fs::remove(b.path);fs::remove(ledger);
        MetadataEdit remove;remove.actions[5]=EditAction::Remove;auto cleared=directory/(L"v035-cleared-"+std::to_wstring(i)+L".webp");fs::remove(cleared);SaveMetadataCopy(output.wstring(),cleared.wstring(),MetadataFingerprint(output.wstring()),remove);
        require(InspectEditable(cleared.wstring()).values[5].empty()&&InspectEditable(cleared.wstring()).values[1]==edit.values[1],"tag removal preserves other WebP metadata");
    }
    auto malformed=samples[0];malformed[4]^=1;auto broken=directory/L"v035-broken.webp";write(broken,malformed);bool rejected=false;try{InspectEditable(broken.wstring());}catch(...){rejected=true;}require(rejected,"malformed WebP container rejected before editing");
    auto batchDirectory=directory/L"v035-batch";fs::create_directories(batchDirectory);
    std::vector<std::wstring> sources;for(auto name:{L"sample.jpg",L"sample-日本語.png",L"v035-source-0.webp"}){auto path=batchDirectory/name;fs::copy_file(directory/name,path,fs::copy_options::overwrite_existing);sources.push_back(path.wstring());}
    Job batch{11,app.generation};batch.files=sources;batch.edit.actions[2]=EditAction::Set;batch.edit.values[2]=L"一括ワールド";batch.edit.actions[5]=EditAction::Append;batch.edit.values[5]=L"一括タグ";
    for(size_t i=0;i<sources.size();++i){batch.metadata.push_back(InspectEditable(sources[i]));auto output=batchDirectory/(L"out-"+std::to_wstring(i)+fs::path(sources[i]).extension().wstring());fs::remove(output);batch.destinations.push_back(output.wstring());}
    std::mutex preparing;std::condition_variable together;unsigned arrived=0;std::vector<DWORD> threads;
    Result saved;RunBatchJob(batch,saved,{},[&](size_t i){if(i>=2)return;std::unique_lock<std::mutex> lock(preparing);threads.push_back(GetCurrentThreadId());++arrived;together.notify_all();if(!together.wait_for(lock,std::chrono::seconds(3),[&]{return arrived==2;}))throw std::runtime_error("parallel preparation timeout");});
    require(threads.size()==2&&threads[0]!=threads[1],"two independent COM worker threads prepare batch images concurrently");require(saved.files.size()==3,"mixed PNG JPEG WebP batch save succeeds");
    for(size_t i=0;i<sources.size();++i){auto actual=InspectEditable(batch.destinations[i]);require(actual.values[0]==batch.metadata[i].values[0]&&actual.values[1]==batch.metadata[i].values[1]&&actual.values[2]==L"一括ワールド"&&actual.values[5].find(L"一括タグ")!=std::wstring::npos,"batch keeps per-image capture date and unchanged author");}
    auto large=samples[0]+WebPChunk("KNWN",std::string(4*1024*1024,'x'));WebPPut(large,4,static_cast<uint32_t>(large.size()-8));auto largePath=(batchDirectory/L"inspection-large.webp").wstring();write(largePath,large);
    Job readJob{10,app.generation,app.currentSelection};for(int i=0;i<80;++i)readJob.files.push_back(i%4==3?largePath:batch.files[i%4]);
    auto started=GetTickCount64();std::vector<EditableMetadata> sequential;for(auto& path:readJob.files)sequential.push_back(InspectEditable(path));auto sequentialMs=GetTickCount64()-started;
    arrived=0;app.batchReadPeak=0;Result inspected;started=GetTickCount64();RunBatchJob(readJob,inspected,{},[&](size_t i){if(i>1)return;std::unique_lock<std::mutex> lock(preparing);++arrived;together.notify_all();if(!together.wait_for(lock,std::chrono::seconds(3),[&]{return arrived==2;}))throw std::runtime_error("parallel inspection timeout");});
    require(inspected.metadata.size()==80&&app.batchReadPeak==2,"80-image metadata inspection uses bounded parallel workers");
    bool ordered=true;for(size_t i=0;i<80;++i)ordered=ordered&&inspected.metadata[i].fingerprint==sequential[i].fingerprint;require(ordered,"parallel inspection preserves source order and fingerprints");
    report<<"INFO: synthetic 80-image inspection sequential_ms="<<sequentialMs<<" parallel_ms="<<GetTickCount64()-started<<'\n';
    auto badRead=readJob;badRead.files[0]=(batchDirectory/L"missing-image.png").wstring();Result badResult;bool badRejected=false;try{RunBatchJob(badRead,badResult);}catch(...){badRejected=true;}require(badRejected&&badResult.metadata.empty(),"failed inspection cannot open partial batch editor");
    Result abandoned;RunBatchJob(readJob,abandoned,{},[&](size_t){++app.currentSelection;});app.currentSelection=readJob.selection;require(abandoned.metadata.empty(),"selection change discards unfinished parallel inspection");fs::remove(largePath);
    auto forbidden=batch;forbidden.edit.actions[0]=EditAction::Set;forbidden.edit.values[0]=L"2026-10-10";rejected=false;try{Result result;RunBatchJob(forbidden,result);}catch(...){rejected=true;}require(rejected,"batch capture date change rejected by save backend");
    auto stale=batch;stale.metadata[1].fingerprint=L"stale";for(size_t i=0;i<3;++i){fs::remove(stale.destinations[i]);}rejected=false;try{Result result;RunBatchJob(stale,result);}catch(...){rejected=true;}require(rejected&&!fs::exists(stale.destinations[0]),"batch preflight failure leaves every destination untouched");
    auto invalid=batch;invalid.destinations[1]=(batchDirectory/L"wrong-extension.webp").wstring();rejected=false;try{Result r;RunBatchJob(invalid,r);}catch(...){rejected=true;}require(rejected&&!fs::exists(batch.destinations[0]),"destination format mismatch rejected before batch writes");
    auto original=read(batch.files[1]);Result result;RunBatchJob(batch,result,[&](size_t i){if(i==0)write(batch.files[1],original+"changed");});
    require(result.files.size()==1&&!result.error.empty()&&!fs::exists(batch.destinations[2]),"batch failure keeps completed image and stops remaining images");write(batch.files[1],original);
    auto retry=batch;retry.files.erase(retry.files.begin());retry.metadata.erase(retry.metadata.begin());retry.destinations.erase(retry.destinations.begin());Result retried;RunBatchJob(retry,retried);require(retried.files.size()==2,"batch retry processes only failed and remaining targets");
    for(auto& path:batch.destinations)fs::remove(path);
    app.cancelBatch=true;Result stopped;RunBatchJob(batch,stopped);app.cancelBatch=false;require(stopped.files.empty()&&!stopped.text.empty(),"batch cancellation before write changes no images");
    Result midway;RunBatchJob(batch,midway,[&](size_t i){if(i==0)app.cancelBatch=true;});app.cancelBatch=false;
    require(midway.files.size()==1&&!fs::exists(batch.destinations[1])&&!fs::exists(batch.destinations[2]),"cancellation between images preserves completed copy and skips remaining images");
    for(auto& entry:fs::directory_iterator(batchDirectory))require(entry.path().filename().wstring().find(L".Kuni-batch-")!=0,"uncommitted prepared files removed after cancellation and failure");
    for(auto& path:batch.destinations)fs::remove(path);Result conflict;RunBatchJob(batch,conflict,[&](size_t i){if(i==0)write(batch.destinations[1],"competing file");});require(conflict.files.size()==1&&!conflict.error.empty()&&read(batch.destinations[1])=="competing file"&&!fs::exists(batch.destinations[2]),"parallel preparation does not replace competing copy destination");
    require(BatchResultCounts(3,1,true)==L"対象: 3件\r\n成功: 1件\r\n失敗: 1件\r\n未処理: 1件","batch result contains counts without filenames");
    auto overwrite=batch;overwrite.overwrite=true;overwrite.autoBackup=true;overwrite.backupLimit=10;overwrite.destinations=overwrite.files;auto ledger=batchDirectory/L"batch-backups.json";fs::remove(ledger);overwrite.destination=ledger.wstring();
    Result replaced;RunBatchJob(overwrite,replaced);auto backups=ReadBackupLedger(ledger);require(replaced.files.size()==3&&backups.size()==3,"mixed-format batch overwrite backs up every image");
    for(size_t i=0;i<batch.files.size();++i){auto record=std::find_if(backups.begin(),backups.end(),[&](const auto& entry){return Same(entry.source,batch.files[i]);});require(record!=backups.end()&&MetadataFingerprint(record->path)==batch.metadata[i].fingerprint&&InspectEditable(batch.files[i]).values[0]==batch.metadata[i].values[0],"batch overwrite backups match original bytes and capture dates remain unchanged");}
    std::vector<std::wstring> protectedPaths;for(auto& entry:backups)protectedPaths.push_back(entry.path);PruneBackups(batchDirectory.wstring(),1,ledger,protectedPaths);require(ReadBackupLedger(ledger).size()==3,"batch protects selected backup images from retention pruning");
    for(auto& entry:backups)fs::remove(entry.path);fs::remove(ledger);
    {std::lock_guard<std::mutex> lock(app.mutex);app.results.clear();}
}
