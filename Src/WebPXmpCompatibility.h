#pragma once
#include <windows.h>
#include <string_view>

// Only a parsing view changes; encoded chunks and original fingerprints stay intact.
inline std::string_view WebPXmpForParsing(std::string_view packet) {
    if(packet.empty()||packet.size()>16*1024*1024||packet.back()!='\0')return packet;
    auto last=packet.find_last_not_of('\0');
    if(last==std::string_view::npos)return packet;
    auto xml=packet.substr(0,last+1);
    // UTF-16/32 and embedded NULs must not be trimmed as UTF-8 terminators.
    if(xml.find('\0')!=std::string_view::npos)return packet;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,xml.data(),static_cast<int>(xml.size()),nullptr,0))return packet;
    return xml;
}
