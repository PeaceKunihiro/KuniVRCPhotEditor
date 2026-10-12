#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Stage3.h"
#include "ThumbnailCache.h"
#include <memory>
#include <map>
namespace person_ocr {
struct Settings {bool always=false;int fps=5,seconds=10;bool highPrecision=false;};
struct Window {HWND handle{};DWORD pid=0;std::wstring title;};
struct TextLine {std::wstring text,bounds;bool original=true;};
struct Observation {stage3::Time time=0;std::vector<TextLine> lines;std::vector<stage3::Person> members;};
class BurstGate {
    std::map<std::wstring,stage3::Time> last;
public:
    bool Followup(const std::wstring& context,stage3::Time time,int seconds){auto it=last.find(context);bool result=it!=last.end()&&(time<=it->second||time-it->second<=seconds*1000LL);if(it==last.end()||time>it->second)last[context]=time;return result;}
};
std::vector<stage3::Candidate> Match(const std::vector<Observation>& observations,const stage3::Record& record);
Settings GetSettings();void LoadSettings(const std::filesystem::path& folder);void SetSettings(Settings settings);
std::vector<Window> Windows();void SelectWindow(HWND window);
void Start(HWND owner);void Stop();void Context(DWORD pid,const std::wstring& session,const std::wstring& instance,const std::vector<stage3::Person>& members={});
bool Submit(const std::wstring& key,const stage3::Record& record,const stage3::InstanceTimeline& timeline);
bool SubmitImage(const std::wstring& key,const stage3::Record& record,const stage3::InstanceTimeline& timeline);
bool Poll(const std::wstring& key,stage3::Record& record);void Forget(const std::wstring& key);
std::shared_ptr<const Pixels> Preview();std::wstring Status();std::wstring Raw();
std::wstring Names();
void RunCaptureTests(std::ostream& report);
void RunTests(const std::filesystem::path& folder,std::ostream& report);
}
