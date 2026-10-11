#pragma once
#include "OcrRuntime.h"
namespace test_ocr {
std::vector<person_ocr::Window> Windows();
Pixels ReadImagePixels(const std::wstring& path);
bool NameLike(const std::wstring& text);
std::wstring FilterNames(const std::wstring& text);
void Capture(HWND window,int fps,int seconds);
void Image(const std::wstring& path,bool continuous=false);
void PauseImage();
void Stop();
bool Active();
std::wstring Raw(),Status();
std::shared_ptr<const Pixels> Preview();
void Tests(const std::filesystem::path& folder,std::ostream& report);
}

