#pragma once
#include <windows.h>
#include <functional>
#include <filesystem>
#include <ostream>
void ShowLicenseInfo(const std::function<void(HWND)>& inspect = {});
void RunLicenseUiTests(const std::filesystem::path&,std::ostream&,const std::function<void(HWND,const wchar_t*)>&);
