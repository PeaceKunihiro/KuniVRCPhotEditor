#pragma once
#include "ThumbnailCache.h"
#include <map>
#include <deque>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <array>

// Disk I/O is confined to thumbnail workers and a maintenance worker.
class DiskThumbnailCache {
public:
    using Clock=std::chrono::steady_clock;
    struct Config {bool enabled=false;size_t limit=512ull*1024*1024;int minutes=30,visits=5;};
    struct Status {size_t bytes=0,entries=0;bool pending=false;std::wstring error;};
    ~DiskThumbnailCache(){Stop();}
    void Configure(const std::filesystem::path&,Config);
    void Visit(const std::wstring&,Clock::time_point now=Clock::now());
    bool Eligible(const std::wstring&,Clock::time_point now=Clock::now());
    bool Get(const std::wstring&,Pixels&);
    void Put(const std::wstring&,const std::wstring& source,const Pixels&);
    void Clear();
    Status GetStatus();
    void WaitIdle();
    void Stop();
private:
    struct Entry {size_t bytes;std::filesystem::file_time_type used;};
    std::mutex control,io;
    std::condition_variable wake,idle;
    std::thread maintenance;
    bool stopping=false,pending=false,working=false,erase=false;
    uint64_t epoch=0,loadedEpoch=UINT64_MAX;
    Config config;
    std::filesystem::path root,loadedRoot;
    std::map<std::wstring,std::deque<Clock::time_point>> visits;
    std::map<std::wstring,Entry> entries;
    Status status;
    void Maintain();
    bool Snapshot(Config&,std::filesystem::path&,uint64_t&);
    bool Prepare(const std::filesystem::path&);
    bool Trim(size_t,size_t extra=0);
    void Publish(const std::wstring& error=L"");
};
