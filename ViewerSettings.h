#pragma once
#include <string>
#include <vector>
struct ViewerSettings {
    int thumbnailWidth = 128, cacheMB = 100;
    bool saveHistory = true, autoBackup=true, darkTheme=false, metadataSearch=false;
    int backupLimit=10;
    std::wstring last, layout = L"right";
    std::vector<std::wstring> history, bookmarks;
    bool autoThumbnails=true; int sideWidth=400,sideHeight=300;
    bool diskCache=false; int diskCacheMiB=512,diskCacheMinutes=30,diskCacheVisits=5;
};
std::wstring NormalizeFolderAddress(const std::wstring& path);
bool SameFolderAddress(const std::wstring& a, const std::wstring& b);
ViewerSettings ParseViewerSettings(const std::string& json);
std::string SerializeViewerSettings(const ViewerSettings& settings);
bool ParseCacheLimitInput(const std::wstring& text, int& value);
