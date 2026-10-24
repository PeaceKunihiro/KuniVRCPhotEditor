#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
HFONT UiFont();
int UiLineHeight();
int UiScale(int);
int UiTextWidth(const std::wstring&);
int UiControlHeight();
bool UiStackEditorFields();
void ScaleUiDialog(HWND);
void ApplyUiFont(HWND);
void ReleaseUiFonts();
UINT TrackUiMenu(HMENU,UINT,int,int,HWND,TPMPARAMS*);

class MenuThemeScope {
public: MenuThemeScope();~MenuThemeScope();
    MenuThemeScope(const MenuThemeScope&)=delete;
    MenuThemeScope& operator=(const MenuThemeScope&)=delete;
private: HHOOK hook=nullptr;
};
void RefreshMenuFrame(HWND);
