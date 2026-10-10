#pragma once
#include "MetadataEditor.h"
#include <windows.h>
#include <vector>
class MetadataEditorUi {
    HWND panel{}, message{};
    std::array<HWND,6> labels{}, inputs{}, choices{};
    int scroll=0;
    bool loading=false;
    std::array<std::wstring,6> original{};
    std::array<bool,6> mixed{};
    bool batch=false;
    static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
public:
    HWND Window() const { return panel; }
    void Create(HWND parent);
    void Load(const EditableMetadata& metadata);
    void LoadBatch(const std::vector<EditableMetadata>& metadata);
    void Place(int x,int y,int width,int height,bool visible);
    MetadataEdit Read() const;
    void Enable(bool enabled);
    void Layout();
};
