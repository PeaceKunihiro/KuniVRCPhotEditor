#pragma once
#include "IdDisplayNames.h"
#include "MdbStore.h"
#include "MetadataModel.h"
#include <vector>
struct PhotoNameChange {std::wstring id,before,after;};
struct PhotoNamePlan {
    IdNameTable before,after;std::vector<PhotoNameChange> changes;
    size_t added=0,updated=0,unchanged=0,skipped=0;
    std::map<std::wstring,size_t> reasons;
};
PhotoNamePlan BuildPhotoNamePlan(const MdbRecord&,const IdNameTable&);
MdbRecord PhotoNameInput(const PhotoSummary&);
std::wstring PhotoNamePlanSummary(const PhotoNamePlan&);
struct MdbApplyHooks {
    std::function<bool(const PhotoNamePlan&)> confirm;
    std::function<void(HWND)> inspectConfirmation;
    unsigned delayMs=0;
};
void SetMdbApplyHooks(const MdbApplyHooks&);
bool ShowMdbApplyConfirmation(const PhotoNamePlan&,const std::function<void(HWND)>& inspect={});
void CreateMdbApplyUi();void LayoutMdbApplyUi(RECT,bool);void RefreshMdbApplyUi();
void StartMdbApply();void CancelMdbApply(const std::wstring& reason=L"");
bool MdbApplyBusy();
struct Job;struct Result;
bool MdbApplyCurrent(const Job&);void ReadMdbApply(const Job&,Result&);bool HandleMdbApplyResult(Result&);
void RunMdbApplyTests(const std::filesystem::path&,std::ostream&);
void RunMdbApplyUiTests(const std::filesystem::path&,std::ostream&);int MdbApplyTest();
