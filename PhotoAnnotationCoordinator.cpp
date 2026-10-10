#define NOMINMAX
#include "Stage3.h"
#include "MetadataModel.h"
#include <regex>
#include <winrt/base.h>
namespace stage3 {
namespace {
std::wstring Escape(const std::wstring& value){std::wstring out;for(auto c:value){switch(c){case L'&':out+=L"&amp;";break;case L'<':out+=L"&lt;";break;case L'>':out+=L"&gt;";break;case L'\"':out+=L"&quot;";break;default:out+=c;}}return out;}
}
std::string RecordXml(const Record& r){
    std::wstring xml=L"<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description rdf:about='' xmlns:kpe='urn:kunivrcphoteditor:metadata:1.0'><kpe:CaptureRecord rdf:parseType='Resource'>";
    auto field=[&](const wchar_t* name,const std::wstring& value){xml+=L"<kpe:"+std::wstring(name)+L">"+Escape(value)+L"</kpe:"+name+L">";};
    field(L"SchemaVersion",L"1");field(L"ToolVersion",L"0.4.3");field(L"ReferenceTime",Iso(r.reference));field(L"DetectedAt",Iso(r.detected));field(L"ReferenceTimeSource",r.timeSource);field(L"TimeStatus",r.timeStatus);field(L"SessionId",r.session);field(L"WorldID",r.world);field(L"InstanceId",r.instance);field(L"MemberListStatus",r.memberStatus);field(L"CandidateStatus",r.candidateStatus);field(L"CandidateWindowSeconds",L"3");if(!r.candidateReason.empty())field(L"CandidateReason",r.candidateReason);
    xml+=L"<kpe:InstanceMembers><rdf:Seq>";for(auto& p:r.members){xml+=L"<rdf:li rdf:parseType='Resource'>";field(L"DisplayName",p.name);field(L"UserId",p.id);xml+=L"</rdf:li>";}xml+=L"</rdf:Seq></kpe:InstanceMembers><kpe:ModelCandidates><rdf:Seq>";for(auto& candidate:r.candidates){xml+=L"<rdf:li rdf:parseType='Resource'>";field(L"DisplayName",candidate.person.name);field(L"UserId",candidate.person.id);field(L"RawText",candidate.raw);field(L"ObservedAt",Iso(candidate.observed));field(L"ObservationCount",std::to_wstring(candidate.count));field(L"MatchMethod",candidate.method);field(L"MatchScore",std::to_wstring(candidate.score));field(L"OcrConfidence",L"");field(L"Bounds",candidate.bounds);xml+=L"</rdf:li>";}xml+=L"</rdf:Seq></kpe:ModelCandidates></kpe:CaptureRecord></rdf:Description></rdf:RDF></x:xmpmeta>";return winrt::to_string(xml);
}
Decision Correlate(const std::wstring& path,const std::wstring& date,const std::wstring& world,Time detected,const LogMonitor& log){
    Decision d;auto times=log.timeline.PhotoTimes(path);if(times.size()!=1){d.reason=times.empty()?L"撮影ログなし：手動コピーとの区別不可":L"同一写真の撮影ログが複数あり曖昧";return d;}
    Time target=0;std::wstring source;std::wsmatch m;auto filename=std::filesystem::path(path).filename().wstring();
    if(!date.empty()){target=ParseTime(date);source=L"xmp";if(!target){d.reason=L"画像内日時を解釈できません";return d;}}
    Time named=0;if(std::regex_match(filename,m,std::wregex(LR"(^VRChat[_-]([0-9]{4}-[0-9]{2}-[0-9]{2})[_-]([0-9]{2}-[0-9]{2}-[0-9]{2})(\.[0-9]{1,7})_[0-9]+x[0-9]+\.(png|jpg|jpeg)$)",std::regex::icase)))named=ParseTime(m[1].str()+L" "+m[2].str()+m[3].str());
    if(target&&named&&std::abs(target-named)>1000){d.reason=L"XMPとファイル名の日時が矛盾";return d;}
    if(!target&&named){target=named;source=L"filename";}if(!target){target=times[0];source=L"photo-log";}
    if(std::abs(target-times[0])>5000){d.reason=L"撮影ログと撮影日時が矛盾（5秒超）";return d;}
    auto snapshot=log.timeline.At(target);if(snapshot.ambiguous){d.reason=L"撮影と入退室・インスタンス境界が同秒で曖昧";return d;}
    if(snapshot.status==L"unknown"){d.reason=L"対象時刻のインスタンスが不明";return d;}
    if(!world.empty()&&world!=snapshot.world){d.reason=L"画像とログのワールドIDが矛盾";return d;}
    d.ready=true;d.record.session=log.Session();d.record.world=snapshot.world;d.record.instance=snapshot.instance;d.record.reference=target;d.record.detected=detected;d.record.timeSource=source;d.record.timeStatus=L"estimated";d.record.memberStatus=snapshot.status;d.record.members=snapshot.members;return d;
}
bool SeparateFolders(const std::filesystem::path& input,const std::filesystem::path& output){
    if(input.empty()||output.empty())return false;std::error_code a,b;auto left=std::filesystem::weakly_canonical(input,a),right=std::filesystem::weakly_canonical(output,b);if(a||b)return false;
    auto descendant=[](const auto& parent,const auto& child){auto p=parent.begin(),c=child.begin();for(;p!=parent.end()&&c!=child.end();++p,++c)if(_wcsicmp(p->c_str(),c->c_str()))return false;return p==parent.end();};
    return !descendant(left,right)&&!descendant(right,left);
}
}
