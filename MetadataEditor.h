#pragma once
#include <array>
#include <string>
#include "MetadataModel.h"
enum class EditAction { Keep, Set, Remove };
struct MetadataEdit {
    std::array<std::wstring, 5> values{};
    std::array<EditAction, 5> actions{};
    bool Changed() const;
    std::wstring Validate() const;
};
struct EditableMetadata { std::array<std::wstring, 5> values{}; std::wstring fingerprint; };
