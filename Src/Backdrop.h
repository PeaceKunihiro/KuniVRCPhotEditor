#pragma once
#include <windows.h>
#include <string>
#include <functional>
struct Pixels;
struct Result;
bool HasBackdrop();
bool UsesBackdrop(HWND);
void PaintBackdrop(HWND,HDC,const RECT&);
HBRUSH BackdropBrush(HWND,HDC);
void SyncBackdrop();
void InstallBackdropControls();
void RequestBackdrop(const std::wstring&);
void AcceptBackdrop(Result&);
void ChooseBackdrop();
void ShowBackdropDialog(const std::function<void(HWND)>& inspect={},const std::function<std::wstring(HWND)>& choose={});
void LoadBackdropJob(Result&);
void ClearBackdrop(bool persist=true);
void DrawBackdropPixels(HDC,const RECT&,const Pixels&);
Pixels ReadBackdrop(const std::wstring&);
