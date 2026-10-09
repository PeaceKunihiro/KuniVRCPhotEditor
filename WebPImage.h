#pragma once
#include <windows.h>
#include <string>
#include <vector>
struct WebPImage {
    UINT width{}, height{}, sourceWidth{}, sourceHeight{};
    bool animated{}, exif{}, icc{};
    std::vector<BYTE> pixels;
};
WebPImage ReadWebP(const std::wstring& path, UINT maxWidth, UINT maxHeight);
