#define NOMINMAX
#include "ViewerSettings.h"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
using namespace winrt::Windows::Data::Json;
bool ParseCacheLimitInput(const std::wstring& text, int& value) {
    if (text.empty() || text.size() > 4 || text.find_first_not_of(L"0123456789") != std::wstring::npos) return false;
    int number = std::stoi(text); if (number < 16 || number > 2048) return false;
    value = number; return true;
}
std::wstring NormalizeFolderAddress(const std::wstring& value) {
    if (value.empty()) throw std::runtime_error("empty path");
    auto p = std::filesystem::absolute(value).lexically_normal(); auto s = p.wstring();
    while (s.size() > p.root_path().wstring().size() && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}
bool SameFolderAddress(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return a == b;
    std::error_code error; bool same = std::filesystem::equivalent(a, b, error);
    return error ? NormalizeFolderAddress(a) == NormalizeFolderAddress(b) : same;
}
ViewerSettings ParseViewerSettings(const std::string& json) {
    if (json.size() > 1024 * 1024) throw std::runtime_error("settings limit");
    auto object = JsonObject::Parse(winrt::to_hstring(json)); ViewerSettings settings;
    auto version = object.GetNamedNumber(L"version");
    if (version != 1 && version != 2 && version != 3) throw std::runtime_error("version");
    auto number = [&](const wchar_t* name, int initial, int minimum, int maximum, int step) {
        auto n = object.GetNamedNumber(name, initial);
        if (!std::isfinite(n) || n < minimum || n > maximum || std::fmod(n - minimum, step) != 0) throw std::runtime_error("setting number");
        return static_cast<int>(n);
    };
    settings.idLinks=object.HasKey(L"pageButtons")?object.GetNamedBoolean(L"pageButtons"):object.GetNamedBoolean(L"idLinks",true);
    settings.sortMode=number(L"sortMode",0,0,3,1);
    settings.mdbEnabled=object.GetNamedBoolean(L"mdbEnabled",false);settings.mdbMode=number(L"mdbMode",0,0,2,1);settings.mdbCapacityMB=number(L"mdbCapacityMB",100,100,4000,1);settings.categoryMode=number(L"categoryMode",0,0,6,1);if(settings.categoryMode==3)settings.categoryMode=2;if(!settings.mdbEnabled)settings.categoryMode=0;
    settings.hideFileNames=object.GetNamedBoolean(L"hideFileNames",false);
    auto timestamp=[&](const wchar_t* key){auto value=object.GetNamedValue(key,JsonValue::CreateNumberValue(0));if(value.ValueType()!=JsonValueType::Number)return uint64_t(0);double n=value.GetNumber();return std::isfinite(n)&&n>=0&&n<=253402300799.0&&std::floor(n)==n?uint64_t(n):uint64_t(0);};
    settings.updateAttemptUtc=timestamp(L"updateAttemptUtc");settings.updateSuccessUtc=timestamp(L"updateSuccessUtc");
    settings.showBackupImages=object.GetNamedBoolean(L"showBackupImages",true);
    settings.thumbnailWidth = number(L"thumbnailWidth", 128, 64, 256, 32);
    settings.autoThumbnails=object.GetNamedBoolean(L"autoThumbnails",false);
    settings.sideWidth=number(L"sideWidth",400,280,10000,1);settings.sideHeight=number(L"sideHeight",300,120,10000,1);
    settings.residentMode=object.GetNamedBoolean(L"residentMode",false);settings.residentOcr=object.GetNamedBoolean(L"residentOcr",false);
    settings.diskCache=object.GetNamedBoolean(L"diskCache",false);
    settings.bookmarkCache=settings.diskCache&&object.GetNamedBoolean(L"bookmarkCache",false);
    settings.diskCacheMiB=number(L"diskCacheMiB",512,128,2048,1);
    if(settings.diskCacheMiB!=128&&settings.diskCacheMiB!=256&&settings.diskCacheMiB!=512&&settings.diskCacheMiB!=1024&&settings.diskCacheMiB!=2048)throw std::runtime_error("disk capacity");
    settings.diskCacheMinutes=number(L"diskCacheMinutes",30,1,1440,1);
    settings.diskCacheVisits=number(L"diskCacheVisits",5,2,100,1);
    settings.cacheMB = number(L"thumbnailCacheMB", 100, 16, 2048, 1);
    settings.autoBackup=object.GetNamedBoolean(L"autoBackup",true);
    settings.darkTheme=object.GetNamedBoolean(L"darkTheme",false);
    settings.metadataSearch=object.GetNamedBoolean(L"metadataSearch",false);
    settings.backupLimit=number(L"backupLimit",10,1,1000,1);
    settings.saveHistory = object.GetNamedBoolean(L"saveFolderHistory", true);
    settings.last = object.GetNamedString(L"lastFolder", L"");
    settings.layout = object.GetNamedString(L"sideView", L"right");
    if (settings.layout != L"right" && settings.layout != L"left" && settings.layout != L"bottom" && settings.layout != L"none") throw std::runtime_error("layout");
    auto paths = [&](const wchar_t* key, std::vector<std::wstring>& list, size_t limit) {
        if (!object.HasKey(key)) return;
        // Restoring saved addresses must not probe every remote/missing folder.
        std::unordered_set<std::wstring> seen;
        for (auto item : object.GetNamedArray(key)) {
            auto path = NormalizeFolderAddress(std::wstring(item.GetString()));
            if (seen.insert(path).second) {
                if (list.size() == limit) throw std::runtime_error("path list limit");
                list.push_back(std::move(path));
            }
        }
    };
    if (settings.saveHistory) paths(L"history", settings.history, 10); else settings.last.clear();
    if(settings.saveHistory){
        if(object.HasKey(L"navigation")){
            for(auto item:object.GetNamedArray(L"navigation")){if(settings.navigation.size()>=10)throw std::runtime_error("navigation limit");settings.navigation.push_back(NormalizeFolderAddress(std::wstring(item.GetString())));}
            settings.navigationIndex=number(L"navigationIndex",-1,-1,9,1);
            if((settings.navigation.empty()&&settings.navigationIndex!=-1)||(!settings.navigation.empty()&&(settings.navigationIndex<0||settings.navigationIndex>=int(settings.navigation.size()))))throw std::runtime_error("navigation position");
        }else{settings.navigation.assign(settings.history.rbegin(),settings.history.rend());settings.navigationIndex=int(settings.navigation.size())-1;}
        if(settings.last.empty()){settings.navigation.clear();settings.navigationIndex=-1;}
        else if(settings.navigation.empty()){settings.navigation.push_back(settings.last);settings.navigationIndex=0;}
        else if(_wcsicmp(settings.navigation[settings.navigationIndex].c_str(),settings.last.c_str())!=0){auto it=std::find_if(settings.navigation.begin(),settings.navigation.end(),[&](auto& p){return _wcsicmp(p.c_str(),settings.last.c_str())==0;});if(it!=settings.navigation.end())settings.navigationIndex=int(it-settings.navigation.begin());else{settings.navigation.push_back(settings.last);if(settings.navigation.size()>10)settings.navigation.erase(settings.navigation.begin());settings.navigationIndex=int(settings.navigation.size())-1;}}
    }
    paths(L"bookmarks", settings.bookmarks, 1000);
    return settings;
}
std::string SerializeViewerSettings(const ViewerSettings& settings) {
    JsonObject object;
    object.Insert(L"mdbEnabled",JsonValue::CreateBooleanValue(settings.mdbEnabled));object.Insert(L"mdbMode",JsonValue::CreateNumberValue(settings.mdbMode));object.Insert(L"mdbCapacityMB",JsonValue::CreateNumberValue(settings.mdbCapacityMB));object.Insert(L"categoryMode",JsonValue::CreateNumberValue(settings.mdbEnabled?(settings.categoryMode==3?2:settings.categoryMode):0));
    object.Insert(L"hideFileNames",JsonValue::CreateBooleanValue(settings.hideFileNames));
    object.Insert(L"updateAttemptUtc",JsonValue::CreateNumberValue(double(settings.updateAttemptUtc)));
    object.Insert(L"updateSuccessUtc",JsonValue::CreateNumberValue(double(settings.updateSuccessUtc)));
    object.Insert(L"version", JsonValue::CreateNumberValue(3));
    object.Insert(L"pageButtons",JsonValue::CreateBooleanValue(settings.idLinks));
    object.Insert(L"sortMode",JsonValue::CreateNumberValue(settings.sortMode));
    object.Insert(L"showBackupImages",JsonValue::CreateBooleanValue(settings.showBackupImages));
    JsonArray navigation;if(settings.saveHistory)for(auto& path:settings.navigation)navigation.Append(JsonValue::CreateStringValue(path));object.Insert(L"navigation",navigation);object.Insert(L"navigationIndex",JsonValue::CreateNumberValue(settings.saveHistory?settings.navigationIndex:-1));
    object.Insert(L"lastFolder", JsonValue::CreateStringValue(settings.saveHistory ? settings.last : L""));
    object.Insert(L"sideView", JsonValue::CreateStringValue(settings.layout));
    object.Insert(L"thumbnailWidth", JsonValue::CreateNumberValue(settings.thumbnailWidth));
    object.Insert(L"autoThumbnails",JsonValue::CreateBooleanValue(settings.autoThumbnails));
    object.Insert(L"sideWidth",JsonValue::CreateNumberValue(settings.sideWidth));object.Insert(L"sideHeight",JsonValue::CreateNumberValue(settings.sideHeight));
    object.Insert(L"residentMode",JsonValue::CreateBooleanValue(settings.residentMode));object.Insert(L"residentOcr",JsonValue::CreateBooleanValue(settings.residentOcr));
    object.Insert(L"diskCache",JsonValue::CreateBooleanValue(settings.diskCache));
    object.Insert(L"bookmarkCache",JsonValue::CreateBooleanValue(settings.diskCache&&settings.bookmarkCache));
    object.Insert(L"diskCacheMiB",JsonValue::CreateNumberValue(settings.diskCacheMiB));
    object.Insert(L"diskCacheMinutes",JsonValue::CreateNumberValue(settings.diskCacheMinutes));
    object.Insert(L"diskCacheVisits",JsonValue::CreateNumberValue(settings.diskCacheVisits));
    object.Insert(L"thumbnailCacheMB", JsonValue::CreateNumberValue(settings.cacheMB));
    object.Insert(L"autoBackup",JsonValue::CreateBooleanValue(settings.autoBackup));
    object.Insert(L"darkTheme",JsonValue::CreateBooleanValue(settings.darkTheme));
    object.Insert(L"metadataSearch",JsonValue::CreateBooleanValue(settings.metadataSearch));
    object.Insert(L"backupLimit",JsonValue::CreateNumberValue(settings.backupLimit));
    object.Insert(L"saveFolderHistory", JsonValue::CreateBooleanValue(settings.saveHistory));
    JsonArray history; if (settings.saveHistory) for (auto& p : settings.history) history.Append(JsonValue::CreateStringValue(p));
    object.Insert(L"history", history);
    JsonArray bookmarks; for (auto& p : settings.bookmarks) bookmarks.Append(JsonValue::CreateStringValue(p)); object.Insert(L"bookmarks", bookmarks);
    auto result = winrt::to_string(object.Stringify());
    if (result.size() > 1024 * 1024) throw std::runtime_error("settings limit"); return result;
}
