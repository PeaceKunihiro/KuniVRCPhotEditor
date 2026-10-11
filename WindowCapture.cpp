#define NOMINMAX
#include "WindowCapture.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <mutex>
#include <atomic>
#include <cmath>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")
using namespace winrt::Windows::Graphics;
namespace {stage3::Time QpcMilliseconds(){LARGE_INTEGER count{},frequency{};QueryPerformanceCounter(&count);QueryPerformanceFrequency(&frequency);return static_cast<stage3::Time>((static_cast<long double>(count.QuadPart)*1000)/frequency.QuadPart);}}
struct WindowCapture::Core : std::enable_shared_from_this<Core> {
    std::mutex mutex;std::atomic<bool> closed{false};std::wstring error;
    winrt::com_ptr<ID3D11Device> device;winrt::com_ptr<ID3D11DeviceContext> context;
    winrt::com_ptr<ID3D11ComputeShader> shader;
    winrt::com_ptr<ID3D11Texture2D> input,output,staging;winrt::com_ptr<ID3D11ShaderResourceView> view;winrt::com_ptr<ID3D11UnorderedAccessView> uav;UINT sourceWidth=0,sourceHeight=0,outputWidth=0,outputHeight=0;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice projected{nullptr};
    Capture::GraphicsCaptureItem item{nullptr};Capture::Direct3D11CaptureFramePool pool{nullptr};Capture::GraphicsCaptureSession session{nullptr};winrt::event_token token{};
    bool native=false,strictNative=false;int fps=5,seconds=10;stage3::Time offset=0,last=0;std::function<void(Pixels&&,stage3::Time)> receive;
    void Initialize(HWND window){
        if(!Capture::GraphicsCaptureSession::IsSupported())throw std::runtime_error("Windows Graphics Capture unavailable");
        D3D_FEATURE_LEVEL feature{};winrt::check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,device.put(),&feature,context.put()));
        if(feature<D3D_FEATURE_LEVEL_11_0)throw std::runtime_error("Direct3D 11 feature level 11 required");
        auto dxgi=device.as<IDXGIDevice>();winrt::com_ptr<::IInspectable> inspectable;winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),inspectable.put()));projected=inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
        auto factory=winrt::get_activation_factory<Capture::GraphicsCaptureItem,IGraphicsCaptureItemInterop>();winrt::check_hresult(factory->CreateForWindow(window,winrt::guid_of<Capture::GraphicsCaptureItem>(),winrt::put_abi(item)));
        const char source[]=R"(Texture2D<float4> inputImage : register(t0); RWTexture2D<float4> outputImage : register(u0);
        [numthreads(8,8,1)] void main(uint3 p : SV_DispatchThreadID) { uint w,h,iw,ih; outputImage.GetDimensions(w,h);inputImage.GetDimensions(iw,ih);if(p.x>=w||p.y>=h)return;uint2 q=min(uint2((float2(p.xy)+0.5)*float2(iw,ih)/float2(w,h)),uint2(iw-1,ih-1));float4 c=inputImage.Load(int3(q,0));outputImage[p.xy]=float4(c.b,c.g,c.r,1); })";
        winrt::com_ptr<ID3DBlob> compiled,errors;winrt::check_hresult(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,compiled.put(),errors.put()));winrt::check_hresult(device->CreateComputeShader(compiled->GetBufferPointer(),compiled->GetBufferSize(),nullptr,shader.put()));
        offset=stage3::Now()-QpcMilliseconds();pool=Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(projected,winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());session=pool.CreateCaptureSession(item);
        std::weak_ptr<Core> weak=shared_from_this();token=pool.FrameArrived([weak](auto const& sender,auto const&){if(auto self=weak.lock())self->Frame(sender);});session.StartCapture();
    }
    void Frame(const Capture::Direct3D11CaptureFramePool& sender){
        std::lock_guard<std::mutex> lock(mutex);if(closed)return;
        try{auto frame=sender.TryGetNextFrame();if(!frame)return;auto size=frame.ContentSize();auto time=frame.SystemRelativeTime().count()/10000;
            if(size.Width<=0||size.Height<=0){frame.Close();return;}
            winrt::com_ptr<ID3D11Texture2D> texture;auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();winrt::check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),texture.put_void()));D3D11_TEXTURE2D_DESC sourceDesc{};texture->GetDesc(&sourceDesc);
            bool resized=sourceDesc.Width!=static_cast<UINT>(size.Width)||sourceDesc.Height!=static_cast<UINT>(size.Height);
            if(resized){frame.Close();pool.Recreate(projected,winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);last=0;return;}
            if(time-last<1000/fps){frame.Close();return;}last=time;
            // GPU downscale before readback; preserve full window aspect ratio.
            double pixelBudget=256.0*1024*1024/(fps*seconds*2)*0.7;
            double scale=std::min({1.0,1920.0/size.Width,1080.0/size.Height,std::sqrt(pixelBudget/(double(size.Width)*size.Height))});if(native)scale=std::min({1.0,4096.0/size.Width,4096.0/size.Height,std::sqrt(16.0*1024*1024/(double(size.Width)*size.Height))});if(strictNative){if(uint64_t(size.Width)*size.Height>16*1024*1024){frame.Close();error=L"高精度：16Mi画素上限を超えるため取得を保留（縮小なし）";return;}scale=1.0;}UINT width=std::max(1U,static_cast<UINT>(size.Width*scale)),height=std::max(1U,static_cast<UINT>(size.Height*scale));
            if(sourceWidth!=sourceDesc.Width||sourceHeight!=sourceDesc.Height||outputWidth!=width||outputHeight!=height){input=nullptr;output=nullptr;staging=nullptr;view=nullptr;uav=nullptr;
            D3D11_TEXTURE2D_DESC sourceCopy=sourceDesc;sourceCopy.Usage=D3D11_USAGE_DEFAULT;sourceCopy.BindFlags=D3D11_BIND_SHADER_RESOURCE;sourceCopy.CPUAccessFlags=0;sourceCopy.MiscFlags=0;winrt::check_hresult(device->CreateTexture2D(&sourceCopy,nullptr,input.put()));winrt::check_hresult(device->CreateShaderResourceView(input.get(),nullptr,view.put()));
            D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;winrt::check_hresult(device->CreateTexture2D(&desc,nullptr,output.put()));winrt::check_hresult(device->CreateUnorderedAccessView(output.get(),nullptr,uav.put()));
            desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;winrt::check_hresult(device->CreateTexture2D(&desc,nullptr,staging.put()));sourceWidth=sourceDesc.Width;sourceHeight=sourceDesc.Height;outputWidth=width;outputHeight=height;}
            context->CopyResource(input.get(),texture.get());
            auto resource=view.get();auto target=uav.get();context->CSSetShader(shader.get(),nullptr,0);context->CSSetShaderResources(0,1,&resource);context->CSSetUnorderedAccessViews(0,1,&target,nullptr);context->Dispatch((width+7)/8,(height+7)/8,1);ID3D11ShaderResourceView* noResource=nullptr;ID3D11UnorderedAccessView* noTarget=nullptr;context->CSSetShaderResources(0,1,&noResource);context->CSSetUnorderedAccessViews(0,1,&noTarget,nullptr);
context->CopyResource(staging.get(),output.get());D3D11_MAPPED_SUBRESOURCE mapped{};winrt::check_hresult(context->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped));Pixels pixels{width,height,std::vector<BYTE>(size_t(width)*height*4)};for(UINT y=0;y<height;++y)memcpy(pixels.data.data()+size_t(y)*width*4,static_cast<BYTE*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(width)*4);context->Unmap(staging.get(),0);frame.Close();receive(std::move(pixels),offset+time);error.clear();
        }catch(const winrt::hresult_error& e){error=L"キャプチャー取得エラー: "+std::wstring(e.message().c_str());}catch(...){error=L"キャプチャー取得失敗";}
    }
    void Close(){closed=true;try{if(pool)pool.FrameArrived(token);}catch(...){}Capture::GraphicsCaptureSession ending{nullptr};Capture::Direct3D11CaptureFramePool frames{nullptr};{std::lock_guard<std::mutex> lock(mutex);ending=session;frames=pool;session=nullptr;pool=nullptr;}try{if(ending)ending.Close();if(frames)frames.Close();}catch(...){}}
};
WindowCapture::WindowCapture(HWND window,int fps,int seconds,std::function<void(Pixels&&,stage3::Time)> receive,bool native,bool strictNative){core=std::make_shared<Core>();core->native=native;core->strictNative=strictNative;core->fps=fps;core->seconds=seconds;core->receive=std::move(receive);try{core->Initialize(window);}catch(...){core->Close();throw;}}
WindowCapture::~WindowCapture(){if(core)core->Close();}
std::wstring WindowCapture::Error()const{std::lock_guard<std::mutex> lock(core->mutex);return core->error;}
