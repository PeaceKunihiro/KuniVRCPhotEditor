#pragma once
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <functional>
#include <iosfwd>
#include <windows.h>
using IdNameTable=std::map<std::wstring,std::wstring>;
struct IdNameImport {IdNameTable names;size_t skipped=0;};
struct IdNameMerge {size_t added=0,kept=0,skipped=0;};
std::wstring CanonicalDisplayId(const std::wstring&);
std::wstring DisplayIdType(const std::wstring&);
std::wstring DisplayIdUrl(const std::wstring&);
std::wstring ValidateDisplayName(const std::wstring&);
IdNameImport ParseIdNames(const std::string&);
std::string SerializeIdNames(const IdNameTable&);
std::string ReadIdNamesFile(const std::filesystem::path&);
IdNameMerge MergeIdNames(IdNameTable&,const IdNameImport&);
void ExportIdNames(const std::filesystem::path&,const IdNameTable&);
class IdNameStore {
    mutable std::mutex mutex;std::filesystem::path file;IdNameTable names;
    std::string original;std::wstring error;bool exists=false,protectedFile=false;
public:
    void Load(const std::filesystem::path&);
    IdNameTable Snapshot()const;
    std::wstring Error()const;
    void Save(const IdNameTable&);
};
struct IdNameUiHooks {
    std::function<void(HWND)> inspect;
    std::function<std::wstring()> importPath,exportPath;
    std::function<bool(size_t,bool)> confirmExport;
    std::function<void(const std::wstring&)> openUrl;
};
void RefreshMdbIdLabels();
std::wstring MdbHeaderIdAt(POINT);
bool MdbHeaderTooltip(HWND,UINT,LPARAM,LRESULT&);
void ShowIdNamesDialog(HWND owner,const IdNameUiHooks& hooks={});
void RunIdNameTests(const std::filesystem::path&,std::ostream&);
void RunIdNameUiTests(const std::filesystem::path&,std::ostream&);
int IdNamesTest();
