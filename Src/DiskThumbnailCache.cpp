#define NOMINMAX
#include "DiskThumbnailCache.h"
#include "ViewerSettings.h"
#include <bcrypt.h>
#include <fstream>
#include <cwctype>
#include <cstring>
#include <stdexcept>
#include <algorithm>
namespace {
namespace fs=std::filesystem;
std::array<BYTE,32> Hash(const void* data,size_t bytes){
    BCRYPT_ALG_HANDLE alg=nullptr;std::array<BYTE,32> digest{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("cache hash");
    auto result=BCryptHash(alg,nullptr,0,const_cast<PUCHAR>(static_cast<const BYTE*>(data)),static_cast<ULONG>(bytes),digest.data(),32);
    BCryptCloseAlgorithmProvider(alg,0);if(result<0)throw std::runtime_error("cache hash");return digest;
}
std::wstring Name(const std::wstring& key){auto hash=Hash(key.data(),key.size()*sizeof(wchar_t));std::wstring s=L"k12-";for(auto b:hash){s+=L"0123456789abcdef"[b>>4];s+=L"0123456789abcdef"[b&15];}return s+L".dtc";}
bool Owned(const fs::path& p){auto s=p.filename().wstring();return s.size()==72&&s.substr(0,4)==L"k12-"&&s.substr(68)==L".dtc"&&s.substr(4,64).find_first_not_of(L"0123456789abcdef")==std::wstring::npos;}
bool Safe(const fs::path& p){auto flags=GetFileAttributesW(p.c_str());return flags!=INVALID_FILE_ATTRIBUTES&&!(flags&FILE_ATTRIBUTE_REPARSE_POINT);}
std::wstring Folder(const std::wstring& value){auto s=NormalizeFolderAddress(value);for(auto& c:s)c=static_cast<wchar_t>(towlower(c));return s;}
bool Under(const std::wstring& folder,const std::vector<std::wstring>& bookmarks){for(auto& p:bookmarks){const auto& root=p;if(root.empty())continue;if(folder==root||(folder.size()>root.size()&&folder.compare(0,root.size(),root)==0&&(root.back()==L'\\'||folder[root.size()]==L'\\')))return true;}return false;}
struct Header {uint32_t magic=0x31435444,width=0,height=0,keyChars=0;std::array<BYTE,32> digest{};};
constexpr size_t MaxPixels=256*256*4;
}
void DiskThumbnailCache::ResetEntries(){entries.clear();lru.clear();folderStats.clear();usedBytes=0;}
void DiskThumbnailCache::EraseEntry(std::map<std::wstring,Entry>::iterator i){lru.erase({i->second.used,i->first});usedBytes-=i->second.bytes;auto folder=folderStats.find(i->second.folder);if(folder!=folderStats.end()){folder->second.bytes-=i->second.bytes;if(--folder->second.entries==0)folderStats.erase(folder);}entries.erase(i);}
void DiskThumbnailCache::SetEntry(const std::wstring& name,Entry value){
    auto prior=entries.find(name);if(prior!=entries.end())EraseEntry(prior);
    decltype(entries.begin()) inserted;try{inserted=entries.emplace(name,value).first;}catch(...){loadedEpoch=UINT64_MAX;throw;}
    try{lru.emplace(value.used,name);}catch(...){entries.erase(inserted);loadedEpoch=UINT64_MAX;throw;}
    usedBytes+=value.bytes;auto& folder=folderStats[value.folder];folder.folder=value.folder;++folder.entries;folder.bytes+=value.bytes;
}
void DiskThumbnailCache::Configure(const fs::path& path,Config value){
    if(value.limit<128ull*1024*1024||value.limit>2ull*1024*1024*1024||value.minutes<1||value.minutes>1440||value.visits<2||value.visits>100)throw std::runtime_error("disk cache configuration");
    for(auto& p:value.bookmarks)p=Folder(p);
    std::lock_guard<std::mutex> lock(control);
    if(!config.enabled||!value.enabled||root!=path)visits.clear();
    if(root!=path)pendingVisits.clear();
    root=path;config=value;++epoch;pending=true;status.pending=true;
    if(!maintenance.joinable()){stopping=false;maintenance=std::thread([this]{Maintain();});}wake.notify_one();
}
void DiskThumbnailCache::Visit(const std::wstring& folder,Clock::time_point now){
    auto key=Folder(folder);std::lock_guard<std::mutex> lock(control);if(!config.enabled)return;
    for(auto i=visits.begin();i!=visits.end();){while(!i->second.empty()&&now-i->second.front()>std::chrono::minutes(config.minutes))i->second.pop_front();if(i->second.empty())i=visits.erase(i);else ++i;}
    if(visits.size()>=256&&!visits.count(key))visits.erase(visits.begin());auto& list=visits[key];list.push_back(now);while(list.size()>100)list.pop_front();
    auto stamp=fs::file_time_type::clock::now().time_since_epoch().count();
    pendingVisits.emplace_back(key,stamp);if(pendingVisits.size()>4096)pendingVisits.erase(pendingVisits.begin());pending=true;status.pending=true;wake.notify_one();
}
bool DiskThumbnailCache::PriorityFolder(const std::wstring& folder){auto key=Folder(folder);std::lock_guard<std::mutex> lock(control);return config.enabled&&config.bookmarkPriority&&Under(key,config.bookmarks);}
size_t DiskThumbnailCache::PrefetchLimit(const std::wstring& folder){auto key=Folder(folder);std::lock_guard<std::mutex> lock(control);return config.enabled&&config.bookmarkPriority&&Under(key,config.bookmarks)?config.limit/(256*256*4+sizeof(Header)+65536):0;}
bool DiskThumbnailCache::Eligible(const std::wstring& folder,Clock::time_point now){
    auto key=Folder(folder);std::lock_guard<std::mutex> lock(control);if(!config.enabled)return false;if(config.bookmarkPriority&&Under(key,config.bookmarks))return true;auto i=visits.find(key);if(i==visits.end())return false;
    while(!i->second.empty()&&now-i->second.front()>std::chrono::minutes(config.minutes))i->second.pop_front();return i->second.size()>=static_cast<size_t>(config.visits);
}
bool DiskThumbnailCache::Snapshot(Config& c,fs::path& p,uint64_t& ticket){std::lock_guard<std::mutex> lock(control);c=config;p=root;ticket=epoch;return c.enabled&&!stopping&&!erase;}
bool DiskThumbnailCache::Prepare(const fs::path& path){
    if(path.empty())throw std::runtime_error("empty cache path");
    // Reject junctions/symlinks both at the cache and its executable-directory parent.
    if(!Safe(path.parent_path()))throw std::runtime_error("cache parent inaccessible or redirected");
    if(!fs::exists(path))fs::create_directory(path);
    if(!Safe(path)||!fs::is_directory(path))throw std::runtime_error("cache inaccessible or redirected");
    bool load=loadedRoot!=path||loadedEpoch==UINT64_MAX;
    if(loadedRoot!=path){ResetEntries();folderUses.clear();loadedRoot=path;loadedEpoch=UINT64_MAX;}
    if(load){auto index=path/L"folders.idx";if(fs::exists(index)&&Safe(index)&&fs::file_size(index)<=2*1024*1024){
        std::ifstream in(index,std::ios::binary);uint32_t magic=0,count=0;in.read(reinterpret_cast<char*>(&magic),4);in.read(reinterpret_cast<char*>(&count),4);std::map<std::wstring,int64_t> parsed;
        if(magic==0x35354644&&count<=4096){for(uint32_t n=0;n<count&&in;++n){uint32_t chars=0;int64_t stamp=0;in.read(reinterpret_cast<char*>(&chars),4);in.read(reinterpret_cast<char*>(&stamp),8);if(!chars||chars>32768){in.setstate(std::ios::failbit);break;}std::wstring name(chars,L'\0');in.read(reinterpret_cast<char*>(name.data()),chars*sizeof(wchar_t));if(in&&name.find(L'\0')==std::wstring::npos)parsed[name]=stamp;else in.setstate(std::ios::failbit);}if(in)folderUses=std::move(parsed);}
    }}
    FolderIndex(path);
    if(loadedEpoch==UINT64_MAX){ResetEntries();for(auto& item:fs::directory_iterator(path)){
        if(item.path().extension()==L".tmp"&&Owned(item.path().stem())&&Safe(item.path())&&item.is_regular_file()){std::error_code ec;fs::remove(item.path(),ec);if(ec)throw std::runtime_error("cache temp cleanup");continue;}
        if(Owned(item.path())&&Safe(item.path())&&item.is_regular_file()){
            auto size=item.file_size();std::wstring folder;std::ifstream in(item.path(),std::ios::binary);Header h{};in.read(reinterpret_cast<char*>(&h),sizeof(h));
            if(in&&h.magic==0x31435444&&h.keyChars&&h.keyChars<=32768){std::wstring key(h.keyChars,L'\0');in.read(reinterpret_cast<char*>(key.data()),h.keyChars*sizeof(wchar_t));auto end=key.find(L'|');if(in&&end!=std::wstring::npos)try{folder=Folder(fs::path(key.substr(0,end)).parent_path().wstring());}catch(...){}}
            SetEntry(item.path().filename().wstring(),{static_cast<size_t>(size),item.last_write_time(),folder});
        }
    }loadedEpoch=0;}
    return true;
}
void DiskThumbnailCache::FolderIndex(const fs::path& path){
    std::vector<std::pair<std::wstring,int64_t>> updates;{std::lock_guard<std::mutex> lock(control);updates.swap(pendingVisits);}if(updates.empty())return;
    for(auto& update:updates)folderUses[update.first]=update.second;
    while(folderUses.size()>4096){auto old=std::min_element(folderUses.begin(),folderUses.end(),[](auto& a,auto& b){return a.second<b.second;});folderUses.erase(old);}
    size_t indexBytes=8;for(auto& f:folderUses)indexBytes+=12+f.first.size()*sizeof(wchar_t);
    while(indexBytes>1024*1024&&!folderUses.empty()){auto old=std::min_element(folderUses.begin(),folderUses.end(),[](auto& a,auto& b){return a.second<b.second;});indexBytes-=12+old->first.size()*sizeof(wchar_t);folderUses.erase(old);}
    auto dest=path/L"folders.idx",temp=path/L"folders.idx.tmp";
    if((fs::exists(dest)&&!Safe(dest))||(fs::exists(temp)&&!Safe(temp)))throw std::runtime_error("redirected folder index");
    {std::ofstream out(temp,std::ios::binary|std::ios::trunc);uint32_t magic=0x35354644,count=static_cast<uint32_t>(folderUses.size());out.write(reinterpret_cast<char*>(&magic),4);out.write(reinterpret_cast<char*>(&count),4);for(auto& f:folderUses){uint32_t chars=static_cast<uint32_t>(f.first.size());out.write(reinterpret_cast<char*>(&chars),4);out.write(reinterpret_cast<char*>(&f.second),8);out.write(reinterpret_cast<const char*>(f.first.data()),chars*sizeof(wchar_t));}out.flush();if(!out)throw std::runtime_error("folder index write");}
    if(!MoveFileExW(temp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("folder index replace");
}
void DiskThumbnailCache::Publish(const std::wstring& error){std::lock_guard<std::mutex> lock(control);status.bytes=usedBytes;status.entries=entries.size();status.error=error;}
bool DiskThumbnailCache::Trim(size_t limit,size_t extra,const std::wstring* incoming){
    limit=limit>2*1024*1024?limit-2*1024*1024:0;
    if(usedBytes+extra<=limit)return true;
    Config c;{std::lock_guard<std::mutex> lock(control);c=config;}
    auto rank=[&](const std::wstring& f){bool priority=c.bookmarkPriority&&!f.empty()&&folderUses.count(f)&&Under(f,c.bookmarks);return std::make_pair(priority,priority?folderUses[f]:int64_t(0));};
    std::vector<std::wstring> order;for(auto& e:entries)order.push_back(e.first);
    if(c.bookmarkPriority)std::sort(order.begin(),order.end(),[&](auto& a,auto& b){auto& x=entries.at(a);auto& y=entries.at(b);auto rx=rank(x.folder),ry=rank(y.folder);return rx!=ry?rx<ry:(x.used!=y.used?x.used<y.used:a<b);});else{order.clear();for(auto& e:lru)order.push_back(e.second);}
    for(auto& name:order){if(usedBytes+extra<=limit)break;auto old=entries.find(name);
        if(incoming&&rank(*incoming)<rank(old->second.folder))return false;
        if(!Safe(loadedRoot))return false;
        if(!fs::exists(loadedRoot/old->first)){EraseEntry(old);continue;}
        if(!Safe(loadedRoot/old->first))return false;
        std::error_code ec;bool removed=fs::remove(loadedRoot/old->first,ec);if(ec||!removed)return false;EraseEntry(old);
    }return usedBytes+extra<=limit;
}
DiskThumbnailCache::Presence DiskThumbnailCache::Probe(const std::wstring& key){
    fs::path path;uint64_t ticket;
    {std::lock_guard<std::mutex> lock(control);if(!config.enabled||stopping)return Presence::Missing;if(erase)return Presence::Pending;path=root;ticket=epoch;}
    std::unique_lock<std::mutex> diskLock(io,std::try_to_lock);
    if(!diskLock.owns_lock())return Presence::Pending;
    if(loadedRoot!=path||loadedEpoch==UINT64_MAX){
        std::lock_guard<std::mutex> lock(control);
        if(!pending&&!working&&!status.error.empty())throw std::runtime_error("Disk cache index unavailable");
        return Presence::Pending;
    }
    auto found=key.size()<=32768&&entries.count(Name(key))!=0;
    {std::lock_guard<std::mutex> lock(control);if(!config.enabled||stopping)return Presence::Missing;if(ticket!=epoch||root!=path||erase)return Presence::Pending;}
    return found?Presence::Present:Presence::Missing;
}
bool DiskThumbnailCache::Get(const std::wstring& key,Pixels& pixels,bool touch){
    try{Config c;fs::path path;uint64_t ticket;if(!Snapshot(c,path,ticket))return false;
        std::lock_guard<std::mutex> diskLock(io);Prepare(path);auto name=Name(key);auto found=entries.find(name);if(found==entries.end())return false;
        if(!fs::exists(path/name)){EraseEntry(found);Publish();return false;}if(!Safe(path/name))return false;std::ifstream in(path/name,std::ios::binary);Header h{};in.read(reinterpret_cast<char*>(&h),sizeof(h));
        size_t bytes=static_cast<size_t>(h.width)*h.height*4;
        if(!in||h.magic!=0x31435444||!h.width||h.width>256||!h.height||h.height>256||h.keyChars!=key.size()||h.keyChars>32768||bytes>MaxPixels||found->second.bytes!=sizeof(h)+h.keyChars*sizeof(wchar_t)+bytes)return false;
        std::wstring stored(h.keyChars,L'\0');in.read(reinterpret_cast<char*>(stored.data()),h.keyChars*sizeof(wchar_t));if(stored!=key)return false;
        Pixels candidate{h.width,h.height,std::vector<BYTE>(bytes)};in.read(reinterpret_cast<char*>(candidate.data.data()),bytes);
        if(!in||Hash(candidate.data.data(),bytes)!=h.digest)return false;
        {std::lock_guard<std::mutex> lock(control);if(epoch!=ticket||!config.enabled||erase)return false;}
        pixels=std::move(candidate);if(!touch)return true;std::error_code ec;auto now=fs::file_time_type::clock::now();fs::last_write_time(path/name,now,ec);auto value=found->second;value.used=now;SetEntry(name,value);Publish();return true;
    }catch(...){std::lock_guard<std::mutex> lock(io);Publish(L"キャッシュ読込み不可。原画像から生成します。");return false;}
}
void DiskThumbnailCache::Put(const std::wstring& key,const std::wstring& source,const Pixels& pixels){
    fs::path temporary;
    try{Config c;fs::path path;uint64_t ticket;if(!Snapshot(c,path,ticket)||!Eligible(fs::path(source).parent_path().wstring()))return;
        if(key.size()>32768||!pixels.width||pixels.width>256||!pixels.height||pixels.height>256||pixels.data.size()!=static_cast<size_t>(pixels.width)*pixels.height*4)return;
        std::lock_guard<std::mutex> diskLock(io);Prepare(path);auto name=Name(key);if(entries.count(name)){
            // A corrupt existing entry must be replaceable after a failed disk hit.
            if(fs::exists(path/name)){if(!Safe(path/name))return;std::error_code ec;fs::remove(path/name,ec);if(ec)return;}auto old=entries.find(name);if(old!=entries.end())EraseEntry(old);
        }
        Header h;h.width=pixels.width;h.height=pixels.height;h.keyChars=static_cast<uint32_t>(key.size());h.digest=Hash(pixels.data.data(),pixels.data.size());
        size_t bytes=sizeof(h)+key.size()*sizeof(wchar_t)+pixels.data.size();auto folder=Folder(fs::path(source).parent_path().wstring());if(!Trim(c.limit,bytes,&folder)){Publish(L"容量整理不可。新規キャッシュ保存を省略します。");return;}
        auto temp=path/(name+L".tmp");temporary=temp;if(fs::exists(temp)&&!Safe(temp))return;
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<char*>(&h),sizeof(h));out.write(reinterpret_cast<const char*>(key.data()),key.size()*sizeof(wchar_t));out.write(reinterpret_cast<const char*>(pixels.data.data()),pixels.data.size());out.flush();if(!out)throw std::runtime_error("cache write");}
        bool valid;{std::lock_guard<std::mutex> lock(control);valid=epoch==ticket&&config.enabled&&!erase;
            if(valid)valid=MoveFileExW(temp.c_str(),(path/name).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;}
        if(!valid){std::error_code ec;fs::remove(temp,ec);Publish();return;}
        SetEntry(name,{bytes,fs::last_write_time(path/name),Folder(fs::path(source).parent_path().wstring())});Publish();
    }catch(...){std::lock_guard<std::mutex> lock(io);if(!temporary.empty()&&Safe(temporary.parent_path())&&Safe(temporary)){std::error_code ec;fs::remove(temporary,ec);}Publish(L"キャッシュ保存不可。原画像は変更していません。");}
}
void DiskThumbnailCache::Clear(){std::lock_guard<std::mutex> lock(control);++epoch;erase=true;pending=true;status.pending=true;if(!maintenance.joinable()){stopping=false;maintenance=std::thread([this]{Maintain();});}wake.notify_one();}
DiskThumbnailCache::Status DiskThumbnailCache::GetStatus(){std::lock_guard<std::mutex> lock(control);return status;}
void DiskThumbnailCache::Maintain(){
    for(;;){Config c;fs::path path;bool clear;uint64_t ticket;
        {std::unique_lock<std::mutex> lock(control);wake.wait(lock,[&]{return stopping||pending;});if(stopping)break;c=config;path=root;clear=erase;ticket=epoch;pending=false;working=true;}
        {std::lock_guard<std::mutex> diskLock(io);try{
            if(c.enabled||fs::exists(path)){Prepare(path);
                if(clear){for(auto i=entries.begin();i!=entries.end();){if(!Safe(path))throw std::runtime_error("redirected cache");if(!fs::exists(path/i->first)){auto removed=i++;EraseEntry(removed);continue;}if(!Safe(path/i->first))throw std::runtime_error("redirected cache");std::error_code ec;fs::remove(path/i->first,ec);if(ec)throw std::runtime_error("cache delete");auto removed=i++;EraseEntry(removed);}}
                else if(c.enabled&&!Trim(c.limit))throw std::runtime_error("cache trim");
            }else if(clear){ResetEntries();loadedRoot=path;loadedEpoch=UINT64_MAX;}Publish();
        }catch(...){Publish(L"キャッシュの確認・整理に失敗しました。");}}
        {std::lock_guard<std::mutex> lock(control);if(epoch==ticket)erase=false;else if(clear)pending=true;working=false;status.pending=pending;idle.notify_all();}
    }
}
void DiskThumbnailCache::WaitIdle(){std::unique_lock<std::mutex> lock(control);idle.wait(lock,[&]{return !pending&&!working;});}
void DiskThumbnailCache::Stop(){ {std::lock_guard<std::mutex> lock(control);stopping=true;++epoch;wake.notify_all();}if(maintenance.joinable())maintenance.join();}

bool DiskThumbnailCache::GetFolders(std::vector<FolderStatus>& result){std::unique_lock<std::mutex> lock(io,std::try_to_lock);if(!lock.owns_lock())return false;std::vector<FolderStatus> rows;rows.reserve(folderStats.size());for(auto& row:folderStats)rows.push_back(row.second);result=std::move(rows);return true;}
