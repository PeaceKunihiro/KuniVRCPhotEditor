#define NOMINMAX
#include "Stage3.h"
#include "MetadataModel.h"
#include "MetadataWriter.h"
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <fstream>
#include <chrono>
namespace stage3 {
// Offline feasibility probe only. OS OCR models are not a portable bundled engine.
void EvaluateReferences(const std::filesystem::path& logPath,const std::filesystem::path& photos,const std::filesystem::path& samples,std::ostream& report){
    LogMonitor log;log.Read(logPath);auto snapshot=log.timeline.At(Now());report<<"Log parser: members="<<snapshot.members.size()<<" status="<<winrt::to_string(snapshot.status)<<" invalid="<<log.invalid<<'\n';
    unsigned photoCount=0,matching=0;for(auto& month:std::filesystem::directory_iterator(photos)){auto name=month.path().filename().wstring();if(!month.is_directory()||name.size()!=7||name[4]!=L'-')continue;for(auto& photo:std::filesystem::directory_iterator(month.path()))if(photo.is_regular_file()&&photo.path().extension()==L".png"){++photoCount;if(!log.timeline.PhotoTimes(photo.path().wstring()).empty())++matching;}}
    report<<"Monthly PNG photos="<<photoCount<<" matching capture-log paths="<<matching<<'\n';
    for(auto& month:std::filesystem::directory_iterator(photos)){auto name=month.path().filename().wstring();if(!month.is_directory()||name.size()!=7||name[4]!=L'-')continue;for(auto& photo:std::filesystem::directory_iterator(month.path())){auto times=log.timeline.PhotoTimes(photo.path().wstring());if(times.empty())continue;auto metadata=InspectEditable(photo.path().wstring());auto decision=Correlate(photo.path().wstring(),metadata.values[0],metadata.values[3],Now(),log);report<<"Capture correlation: file="<<winrt::to_string(photo.path().filename().wstring())<<" ready="<<decision.ready<<" reason="<<winrt::to_string(decision.reason)<<" members="<<decision.record.members.size()<<" reference="<<winrt::to_string(Iso(decision.record.reference))<<" source="<<winrt::to_string(decision.record.timeSource)<<" status="<<winrt::to_string(decision.record.memberStatus)<<'\n';}}
    using namespace winrt::Windows;auto engine=Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(L"ja"));if(!engine)throw std::runtime_error("Japanese Windows OCR is unavailable; OS setup is not modified");
    for(auto& sample:std::filesystem::directory_iterator(samples)){if(sample.path().extension()!=L".png")continue;
        auto started=std::chrono::steady_clock::now();auto file=Storage::StorageFile::GetFileFromPathAsync(sample.path().wstring()).get();auto stream=file.OpenAsync(Storage::FileAccessMode::Read).get();auto decoder=Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();
        auto bitmap=decoder.GetSoftwareBitmapAsync(Graphics::Imaging::BitmapPixelFormat::Bgra8,Graphics::Imaging::BitmapAlphaMode::Ignore).get();auto result=engine.RecognizeAsync(bitmap).get();
        auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();report<<"Sample="<<winrt::to_string(sample.path().filename().wstring())<<" elapsed_ms="<<elapsed<<" lines="<<result.Lines().Size()<<'\n';
        for(auto line:result.Lines()){report<<"  "<<winrt::to_string(line.Text());for(auto word:line.Words()){auto bounds=word.BoundingRect();report<<" ["<<bounds.X<<','<<bounds.Y<<','<<bounds.Width<<','<<bounds.Height<<']';}report<<'\n';}
        bitmap.Close();
        // Restrict to the nameplate area, enlarge text, and evaluate Japanese and English independently.
        for(int tile=0;tile<3;++tile){Graphics::Imaging::BitmapTransform transform;transform.ScaledWidth(decoder.PixelWidth()*2);transform.ScaledHeight(decoder.PixelHeight()*2);Graphics::Imaging::BitmapBounds bounds{};bounds.X=tile*(decoder.PixelWidth()/3)*2;bounds.Y=0;bounds.Width=(decoder.PixelWidth()/3)*2;bounds.Height=std::min(decoder.PixelHeight(),uint32_t(400))*2;transform.Bounds(bounds);
            auto pixels=decoder.GetPixelDataAsync(Graphics::Imaging::BitmapPixelFormat::Bgra8,Graphics::Imaging::BitmapAlphaMode::Ignore,transform,Graphics::Imaging::ExifOrientationMode::IgnoreExifOrientation,Graphics::Imaging::ColorManagementMode::DoNotColorManage).get().DetachPixelData();
            for(int mode=0;mode<2;++mode){std::vector<uint8_t> working(pixels.begin(),pixels.end());if(mode)for(size_t i=0;i<working.size();i+=4){bool yellow=working[i+2]>100&&working[i+1]>65&&working[i+2]>working[i]*1.25;working[i]=working[i+1]=working[i+2]=yellow?0:255;working[i+3]=255;}
                Storage::Streams::DataWriter writer;writer.WriteBytes(working);auto cropped=Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(),Graphics::Imaging::BitmapPixelFormat::Bgra8,bounds.Width,bounds.Height,Graphics::Imaging::BitmapAlphaMode::Ignore);
                for(auto language:{L"ja",L"en"}){auto recognizer=Media::Ocr::OcrEngine::TryCreateFromLanguage(Globalization::Language(language));if(!recognizer)continue;auto ocr=recognizer.RecognizeAsync(cropped).get();report<<"  ROI tile="<<tile<<" scale=2 mode="<<(mode?"yellow-mask":"color")<<" language="<<winrt::to_string(language)<<" lines="<<ocr.Lines().Size()<<'\n';for(auto line:ocr.Lines())report<<"    "<<winrt::to_string(line.Text())<<'\n';}
                cropped.Close();
            }
        }
        stream.Close();
    }
    report<<"OCR confidence is unavailable from this API; scores are not fabricated. No live capture or metadata writes.\n";
}
}
