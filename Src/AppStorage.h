#pragma once
#include <filesystem>
#include <string>
std::string ReadSettingsSection(const std::filesystem::path&,const wchar_t* key,const wchar_t* legacy,bool& fromLegacy);
void WriteSettingsSection(const std::filesystem::path&,const wchar_t* key,const std::string&,const wchar_t* legacy=L"");
void WriteViewerSettingsFile(const std::filesystem::path&,const std::string&);
std::filesystem::path AppDataFile(const std::filesystem::path&,const wchar_t* name);
