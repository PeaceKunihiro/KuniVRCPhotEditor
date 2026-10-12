#pragma once
#include <array>
#include <string>
#include <vector>
#include "MetadataModel.h"
enum class EditAction { Keep, Set, Remove, Append };
struct CandidateChoice {
    std::wstring name, id;
    bool existing=false;
    size_t index=0;
};
struct MetadataEdit {
    bool editCandidates=false, createCaptureRecord=false;
    std::vector<CandidateChoice> candidates;
    std::wstring additionalCandidates;
    std::string stage3Xml;
    std::array<std::wstring, 6> values{};
    std::array<EditAction, 6> actions{};
    bool Changed() const;
    std::wstring Validate() const;
};
struct EditableMetadata { std::array<std::wstring, 6> values{}; std::wstring fingerprint,recordReference; std::vector<CandidateChoice> members,candidates; bool candidatesEditable=true; std::wstring candidateError; };
std::wstring NormalizeTags(const std::wstring& text);
