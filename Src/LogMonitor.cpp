#define NOMINMAX
#include "Stage3.h"
#include <winrt/base.h>
#include <stdexcept>
namespace stage3 {
void LogMonitor::Read(const std::filesystem::path& path){
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)winrt::throw_last_error();
    struct Close{HANDLE h;~Close(){CloseHandle(h);}}close{h};BY_HANDLE_FILE_INFORMATION info{};winrt::check_bool(GetFileInformationByHandle(h,&info));
    uint64_t length=(uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    auto id=path.filename().wstring()+L":"+std::to_wstring(info.dwVolumeSerialNumber)+L":"+std::to_wstring(info.nFileIndexHigh)+L":"+std::to_wstring(info.nFileIndexLow)+L":"+std::to_wstring(info.ftCreationTime.dwHighDateTime)+L":"+std::to_wstring(info.ftCreationTime.dwLowDateTime);
    if(id!=identity||length<offset){timeline.Clear();if(id==identity){timeline.Invalidate();invalid=true;}else invalid=false;identity=id;++resets;offset=0;unfinished.clear();lastTime=0;}
    LARGE_INTEGER position{};position.QuadPart=offset;winrt::check_bool(SetFilePointerEx(h,position,nullptr,FILE_BEGIN));
    char buffer[65536];while(offset<length){DWORD done{};winrt::check_bool(ReadFile(h,buffer,static_cast<DWORD>(std::min<uint64_t>(sizeof(buffer),length-offset)),&done,nullptr));if(!done)break;offset+=done;unfinished.append(buffer,done);
        size_t start=0,end;while((end=unfinished.find('\n',start))!=std::string::npos){auto text=unfinished.substr(start,end-start);if(!text.empty()&&text.back()=='\r')text.pop_back();if(text.compare(0,3,"\xef\xbb\xbf")==0)text.erase(0,3);
            try{Event event;if(ParseLogLine(winrt::to_hstring(text).c_str(),event)){if(event.kind==Kind::Invalid)invalid=true;if(event.time<lastTime){timeline.Invalidate();invalid=true;}lastTime=std::max(lastTime,event.time);timeline.Add(std::move(event));}}catch(...){timeline.Invalidate();invalid=true;}start=end+1;}
        unfinished.erase(0,start);if(unfinished.size()>1024*1024)throw std::runtime_error("Incomplete log line exceeds limit");
    }
}
}
