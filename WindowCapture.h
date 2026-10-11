#pragma once
#include "OcrRuntime.h"
#include <functional>
class WindowCapture {
    struct Core;std::shared_ptr<Core> core;
public:
    WindowCapture(HWND window,int fps,int seconds,std::function<void(Pixels&&,stage3::Time)> receive,bool native=false);
    ~WindowCapture();
    std::wstring Error()const;
};
