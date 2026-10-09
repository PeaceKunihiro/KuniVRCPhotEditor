#pragma once
#include <string>
#include <vector>
std::string XmpMd5(const std::string& bytes);
std::string RestoreExtendedXmp(const std::vector<std::string>& standard,
    const std::vector<std::string>& segments, std::wstring& notes);
