#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include <webp/decode.h>
#include <webp/demux.h>

// Keep encoded chunks verbatim; only the container header, VP8X and XMP change.
inline uint32_t WebPLe(const char* p) { auto b=reinterpret_cast<const unsigned char*>(p);return b[0]|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24; }
inline void WebPPut(std::string& s,size_t p,uint32_t v) { for(int i=0;i<4;++i)s[p+i]=char(v>>(i*8)); }
inline std::string WebPChunk(const std::string& type,const std::string& data) {
    std::string s=type+std::string(4,'\0')+data;WebPPut(s,4,static_cast<uint32_t>(data.size()));if(data.size()%2)s+='\0';return s;
}
inline void InspectWebPChunks(const std::string& bytes,std::vector<std::string>& parts,std::vector<bool>& xmp,std::vector<std::string>& packets) {
    if(bytes.size()<20||bytes.substr(0,4)!="RIFF"||bytes.substr(8,4)!="WEBP"||uint64_t(WebPLe(bytes.data()+4))+8!=bytes.size())throw std::runtime_error("Invalid WebP RIFF size");
    WebPData data{reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()};
    auto demux=WebPDemux(&data);if(!demux)throw std::runtime_error("Invalid WebP image structure");WebPDemuxDelete(demux);
    parts.push_back(bytes.substr(0,12));xmp.push_back(false);int extended=0;
    for(size_t p=12;p<bytes.size();) {
        if(bytes.size()-p<8)throw std::runtime_error("Truncated WebP chunk");
        size_t n=WebPLe(bytes.data()+p+4),padded=n+(n%2);
        if(padded>bytes.size()-p-8)throw std::runtime_error("Invalid WebP chunk length");
        if(n%2&&bytes[p+8+n]!=0)throw std::runtime_error("Invalid WebP padding");
        auto type=bytes.substr(p,4);if(type=="VP8X"&&(++extended!=1||p!=12||n!=10))throw std::runtime_error("Invalid WebP VP8X");
        bool meta=type=="XMP ";if(meta)packets.push_back(bytes.substr(p+8,n));
        parts.push_back(bytes.substr(p,8+padded));xmp.push_back(meta);p+=8+padded;
    }
}
inline std::string WriteWebPXmp(const std::vector<std::string>& parts,const std::vector<bool>& xmp,const std::string& packet) {
    auto header=parts[0];std::string output=header;bool extended=parts.size()>1&&parts[1].substr(0,4)=="VP8X";
    if(!extended) {
        std::string original;for(auto& part:parts)original+=part;
        WebPBitstreamFeatures features{};if(WebPGetFeatures(reinterpret_cast<const uint8_t*>(original.data()),original.size(),&features)!=VP8_STATUS_OK)throw std::runtime_error("WebP features unreadable");
        std::string data(10,'\0');data[0]=char(0x04|(features.has_alpha?0x10:0));
        for(int j=0;j<3;++j){data[4+j]=char((features.width-1)>>(j*8));data[7+j]=char((features.height-1)>>(j*8));}
        output+=WebPChunk("VP8X",data);
    }
    for(size_t i=1;i<parts.size();++i)if(!xmp[i]) {
        auto part=parts[i];if(part.substr(0,4)=="VP8X")part[8]=char(static_cast<unsigned char>(part[8])|0x04);
        output+=part;
    }
    output+=WebPChunk("XMP ",packet);WebPPut(output,4,static_cast<uint32_t>(output.size()-8));return output;
}
