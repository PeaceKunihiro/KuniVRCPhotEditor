#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <ostream>
#include <functional>
void ShowUpdateChecker(HWND parent);
void RunUpdateTests(const std::filesystem::path&,std::ostream&);
void RunUpdateNetworkTests(const std::filesystem::path&,std::ostream&);

void RunUpdateDialogTests(const std::filesystem::path&,std::ostream&,const std::function<void(HWND)>&);

void StartAutomaticUpdateCheck(HWND);
void PollUpdateChecker();
void StopUpdateChecker();

void RunUpdateWorkflowTests(std::ostream&);
