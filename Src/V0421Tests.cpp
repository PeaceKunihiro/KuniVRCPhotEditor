#include "ViewerRuntime.h"
#include "V048Features.h"
#include <regex>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace {
// Independent copy of the pre-O14 URL construction, retained only for comparison.
std::vector<SummaryLink> LegacyLinks(const PhotoSummary& summary){
    std::vector<SummaryLink> links;
    auto normalized=[](const std::wstring& text){std::wstring result;for(size_t i=0;i<text.size();++i){if(text[i]==L'\r'&&i+1<text.size()&&text[i+1]==L'\n')++i;result+=text[i]==L'\n'?L'\r':text[i];}return result;};
    auto url=[](const std::wstring& id,const std::wstring& prefix,const std::wstring& page){std::wregex pattern(L"^"+prefix+LR"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)");return std::regex_match(id,pattern)?L"https://vrchat.com/home/"+page+L"/"+id:L"";};
    auto add=[&](int field,size_t first,const std::wstring& text,const std::wstring& target){if(!target.empty())links.push_back({field,static_cast<long>(first),static_cast<long>(first+text.size()),target});};
    auto world=summary.values[6];add(6,0,world,url(world,L"wrld_",L"world"));
    auto tags=normalized(summary.values[8]);std::wistringstream lines(tags);std::wstring line;size_t offset=0;while(std::getline(lines,line,L'\r')){const std::wstring prefix=L"【グループID】";if(line.find(prefix)==0){auto id=line.substr(prefix.size());add(8,offset+prefix.size(),id,GroupUrl(id));}offset+=line.size()+1;}
    auto members=normalized(summary.values[9]);size_t at=summary.memberOffset;for(auto& person:summary.people){auto name=normalized(person.name);if(members.compare(at,name.size(),name)!=0)break;add(9,at,name,url(person.id,L"usr_",L"user"));at+=name.size()+1;}
    return links;
}
bool SameLinks(const std::vector<SummaryLink>& a,const std::vector<SummaryLink>& b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(a[i].field!=b[i].field||a[i].first!=b[i].first||a[i].last!=b[i].last||a[i].url!=b[i].url)return false;return true;}
template<class F> double Timed(F work){auto begin=std::chrono::steady_clock::now();work();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();}
}
void RunV0421RegexTests(std::ostream& report){
    auto require=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: "<<label<<'\n';};
    const std::wstring uuid=L"12345678-ABCD-abcd-0123-123456789abc";
    PhotoSummary sample;sample.values[6]=L"wrld_"+uuid;sample.values[8]=L"tag\n【グループID】grp_"+uuid;sample.values[9]=L"部分的な記録\r\n";sample.memberOffset=static_cast<long>(sample.values[9].size()-1);
    for(int i=0;i<256;++i){auto name=L"同名 & 名前 "+std::to_wstring(i%2);auto id=i%3==0?L"usr_"+uuid:i%3==1?L"":L"usr_bad";sample.people.push_back({name,id});sample.values[9]+=name+L"\r\n";}
    auto expected=LegacyLinks(sample),actual=SummaryLinks(sample);require(actual.size()==88&&SameLinks(expected,actual),"v0.4.21 shared ID patterns preserve links, duplicate names, order and offsets");
    sample.values[6]=L"wrld_"+uuid+L"extra";require(SameLinks(LegacyLinks(sample),SummaryLinks(sample)),"v0.4.21 malformed ID rejection matches previous construction");
    size_t beforeCount=0,afterCount=0;
    auto before=Timed([&]{for(int i=0;i<10;++i)beforeCount+=LegacyLinks(sample).size();});
    auto after=Timed([&]{for(int i=0;i<10;++i)afterCount+=SummaryLinks(sample).size();});
    require(beforeCount==afterCount,"v0.4.21 link benchmark output counts match");
    report<<"INFO: v0.4.21 links members=256 passes=10 legacy_ms="<<before<<" reuse_ms="<<after<<'\n';
    MetadataEdit edit;edit.actions[3]=EditAction::Set;const std::wregex worldPattern(L"wrld_[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}");
    for(const auto& id:std::vector<std::wstring>{L"wrld_"+uuid,L"WRLD_"+uuid,L"wrld_"+uuid+L"\n",L"wrld_bad",std::wstring{}}){edit.values[3]=id;require(edit.Validate().empty()==std::regex_match(id,worldPattern),"v0.4.21 world edit ID acceptance unchanged");}
    edit.values[3]=L"wrld_"+uuid;size_t acceptedBefore=0,acceptedAfter=0;
    before=Timed([&]{for(int i=0;i<1000;++i)acceptedBefore+=std::regex_match(edit.values[3],std::wregex(L"wrld_[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"));});
    after=Timed([&]{for(int i=0;i<1000;++i)acceptedAfter+=edit.Validate().empty();});
    require(acceptedBefore==acceptedAfter,"v0.4.21 validation benchmark acceptance matches");
    report<<"INFO: v0.4.21 world ID passes=1000 legacy_regex_ms="<<before<<" reuse_full_validate_ms="<<after<<'\n';
    stage3::Event event;const auto player=L"2026.10.12 12:00:00 Log - [Behaviour] OnPlayerJoined 同名 (括弧) & 名前 (usr_"+uuid+L")";
    require(stage3::ParseLogLine(player,event)&&event.kind==stage3::Kind::Join&&event.person.name==L"同名 (括弧) & 名前"&&event.person.id==L"usr_"+uuid,"v0.4.21 static player expression preserves full name and ID");
    const auto room=L"2026.10.12 12:00:00 Log - [Behaviour] Joining wrld_"+uuid+L":123~group(grp_"+uuid+L")~groupAccessType(members)";
    require(stage3::ParseLogLine(room,event)&&event.kind==stage3::Kind::Room&&event.group==L"grp_12345678-abcd-abcd-0123-123456789abc","v0.4.21 static group expression preserves lowercase group extraction");
}
