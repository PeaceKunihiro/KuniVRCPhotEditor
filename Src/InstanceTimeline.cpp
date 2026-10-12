#define NOMINMAX
#include "Stage3.h"
#include "ExtendedXmp.h"
#include <regex>
#include <algorithm>
#include <stdexcept>
#include <winrt/base.h>
namespace stage3 {
namespace { const std::wregex personPattern(LR"(^(.*) \((usr_[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\)$)"); }
namespace { const std::wregex groupPattern(LR"(~group\((grp_[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\))"); }
Time Now(){FILETIME f{};GetSystemTimeAsFileTime(&f);ULARGE_INTEGER u{};u.LowPart=f.dwLowDateTime;u.HighPart=f.dwHighDateTime;return u.QuadPart/10000;}
Time ParseTime(const std::wstring& text){
    std::wsmatch m;static const std::wregex re(LR"(^([0-9]{4})[.\-]([0-9]{2})[.\-]([0-9]{2})[ T_]([0-9]{2})[:\-]([0-9]{2})[:\-]([0-9]{2})(?:\.([0-9]{1,7}))?(Z|[+\-][0-9]{2}:[0-9]{2})?$)");
    if(!std::regex_match(text,m,re))return 0;
    SYSTEMTIME s{};s.wYear=std::stoi(m[1]);s.wMonth=std::stoi(m[2]);s.wDay=std::stoi(m[3]);s.wHour=std::stoi(m[4]);s.wMinute=std::stoi(m[5]);s.wSecond=std::stoi(m[6]);
    if(m[7].matched){auto fraction=m[7].str()+L"000";s.wMilliseconds=std::stoi(fraction.substr(0,3));}
    FILETIME f{};if(!SystemTimeToFileTime(&s,&f))return 0;SYSTEMTIME check{};FileTimeToSystemTime(&f,&check);if(check.wYear!=s.wYear||check.wMonth!=s.wMonth||check.wDay!=s.wDay)return 0;
    auto zone=m[8].str();
    if(zone.empty()){SYSTEMTIME utc{},back{};if(!TzSpecificLocalTimeToSystemTime(nullptr,&s,&utc)||!SystemTimeToTzSpecificLocalTime(nullptr,&utc,&back))return 0;if(back.wHour!=s.wHour||back.wDay!=s.wDay)return 0;if(!SystemTimeToFileTime(&utc,&f))return 0;}
    ULARGE_INTEGER u{};u.LowPart=f.dwLowDateTime;u.HighPart=f.dwHighDateTime;Time time=u.QuadPart/10000;
    if(zone.size()==6){int h=std::stoi(zone.substr(1,2)),min=std::stoi(zone.substr(4,2));if(h>14||min>59||(h==14&&min))return 0;time+=(zone[0]==L'+'?-1:1)*(h*60+min)*60000LL;}
    return time;
}
std::wstring Iso(Time time){ULARGE_INTEGER u{};u.QuadPart=time*10000;FILETIME f{u.LowPart,u.HighPart};SYSTEMTIME s{};if(!time||!FileTimeToSystemTime(&f,&s))return {};wchar_t text[40]{};swprintf_s(text,L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond,s.wMilliseconds);return text;}
bool ParseLogLine(const std::wstring& line,Event& e){
    // Per-player room notifications precede OnPlayerLeft; they are not our own room boundary.
    if(line.find(L"] OnPlayerLeftRoom")!=std::wstring::npos)return false;
    auto joined=line.find(L"] OnPlayerJoined "),left=line.find(L"] OnPlayerLeft "),room=line.find(L"] Joining wrld_"),photo=line.find(L"] Took screenshot to: ");
    bool leftRoom=line.find(L"] OnLeftRoom")!=std::wstring::npos||line.find(L"] Successfully left room")!=std::wstring::npos;
    auto roomName=line.find(L"] Joining or Creating Room: ");
    bool relevant=roomName!=std::wstring::npos||joined!=std::wstring::npos||left!=std::wstring::npos||room!=std::wstring::npos||photo!=std::wstring::npos||leftRoom||line.find(L"OnPlayerJoined")!=std::wstring::npos||line.find(L"OnPlayerLeft")!=std::wstring::npos;
    if(!relevant)return false;e={};e.time=ParseTime(line.substr(0,19));if(!e.time)return true;
    if(roomName!=std::wstring::npos){e.kind=Kind::RoomName;e.worldName=line.substr(roomName+28);if(!e.worldName.empty()&&e.worldName.back()==L'\r')e.worldName.pop_back();return true;}
    if(room!=std::wstring::npos){auto location=line.substr(room+10);auto colon=location.find(L':');if(colon==std::wstring::npos)return true;e.kind=Kind::Room;e.world=location.substr(0,colon);std::wsmatch group;if(std::regex_search(location,group,groupPattern)){e.group=group[1];std::transform(e.group.begin(),e.group.end(),e.group.begin(),towlower);}e.instance=winrt::to_hstring(XmpMd5(winrt::to_string(location))).c_str();return true;}
    if(leftRoom){e.kind=Kind::LeftRoom;return true;}
    if(photo!=std::wstring::npos){e.kind=Kind::Photo;e.path=line.substr(photo+22);return true;}
    if(joined!=std::wstring::npos||left!=std::wstring::npos){bool join=joined!=std::wstring::npos;auto value=line.substr((join?joined:left)+(join?17:15));if(!value.empty()&&value.back()==L'\r')value.pop_back();
        std::wsmatch match;if(std::regex_match(value,match,personPattern)){e.person.name=match[1];e.person.id=match[2];}else e.person.name=value;
        if(!e.person.name.empty())e.kind=join?Kind::Join:Kind::Leave;return true;}
    return true;
}
void InstanceTimeline::Add(Event e){snapshotValid=false;if(e.kind==Kind::Photo)photosDirty=true;if(events.size()>=200000)throw std::runtime_error("Session event limit exceeded");if(e.kind==Kind::Invalid){damaged=true;return;}if(!events.empty()&&e.time<events.back().time)damaged=true;if(!events.empty()){auto& prior=events.back();if(e.time>=prior.time&&e.time-prior.time<=2000){if(e.kind==Kind::Room&&prior.kind==Kind::RoomName){if(!prior.instance.empty()){if(events.size()>=2&&events[events.size()-2].kind==Kind::Room&&events[events.size()-2].instance==prior.instance)events[events.size()-2].worldName.clear();}else e.worldName=prior.worldName;}else if(e.kind==Kind::RoomName&&prior.kind==Kind::Room&&prior.worldName.empty()){prior.worldName=e.worldName;e.instance=prior.instance;}}}e.order=events.size();events.push_back(std::move(e));}
Snapshot InstanceTimeline::At(Time time)const{
    if(snapshotValid&&snapshotTime==time)return snapshot;
    snapshotValid=false;
    Snapshot s;bool room=false,partial=damaged;
    for(auto& e:events){if(e.time>time)continue;if(e.time/1000==time/1000&&e.kind!=Kind::Photo)s.ambiguous=true;
        if(e.kind==Kind::Room){s.members.clear();s.world=e.world;s.worldName=e.worldName;s.group=e.group;s.instance=e.instance;room=true;}
        else if(e.kind==Kind::LeftRoom){s.members.clear();s.world.clear();s.worldName.clear();s.group.clear();s.instance.clear();room=false;}
        else if(e.kind==Kind::Join){if(e.person.id.empty()){s.members.push_back(e.person);partial=true;}else {auto found=std::find_if(s.members.begin(),s.members.end(),[&](auto& p){return p.id==e.person.id;});if(found==s.members.end())s.members.push_back(e.person);else *found=e.person;}}
        else if(e.kind==Kind::Leave){if(e.person.id.empty())partial=true;else {auto found=std::find_if(s.members.begin(),s.members.end(),[&](auto& p){return p.id==e.person.id;});if(found==s.members.end())partial=true;else s.members.erase(found);}}
    }
    s.status=!room?L"unknown":partial?L"partial":L"reconstructed";snapshot=s;snapshotTime=time;snapshotValid=true;return s;
}
std::vector<Time> InstanceTimeline::PhotoTimes(const std::wstring& path)const{
    if(photosDirty){decltype(photos) rebuilt;for(const auto& e:events)if(e.kind==Kind::Photo)rebuilt[e.path].push_back(e.time);photos=std::move(rebuilt);photosDirty=false;}
    auto found=photos.find(path);return found==photos.end()?std::vector<Time>{}:found->second;
}
}
