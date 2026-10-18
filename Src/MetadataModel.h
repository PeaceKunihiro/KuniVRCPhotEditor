#pragma once
#include <windows.h>
#include <array>
#include <string>
#include <vector>
#include <string_view>
struct SummaryPerson { std::wstring name,id; };
struct SummaryLink { int field=0; long first=0,last=0; std::wstring url; };

struct PhotoSummary {
    std::array<std::wstring, 11> values{};
    std::wstring notes, xmpDetail;
    std::vector<SummaryPerson> people; long memberOffset=0; std::vector<SummaryPerson> candidatePeople; bool metadataFailed=false;
};
struct XmpFields {
    std::wstring date, author, worldName, worldId, extendedGuid, subjects, tags;
    std::wstring members, candidates, memberStatus, candidateStatus, recordReference;
    std::vector<SummaryPerson> people,candidatePeople;
};
bool ParseXmp(const std::string& xml, XmpFields& fields, std::wstring& error);
bool ValidXmpDate(const std::wstring& text);
std::wstring FilenameDate(const std::wstring& filename);
std::wstring ChooseDate(const std::wstring& xmp, const std::wstring& filename,
    const std::wstring& created, const std::wstring& modified);
// Borrowed only during this call; reuse requires an exact XML byte match.
struct ValidatedXmpView { std::string_view xml; const XmpFields* fields; };
PhotoSummary ReadPhotoSummary(const std::wstring& path, const std::vector<ValidatedXmpView>& validated = {});

std::vector<SummaryLink> SummaryLinks(const PhotoSummary& summary);
