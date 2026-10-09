#define NOMINMAX
#include "WebPImage.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <memory>
#include <stdexcept>

WebPImage ReadWebP(const std::wstring& path, UINT maxWidth, UINT maxHeight) {
    const auto size = std::filesystem::file_size(path);
    if (!size || size > 256ULL * 1024 * 1024) throw std::runtime_error("WebP input limit");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("WebP read");
    WebPData data{ bytes.data(), bytes.size() };
    std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)> demux(WebPDemux(&data), WebPDemuxDelete);
    if (!demux) throw std::runtime_error("WebP container");
    WebPImage result;
    result.sourceWidth = WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_WIDTH);
    result.sourceHeight = WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_HEIGHT);
    auto flags = WebPDemuxGetI(demux.get(), WEBP_FF_FORMAT_FLAGS);
    result.animated = (flags & ANIMATION_FLAG) != 0;
    result.exif = (flags & EXIF_FLAG) != 0; result.icc = (flags & ICCP_FLAG) != 0;
    const uint64_t pixelLimit = (result.animated ? 16ULL : 64ULL) * 1024 * 1024;
    if (!result.sourceWidth || !result.sourceHeight || uint64_t(result.sourceWidth) * result.sourceHeight > pixelLimit)
        throw std::runtime_error("WebP pixel limit");
    if (!maxWidth || !maxHeight) return result; // Container information only; no second image decode.
    double scale = std::min({ 1.0, double(maxWidth) / result.sourceWidth, double(maxHeight) / result.sourceHeight });
    result.width = std::max(1U, UINT(result.sourceWidth * scale)); result.height = std::max(1U, UINT(result.sourceHeight * scale));
    result.pixels.resize(size_t(result.width) * result.height * 4);
    if (result.animated) {
        // AnimDecoder returns the composited canvas, rather than an ANMF fragment.
        WebPAnimDecoderOptions options; if (!WebPAnimDecoderOptionsInit(&options)) throw std::runtime_error("WebP ABI");
        options.color_mode = MODE_BGRA;
        std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(WebPAnimDecoderNew(&data, &options), WebPAnimDecoderDelete);
        uint8_t* frame = nullptr; int timestamp = 0;
        if (!decoder || !WebPAnimDecoderGetNext(decoder.get(), &frame, &timestamp)) throw std::runtime_error("WebP animation");
        for (UINT y = 0; y < result.height; ++y) for (UINT x = 0; x < result.width; ++x) {
            auto source = frame + (size_t(uint64_t(y) * result.sourceHeight / result.height) * result.sourceWidth + uint64_t(x) * result.sourceWidth / result.width) * 4;
            std::copy_n(source, 4, result.pixels.data() + (size_t(y) * result.width + x) * 4);
        }
    } else {
        WebPDecoderConfig config; if (!WebPInitDecoderConfig(&config)) throw std::runtime_error("WebP ABI");
        config.options.use_scaling = 1; config.options.scaled_width = result.width; config.options.scaled_height = result.height;
        config.output.colorspace = MODE_BGRA; config.output.is_external_memory = 1;
        config.output.u.RGBA.rgba = result.pixels.data(); config.output.u.RGBA.stride = result.width * 4;
        config.output.u.RGBA.size = result.pixels.size();
        auto status = WebPDecode(bytes.data(), bytes.size(), &config); WebPFreeDecBuffer(&config.output);
        if (status != VP8_STATUS_OK) throw std::runtime_error("WebP decode");
    }
    for (size_t i = 0; i < result.pixels.size(); i += 4) for (int c = 0; c < 3; ++c)
        result.pixels[i + c] = BYTE((unsigned(result.pixels[i + c]) * result.pixels[i + 3] + 127) / 255);
    return result;
}
