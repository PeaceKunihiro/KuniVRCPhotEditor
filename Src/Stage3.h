#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <ostream>
#include <map>
namespace stage3 {
using Time=int64_t; // UTC milliseconds since Windows epoch.
struct Person {std::wstring name,id;};
enum class Kind {Join,Leave,Room,LeftRoom,Photo,Invalid,RoomName};
struct Event {Time time=0;Kind kind=Kind::Invalid;Person person;std::wstring world,instance,path;uint64_t order=0;std::wstring worldName,group;};
struct Snapshot {std::vector<Person> members;std::wstring status=L"unknown",world,instance;bool ambiguous=false;std::wstring worldName,group;};
Time Now();Time ParseTime(const std::wstring& text);std::wstring Iso(Time time);
bool ParseLogLine(const std::wstring& line,Event& event);
class InstanceTimeline {
    std::vector<Event> events;bool damaged=false;
    mutable bool snapshotValid=false,photosDirty=true;mutable Time snapshotTime=0;mutable Snapshot snapshot;
    struct PhotoLess{bool operator()(const std::wstring& a,const std::wstring& b)const{return _wcsicmp(a.c_str(),b.c_str())<0;}};
    mutable std::map<std::wstring,std::vector<Time>,PhotoLess> photos;
public:
    void Add(Event event);void Invalidate(){damaged=true;snapshotValid=false;}void Clear(){events.clear();damaged=false;snapshotValid=false;snapshot={};photos.clear();photosDirty=true;}
    Snapshot At(Time time)const;
    std::vector<Time> PhotoTimes(const std::wstring& path)const;
};
class LogMonitor {
    uint64_t offset=0;std::string unfinished;std::wstring identity;Time lastTime=0;unsigned resets=0;
public:
    InstanceTimeline timeline;bool invalid=false;
    void Read(const std::filesystem::path& path);
    std::wstring Session()const{return identity+L":reset:"+std::to_wstring(resets);}
};
struct Candidate {Person person;std::wstring raw,method,bounds;Time observed=0;unsigned count=0;double score=0;};
struct Record {std::wstring session,world,instance,timeSource,timeStatus,memberStatus,candidateStatus=L"not-observed",candidateReason;Time reference=0,detected=0;std::vector<Person> members;std::vector<Candidate> candidates;std::wstring worldName,group;};
std::string RecordXml(const Record& record,bool saveUserIds=false);
struct Decision {bool ready=false;std::wstring reason;Record record;};
Decision Correlate(const std::wstring& path,const std::wstring& date,const std::wstring& world,Time detected,const LogMonitor& log);
Decision CorrelateSteam(const std::wstring& path,Time detected,const LogMonitor& log);
struct Config {std::wstring logFolder,photoFolder,outputFolder,selectedLog;bool autoRecord=false,autoStart=false;std::wstring steamFolder;DWORD processId=0;int graceSeconds=5,retrySeconds=60,backupLimit=10;bool saveUserIds=false,saveGroupIds=true;bool ocrEnabled=false;};
struct Item {std::wstring path,state,reason,output;};
bool SeparateFolders(const std::filesystem::path& input,const std::filesystem::path& output);
void EvaluateReferences(const std::filesystem::path& log,const std::filesystem::path& photos,const std::filesystem::path& samples,std::ostream& report);
void RunMonitorTests(const std::filesystem::path& directory,std::ostream& report);
void ShowMonitorDialog();std::wstring MonitorStatus();void Shutdown();void RunTests(const std::filesystem::path& directory,std::ostream& report);
bool RecordingOcr();bool Stopping();void RequestStop();
bool Recording();void ToggleRecording();void AutoTick();
std::filesystem::path LatestLog(const std::filesystem::path& folder);
}
