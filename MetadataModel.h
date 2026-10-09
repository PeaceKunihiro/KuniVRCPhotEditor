#pragma once
#include <windows.h>
#include <array>
#include <string>

struct PhotoSummary {
    std::array<std::wstring, 8> values{};
    std::wstring notes, xmpDetail;
};
struct XmpFields {
    std::wstring date, author, worldName, worldId;
};
bool ParseXmp(const std::string& xml, XmpFields& fields, std::wstring& error);
bool ValidXmpDate(const std::wstring& text);
std::wstring FilenameDate(const std::wstring& filename);
std::wstring ChooseDate(const std::wstring& xmp, const std::wstring& filename,
    const std::wstring& created, const std::wstring& modified);
PhotoSummary ReadPhotoSummary(const std::wstring& path);
