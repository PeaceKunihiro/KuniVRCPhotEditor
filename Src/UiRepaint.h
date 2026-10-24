#pragma once
#include <windows.h>
#include <cstdint>
// UI-thread-only transaction. Painting resumes once the outermost layout is complete.
class UiRepaintBatch {
public:
    explicit UiRepaintBatch(HWND);
    ~UiRepaintBatch();
    UiRepaintBatch(const UiRepaintBatch&)=delete;
    UiRepaintBatch& operator=(const UiRepaintBatch&)=delete;
private: bool joined=false;
};
bool UiRepaintActive();
uint64_t UiRepaintSuppressed();
