#define NOMINMAX
#include "OcrRuntime.h"
#include "WindowCapture.h"
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <roapi.h>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <atomic>
#include <fstream>
#include <set>
#include <algorithm>
#include <cmath>
namespace person_ocr {
namespace {
constexpr size_t Budget=256*1024*1024;
std::atomic<size_t> liveBytes{0};
struct Frame {UINT width=0,height=0;stage3::Time time=0;std::wstring session,instance;uint64_t generation=0;std::vector<BYTE> gray,mask;std::vector<TextLine> lines;bool recognized=false;size_t charged=0;~Frame(){liveBytes-=charged;}};
struct Job {std::wstring key;stage3::Record record;std::vector<std::pair<std::shared_ptr<Frame>,std::vector<stage3::Person>>> frames;uint64_t generation=0;};
std::mutex mutex;std::condition_variable wake;std::thread worker;bool stopping=false,started=false,configInvalid=false;
Settings settings;std::filesystem::path configPath;HWND owner{},chosen{};DWORD pid=0;std::wstring session,instance,status=L"記録停止",raw;
uint64_t generation=0;std::deque<std::shared_ptr<Frame>> ring;std::deque<Job> jobs;std::map<std::wstring,stage3::Record> results;
std::shared_ptr<const Pixels> preview;stage3::Time latestTime=0;std::atomic<bool> cancel{false},recognizing{false},engineReady{false};
void Notify(){if(owner)PostMessageW(owner,WM_APP+4,0,0);}
void Reset(const std::wstring& reason){++generation;ring.clear();preview.reset();latestTime=0;raw.clear();for(auto& job:jobs){job.record.candidateStatus=L"not-observed";job.record.candidateReason=reason;results[job.key]=job.record;}jobs.clear();}
void Receive(Pixels&& pixels,stage3::Time time,uint64_t expectedGeneration){
    std::lock_guard<std::mutex> lock(mutex);if(stopping||expectedGeneration!=generation)return;if(time>stage3::Now()+2000||time<stage3::Now()-settings.seconds*1000LL){status=L"キャプチャー時刻が不整合";Reset(status);return;}
    while(!ring.empty()&&ring.front()->time<time-settings.seconds*1000LL)ring.pop_front();
    size_t bytes=size_t(pixels.width)*pixels.height*2;if(liveBytes+bytes>Budget){status=L"OCRバッファ容量不足：新フレームを省略";return;}
    auto frame=std::make_shared<Frame>();liveBytes+=bytes;frame->charged=bytes;frame->width=pixels.width;frame->height=pixels.height;frame->time=time;frame->session=session;frame->instance=instance;frame->generation=generation;frame->gray.resize(bytes/2);frame->mask.resize(bytes/2);
    for(size_t i=0;i<bytes/2;++i){auto p=pixels.data.data()+i*4;frame->gray[i]=BYTE((p[2]*77+p[1]*150+p[0]*29)>>8);frame->mask[i]=(p[2]>100&&p[1]>65&&p[2]>p[0]*1.25)?0:255;}
    auto thumbnailPreview=std::make_shared<Pixels>();double scale=std::min({1.0,640.0/pixels.width,360.0/pixels.height});thumbnailPreview->width=std::max(1U,UINT(pixels.width*scale));thumbnailPreview->height=std::max(1U,UINT(pixels.height*scale));thumbnailPreview->data.resize(size_t(thumbnailPreview->width)*thumbnailPreview->height*4);for(UINT y=0;y<thumbnailPreview->height;++y)for(UINT x=0;x<thumbnailPreview->width;++x){auto source=(size_t(UINT(y/scale))*pixels.width+UINT(x/scale))*4;memcpy(thumbnailPreview->data.data()+(size_t(y)*thumbnailPreview->width+x)*4,pixels.data.data()+source,4);}
    preview=std::move(thumbnailPreview);latestTime=time;ring.push_back(std::move(frame));status=L"キャプチャー中 | バッファ "+std::to_wstring(ring.size())+L"枚 / "+std::to_wstring(settings.seconds)+L"秒 | 採用 "+std::to_wstring(settings.fps)+L"FPS | Windows OCR";Notify();wake.notify_all();
}
std::vector<TextLine> Recognize(const std::shared_ptr<Frame>& frame,const std::vector<winrt::Windows::Media::Ocr::OcrEngine>& engines){
    using namespace winrt::Windows;std::vector<TextLine> lines;
    for(auto* pixels:{&frame->gray,&frame->mask}){if(cancel)break;Storage::Streams::DataWriter writer;writer.WriteBytes(*pixels);auto bitmap=Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(),Graphics::Imaging::BitmapPixelFormat::Gray8,frame->width,frame->height);
        for(auto& engine:engines){if(cancel)break;auto result=engine.RecognizeAsync(bitmap).get();for(auto line:result.Lines()){std::vector<std::wstring> words;std::wstring bounds;for(auto word:line.Words()){words.push_back(word.Text().c_str());auto r=word.BoundingRect();bounds+=std::to_wstring(int(r.X))+L","+std::to_wstring(int(r.Y))+L","+std::to_wstring(int(r.Width))+L","+std::to_wstring(int(r.Height))+L";";}if(lines.size()>=512)break;lines.push_back({std::wstring(line.Text().c_str()).substr(0,256),bounds.substr(0,2048)});for(size_t i=0;i<words.size();++i){std::wstring text;for(size_t j=i;j<std::min(words.size(),i+6);++j){if(!text.empty())text+=L" ";text+=words[j];if(text!=line.Text()&&lines.size()<512)lines.push_back({text.substr(0,256),bounds.substr(0,2048),false});}}if(lines.size()>=512)break;}}
        bitmap.Close();
    }if(lines.size()>512)lines.resize(512);return lines;
}
void Work(){
    using namespace winrt::Windows;auto initialized=RoInitialize(RO_INIT_MULTITHREADED);std::unique_ptr<WindowCapture> capture;HWND active{};uint64_t captureGeneration=UINT64_MAX;
    try{winrt::check_hresult(initialized);std::vector<Media::Ocr::OcrEngine> engines;for(auto language:{L"ja",L"en"}){auto engine=Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(language));if(engine)engines.push_back(engine);}if(engines.empty()){auto engine=Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();if(engine)engines.push_back(engine);}
        engineReady=!engines.empty();for(;;){Settings config;HWND target{};uint64_t current;{std::lock_guard<std::mutex> lock(mutex);if(stopping)break;config=settings;current=generation;auto windows=Windows();if(chosen){auto found=std::find_if(windows.begin(),windows.end(),[](auto& window){return window.handle==chosen&&(pid==0||window.pid==pid);});if(found!=windows.end())target=chosen;}else{windows.erase(std::remove_if(windows.begin(),windows.end(),[](auto& window){return pid!=0&&window.pid!=pid;}),windows.end());if(windows.size()==1)target=windows[0].handle;}}
            {std::lock_guard<std::mutex> lock(mutex);if(session.empty())target=nullptr;}
            if(target&&IsIconic(target))target=nullptr;
            if(target!=active||current!=captureGeneration){capture.reset();active=nullptr;{std::lock_guard<std::mutex> lock(mutex);preview.reset();ring.clear();latestTime=0;status=target?L"キャプチャー開始中":L"VRChatウィンドウ待ち／複数候補／最小化";Notify();}if(target){try{capture=std::make_unique<WindowCapture>(target,config.fps,config.seconds,[current](Pixels&& pixels,stage3::Time time){Receive(std::move(pixels),time,current);});active=target;}catch(const winrt::hresult_error& e){std::lock_guard<std::mutex> lock(mutex);status=L"キャプチャー未取得: "+std::wstring(e.message().c_str());}catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);status=winrt::to_hstring(e.what()).c_str();}}captureGeneration=current;}
            if(capture){auto error=capture->Error();if(!error.empty()){std::lock_guard<std::mutex> lock(mutex);status=error;preview.reset();ring.clear();Notify();}}
            Job job;std::shared_ptr<Frame> alwaysFrame;bool haveJob=false;{std::lock_guard<std::mutex> lock(mutex);if(!jobs.empty()){job=std::move(jobs.front());jobs.pop_front();haveJob=true;}else if(settings.always&&!ring.empty()&&!ring.back()->recognized)alwaysFrame=ring.back();}
            if(haveJob){recognizing=true;std::vector<Observation> observations;bool failed=false;for(auto& pair:job.frames){if(cancel)break;try{auto& frame=pair.first;if(!frame->recognized){frame->lines=Recognize(frame,engines);frame->recognized=true;}observations.push_back({frame->time,frame->lines,pair.second});}catch(...){failed=true;break;}}
                if(engines.empty()||observations.empty()||failed||cancel){job.record.candidateStatus=L"not-observed";job.record.candidateReason=engines.empty()?L"Windows OCR言語モデル未準備":failed?L"OCR処理失敗":L"有効な撮影前フレームなし／中止";}else{job.record.candidates=Match(observations,job.record);job.record.candidateStatus=job.record.candidates.empty()?L"no-match":L"observed";job.record.candidateReason=L"Windows OCR／信頼度は未提供";}
                std::lock_guard<std::mutex> lock(mutex);if(job.generation!=generation){job.record.candidates.clear();job.record.candidateStatus=L"not-observed";job.record.candidateReason=L"取得設定／セッション変更";}std::wstring text;for(auto& observation:observations){text+=stage3::Iso(observation.time)+L"\r\n";std::set<std::wstring> unique;for(auto& line:observation.lines)if(line.original&&unique.insert(line.text).second)text+=line.text+L"\r\n";if(text.size()>60000){text.resize(60000);break;}}if(job.generation==generation)raw=std::move(text);results[job.key]=std::move(job.record);recognizing=false;Notify();
            }else if(alwaysFrame){recognizing=true;try{alwaysFrame->lines=Recognize(alwaysFrame,engines);alwaysFrame->recognized=true;std::lock_guard<std::mutex> lock(mutex);if(alwaysFrame->generation==generation){raw=stage3::Iso(alwaysFrame->time)+L"\r\n";std::set<std::wstring> unique;for(auto& line:alwaysFrame->lines)if(line.original&&unique.insert(line.text).second&&raw.size()<60000)raw+=line.text+L"\r\n";if(engines.empty())status=L"Windows OCR言語モデル未準備";Notify();}}catch(...){std::lock_guard<std::mutex> lock(mutex);status=L"常時OCR処理失敗";}recognizing=false;}
            std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(100),[]{return stopping||!jobs.empty();});
        }
    }catch(...){std::lock_guard<std::mutex> lock(mutex);status=L"キャプチャー／OCR初期化失敗";for(auto& job:jobs){job.record.candidateStatus=L"not-observed";job.record.candidateReason=status;results[job.key]=job.record;}jobs.clear();started=false;recognizing=false;Notify();}
    capture.reset();if(SUCCEEDED(initialized))RoUninitialize();
}
}
std::vector<Window> Windows(){std::vector<Window> windows;EnumWindows([](HWND h,LPARAM data)->BOOL{if(!IsWindowVisible(h)||GetWindow(h,GW_OWNER))return TRUE;DWORD process{};GetWindowThreadProcessId(h,&process);HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process);if(!p)return TRUE;wchar_t path[32768]{};DWORD count=32768;bool valid=QueryFullProcessImageNameW(p,0,path,&count)&&_wcsicmp(std::filesystem::path(path).filename().c_str(),L"VRChat.exe")==0;CloseHandle(p);if(valid){wchar_t title[1024]{};GetWindowTextW(h,title,1024);reinterpret_cast<std::vector<Window>*>(data)->push_back({h,process,title});}return TRUE;},reinterpret_cast<LPARAM>(&windows));return windows;}
Settings GetSettings(){std::lock_guard<std::mutex> lock(mutex);return settings;}
void LoadSettings(const std::filesystem::path& folder){std::lock_guard<std::mutex> lock(mutex);configPath=folder/L"ocr-settings.json";if(started)return;settings={};configInvalid=false;try{if(std::filesystem::exists(configPath)){if(std::filesystem::file_size(configPath)>4096)throw std::runtime_error("OCR settings limit");std::ifstream input(configPath,std::ios::binary);auto json=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(std::string((std::istreambuf_iterator<char>(input)),{})));if(json.GetNamedNumber(L"version")!=1)throw std::runtime_error("OCR settings version");auto fps=json.GetNamedNumber(L"fps"),seconds=json.GetNamedNumber(L"seconds");if(fps<5||fps>15||seconds<1||seconds>30||std::floor(fps)!=fps||std::floor(seconds)!=seconds)throw std::runtime_error("OCR settings range");settings={json.GetNamedBoolean(L"always",false),int(fps),int(seconds)};}}catch(...){configInvalid=true;status=L"OCR設定が不正：原本保全、OCR開始不可";}}
void SetSettings(Settings value){if(value.fps<5||value.fps>15||value.seconds<1||value.seconds>30)throw std::runtime_error("OCR FPS: 5-15, buffer: 1-30 seconds");std::lock_guard<std::mutex> lock(mutex);if(configInvalid)throw std::runtime_error("Corrupt OCR settings are preserved");auto json=winrt::Windows::Data::Json::JsonObject();using winrt::Windows::Data::Json::JsonValue;json.Insert(L"version",JsonValue::CreateNumberValue(1));json.Insert(L"always",JsonValue::CreateBooleanValue(value.always));json.Insert(L"fps",JsonValue::CreateNumberValue(value.fps));json.Insert(L"seconds",JsonValue::CreateNumberValue(value.seconds));auto temp=configPath;temp+=L".tmp";{std::ofstream file(temp,std::ios::binary|std::ios::trunc);file<<winrt::to_string(json.Stringify());file.flush();if(!file)throw std::runtime_error("OCR settings write failed");}if(!MoveFileExW(temp.c_str(),configPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))winrt::throw_last_error();settings=value;Reset(L"OCR設定変更");wake.notify_all();}
void SelectWindow(HWND window){std::lock_guard<std::mutex> lock(mutex);chosen=window;Reset(L"対象ウィンドウ変更");wake.notify_all();}
void Start(HWND window){Stop();std::lock_guard<std::mutex> lock(mutex);owner=window;if(configInvalid){status=L"OCR設定が不正：原本保全、OCR開始不可";return;}stopping=false;cancel=false;started=true;Reset(L"記録開始");worker=std::thread(Work);}
void Stop(){ {std::lock_guard<std::mutex> lock(mutex);stopping=true;cancel=true;}wake.notify_all();if(worker.joinable())worker.join();std::lock_guard<std::mutex> lock(mutex);started=false;recognizing=false;Reset(L"記録停止");results.clear();status=L"記録停止";Notify();}
void Context(DWORD process,const std::wstring& logSession,const std::wstring& room){std::lock_guard<std::mutex> lock(mutex);if(pid!=process||session!=logSession||instance!=room){pid=process;session=logSession;instance=room;Reset(L"セッション／インスタンス変更");wake.notify_all();}}
bool Submit(const std::wstring& key,const stage3::Record& record,const stage3::InstanceTimeline& timeline){
    std::lock_guard<std::mutex> lock(mutex);if(results.count(key))return true;if(jobs.size()>=4)return false;Job job;job.key=key;job.record=record;job.generation=generation;
    if(started)for(auto& frame:ring)if(frame->session==record.session&&frame->instance==record.instance&&frame->time<=record.reference&&frame->time>=record.reference-settings.seconds*1000LL){auto members=timeline.At(frame->time);if(!members.ambiguous&&members.instance==record.instance)job.frames.push_back({frame,std::move(members.members)});}
    if(!started||job.frames.empty()){job.record.candidateStatus=L"not-observed";job.record.candidateReason=configInvalid?L"OCR設定不正":L"撮影前の有効フレームなし";results[key]=job.record;}else{jobs.push_back(std::move(job));wake.notify_all();}return true;
}
bool Poll(const std::wstring& key,stage3::Record& record){std::lock_guard<std::mutex> lock(mutex);auto found=results.find(key);if(found==results.end())return false;if(record.reference!=found->second.reference||record.session!=found->second.session||record.instance!=found->second.instance){record.candidates.clear();record.candidateStatus=L"not-observed";record.candidateReason=L"撮影時刻／セッション再照合で不整合";return true;}record.candidates=found->second.candidates;record.candidateStatus=found->second.candidateStatus;record.candidateReason=found->second.candidateReason;return true;}
void Forget(const std::wstring& key){std::lock_guard<std::mutex> lock(mutex);results.erase(key);}
std::shared_ptr<const Pixels> Preview(){std::lock_guard<std::mutex> lock(mutex);if(stage3::Now()-latestTime>1500)return {};return preview;}
std::wstring Status(){std::lock_guard<std::mutex> lock(mutex);return status+(started&&!engineReady?L" | Windows OCR言語モデル未準備":L"")+(recognizing?L" | OCR処理中":L"");}
std::wstring Raw(){std::lock_guard<std::mutex> lock(mutex);return raw;}
std::vector<TextLine> RecognizeTestPixels(const Pixels& pixels){
    using namespace winrt::Windows;std::vector<Media::Ocr::OcrEngine> engines;auto engine=Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();if(!engine)throw std::runtime_error("Windows OCR language model unavailable");engines.push_back(engine);auto frame=std::make_shared<Frame>();frame->width=pixels.width;frame->height=pixels.height;frame->gray.resize(size_t(pixels.width)*pixels.height);frame->mask.resize(frame->gray.size());for(size_t i=0;i<frame->gray.size();++i){auto p=pixels.data.data()+i*4;frame->gray[i]=BYTE((p[2]*77+p[1]*150+p[0]*29)>>8);frame->mask[i]=frame->gray[i];}cancel=false;return Recognize(frame,engines);
}

void RunBufferTest(const Pixels& pixels,std::ostream& report){
    struct Cleanup{~Cleanup(){Stop();}}cleanup;Start(nullptr);Context(GetCurrentProcessId(),L"test-session",L"test-instance");auto deadline=GetTickCount64()+5000;while(Status().find(L"VRChatウィンドウ待ち")==std::wstring::npos){if(GetTickCount64()>deadline)throw std::runtime_error("OCR worker initialization timeout");Sleep(10);}
    stage3::Record record;record.reference=stage3::Now();record.session=L"test-session";record.instance=L"test-instance";record.members={{L"TESTMODEL",L"usr_11111111-1111-1111-1111-111111111111"}};stage3::InstanceTimeline timeline;stage3::Event room;room.kind=stage3::Kind::Room;room.time=record.reference-10000;room.instance=record.instance;room.world=L"wrld_test";timeline.Add(room);auto joined=room;joined.kind=stage3::Kind::Join;joined.time=record.reference-9000;joined.person=record.members[0];timeline.Add(joined);
    uint64_t version;{std::lock_guard<std::mutex> lock(mutex);version=generation;}Pixels copy=pixels;Receive(std::move(copy),record.reference-1000,version);if(!Preview())throw std::runtime_error("OCR buffer preview missing");if(!Submit(L"private-buffer-test",record,timeline))throw std::runtime_error("OCR job submit failed");deadline=GetTickCount64()+15000;while(!Poll(L"private-buffer-test",record)){if(GetTickCount64()>deadline)throw std::runtime_error("OCR buffer worker timeout");Sleep(10);}if(record.candidates.size()!=1||record.candidates[0].person.id!=record.members[0].id||record.candidateStatus!=L"observed")throw std::runtime_error("OCR buffer job did not produce matched candidate");Forget(L"private-buffer-test");Context(GetCurrentProcessId(),L"changed-session",L"changed-room");if(Preview())throw std::runtime_error("OCR context change retained previous preview");Pixels stale=pixels;Receive(std::move(stale),stage3::Now(),version);if(Preview())throw std::runtime_error("stale capture generation entered new session");Stop();if(Preview()||!Raw().empty())throw std::runtime_error("OCR stop did not release buffer/raw");report<<"PASS: buffer -> OCR worker -> matched candidate; stale context rejection and stop release\n";
}

}
