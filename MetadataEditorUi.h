#pragma once
#include "MetadataEditor.h"
#include <windows.h>
class MetadataEditorUi {
    HWND panel{}, message{};
    std::array<HWND,5> labels{}, inputs{}, choices{};
    int scroll=0;
    bool loading=false;
    std::array<std::wstring,5> original{};
    static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
public:
    HWND Window() const { return panel; }
    void Create(HWND parent);
    void Load(const EditableMetadata& metadata);
    void Place(int x,int y,int width,int height,bool visible);
    MetadataEdit Read() const;
    void Enable(bool enabled);
    void Layout();
};
