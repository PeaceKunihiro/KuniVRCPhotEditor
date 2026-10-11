#include "NativeTileOcr.h"
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <roapi.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <stdexcept>
namespace person_ocr {
namespace {
// Share the limit between automatic recording and test OCR. Only one batch owns workers.
std::mutex batchGate;
struct Line {TextLine value;RECT rect;};
bool Overlaps(const RECT& a,const RECT& b){return a.left<b.right&&b.left<a.right&&a.top<b.bottom&&b.top<a.bottom;}
}
std::vector<NativeTile> NativeTiles(UINT width,UINT height,UINT limit){
    if(!width||!height||limit<2)throw std::runtime_error("Invalid native OCR dimensions");
    const UINT tile=std::min(1024U,limit),overlap=std::min(128U,tile/4),step=tile-overlap;
    std::vector<NativeTile> tiles;
    for(UINT y=0;;y+=step){for(UINT x=0;;x+=step){tiles.push_back({x,y,std::min(tile,width-x),std::min(tile,height-y)});if(width-x<=tile)break;}if(height-y<=tile)break;}
    return tiles;
}
std::vector<TextLine> RecognizeNativeTiles(UINT width,UINT height,const std::vector<BYTE>& gray,const std::function<bool()>& cancelled,NativeTileStats* stats){
    if(uint64_t(width)*height>16*1024*1024||gray.size()!=size_t(width)*height)throw std::runtime_error("Native OCR image limit: 16Mi pixels");
    std::unique_lock<std::mutex> gate(batchGate,std::defer_lock);
    while(!gate.try_lock()){if(cancelled())return {};Sleep(10);}
    if(cancelled())return {};
    using namespace winrt::Windows;
    auto tiles=NativeTiles(width,height,Media::Ocr::OcrEngine::MaxImageDimension());
    std::vector<std::vector<Line>> output(tiles.size());std::atomic<size_t> next{0};std::atomic<bool> failed{false};
    std::atomic<unsigned> active{0},peak{0};
    std::exception_ptr error;std::mutex errorMutex;
    auto work=[&]{unsigned current=++active,prior=peak.load();while(current>prior&&!peak.compare_exchange_weak(prior,current)){}
        auto initialized=RoInitialize(RO_INIT_MULTITHREADED);
        try{winrt::check_hresult(initialized);std::vector<Media::Ocr::OcrEngine> engines;
            for(auto language:{L"ja",L"en"}){auto engine=Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(language));if(engine)engines.push_back(engine);}
            if(engines.empty()){auto engine=Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();if(engine)engines.push_back(engine);}
            if(engines.empty())throw std::runtime_error("Windows OCR language model unavailable");
            while(!failed&&!cancelled()){auto index=next.fetch_add(1);if(index>=tiles.size())break;auto t=tiles[index];
                std::vector<BYTE> pixels(size_t(t.width)*t.height);
                for(UINT y=0;y<t.height;++y)std::copy_n(gray.data()+size_t(y+t.y)*width+t.x,t.width,pixels.data()+size_t(y)*t.width);
                std::vector<UINT> integral(size_t(t.width+1)*(t.height+1));for(UINT y=0;y<t.height;++y){UINT sum=0;for(UINT x=0;x<t.width;++x){sum+=pixels[size_t(y)*t.width+x];integral[size_t(y+1)*(t.width+1)+x+1]=integral[size_t(y)*(t.width+1)+x+1]+sum;}}
                for(int pass=0;pass<2&&!cancelled();++pass){auto input=pixels;
                    if(pass)for(UINT y=0;y<t.height;++y)for(UINT x=0;x<t.width;++x){UINT left=x>2?x-2:0,top=y>2?y-2:0,right=std::min(t.width,x+3),bottom=std::min(t.height,y+3);auto at=[&](UINT a,UINT b){return integral[size_t(b)*(t.width+1)+a];};UINT sum=at(right,bottom)-at(left,bottom)-at(right,top)+at(left,top),count=(right-left)*(bottom-top);input[size_t(y)*t.width+x]=pixels[size_t(y)*t.width+x]>=sum/count?255:0;}
                    Storage::Streams::DataWriter writer;writer.WriteBytes(input);auto bitmap=Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(),Graphics::Imaging::BitmapPixelFormat::Gray8,t.width,t.height);
                    for(auto& engine:engines){if(cancelled())break;auto result=engine.RecognizeAsync(bitmap).get();
                        for(auto line:result.Lines()){if(output[index].size()>=512)break;std::wstring bounds;std::vector<std::wstring> words;RECT rect{LONG(width),LONG(height),0,0};
                            for(auto word:line.Words()){auto b=word.BoundingRect();LONG x=LONG(b.X)+t.x,y=LONG(b.Y)+t.y,w=LONG(b.Width),h=LONG(b.Height);rect.left=std::min(rect.left,x);rect.top=std::min(rect.top,y);rect.right=std::max(rect.right,x+w);rect.bottom=std::max(rect.bottom,y+h);bounds+=std::to_wstring(x)+L","+std::to_wstring(y)+L","+std::to_wstring(w)+L","+std::to_wstring(h)+L";";words.push_back(word.Text().c_str());}
                            std::wstring text=line.Text().c_str();output[index].push_back({{text.substr(0,256),bounds.substr(0,2048),true},rect});
                            for(size_t i=0;i<words.size()&&output[index].size()<512;++i){std::wstring joined;for(size_t j=i;j<std::min(words.size(),i+6)&&output[index].size()<512;++j){if(!joined.empty())joined+=L" ";joined+=words[j];if(joined!=text)output[index].push_back({{joined.substr(0,256),bounds.substr(0,2048),false},rect});}}
                        }
                    }bitmap.Close();
                }
            }
        }catch(...){std::lock_guard<std::mutex> lock(errorMutex);if(!error)error=std::current_exception();failed=true;}
        if(SUCCEEDED(initialized))RoUninitialize();--active;
    };
    std::vector<std::thread> threads;auto count=std::min<size_t>({tiles.size(),4,std::max(1U,std::thread::hardware_concurrency())});
    try{for(size_t i=0;i<count;++i)threads.emplace_back(work);}catch(...){failed=true;for(auto& thread:threads)thread.join();throw;}
    for(auto& thread:threads)thread.join();if(stats){stats->peakWorkers=peak;stats->tiles=tiles.size();}if(error)std::rethrow_exception(error);if(cancelled())return {};
    std::vector<Line> merged;std::vector<TextLine> lines;
    for(auto& group:output)for(auto& line:group){if(merged.size()>=512)break;bool duplicate=std::any_of(merged.begin(),merged.end(),[&](auto& prior){return prior.value.text==line.value.text&&prior.value.original==line.value.original&&Overlaps(prior.rect,line.rect);});if(!duplicate){merged.push_back(line);lines.push_back(std::move(line.value));}}
    return lines;
}
void RunNativeTileTests(std::ostream& report){
    auto tiles=NativeTiles(2300,1400,1024);std::vector<bool> coverage(2300*1400);
    for(auto t:tiles){if(t.width>1024||t.height>1024)throw std::runtime_error("Native tile exceeds OCR limit");for(UINT y=t.y;y<t.y+t.height;++y)for(UINT x=t.x;x<t.x+t.width;++x)coverage[size_t(y)*2300+x]=true;}
    if(std::find(coverage.begin(),coverage.end(),false)!=coverage.end()||tiles.size()!=6||tiles[1].x>=tiles[0].width)throw std::runtime_error("Native tile coverage/overlap");
    if(NativeTiles(50,30,64).size()!=1||NativeTiles(128,64,64).size()!=3)throw std::runtime_error("Native tile small limit");
    std::vector<BYTE> gray(64*64,255);if(!RecognizeNativeTiles(64,64,gray,[]{return true;}).empty())throw std::runtime_error("Cancelled native OCR returned results");
    report<<"PASS: v0.4.10 automatic native tile coverage, overlap, API input limit and cancellation\n";
}
}
