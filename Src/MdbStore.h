#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
struct MdbTerm {int axis=0;std::wstring value,label;int roles=0;};
struct MdbRecord {std::wstring path,revision,text;std::vector<MdbTerm> terms;bool backup=false,failed=false;};
struct MdbRow {std::wstring key,label;bool multiple=false;};
struct MdbConfig {bool enabled=false;int mode=0,capacityMB=100;uint64_t Limit()const{return mode==2?0:mode==1?4000000000ULL:uint64_t(capacityMB)*1000000;}};
struct MdbStatus {uint64_t bytes=0,entries=0,hits=0,parsed=0;size_t pending=0;bool working=false,deletionFailed=false;std::wstring error;};
class MetadataDb {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    MetadataDb();~MetadataDb();
    MetadataDb(const MetadataDb&)=delete;MetadataDb& operator=(const MetadataDb&)=delete;
    void Configure(const std::filesystem::path&,MdbConfig,std::function<MdbRecord(const std::wstring&)>,std::function<bool(const std::wstring&)>);
    MdbRecord Read(const std::wstring&,int priority=-1,bool includeText=true);
    void Queue(const std::wstring&);
    void QueueCached(const std::vector<std::wstring>&);
    void Invalidate(const std::wstring&);
    void Reconcile(const std::wstring&,const std::vector<std::wstring>&);
    std::vector<std::wstring> Labels(int axis,const std::wstring& value);
    std::vector<std::wstring> Values(int axis);
    MdbStatus Status();void WaitIdle();void Stop();
};
MdbRecord ReadMdbSource(const std::wstring&);
std::wstring MdbRevision(const std::wstring&);
