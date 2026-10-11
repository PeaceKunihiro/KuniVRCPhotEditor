#include "ViewerRuntime.h"
#include "TestOcr.h"
#include "WindowCapture.h"
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <cwctype>
#include <set>
namespace test_ocr {
namespace {
std::mutex guard;std::condition_variable signal;std::thread thread;
bool stopping=true,active=false;std::shared_ptr<Pixels> pending,preview;std::wstring raw,status=L"テスト停止";
uint64_t generation=0;
void Notify(){if(app.window)PostMessageW(app.window,WM_APP+4,0,0);}
std::wstring Trim(std::wstring text){while(!text.empty()&&iswspace(text.back()))text.pop_back();auto i=text.find_first_not_of(L" \t\r\n");return i==text.npos?L"":text.substr(i);}
void Receive(Pixels&& p,uint64_t version){std::lock_guard<std::mutex> lock(guard);if(stopping||version!=generation)return;pending=std::make_shared<Pixels>(std::move(p));preview=pending;signal.notify_all();Notify();}
Pixels ReadImage(const std::wstring& path){
    if(_wcsicmp(fs::path(path).extension().c_str(),L".webp")==0){auto p=ReadWebP(path,4096,4096);if(uint64_t(p.sourceWidth)*p.sourceHeight>16*1024*1024)throw std::runtime_error("Test OCR image limit: 16Mi pixels");return {p.width,p.height,std::move(p.pixels)};}
    ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICBitmapDecoder> decoder;winrt::check_hresult(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));ComPtr<IWICBitmapFrameDecode> frame;winrt::check_hresult(decoder->GetFrame(0,&frame));UINT w{},h{};frame->GetSize(&w,&h);if(uint64_t(w)*h>16*1024*1024)throw std::runtime_error("Test OCR image limit: 16Mi pixels");return Decode(factory.Get(),frame.Get(),w,h);
}
std::wstring Recognize(const Pixels& p,uint64_t version){
    using namespace winrt::Windows;std::vector<Media::Ocr::OcrEngine> engines;
    for(auto lang:{L"ja",L"en"}){auto e=Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(lang));if(e)engines.push_back(e);}
    if(engines.empty()){auto e=Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();if(e)engines.push_back(e);}
    if(engines.empty())throw std::runtime_error("Windows OCR language model unavailable");
    // Overlapping native-resolution tiles preserve small characters; two passes tolerate backgrounds.
    std::set<std::wstring> unique;std::wstring text;
    constexpr UINT tile=768,step=704;
    for(UINT y=0;y<p.height;y+=step)for(UINT x=0;x<p.width;x+=step){
        {std::lock_guard<std::mutex> lock(guard);if(stopping||version!=generation)return {};}
        UINT w=std::min(tile,p.width-x),h=std::min(tile,p.height-y);std::vector<BYTE> gray(size_t(w)*h);
        for(UINT j=0;j<h;++j)for(UINT i=0;i<w;++i){auto q=&p.data[(size_t(y+j)*p.width+x+i)*4];gray[size_t(j)*w+i]=BYTE((q[2]*77+q[1]*150+q[0]*29)>>8);}
        for(int pass=0;pass<2;++pass){
            std::vector<BYTE> scaled(size_t(w)*h*4);
            for(UINT j=0;j<h*2;++j)for(UINT i=0;i<w*2;++i){int v=gray[size_t(j/2)*w+i/2];if(pass){int sum=0,count=0;for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){int a=int(i/2)+dx,b=int(j/2)+dy;if(a>=0&&b>=0&&a<int(w)&&b<int(h)){sum+=gray[size_t(b)*w+a];++count;}}v=v>=sum/count?255:0;}scaled[size_t(j)*w*2+i]=BYTE(v);}
            Storage::Streams::DataWriter writer;writer.WriteBytes(scaled);auto bitmap=Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(),Graphics::Imaging::BitmapPixelFormat::Gray8,w*2,h*2);
            for(auto& e:engines){{std::lock_guard<std::mutex> lock(guard);if(stopping||version!=generation)return {};}auto result=e.RecognizeAsync(bitmap).get();for(auto line:result.Lines()){auto value=std::wstring(line.Text().c_str());if(unique.insert(value).second&&text.size()<60000)text+=value+L"\r\n";}}
            bitmap.Close();
        }
    }return text.substr(0,60000);
}
void Work(HWND target,int fps,int seconds,std::wstring path,uint64_t version){
    auto initialized=RoInitialize(RO_INIT_MULTITHREADED);std::unique_ptr<WindowCapture> capture;
    try{winrt::check_hresult(initialized);
        if(!path.empty())Receive(ReadImage(path),version);
        else capture=std::make_unique<WindowCapture>(target,fps,1,[version](Pixels&& p,stage3::Time){Receive(std::move(p),version);},true);
        for(;;){std::shared_ptr<Pixels> image;{std::unique_lock<std::mutex> lock(guard);signal.wait_for(lock,std::chrono::milliseconds(100),[]{return stopping||pending;});if(stopping)break;image=std::move(pending);if(image)status=L"テストOCR処理中（保存なし）";}
            if(capture){auto error=capture->Error();if(!error.empty()||!IsWindow(target)||IsIconic(target)){std::lock_guard<std::mutex> lock(guard);status=error.empty()?L"対象ウィンドウ待ち／最小化":error;preview.reset();pending.reset();Notify();}}
            if(!image)continue;Notify();auto result=Recognize(*image,version);{std::lock_guard<std::mutex> lock(guard);if(!stopping&&version==generation){raw=std::move(result);status=L"テストOCR完了（推測・保存なし）";Notify();}}
            if(!path.empty())break;
        }
    }catch(const winrt::hresult_error& e){std::lock_guard<std::mutex> lock(guard);status=L"テストOCR失敗: "+std::wstring(e.message().c_str());Notify();}
    catch(const std::exception& e){std::lock_guard<std::mutex> lock(guard);status=L"テストOCR失敗: "+std::wstring(winrt::to_hstring(e.what()).c_str());Notify();}
    capture.reset();{std::lock_guard<std::mutex> lock(guard);if(version==generation)active=false;}if(SUCCEEDED(initialized))RoUninitialize();
}
}
Pixels ReadImagePixels(const std::wstring& path){return ReadImage(path);}
bool NameLike(const std::wstring& value){
    auto text=Trim(value);if(text.empty())return false;
    int n=NormalizeString(NormalizationKC,text.c_str(),int(text.size()),nullptr,0);if(n<=0)return false;std::wstring normalized(n,L'\0');n=NormalizeString(NormalizationKC,text.c_str(),int(text.size()),normalized.data(),n);if(n<=0)return false;normalized.resize(n);
    int count=0;bool numeric=true,letter=false,joined=false;
    for(size_t i=0;i<normalized.size();++i){wchar_t c=normalized[i];WORD kind{},extra{};GetStringTypeW(CT_CTYPE1,&c,1,&kind);GetStringTypeW(CT_CTYPE3,&c,1,&extra);
        if(c==0x200D){joined=true;continue;}if(((extra&C3_NONSPACING)&&!(kind&C1_ALPHA))||c==0xFE0F)continue;
        if(!joined)++count;joined=false;
        if(c>=0xD800&&c<=0xDBFF&&i+1<normalized.size()&&normalized[i+1]>=0xDC00&&normalized[i+1]<=0xDFFF){++i;numeric=false;continue;}
        if(!(kind&C1_DIGIT)&&!iswspace(c))numeric=false;if(kind&C1_ALPHA)letter=true;
    }return count>=2&&count<20&&!numeric&&letter;
}
std::wstring FilterNames(const std::wstring& value){std::wistringstream stream(value);std::wstring line,out;std::set<std::wstring> unique;while(std::getline(stream,line)){line=Trim(line);if(NameLike(line)&&unique.insert(line).second)out+=line+L"\r\n";}return out;}
std::vector<person_ocr::Window> Windows(){std::vector<person_ocr::Window> list;EnumWindows([](HWND h,LPARAM data)->BOOL{DWORD pid{};GetWindowThreadProcessId(h,&pid);if(pid==GetCurrentProcessId()||!IsWindowVisible(h)||GetWindow(h,GW_OWNER))return TRUE;wchar_t title[1024]{};if(!GetWindowTextW(h,title,1024))return TRUE;reinterpret_cast<std::vector<person_ocr::Window>*>(data)->push_back({h,pid,title});return TRUE;},reinterpret_cast<LPARAM>(&list));return list;}
void Stop(){ {std::lock_guard<std::mutex> lock(guard);stopping=true;++generation;}signal.notify_all();if(thread.joinable())thread.join();std::lock_guard<std::mutex> lock(guard);active=false;pending.reset();preview.reset();raw.clear();status=L"テスト停止";}
void Capture(HWND target,int fps,int seconds){Stop();if(!target)return;std::lock_guard<std::mutex> lock(guard);stopping=false;active=true;auto version=++generation;status=L"テストキャプチャ開始";thread=std::thread(Work,target,fps,seconds,L"",version);}
void Image(const std::wstring& path){Stop();if(path.empty())return;std::lock_guard<std::mutex> lock(guard);stopping=false;active=true;auto version=++generation;status=L"画像OCR開始";thread=std::thread(Work,nullptr,5,1,path,version);}
bool Active(){std::lock_guard<std::mutex> lock(guard);return active;}
std::wstring Raw(){std::lock_guard<std::mutex> lock(guard);return raw;}
std::wstring Status(){std::lock_guard<std::mutex> lock(guard);return status;}
std::shared_ptr<const Pixels> Preview(){std::lock_guard<std::mutex> lock(guard);return preview;}
void Tests(const std::filesystem::path& folder,std::ostream& report){
    auto require=[](bool v){if(!v)throw std::runtime_error("v0.4.5 name filter");};
    for(auto& value:std::vector<std::pair<std::wstring,bool>>{{L"1",false},{L"１２３４",false},{L"___",false},{L"あ",false},{std::wstring(20,L'a'),false},{std::wstring(19,L'a'),true},{L"名前_01",true},{L"e\u0301x",true}})if(NameLike(value.first)!=value.second)throw std::runtime_error("name filter case: "+winrt::to_string(value.first));
    require(FilterNames(L"名前\n名前\n123\na\n")==L"名前\r\n");
    report<<"PASS: v0.4.5 test name filter Unicode/digits/symbols/1/19/20/dedup\n";
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=640;info.bmiHeader.biHeight=-240;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* data{};auto dc=CreateCompatibleDC(nullptr);auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);auto old=SelectObject(dc,bitmap);RECT rect{0,0,640,240};FillRect(dc,&rect,GetSysColorBrush(COLOR_WINDOW));auto font=CreateFontW(48,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Arial");auto oldFont=SelectObject(dc,font);SetTextColor(dc,RGB(0,0,0));SetBkColor(dc,RGB(255,255,255));TextOutW(dc,20,30,L"TESTMODEL",9);TextOutW(dc,20,110,L"OTHERNAME",9);GdiFlush();std::vector<BYTE> pixels(640*240*4);memcpy(pixels.data(),data,pixels.size());for(size_t i=3;i<pixels.size();i+=4)pixels[i]=255;SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    auto path=folder/L"v045-ocr-image.png";ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;winrt::check_hresult(factory->CreateStream(&stream));winrt::check_hresult(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));ComPtr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;winrt::check_hresult(encoder->CreateNewFrame(&frame,nullptr));winrt::check_hresult(frame->Initialize(nullptr));winrt::check_hresult(frame->SetSize(640,240));auto format=GUID_WICPixelFormat32bppBGRA;winrt::check_hresult(frame->SetPixelFormat(&format));winrt::check_hresult(frame->WritePixels(240,640*4,UINT(pixels.size()),pixels.data()));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());frame.Reset();encoder.Reset();stream.Reset();
    auto original=MetadataFingerprint(path.wstring()),normal=person_ocr::Raw();Image(path.wstring());auto deadline=GetTickCount64()+20000;while(Active()){if(GetTickCount64()>deadline){Stop();throw std::runtime_error("test image OCR timeout");}Sleep(10);}auto result=Raw();if(result.find(L"TESTMODEL")==result.npos||result.find(L"OTHERNAME")==result.npos){auto error=Status();Stop();throw std::runtime_error("test image OCR names missing: "+winrt::to_string(error));}require(person_ocr::Raw()==normal&&MetadataFingerprint(path.wstring())==original&&Preview());Stop();require(!Preview()&&Raw().empty());
    struct RecordingCleanup{~RecordingCleanup(){person_ocr::Stop();}}cleanup;person_ocr::Start(nullptr);person_ocr::Context(GetCurrentProcessId(),L"image-test-session",L"image-test-instance");stage3::Record r;r.reference=stage3::Now();r.session=L"image-test-session";r.instance=L"image-test-instance";r.members={{L"TESTMODEL",L"usr_1"},{L"OTHERNAME",L"usr_2"}};stage3::InstanceTimeline timeline;stage3::Event event;event.time=r.reference-10000;event.kind=stage3::Kind::Room;event.instance=r.instance;timeline.Add(event);event.kind=stage3::Kind::Join;event.time=r.reference-9000;for(auto& p:r.members){event.person=p;timeline.Add(event);}require(person_ocr::SubmitImage(path.wstring(),r,timeline));deadline=GetTickCount64()+20000;while(!person_ocr::Poll(path.wstring(),r)){if(GetTickCount64()>deadline)throw std::runtime_error("F12 image OCR job timeout");Sleep(10);}require(r.candidates.size()==2&&r.candidateStatus==L"observed"&&person_ocr::Names().find(L"TESTMODEL")!=std::wstring::npos);person_ocr::Stop();report<<"PASS: v0.4.5 direct screenshot OCR produces two log-matched candidates\n";
    report<<"PASS: v0.4.5 async image OCR two names, original unchanged, normal output isolated, stop release\n";
}
}

