#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <utility>
#include <map>
struct ViewerSettings {
    int thumbnailWidth = 128, cacheMB = 100;
    bool saveHistory = true, autoBackup=true, darkTheme=false, metadataSearch=false;
    int backupLimit=10;
    std::wstring last, layout = L"right";
    std::vector<std::wstring> history, bookmarks;
    bool autoThumbnails=true; int sideWidth=400,sideHeight=300;
    bool residentMode=false,residentOcr=false;
    bool hideFileNames=false; uint64_t updateAttemptUtc=0,updateSuccessUtc=0;
    int sortMode=0; bool idLinks=true,showBackupImages=true;
    std::vector<std::wstring> navigation; int navigationIndex=-1;
    bool bookmarkCache=false,diskCache=false; int diskCacheMiB=512,diskCacheMinutes=30,diskCacheVisits=5;
    bool mdbEnabled=false;int mdbMode=0,mdbCapacityMB=100,categoryMode=0;
    std::wstring fontName;int fontPt=12;
    std::wstring backgroundPath;
    std::vector<std::pair<std::wstring,std::wstring>> backgroundCopies;
    std::map<std::wstring,bool> backgroundThemes;
};
std::wstring NormalizeFolderAddress(const std::wstring& path);
bool SameFolderAddress(const std::wstring& a, const std::wstring& b);
ViewerSettings ParseViewerSettings(const std::string& json);
std::string SerializeViewerSettings(const ViewerSettings& settings);
bool ParseCacheLimitInput(const std::wstring& text, int& value);
