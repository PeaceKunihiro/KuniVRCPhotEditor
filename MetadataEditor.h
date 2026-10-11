#pragma once
#include <array>
#include <string>
#include "MetadataModel.h"
enum class EditAction { Keep, Set, Remove, Append };
struct MetadataEdit {
    std::string stage3Xml;
    std::array<std::wstring, 6> values{};
    std::array<EditAction, 6> actions{};
    bool Changed() const;
    std::wstring Validate() const;
};
struct EditableMetadata { std::array<std::wstring, 6> values{}; std::wstring fingerprint,recordReference; };
std::wstring NormalizeTags(const std::wstring& text);
