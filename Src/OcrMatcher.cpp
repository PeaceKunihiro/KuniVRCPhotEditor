#include "OcrRuntime.h"
#include <algorithm>
#include <cwctype>
#include <set>
#include <cmath>
#pragma comment(lib,"normaliz.lib")
namespace person_ocr {
namespace {
std::wstring Normalize(const std::wstring& value){int size=NormalizeString(NormalizationKC,value.c_str(),static_cast<int>(value.size()),nullptr,0);if(size<=0)return {};std::wstring normalized(size,L'\0');size=NormalizeString(NormalizationKC,value.c_str(),static_cast<int>(value.size()),normalized.data(),size);if(size<=0)return {};normalized.resize(size);std::wstring lower(normalized.size(),L'\0');if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,normalized.data(),static_cast<int>(normalized.size()),lower.data(),static_cast<int>(lower.size()),nullptr,nullptr,0))return {};lower.erase(std::remove_if(lower.begin(),lower.end(),[](wchar_t c){return iswspace(c)!=0;}),lower.end());return lower;}
double Similarity(const std::wstring& a,const std::wstring& b){if(a.empty()||b.empty()||a.size()>256||b.size()>256)return 0;std::vector<size_t> row(b.size()+1);for(size_t j=0;j<row.size();++j)row[j]=j;for(size_t i=1;i<=a.size();++i){auto diagonal=row[0];row[0]=i;for(size_t j=1;j<=b.size();++j){auto old=row[j];row[j]=std::min({row[j]+1,row[j-1]+1,diagonal+(a[i-1]!=b[j-1])});diagonal=old;}}return 1.0-double(row.back())/std::max(a.size(),b.size());}
}
std::vector<stage3::Candidate> Match(const std::vector<Observation>& observations,const stage3::Record& record){
    struct Vote {stage3::Candidate candidate;std::set<stage3::Time> frames;bool exact=false;};std::map<std::wstring,Vote> votes;
    for(auto& observation:observations){if(observation.time>record.reference||observation.time<record.reference-3000)continue;
        for(auto& line:observation.lines){auto raw=Normalize(line.text);if(raw.empty()||raw.size()>256)continue;double best=0,second=0;const stage3::Person* found=nullptr;size_t ties=0;
            for(auto& person:observation.members){if(person.id.empty()||std::none_of(record.members.begin(),record.members.end(),[&](auto& p){return p.id==person.id;}))continue;auto normalized=Normalize(person.name);auto score=Similarity(raw,normalized);if(score>best){second=best;best=score;found=&person;ties=1;}else if(score==best){++ties;second=best;}else second=std::max(second,score);}
            if(!found||ties!=1)continue;bool exact=best==1.0;if(!exact&&(raw.size()<5||best<0.92||best-second<0.12))continue;
            auto& vote=votes[found->id];vote.candidate.person=*found;vote.frames.insert(observation.time);vote.exact=vote.exact||exact;
            if(observation.time>=vote.candidate.observed){vote.candidate.observed=observation.time;vote.candidate.raw=line.text;vote.candidate.bounds=line.bounds;vote.candidate.score=best;}
        }
    }
    std::vector<stage3::Candidate> result;for(auto& pair:votes){auto& vote=pair.second;if(!vote.exact&&vote.frames.size()<2)continue;vote.candidate.count=static_cast<unsigned>(vote.frames.size());vote.candidate.method=vote.exact?L"normalized-exact":L"edit-distance-multi-frame";result.push_back(std::move(vote.candidate));}return result;
}
}
