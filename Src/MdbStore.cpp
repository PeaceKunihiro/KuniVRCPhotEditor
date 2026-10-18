#include "ViewerRuntime.h"
#include "MdbStore.h"
#include "V048Features.h"
#include <winsqlite/winsqlite3.h>
#include <map>
#include <set>
#include <cstring>
#pragma comment(lib,"winsqlite3.lib")
namespace {
std::wstring PathKey(const std::wstring& p){auto s=NormalizeFolderAddress(p);CharLowerBuffW(s.data(),DWORD(s.size()));return s;}
bool BackupName(const std::wstring& p){auto s=fs::path(p).filename().wstring();CharLowerBuffW(s.data(),DWORD(s.size()));return s.find(L"ab_")!=s.npos;}
bool Safe(const fs::path& p){auto a=GetFileAttributesW(p.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);}
bool Owned(HANDLE file){LARGE_INTEGER size{},zero{};if(!GetFileSizeEx(file,&size)||size.QuadPart!=8||!SetFilePointerEx(file,zero,nullptr,FILE_BEGIN))return false;char text[8]{};DWORD n=0;return ReadFile(file,text,8,&n,nullptr)&&n==8&&memcmp(text,"KPEMDB01",8)==0;}
void Claim(HANDLE file,bool existingDb){LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size))throw std::runtime_error("MDB ownership read");if(!size.QuadPart&&!existingDb){DWORD n=0;if(!WriteFile(file,"KPEMDB01",8,&n,nullptr)||n!=8||!FlushFileBuffers(file))throw std::runtime_error("MDB ownership write");}else if(!Owned(file))throw std::runtime_error("MDB files are not owned by this application");}
std::vector<std::wstring> Lines(const std::wstring& text){std::vector<std::wstring> out;std::wistringstream stream(text);std::wstring s;while(std::getline(stream,s)){auto a=s.find_first_not_of(L" \r\t");if(a!=s.npos){s=s.substr(a,s.find_last_not_of(L" \r\t")-a+1);if(std::find(out.begin(),out.end(),s)==out.end())out.push_back(s);}}return out;}
void Check(int rc){if(rc!=SQLITE_OK&&rc!=SQLITE_DONE&&rc!=SQLITE_ROW)throw std::runtime_error("MDB SQLite operation failed: "+std::to_string(rc));}
void Sql(sqlite3* db,const std::string& text){Check(sqlite3_exec(db,text.c_str(),nullptr,nullptr,nullptr));}
struct Statement {sqlite3_stmt* p=nullptr;Statement(sqlite3* db,const char* sql){Check(sqlite3_prepare_v2(db,sql,-1,&p,nullptr));}~Statement(){sqlite3_finalize(p);}void Text(int n,const std::wstring& s){Check(sqlite3_bind_text16(p,n,s.data(),int(s.size()*sizeof(wchar_t)),SQLITE_TRANSIENT));}void Number(int n,int64_t value){Check(sqlite3_bind_int64(p,n,value));}int Step(){auto r=sqlite3_step(p);Check(r);return r;}std::wstring Text(int n){auto s=static_cast<const wchar_t*>(sqlite3_column_text16(p,n));return s?std::wstring(s,sqlite3_column_bytes16(p,n)/sizeof(wchar_t)):L"";}int64_t Number(int n){return sqlite3_column_int64(p,n);}};
int64_t Scalar(sqlite3* db,const char* sql){Statement s(db,sql);return s.Step()==SQLITE_ROW?s.Number(0):0;}
std::wstring Encode(const MdbRecord& row){JsonArray a;for(auto& t:row.terms){JsonObject o;o.Insert(L"a",JsonValue::CreateNumberValue(t.axis));o.Insert(L"v",JsonValue::CreateStringValue(t.value));o.Insert(L"l",JsonValue::CreateStringValue(t.label));o.Insert(L"r",JsonValue::CreateNumberValue(t.roles));a.Append(o);}return std::wstring(a.Stringify());}
void Decode(const std::wstring& json,MdbRecord& row){for(auto v:JsonArray::Parse(json)){auto o=v.GetObject();MdbTerm t;t.axis=int(o.GetNamedNumber(L"a"));t.value=o.GetNamedString(L"v");t.label=o.GetNamedString(L"l");t.roles=int(o.GetNamedNumber(L"r",0));if(t.axis<0||t.axis>5||t.value.empty())throw std::runtime_error("MDB invalid term");row.terms.push_back(std::move(t));}}
}
std::wstring MdbRevision(const std::wstring& path){
    auto key=ThumbnailCache::Key(path);HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){FILE_BASIC_INFO basic{};FILE_ID_INFO id{};if(GetFileInformationByHandleEx(file,FileBasicInfo,&basic,sizeof(basic)))key+=L"|change="+std::to_wstring(basic.ChangeTime.QuadPart);if(GetFileInformationByHandleEx(file,FileIdInfo,&id,sizeof(id))){key+=L"|volume="+std::to_wstring(id.VolumeSerialNumber);for(auto b:id.FileId.Identifier)key+=L"-"+std::to_wstring(b);}CloseHandle(file);}return key;
}
MdbRecord ReadMdbSource(const std::wstring& path){
    MdbRecord row;row.path=path;row.revision=MdbRevision(path);row.backup=BackupName(path);
    auto summary=ReadPhotoSummary(path);row.failed=summary.metadataFailed;
    row.text=summary.xmpDetail;for(size_t i=3;i<summary.values.size();++i)row.text+=L"\n"+summary.values[i];
    ComPtr<IWICImagingFactory> factory;CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(factory&&!Same(fs::path(path).extension().wstring(),L".webp")){ComPtr<IWICBitmapDecoder> decoder;if(SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder))){ComPtr<IWICBitmapFrameDecode> frame;if(SUCCEEDED(decoder->GetFrame(0,&frame))){ComPtr<IWICMetadataQueryReader> reader;if(SUCCEEDED(frame->GetMetadataQueryReader(&reader))){int count=0;bool unsupported=false,failed=false;Metadata(reader.Get(),L"",row.text,0,count,unsupported,failed);}}}}
    auto add=[&](int axis,const std::wstring& value,const std::wstring& label,int roles=0){if(value.empty())return;for(auto& t:row.terms)if(t.axis==axis&&t.value==value&&t.label==label){t.roles|=roles;return;}row.terms.push_back({axis,value,label,roles});};
    auto tags=summary.values[8];for(auto& s:Lines(tags)){if(s==L"【AutoBackup】")row.backup=true;add(0,s,s);}
    auto world=summary.values[6];add(3,world,summary.values[5]);
    for(auto& id:GroupIds(tags))add(4,id,L"");
    add(5,summary.values[4],summary.values[4]);
    for(auto& s:Lines(summary.values[7]))add(2,s,s,2);
    for(auto& p:summary.people){add(2,p.name,p.name,1);add(1,p.id,p.name,1);}
    for(auto& p:summary.candidatePeople){add(2,p.name,p.name,4);add(1,p.id,p.name,4);}
    if(row.revision!=MdbRevision(path))throw std::runtime_error("MDB input changed during read");return row;
}
struct MetadataDb::Impl {
    struct Task{std::wstring path;bool cachedOnly=false;uint64_t epoch=0;std::shared_ptr<const std::vector<std::wstring>> present;};
    std::mutex control,io;std::condition_variable wake,idle;std::vector<std::thread> workers;
    std::deque<Task> tasks;std::set<std::wstring> queued;std::vector<fs::path> eraseRoots;
    MdbConfig cfg;fs::path root,openedRoot;uint64_t epoch=0;std::atomic<bool> stopping{false};bool pending=false;size_t active=0;
    std::function<MdbRecord(const std::wstring&)> reader;std::function<bool(const std::wstring&)> priority;
    std::map<std::wstring,std::pair<int64_t,int>> touches;
    MdbStatus status;sqlite3* db=nullptr;HANDLE owner=INVALID_HANDLE_VALUE;int64_t tick=0;uint64_t applied=UINT64_MAX;
    ~Impl(){Close();}
    void Close(bool persist=true){if(db){if(persist)try{FlushTouches();}catch(...){}touches.clear();sqlite3_close(db);db=nullptr;}if(owner!=INVALID_HANDLE_VALUE){CloseHandle(owner);owner=INVALID_HANDLE_VALUE;}openedRoot.clear();applied=UINT64_MAX;}
    void Error(const std::wstring& error,bool deletion=false){std::lock_guard<std::mutex> l(control);status.error=error;status.deletionFailed=deletion;}
    void Delete(const fs::path& folder){
        Close(false);if(!fs::exists(folder))return;if(!Safe(folder)||!Safe(folder.parent_path()))throw std::runtime_error("MDB deletion path redirected");
        auto guard=folder/L"owner.lock";if(fs::exists(guard)&&!Safe(guard))throw std::runtime_error("MDB lock redirected");
        bool data=false;for(auto name:{L"mdb.sqlite",L"mdb.sqlite-journal",L"mdb.sqlite-wal",L"mdb.sqlite-shm"})data=data||fs::exists(folder/name);if(!data){std::lock_guard<std::mutex> l(control);status.bytes=status.entries=0;status.error.clear();status.deletionFailed=false;return;}
        HANDLE lock=CreateFileW(guard.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("MDB is in use or ownership marker is missing");
        try{if(!Owned(lock))throw std::runtime_error("MDB deletion ownership mismatch");for(auto name:{L"mdb.sqlite",L"mdb.sqlite-journal",L"mdb.sqlite-wal",L"mdb.sqlite-shm"}){auto p=folder/name;if(fs::exists(p)){if(!Safe(p)||!fs::is_regular_file(p))throw std::runtime_error("MDB file redirected");fs::remove(p);}}}catch(...){CloseHandle(lock);throw;}
        CloseHandle(lock);fs::remove(guard);if(fs::is_empty(folder))fs::remove(folder);
        std::lock_guard<std::mutex> l(control);status.bytes=status.entries=0;status.error.clear();status.deletionFailed=false;
    }
    bool Sync(MdbConfig& c,uint64_t& ticket){
        fs::path folder;std::vector<fs::path> erase;{std::lock_guard<std::mutex> l(control);c=cfg;folder=root;ticket=epoch;erase=eraseRoots;}
        for(auto& p:erase){try{Delete(p);}catch(...){Error(L"MDBを削除できません。使用中・アクセス権・保存先を確認してください。残存DBは利用しません。",true);throw;}
            std::lock_guard<std::mutex> l(control);auto i=std::find(eraseRoots.begin(),eraseRoots.end(),p);if(i!=eraseRoots.end())eraseRoots.erase(i);}
        if(!c.enabled||stopping){Close();return false;}
        if(db&&openedRoot==folder&&applied==ticket)return true;
        Close();if(!Safe(folder.parent_path()))throw std::runtime_error("MDB parent inaccessible or redirected");if(!fs::exists(folder))fs::create_directory(folder);if(!Safe(folder)||!fs::is_directory(folder))throw std::runtime_error("MDB path redirected");
        for(auto name:{L"owner.lock",L"mdb.sqlite",L"mdb.sqlite-journal",L"mdb.sqlite-wal",L"mdb.sqlite-shm"})if(fs::exists(folder/name)&&(!Safe(folder/name)||!fs::is_regular_file(folder/name)))throw std::runtime_error("MDB file redirected");
        owner=CreateFileW((folder/L"owner.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(owner==INVALID_HANDLE_VALUE)throw std::runtime_error("MDB is in use");
        try{Claim(owner,fs::exists(folder/L"mdb.sqlite"));Check(sqlite3_open16((folder/L"mdb.sqlite").c_str(),&db));sqlite3_busy_timeout(db,100);sqlite3_limit(db,SQLITE_LIMIT_LENGTH,64*1024*1024);
            auto version=Scalar(db,"PRAGMA user_version");if(version!=0&&version!=1)throw std::runtime_error("Unsupported MDB schema");
            {Statement check(db,"PRAGMA quick_check(1)");if(check.Step()!=SQLITE_ROW||check.Text(0)!=L"ok")throw std::runtime_error("MDB corrupt");}
            Sql(db,"PRAGMA foreign_keys=ON; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA cache_size=-4096; PRAGMA temp_store=MEMORY;");
            if(!version)Sql(db,"PRAGMA auto_vacuum=FULL; VACUUM;");
            Sql(db,"CREATE TABLE IF NOT EXISTS photos(path TEXT PRIMARY KEY COLLATE NOCASE, revision TEXT NOT NULL, body TEXT NOT NULL, terms TEXT NOT NULL, used INTEGER NOT NULL, priority INTEGER NOT NULL, bytes INTEGER NOT NULL); CREATE INDEX IF NOT EXISTS photo_usage ON photos(priority,used); CREATE TABLE IF NOT EXISTS items(path TEXT NOT NULL COLLATE NOCASE REFERENCES photos(path) ON DELETE CASCADE, axis INTEGER NOT NULL, value TEXT NOT NULL, label TEXT NOT NULL, roles INTEGER NOT NULL, PRIMARY KEY(path,axis,value,label)); CREATE INDEX IF NOT EXISTS item_values ON items(axis,value,label); PRAGMA user_version=1;");
            if(c.mode!=1)Sql(db,"UPDATE photos SET priority=0 WHERE priority<>0");
            tick=Scalar(db,"SELECT COALESCE(MAX(used),0) FROM photos");openedRoot=folder;applied=ticket;
            Trim(c,0,true);if(auto limit=c.Limit()){auto pages=std::max<uint64_t>(32,(limit/2-65536)/4096);Sql(db,"PRAGMA max_page_count="+std::to_string(pages));}Publish();return true;
        }catch(...){Close();throw;}
    }
    void Publish(){if(!db)return;auto entries=Scalar(db,"SELECT COUNT(*) FROM photos");uint64_t bytes=0;for(auto name:{L"mdb.sqlite",L"mdb.sqlite-journal",L"mdb.sqlite-wal",L"mdb.sqlite-shm",L"owner.lock"}){std::error_code ec;auto size=fs::file_size(openedRoot/name,ec);if(!ec)bytes+=size;}
        std::lock_guard<std::mutex> l(control);status.entries=entries;status.bytes=bytes;}
    void FlushTouches(){if(!db||touches.empty())return;Sql(db,"BEGIN IMMEDIATE");try{Statement s(db,"UPDATE photos SET used=?,priority=CASE WHEN ?<0 THEN priority ELSE ? END WHERE path=?");for(auto& item:touches){sqlite3_reset(s.p);sqlite3_clear_bindings(s.p);s.Number(1,item.second.first);s.Number(2,item.second.second);s.Number(3,item.second.second);s.Text(4,item.first);s.Step();}Sql(db,"COMMIT");touches.clear();}catch(...){sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}}
    bool Trim(const MdbConfig& c,uint64_t extra,bool incomingPriority){
        FlushTouches();
        if(!c.Limit())return true;uint64_t available=c.Limit()/4;
        while(uint64_t(Scalar(db,"SELECT COALESCE(SUM(bytes),0) FROM photos"))+extra>available){Statement old(db,"SELECT path,priority FROM photos ORDER BY priority,used,path LIMIT 1");if(old.Step()!=SQLITE_ROW||(!incomingPriority&&old.Number(1)>0))return false;auto path=old.Text(0);Statement remove(db,"DELETE FROM photos WHERE path=?");remove.Text(1,path);remove.Step();}
        return true;
    }
    void Work(MetadataDb* api){
        auto init=RoInitialize(RO_INIT_MULTITHREADED);
        for(;;){Task task;bool config=false;{std::unique_lock<std::mutex> l(control);wake.wait(l,[&]{return stopping||pending||!tasks.empty();});if(stopping)break;config=pending;pending=false;if(!config){task=std::move(tasks.front());tasks.pop_front();}++active;}
            try{if(config){std::lock_guard<std::mutex> l(io);MdbConfig c;uint64_t t;if(Sync(c,t))FlushTouches();}
                else{std::function<bool(const std::wstring&)> test;bool run=false;{std::lock_guard<std::mutex> l(control);run=task.epoch==epoch&&cfg.enabled;test=priority;}
                    if(run){if(task.present){std::set<std::wstring> live;for(auto& p:*task.present)live.insert(PathKey(p));std::lock_guard<std::mutex> l(io);MdbConfig c;uint64_t t;if(Sync(c,t)){
                        auto folder=PathKey(task.path),prefix=folder+L"\\";std::wstring pattern;for(auto ch:prefix){if(ch==L'!'||ch==L'%'||ch==L'_')pattern+=L'!';pattern+=ch;}pattern+=L'%';std::vector<std::wstring> removed;
                        {Statement q(db,"SELECT path FROM photos WHERE path LIKE ? ESCAPE '!'");q.Text(1,pattern);while(q.Step()==SQLITE_ROW){auto p=q.Text(0);if(PathKey(fs::path(p).parent_path().wstring())==folder&&!live.count(p))removed.push_back(p);}}
                        for(auto& p:removed){Statement d(db,"DELETE FROM photos WHERE path=?");d.Text(1,p);d.Step();touches.erase(p);}Publish();}}
                    else if(task.cachedOnly){bool cached=test&&test(task.path);if(cached)api->Read(task.path,1,false);else{std::lock_guard<std::mutex> l(io);MdbConfig c;uint64_t t;if(Sync(c,t)){Statement s(db,"UPDATE photos SET priority=0 WHERE path=?");s.Text(1,PathKey(task.path));s.Step();}}}else api->Read(task.path,-1,false);}}}
            catch(...){std::lock_guard<std::mutex> l(control);if(!status.deletionFailed)status.error=L"MDBの読込み・収集・保存に失敗しました。元画像の閲覧と直接検索は継続できます。";}
            {std::lock_guard<std::mutex> l(control);if(!config&&!task.present)queued.erase(PathKey(task.path));--active;idle.notify_all();}
        }if(SUCCEEDED(init))RoUninitialize();
    }
};
MetadataDb::MetadataDb():impl(std::make_unique<Impl>()){}MetadataDb::~MetadataDb(){Stop();}
void MetadataDb::Configure(const fs::path& path,MdbConfig cfg,std::function<MdbRecord(const std::wstring&)> reader,std::function<bool(const std::wstring&)> priority){
    if(cfg.mode<0||cfg.mode>2||cfg.capacityMB<100||cfg.capacityMB>4000)throw std::runtime_error("Invalid MDB configuration");
    std::lock_guard<std::mutex> l(impl->control);if(impl->cfg.enabled&&!cfg.enabled&&std::find(impl->eraseRoots.begin(),impl->eraseRoots.end(),impl->root)==impl->eraseRoots.end())impl->eraseRoots.push_back(impl->root);
    impl->cfg=cfg;impl->root=path;impl->reader=std::move(reader);impl->priority=std::move(priority);++impl->epoch;impl->tasks.clear();impl->queued.clear();impl->pending=true;impl->status.error.clear();
    if(impl->workers.empty()&&(cfg.enabled||!impl->eraseRoots.empty())){impl->stopping=false;for(int n=0;n<2;++n)impl->workers.emplace_back([this]{impl->Work(this);});}if(impl->workers.empty())impl->pending=false;impl->wake.notify_all();
}
MdbRecord MetadataDb::Read(const std::wstring& path,int priority,bool includeText){
    MdbConfig cfg;uint64_t epoch=0;std::function<MdbRecord(const std::wstring&)> reader;std::function<bool(const std::wstring&)> test;{std::lock_guard<std::mutex> l(impl->control);cfg=impl->cfg;epoch=impl->epoch;reader=impl->reader;test=impl->priority;}
    auto revision=MdbRevision(path),key=PathKey(path);bool available=false,stale=false;
    if(cfg.enabled&&!BackupName(path)){try{std::lock_guard<std::mutex> l(impl->io);MdbConfig current;uint64_t ticket;available=impl->Sync(current,ticket)&&epoch==ticket;
        if(available){Statement find(impl->db,includeText?"SELECT revision,body,terms FROM photos WHERE path=?":"SELECT revision,'',terms FROM photos WHERE path=?");find.Text(1,key);bool found=find.Step()==SQLITE_ROW;stale=found&&find.Text(0)!=revision;if(found&&!stale){MdbRecord row;row.path=path;row.revision=revision;row.text=find.Text(1);Decode(find.Text(2),row);if(revision!=MdbRevision(path))throw std::runtime_error("MDB source changed");auto stamp=++impl->tick;auto it=impl->touches.find(key);int prior=it!=impl->touches.end()?it->second.second:-1;impl->touches[key]={stamp,priority<0?prior:priority};std::lock_guard<std::mutex> c(impl->control);++impl->status.hits;if(impl->touches.size()>=128){impl->pending=true;impl->wake.notify_one();}return row;}}}
        catch(...){if(!Status().deletionFailed)impl->Error(L"MDBを利用できません。直接メタデータを読み込みます。");}}
    if(stale)Invalidate(path);
    auto row=reader?reader(path):ReadMdbSource(path);row.path=path;row.revision=revision;row.backup=row.backup||BackupName(path);
    {std::lock_guard<std::mutex> l(impl->control);++impl->status.parsed;}
    if(revision!=MdbRevision(path))throw std::runtime_error("MDB source changed");
    if(!cfg.enabled||row.backup||row.failed){if(row.backup||row.failed)Invalidate(path);if(!includeText)std::wstring().swap(row.text);return row;}
    if(priority<0)priority=cfg.mode==1&&test&&test(path)?1:0;
    try{auto json=Encode(row);uint64_t bytes=uint64_t((row.text.size()+json.size()+revision.size()+key.size())*2)+512;for(auto& t:row.terms)bytes+=(t.value.size()+t.label.size()+key.size())*2+128;
        std::lock_guard<std::mutex> l(impl->io);MdbConfig current;uint64_t ticket;if(!impl->Sync(current,ticket)||ticket!=epoch)return row;
        if(cfg.Limit()&&bytes>cfg.Limit()/4){impl->Error(L"MDB容量上限のため登録できない画像があります。直接検索で補完します。");return row;}
        if(!impl->Trim(cfg,bytes,priority!=0)){impl->Error(L"MDB容量上限のため一部未保持です。直接検索で補完します。");return row;}
        Sql(impl->db,"BEGIN IMMEDIATE");try{
            {Statement d(impl->db,"DELETE FROM photos WHERE path=?");d.Text(1,key);d.Step();}
            {Statement s(impl->db,"INSERT INTO photos VALUES(?,?,?,?,?,?,?)");s.Text(1,key);s.Text(2,revision);s.Text(3,row.text);s.Text(4,json);s.Number(5,++impl->tick);s.Number(6,priority);s.Number(7,bytes);s.Step();}
            for(auto& t:row.terms){Statement s(impl->db,"INSERT OR REPLACE INTO items VALUES(?,?,?,?,?)");s.Text(1,key);s.Number(2,t.axis);s.Text(3,t.value);s.Text(4,t.label);s.Number(5,t.roles);s.Step();}
            {std::lock_guard<std::mutex> c(impl->control);if(epoch!=impl->epoch||!impl->cfg.enabled)throw std::runtime_error("MDB generation changed");Sql(impl->db,"COMMIT");}
        }catch(...){sqlite3_exec(impl->db,"ROLLBACK",nullptr,nullptr,nullptr);throw;}impl->Publish();
    }catch(...){if(!Status().deletionFailed)impl->Error(L"MDBへ保存できませんでした。直接検索の結果は利用できます。");}if(!includeText)std::wstring().swap(row.text);return row;
}
void MetadataDb::Queue(const std::wstring& path){std::lock_guard<std::mutex> l(impl->control);if(!impl->cfg.enabled||impl->stopping||BackupName(path))return;auto key=PathKey(path);if(impl->queued.insert(key).second)impl->tasks.push_back({path,false,impl->epoch});impl->wake.notify_one();}
void MetadataDb::QueueCached(const std::vector<std::wstring>& paths){std::lock_guard<std::mutex> l(impl->control);if(!impl->cfg.enabled||impl->cfg.mode!=1||impl->stopping)return;for(auto i=paths.rbegin();i!=paths.rend();++i){auto key=PathKey(*i);if(!BackupName(*i)&&impl->queued.insert(key).second)impl->tasks.push_front({*i,true,impl->epoch});}impl->wake.notify_all();}
void MetadataDb::Reconcile(const std::wstring& folder,const std::vector<std::wstring>& files){std::lock_guard<std::mutex> l(impl->control);if(!impl->cfg.enabled||impl->stopping)return;Impl::Task t{folder,false,impl->epoch};t.present=std::make_shared<const std::vector<std::wstring>>(files);impl->tasks.push_front(std::move(t));impl->wake.notify_one();}
void MetadataDb::Invalidate(const std::wstring& path){try{std::lock_guard<std::mutex> l(impl->io);MdbConfig c;uint64_t t;if(impl->Sync(c,t)){Statement s(impl->db,"DELETE FROM photos WHERE path=?");s.Text(1,PathKey(path));s.Step();impl->touches.erase(PathKey(path));impl->Publish();}}catch(...){}}
std::vector<std::wstring> MetadataDb::Labels(int axis,const std::wstring& value){std::vector<std::wstring> labels;try{std::lock_guard<std::mutex> l(impl->io);MdbConfig c;uint64_t t;if(impl->Sync(c,t)){Statement s(impl->db,"SELECT DISTINCT label FROM items WHERE axis=? AND value=? LIMIT 2");s.Number(1,axis);s.Text(2,value);while(s.Step()==SQLITE_ROW)labels.push_back(s.Text(0));}}catch(...){}return labels;}
std::vector<std::wstring> MetadataDb::Values(int axis){std::vector<std::wstring> values;std::lock_guard<std::mutex> l(impl->io);MdbConfig c;uint64_t t;if(impl->Sync(c,t)){Statement s(impl->db,"SELECT DISTINCT value FROM items WHERE axis=? ORDER BY value");s.Number(1,axis);while(s.Step()==SQLITE_ROW)values.push_back(s.Text(0));}return values;}
MdbStatus MetadataDb::Status(){std::lock_guard<std::mutex> l(impl->control);auto s=impl->status;s.pending=impl->tasks.size();s.working=impl->active||impl->pending;return s;}
void MetadataDb::WaitIdle(){std::unique_lock<std::mutex> l(impl->control);impl->idle.wait(l,[&]{return !impl->pending&&impl->active==0&&impl->tasks.empty();});}
void MetadataDb::Stop(){if(!impl)return;{std::lock_guard<std::mutex> l(impl->control);impl->stopping=true;++impl->epoch;impl->tasks.clear();impl->wake.notify_all();}for(auto& w:impl->workers)if(w.joinable())w.join();impl->workers.clear();{std::lock_guard<std::mutex> l(impl->io);std::vector<fs::path> roots;{std::lock_guard<std::mutex> c(impl->control);roots=impl->eraseRoots;}for(auto& p:roots)try{impl->Delete(p);}catch(...){impl->Error(L"MDB削除が未完了です。残存ファイルを確認してください。",true);}impl->Close();}{std::lock_guard<std::mutex> l(impl->control);impl->pending=false;impl->active=0;impl->idle.notify_all();}}
