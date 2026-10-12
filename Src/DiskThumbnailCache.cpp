#define NOMINMAX
#include "DiskThumbnailCache.h"
#include "ViewerSettings.h"
#include <bcrypt.h>
#include <fstream>
#include <cwctype>
#include <cstring>
#include <stdexcept>
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
struct Header {uint32_t magic=0x31435444,width=0,height=0,keyChars=0;std::array<BYTE,32> digest{};};
constexpr size_t MaxPixels=256*192*4;
}
void DiskThumbnailCache::ResetEntries(){entries.clear();lru.clear();usedBytes=0;}
void DiskThumbnailCache::EraseEntry(std::map<std::wstring,Entry>::iterator i){lru.erase({i->second.used,i->first});usedBytes-=i->second.bytes;entries.erase(i);}
void DiskThumbnailCache::SetEntry(const std::wstring& name,Entry value){
    auto prior=entries.find(name);if(prior!=entries.end())EraseEntry(prior);
    decltype(entries.begin()) inserted;try{inserted=entries.emplace(name,value).first;}catch(...){loadedEpoch=UINT64_MAX;throw;}
    try{lru.emplace(value.used,name);}catch(...){entries.erase(inserted);loadedEpoch=UINT64_MAX;throw;}
    usedBytes+=value.bytes;
}
void DiskThumbnailCache::Configure(const fs::path& path,Config value){
    if(value.limit<128ull*1024*1024||value.limit>2ull*1024*1024*1024||value.minutes<1||value.minutes>1440||value.visits<2||value.visits>100)throw std::runtime_error("disk cache configuration");
    std::lock_guard<std::mutex> lock(control);
    if(!config.enabled||!value.enabled||root!=path)visits.clear();
    root=path;config=value;++epoch;pending=true;status.pending=true;
    if(!maintenance.joinable()){stopping=false;maintenance=std::thread([this]{Maintain();});}wake.notify_one();
}
void DiskThumbnailCache::Visit(const std::wstring& folder,Clock::time_point now){
    auto key=Folder(folder);std::lock_guard<std::mutex> lock(control);if(!config.enabled)return;
    for(auto i=visits.begin();i!=visits.end();){while(!i->second.empty()&&now-i->second.front()>std::chrono::minutes(config.minutes))i->second.pop_front();if(i->second.empty())i=visits.erase(i);else ++i;}
    if(visits.size()>=256&&!visits.count(key))visits.erase(visits.begin());auto& list=visits[key];list.push_back(now);while(list.size()>100)list.pop_front();
}
bool DiskThumbnailCache::Eligible(const std::wstring& folder,Clock::time_point now){
    auto key=Folder(folder);std::lock_guard<std::mutex> lock(control);if(!config.enabled)return false;auto i=visits.find(key);if(i==visits.end())return false;
    while(!i->second.empty()&&now-i->second.front()>std::chrono::minutes(config.minutes))i->second.pop_front();return i->second.size()>=static_cast<size_t>(config.visits);
}
bool DiskThumbnailCache::Snapshot(Config& c,fs::path& p,uint64_t& ticket){std::lock_guard<std::mutex> lock(control);c=config;p=root;ticket=epoch;return c.enabled&&!stopping&&!erase;}
bool DiskThumbnailCache::Prepare(const fs::path& path){
    if(path.empty())throw std::runtime_error("empty cache path");
    // Reject junctions/symlinks both at the cache and its executable-directory parent.
    if(!Safe(path.parent_path()))throw std::runtime_error("cache parent inaccessible or redirected");
    if(!fs::exists(path))fs::create_directory(path);
    if(!Safe(path)||!fs::is_directory(path))throw std::runtime_error("cache inaccessible or redirected");
    if(loadedRoot!=path){ResetEntries();loadedRoot=path;loadedEpoch=UINT64_MAX;}
    if(loadedEpoch==UINT64_MAX){ResetEntries();for(auto& item:fs::directory_iterator(path)){
        if(item.path().extension()==L".tmp"&&Owned(item.path().stem())&&Safe(item.path())&&item.is_regular_file()){std::error_code ec;fs::remove(item.path(),ec);if(ec)throw std::runtime_error("cache temp cleanup");continue;}
        if(Owned(item.path())&&Safe(item.path())&&item.is_regular_file()){
            auto size=item.file_size();SetEntry(item.path().filename().wstring(),{static_cast<size_t>(size),item.last_write_time()});
        }
    }loadedEpoch=0;}
    return true;
}
void DiskThumbnailCache::Publish(const std::wstring& error){std::lock_guard<std::mutex> lock(control);status.bytes=usedBytes;status.entries=entries.size();status.error=error;}
bool DiskThumbnailCache::Trim(size_t limit,size_t extra){
    while(usedBytes+extra>limit&&!entries.empty()){
        auto old=entries.find(lru.begin()->second);
        if(!Safe(loadedRoot))return false;
        if(!fs::exists(loadedRoot/old->first)){EraseEntry(old);continue;}
        if(!Safe(loadedRoot/old->first))return false;
        std::error_code ec;bool removed=fs::remove(loadedRoot/old->first,ec);if(ec||!removed)return false;EraseEntry(old);
    }return usedBytes+extra<=limit;
}
bool DiskThumbnailCache::Get(const std::wstring& key,Pixels& pixels){
    try{Config c;fs::path path;uint64_t ticket;if(!Snapshot(c,path,ticket))return false;
        std::lock_guard<std::mutex> diskLock(io);Prepare(path);auto name=Name(key);auto found=entries.find(name);if(found==entries.end())return false;
        if(!fs::exists(path/name)){EraseEntry(found);Publish();return false;}if(!Safe(path/name))return false;std::ifstream in(path/name,std::ios::binary);Header h{};in.read(reinterpret_cast<char*>(&h),sizeof(h));
        size_t bytes=static_cast<size_t>(h.width)*h.height*4;
        if(!in||h.magic!=0x31435444||!h.width||h.width>256||!h.height||h.height>192||h.keyChars!=key.size()||h.keyChars>32768||bytes>MaxPixels||found->second.bytes!=sizeof(h)+h.keyChars*sizeof(wchar_t)+bytes)return false;
        std::wstring stored(h.keyChars,L'\0');in.read(reinterpret_cast<char*>(stored.data()),h.keyChars*sizeof(wchar_t));if(stored!=key)return false;
        Pixels candidate{h.width,h.height,std::vector<BYTE>(bytes)};in.read(reinterpret_cast<char*>(candidate.data.data()),bytes);
        if(!in||Hash(candidate.data.data(),bytes)!=h.digest)return false;
        {std::lock_guard<std::mutex> lock(control);if(epoch!=ticket||!config.enabled||erase)return false;}
        pixels=std::move(candidate);std::error_code ec;auto now=fs::file_time_type::clock::now();fs::last_write_time(path/name,now,ec);auto value=found->second;value.used=now;SetEntry(name,value);Publish();return true;
    }catch(...){std::lock_guard<std::mutex> lock(io);Publish(L"キャッシュ読込み不可。原画像から生成します。");return false;}
}
void DiskThumbnailCache::Put(const std::wstring& key,const std::wstring& source,const Pixels& pixels){
    fs::path temporary;
    try{Config c;fs::path path;uint64_t ticket;if(!Snapshot(c,path,ticket)||!Eligible(fs::path(source).parent_path().wstring()))return;
        if(key.size()>32768||!pixels.width||pixels.width>256||!pixels.height||pixels.height>192||pixels.data.size()!=static_cast<size_t>(pixels.width)*pixels.height*4)return;
        std::lock_guard<std::mutex> diskLock(io);Prepare(path);auto name=Name(key);if(entries.count(name)){
            // A corrupt existing entry must be replaceable after a failed disk hit.
            if(fs::exists(path/name)){if(!Safe(path/name))return;std::error_code ec;fs::remove(path/name,ec);if(ec)return;}auto old=entries.find(name);if(old!=entries.end())EraseEntry(old);
        }
        Header h;h.width=pixels.width;h.height=pixels.height;h.keyChars=static_cast<uint32_t>(key.size());h.digest=Hash(pixels.data.data(),pixels.data.size());
        size_t bytes=sizeof(h)+key.size()*sizeof(wchar_t)+pixels.data.size();if(!Trim(c.limit,bytes)){Publish(L"容量整理不可。新規キャッシュ保存を省略します。");return;}
        auto temp=path/(name+L".tmp");temporary=temp;if(fs::exists(temp)&&!Safe(temp))return;
        {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<char*>(&h),sizeof(h));out.write(reinterpret_cast<const char*>(key.data()),key.size()*sizeof(wchar_t));out.write(reinterpret_cast<const char*>(pixels.data.data()),pixels.data.size());out.flush();if(!out)throw std::runtime_error("cache write");}
        bool valid;{std::lock_guard<std::mutex> lock(control);valid=epoch==ticket&&config.enabled&&!erase;
            if(valid)valid=MoveFileExW(temp.c_str(),(path/name).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;}
        if(!valid){std::error_code ec;fs::remove(temp,ec);Publish();return;}
        SetEntry(name,{bytes,fs::last_write_time(path/name)});Publish();
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
