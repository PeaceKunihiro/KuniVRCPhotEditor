#include "MetadataWriterTests.h"
#include "MetadataWriter.h"
#include "ExtendedXmp.h"
#include <fstream>
#include <stdexcept>
#include <wincodec.h>
#include <wrl/client.h>
#include <winrt/base.h>
#include "PngCrc.h"
#include <chrono>
void RunMetadataWriterTests(const std::filesystem::path& directory,std::ostream& report){
    namespace fs=std::filesystem;
    auto require=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: "<<label<<'\n';};
    {
        auto reference=[](std::string_view s){uint32_t c=~0U;for(unsigned char b:s){c^=b;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320U:0);}return ~c;};
        require(PngCrc("")==0&&PngCrc("123456789")==0xcbf43926U,"v0.4.19 PNG CRC known vectors");
        std::string payload(4*1024*1024,'\0');for(size_t i=0;i<payload.size();++i)payload[i]=char((i*37+i/256)&255);
        for(size_t n:{size_t(1),size_t(7),size_t(255),size_t(4096),payload.size()}){auto part=std::string_view(payload).substr(0,n);require(PngCrc(part)==reference(part),"v0.4.19 PNG CRC equals independent bitwise reference");auto middle=n/2;require(~PngCrcUpdate(PngCrcUpdate(~0U,part.substr(0,middle)),part.substr(middle))==PngCrc(part),"v0.4.19 split CRC equals contiguous CRC");}
        auto measure=[&](auto f){auto start=std::chrono::steady_clock::now();auto crc=f(payload);auto end=std::chrono::steady_clock::now();return std::make_pair(crc,std::chrono::duration<double,std::milli>(end-start).count());};
        auto old=measure(reference),current=measure(PngCrc);require(old.first==current.first,"v0.4.19 measured CRC equality");report<<"INFO: v0.4.19 CRC bytes="<<payload.size()<<" bitwise_ms="<<old.second<<" table_ms="<<current.second<<'\n';
    }
    auto bytes=[](const fs::path& p){std::ifstream in(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(in)),{});};
    auto write=[](const fs::path& p,const std::string& s){std::ofstream out(p,std::ios::binary);out.write(s.data(),s.size());};
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    auto alphaSource=directory/L"writer-alpha.png";
    {Microsoft::WRL::ComPtr<IWICStream> stream;winrt::check_hresult(factory->CreateStream(&stream));winrt::check_hresult(stream->InitializeFromFilename(alphaSource.c_str(),GENERIC_WRITE));
        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));winrt::check_hresult(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;winrt::check_hresult(encoder->CreateNewFrame(&frame,nullptr));winrt::check_hresult(frame->Initialize(nullptr));winrt::check_hresult(frame->SetSize(2,2));auto format=GUID_WICPixelFormat32bppBGRA;winrt::check_hresult(frame->SetPixelFormat(&format));require(format==GUID_WICPixelFormat32bppBGRA,"transparent PNG fixture format");
        BYTE pixels[]{0,0,255,255,0,255,0,128,255,0,0,0,12,34,56,200};winrt::check_hresult(frame->WritePixels(2,8,sizeof(pixels),pixels));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());}
    {
        std::string data="XML:com.adobe.xmp";data+='\0';std::string payload="tEXt"+data,chunk;auto put=[&](uint32_t n){for(int i=24;i>=0;i-=8)chunk+=char(n>>i);};put(static_cast<uint32_t>(data.size()));chunk+=payload;put(PngCrc(payload));
        auto image=bytes(alphaSource);image.insert(8,chunk);auto path=directory/L"writer-empty-xmp.png";write(path,image);
        PhotoSummary shared;InspectEditable(path.wstring(),&shared);auto independent=ReadPhotoSummary(path.wstring());require(!independent.notes.empty()&&shared.notes==independent.notes&&shared.values==independent.values,"v0.4.19 empty XMP remains invalid in shared summary");
    }
    MetadataEdit edit;edit.values={L"2026-10-10T12:30:00.123+09:00",L"撮影者 & < > \"",L"ワールド\n日本語",L"wrld_12345678-1234-1234-1234-123456789abc",L"被写体A\r\n被写体B & < >"};edit.actions.fill(EditAction::Set);edit.actions[5]=EditAction::Keep;
    require(edit.Validate().empty(),"editor Japanese and special characters valid");
    auto invalid=edit;invalid.values[0]=L"2026-02-30";require(!invalid.Validate().empty(),"editor invalid date rejected");invalid=edit;invalid.values[3]=L"wrld_invalid";require(!invalid.Validate().empty(),"editor invalid world UUID rejected");invalid=edit;invalid.values[1].clear();require(!invalid.Validate().empty(),"empty value requires explicit delete");
    for(auto filename:{L"sample-日本語.png",L"sample.jpg",L"writer-alpha.png"}){auto source=directory/filename;auto original=bytes(source);PhotoSummary shared;auto info=InspectEditable(source.wstring(),&shared);auto independent=ReadPhotoSummary(source.wstring());require(shared.values==independent.values&&shared.notes==independent.notes&&shared.xmpDetail==independent.xmpDetail&&shared.people.size()==independent.people.size()&&shared.memberOffset==independent.memberOffset,"v0.4.19 shared summary equals independent extraction");auto output=directory/(std::wstring(L"writer-")+filename);fs::remove(output);
        SaveMetadataCopy(source.wstring(),output.wstring(),info.fingerprint,edit);auto actual=InspectEditable(output.wstring());
        XmpFields stale;stale.author=L"stale";auto uncached=ReadPhotoSummary(output.wstring());require(ReadPhotoSummary(output.wstring(),{{"different XML bytes",&stale}}).values==uncached.values,"v0.4.19 mismatched cached XML is never reused");
        require(actual.values[0]==edit.values[0]&&actual.values[1]==edit.values[1]&&actual.values[2]==edit.values[2]&&actual.values[3]==edit.values[3]&&actual.values[4]==L"被写体A\r\n被写体B & < >","PNG/JPEG edited fields and multiple subjects round trip");
        require(bytes(source)==original,"metadata save leaves original unchanged");bool rejected=false;try{SaveMetadataCopy(source.wstring(),output.wstring(),info.fingerprint,edit);}catch(...){rejected=true;}require(rejected,"existing destination never overwritten");
        auto decode=[&](const fs::path& path){Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;winrt::check_hresult(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;winrt::check_hresult(decoder->GetFrame(0,&frame));Microsoft::WRL::ComPtr<IWICFormatConverter> converter;winrt::check_hresult(factory->CreateFormatConverter(&converter));winrt::check_hresult(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));UINT width{},height{};frame->GetSize(&width,&height);std::vector<BYTE> pixels(size_t(width)*height*4);winrt::check_hresult(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data()));return pixels;};
        require(decode(source)==decode(output),"copy preserves decoded pixels including PNG transparency");
        auto second=directory/(std::wstring(L"removed-")+filename);fs::remove(second);MetadataEdit removal;removal.actions[1]=EditAction::Remove;SaveMetadataCopy(output.wstring(),second.wstring(),actual.fingerprint,removal);auto removed=InspectEditable(second.wstring());require(removed.values[1].empty()&&removed.values[0]==actual.values[0]&&removed.values[4]==actual.values[4],"explicit delete keeps other fields");
        auto third=directory/(std::wstring(L"stale-")+filename);fs::remove(third);rejected=false;try{SaveMetadataCopy(source.wstring(),third.wstring(),L"STALE",edit);}catch(...){rejected=true;}require(rejected&&!fs::exists(third),"external source change aborts without output");
    }
    auto changed=directory/L"writer-external-change.jpg";fs::copy_file(directory/L"sample.jpg",changed,fs::copy_options::overwrite_existing);auto oldInfo=InspectEditable(changed.wstring());auto changedBytes=bytes(changed);changedBytes+='x';write(changed,changedBytes);auto rejectedOutput=directory/L"writer-external-rejected.jpg";fs::remove(rejectedOutput);bool changedRejected=false;try{SaveMetadataCopy(changed.wstring(),rejectedOutput.wstring(),oldInfo.fingerprint,edit);}catch(...){changedRejected=true;}require(changedRejected&&!fs::exists(rejectedOutput),"actual external byte change after edit snapshot aborts save");
    auto app1=[](const std::string& data){size_t n=data.size()+2;return std::string("\xff\xe1",2)+char(n>>8)+char(n)+data;};
    const char stdSig[]="http://ns.adobe.com/xap/1.0/",extSig[]="http://ns.adobe.com/xmp/extension/";
    auto be=[](uint32_t n){std::string s;for(int i=24;i>=0;i-=8)s+=char(n>>i);return s;};
    const std::string start="<x:xmpmeta xmlns:x='adobe:ns:meta/'><rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><rdf:Description rdf:about='' xmlns:a='http://ns.adobe.com/xap/1.0/' xmlns:u='urn:unknown' ";
    const std::string end="</rdf:Description></rdf:RDF></x:xmpmeta>";
    std::string extended=start+"a:Author='old'><u:Unknown attr='keep'>"+std::string(70000,'x')+"</u:Unknown>"+end;auto guid=XmpMd5(extended);
    auto standard=start+"xmlns:n='http://ns.adobe.com/xmp/note/' n:HasExtendedXMP='"+guid+"'><u:Custom>保持</u:Custom>"+end;
    std::string segments=app1(std::string(stdSig,sizeof(stdSig))+standard);for(size_t off=0;off<extended.size();){size_t n=std::min<size_t>(40000,extended.size()-off);segments+=app1(std::string(extSig,sizeof(extSig))+guid+be(static_cast<uint32_t>(extended.size()))+be(static_cast<uint32_t>(off))+extended.substr(off,n));off+=n;}
    auto original=bytes(directory/L"sample.jpg");original.insert(2,segments);auto source=directory/L"writer-extended-source.jpg";write(source,original);auto info=InspectEditable(source.wstring());auto output=directory/L"writer-extended-output.jpg";fs::remove(output);
    SaveMetadataCopy(source.wstring(),output.wstring(),info.fingerprint,edit);auto result=ReadPhotoSummary(output.wstring());
    require(result.values[4]==edit.values[1]&&result.values[7]==edit.values[4]&&result.xmpDetail.find(L"保持")!=std::wstring::npos&&result.xmpDetail.find(std::wstring(70000,L'x'))!=std::wstring::npos&&result.notes.find(L"検証成功")!=std::wstring::npos,"Extended XMP rewritten with unknown data and valid MD5");
    require(bytes(source)==original,"Extended XMP original unchanged");
    original[original.find(guid)+1]='Z';auto broken=directory/L"writer-broken.jpg";write(broken,original);bool rejected=false;try{InspectEditable(broken.wstring());}catch(...){rejected=true;}require(rejected,"broken Extended XMP cannot enter editor");
    auto large=edit;large.values[1]=std::wstring(50000,L'界');auto promoted=directory/L"writer-promoted.jpg";fs::remove(promoted);auto simple=directory/L"sample.jpg";SaveMetadataCopy(simple.wstring(),promoted.wstring(),MetadataFingerprint(simple.wstring()),large);require(InspectEditable(promoted.wstring()).values[1]==large.values[1],"large standard XMP safely promoted to extension");
    auto webp=directory/L"metadata.webp"; if(fs::exists(webp)){rejected=false;try{InspectEditable(webp.wstring());}catch(...){rejected=true;}require(!rejected,"WebP supports metadata editing");}
}
