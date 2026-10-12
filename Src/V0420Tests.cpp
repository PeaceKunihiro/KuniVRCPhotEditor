#include "ViewerRuntime.h"
#include <chrono>
#include <limits>
#include <stdexcept>

void RunV0420Tests(const fs::path& folder,std::ostream& report){
    auto check=[&](bool ok,const char* text){if(!ok)throw std::runtime_error(text);report<<"PASS: v0.4.20 "<<text<<'\n';};
    {
        ThumbnailCache cache;Pixels pixel{1,1,{1,2,3,255}},other{1,1,{4,5,6,255}},out;
        cache.SetLimit(8);auto ticket=cache.Ticket();cache.Put(L"A",pixel,ticket);cache.Put(L"B",other,ticket);
        check(cache.Get(L"A",out)&&out.data==pixel.data,"indexed thumbnail hit");cache.Put(L"C",other,ticket);
        check(!cache.Get(L"B",out)&&cache.Get(L"A",out)&&cache.Get(L"C",out)&&cache.Bytes()==8,"LRU promotion and eviction preserve indexed entries");
        cache.Put(L"A",other,ticket);cache.Get(L"A",out);check(out.data==pixel.data,"duplicate thumbnail keeps original pixels");
        cache.Clear();cache.Put(L"stale",pixel,ticket);check(!cache.Get(L"stale",out)&&cache.Bytes()==0,"clear rejects old epoch and clears index");
        cache.Put(L"new",pixel,cache.Ticket());cache.SetLimit(0);check(!cache.Get(L"new",out)&&cache.Bytes()==0,"limit decrease clears indexed entries");
        cache.SetLimit(1024*1024);for(int i=0;i<10000;++i)cache.Put(std::to_wstring(i),pixel);
        auto start=std::chrono::steady_clock::now();for(int i=0;i<10000;++i)if(cache.Get(L"absent",out))throw std::runtime_error("unexpected cache hit");
        report<<"INFO: v0.4.20 thumbnail entries=10000 misses=10000 elapsed_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<'\n';
    }
    {
        stage3::InstanceTimeline timeline;stage3::Event room;room.kind=stage3::Kind::Room;room.time=1000;room.instance=L"instance";room.world=L"world";timeline.Add(room);
        auto old=timeline.At(1200);check(timeline.At(1200).world==old.world,"same-time timeline reuse");
        stage3::Event name;name.kind=stage3::Kind::RoomName;name.time=1500;name.worldName=L"retroactive";timeline.Add(name);
        check(timeline.At(1200).worldName==L"retroactive","room-name backfill invalidates previous snapshot");
        timeline.Invalidate();check(timeline.At(1200).status==L"partial","invalidate changes cached past snapshot");
        stage3::Event photo;photo.kind=stage3::Kind::Photo;photo.time=2000;photo.path=L"A.PNG";timeline.Add(photo);
        check(timeline.PhotoTimes(L"a.png")==std::vector<stage3::Time>{2000},"photo index preserves case-insensitive paths");
        photo.time=1000;photo.path=L"a.png";timeline.Add(photo);
        check(timeline.PhotoTimes(L"A.PNG")==std::vector<stage3::Time>{2000,1000},"photo index preserves duplicates and insertion order");
        timeline.Clear();check(timeline.At(1200).instance.empty()&&timeline.PhotoTimes(L"A.PNG").empty(),"clear releases snapshot and photo index");
    }
    {
        auto metadata=InspectEditable((folder/L"sample.jpg").wstring());ValidateMetadataDestination(metadata,(folder/L"valid-destination.jpg").wstring());
        bool rejected=false;try{ValidateMetadataDestination(metadata,(folder/L"bad-destination.png").wstring());}catch(...){rejected=true;}
        check(rejected,"preflight format snapshot rejects mismatched destination");
    }
    {
        auto directory=folder/L"v0420-ocr";fs::create_directories(directory);auto path=directory/L"ocr-settings.json";
        if(fs::is_directory(path))fs::remove(path);
        struct Restore{fs::path folder;~Restore(){person_ocr::LoadSettings(folder);}} restore{folder};
        {std::ofstream out(path);out<<R"({"version":1,"fps":5,"seconds":10,"highPrecision":true})";}
        person_ocr::LoadSettings(directory);check(!person_ocr::GetSettings().showHighPrecision&&!person_ocr::GetSettings().highPrecision,"legacy high precision ON migrates to hidden normal mode");
        person_ocr::SetSettings({false,5,10,false,true});person_ocr::LoadSettings(directory);check(person_ocr::GetSettings().showHighPrecision&&!person_ocr::GetSettings().highPrecision,"display ON persists without enabling OCR mode");
        person_ocr::SetSettings({false,5,10,true,true});person_ocr::LoadSettings(directory);check(person_ocr::GetSettings().highPrecision,"visible high precision ON persists");
        fs::remove(path);fs::create_directory(path);bool failed=false;try{person_ocr::SetSettings({false,5,10,true,false});}catch(...){failed=true;}
        check(failed&&person_ocr::GetSettings().showHighPrecision&&person_ocr::GetSettings().highPrecision,"failed setting save keeps previous visibility and mode");fs::remove(path);
        person_ocr::SetSettings({false,5,10,true,false});person_ocr::LoadSettings(directory);check(!person_ocr::GetSettings().showHighPrecision&&!person_ocr::GetSettings().highPrecision,"display OFF persists and forces normal mode");
        {std::ofstream out(path);out<<"broken";}person_ocr::LoadSettings(directory);failed=false;try{person_ocr::SetSettings({false,5,10,false,true});}catch(...){failed=true;}
        std::ifstream in(path);std::string bytes((std::istreambuf_iterator<char>(in)),{});check(failed&&bytes=="broken","corrupt OCR settings are preserved");
    }
}

void RunV0420UiTests(std::ostream& report){
    const unsigned id=std::numeric_limits<unsigned>::max();
    for(int i=0;i<1000;++i){Result r;r.job.kind=6;r.job.generation=app.generation;r.job.searchGeneration=id;r.progress=true;r.text=std::to_wstring(i);QueueProgress(std::move(r));}
    {std::lock_guard<std::mutex> lock(app.mutex);size_t count=0;for(const auto& r:app.results)if(r.job.kind==6&&r.job.searchGeneration==id){++count;if(r.text!=L"999"||!r.job.files.empty())throw std::runtime_error("progress did not retain latest lightweight message");}
        if(count!=1)throw std::runtime_error("progress queue was not coalesced");
        Result done;done.job.kind=6;done.job.generation=app.generation;done.job.searchGeneration=id;done.files={L"completed"};app.results.push_back(std::move(done));}
    Result next;next.job.kind=6;next.job.generation=app.generation;next.job.searchGeneration=id;next.progress=true;next.text=L"latest";QueueProgress(std::move(next));
    {std::lock_guard<std::mutex> lock(app.mutex);size_t completions=0;for(const auto& r:app.results)if(r.job.kind==6&&r.job.searchGeneration==id&&!r.progress&&r.files==std::vector<std::wstring>{L"completed"})++completions;
        if(completions!=1)throw std::runtime_error("coalescing lost completion");
        app.results.erase(std::remove_if(app.results.begin(),app.results.end(),[&](const auto& r){return r.job.kind==6&&r.job.searchGeneration==id;}),app.results.end());}
    report<<"PASS: v0.4.20 progress coalesces 1000 updates and preserves completion\n";
    bool visible=IsWindowVisible(app.window)!=FALSE;ShowWindow(app.window,SW_SHOWNOACTIVATE);
    struct RestoreVisible{bool visible;~RestoreVisible(){if(!visible)ShowWindow(app.window,SW_HIDE);}} restore{visible};
    SendMessageW(app.window,WM_TIMER,RecordTimer,0);RedrawWindow(app.preview,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
    unsigned paints=0;
    auto probe=[](HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT{if(m==WM_PAINT)++*reinterpret_cast<unsigned*>(data);return DefSubclassProc(h,m,w,l);};
    if(!SetWindowSubclass(app.preview,probe,42,reinterpret_cast<DWORD_PTR>(&paints)))throw std::runtime_error("preview paint probe");
    for(int i=0;i<5;++i)SendMessageW(app.window,WM_TIMER,RecordTimer,0);
    RedrawWindow(app.preview,nullptr,nullptr,RDW_UPDATENOW);RemoveWindowSubclass(app.preview,probe,42);
    if(paints)throw std::runtime_error("unchanged preview was invalidated by idle timer");
    report<<"PASS: v0.4.20 unchanged preview avoids idle timer repaint\n";
}
