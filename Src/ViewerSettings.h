#pragma once
#include <string>
#include <array>
#include <cstdint>
#include <vector>
#include <utility>
#include <map>
#include <optional>
#include <functional>
using ObservationColors=std::array<std::array<uint32_t,10>,2>;
inline ObservationColors DefaultObservationColors(){
    ObservationColors colors{};colors[0]={0,0,0,0xED9564,0x32CD9A,0xCD0000,0x000080,0x006400,0,0};
    colors[1]=colors[0];for(auto& color:colors[1])if(!color)color=0xFFFFFF;return colors;
}
uint32_t ParseObservationRgb(const std::wstring&);
std::wstring FormatObservationRgb(uint32_t);
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
    ObservationColors observationColors=DefaultObservationColors();
    int observationMB=10;
    bool observationWrap=true;
    bool memoryCache=true;
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

// Nullopt means no settings file; an empty string still means a corrupt existing file.
ViewerSettings InitialViewerSettings(const std::optional<std::string>&,const std::function<bool()>&);
bool DetectWindowsDarkTheme();
bool DarkThemeFromForeground(unsigned red,unsigned green,unsigned blue);
