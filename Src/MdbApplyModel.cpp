#include "ViewerRuntime.h"
#include "MdbApply.h"
#include <set>
MdbRecord PhotoNameInput(const PhotoSummary& summary){
    MdbRecord row;row.failed=summary.metadataFailed;
    if(!summary.values[5].empty()||!summary.values[6].empty())row.terms.push_back({3,summary.values[6],summary.values[5]});
    for(auto& person:summary.people)if(!person.id.empty()||!person.name.empty())row.terms.push_back({1,person.id,person.name,1});
    for(auto& person:summary.candidatePeople)if(!person.id.empty()||!person.name.empty())row.terms.push_back({1,person.id,person.name,4});
    return row;
}
PhotoNamePlan BuildPhotoNamePlan(const MdbRecord& record,const IdNameTable& names){
    if(record.failed)throw std::runtime_error("image metadata could not be read");PhotoNamePlan plan;plan.before=plan.after=names;std::map<std::wstring,std::set<std::wstring>> pairs;
    auto skip=[&](const wchar_t* reason){++plan.skipped;++plan.reasons[reason];};
    for(auto& term:record.terms){if(term.axis!=1&&term.axis!=3)continue;
        auto id=CanonicalDisplayId(term.value);auto expected=term.axis==1?L"person":L"world";
        if(id.empty()||DisplayIdType(id)!=expected){skip(L"IDなし・不正ID");continue;}
        std::wstring name;try{name=ValidateDisplayName(term.label);}catch(...){skip(L"保存できない表示名");continue;}
        if(name.empty()){skip(L"名前なし");continue;}
        pairs[id].insert(std::move(name));
    }
    for(auto& pair:pairs){if(pair.second.size()!=1){skip(L"同じIDの名称不一致");continue;}auto name=*pair.second.begin();auto found=names.find(pair.first);auto previous=found==names.end()?L"":found->second;
        if(previous==name){++plan.unchanged;continue;}if(previous.empty())++plan.added;else ++plan.updated;plan.changes.push_back({pair.first,previous,name});plan.after[pair.first]=name;
    }
    SerializeIdNames(plan.after);return plan;
}
std::wstring PhotoNamePlanSummary(const PhotoNamePlan& plan){auto text=L"追加 "+std::to_wstring(plan.added)+L"件／上書き "+std::to_wstring(plan.updated)+L"件／変更なし "+std::to_wstring(plan.unchanged)+L"件／除外 "+std::to_wstring(plan.skipped)+L"件";for(auto& reason:plan.reasons)text+=L"\r\n"+reason.first+L": "+std::to_wstring(reason.second)+L"件";return text;}
