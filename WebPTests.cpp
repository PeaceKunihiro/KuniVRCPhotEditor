#define NOMINMAX
#include "WebPTests.h"
#include "WebPImage.h"
#include "WebPFixtures.h"
#include "MetadataModel.h"
#include "ThumbnailCache.h"
#include <fstream>
#include <stdexcept>
#include <chrono>

void RunWebPTests(const std::filesystem::path& directory, std::ostream& report) {
    auto require = [&](bool ok, const char* label) { if (!ok) throw std::runtime_error(label); report << "PASS: " << label << '\n'; };
    auto write = [&](const wchar_t* name, const unsigned char* data, size_t size) {
        auto path = directory / name; std::ofstream output(path, std::ios::binary); output.write(reinterpret_cast<const char*>(data), size); output.close(); return path;
    };
    auto lossless = write(L"webp-透過.WEBP", LosslessWebP, sizeof(LosslessWebP));
    auto lossy = write(L"webp-lossy.webp", LossyWebP, sizeof(LossyWebP));
    auto animation = write(L"webp-animation.webp", AnimatedWebP, sizeof(AnimatedWebP));
    for (auto path : { lossless, lossy }) {
        auto image = ReadWebP(path.wstring(), 128, 96);
        require(image.sourceWidth == 320 && image.sourceHeight == 160 && image.width == 128 && image.height == 64 && image.pixels.size() == 128 * 64 * 4, "WebP lossy/lossless aspect preserving decode");
        require(image.pixels[3] == 128 && image.pixels[2] <= 102 && image.pixels[1] <= 52, "WebP alpha premultiplication");
        auto summary = ReadPhotoSummary(path.wstring());
        require(summary.values[4] == L"WebP撮影者" && summary.values[5] == L"WebPワールド" && summary.values[6] == L"wrld_webp" && summary.values[3].find(L"XMP") != std::wstring::npos, "WebP RIFF XMP fields");
        auto expected = path == lossless ? sizeof(LosslessWebP) : sizeof(LossyWebP);
        require(std::filesystem::file_size(path) == expected, "WebP original size unchanged");
    }
    auto animated = ReadWebP(animation.wstring(), 128, 96);
    require(animated.animated && animated.sourceWidth == 320 && animated.pixels[2] == 100 && animated.pixels[3] == 128, "animated WebP first composited frame");
    require(ReadPhotoSummary(animation.wstring()).values[4].empty(), "WebP missing author blank");
    auto broken = write(L"webp-broken.webp", LosslessWebP, 20);
    bool rejected = false; try { ReadWebP(broken.wstring(), 128, 96); } catch (...) { rejected = true; }
    require(rejected && !ReadPhotoSummary(broken.wstring()).notes.empty(), "truncated WebP image and RIFF metadata rejected");
    auto bad = std::vector<unsigned char>(LosslessWebP, LosslessWebP + sizeof(LosslessWebP));
    bad[16] = bad[17] = bad[18] = bad[19] = 255;
    auto invalid = write(L"webp-length.webp", bad.data(), bad.size());
    require(!ReadPhotoSummary(invalid.wstring()).notes.empty(), "WebP chunk length overflow rejected");
    ThumbnailCache cache; Pixels master{256,192,std::vector<BYTE>(256 * 192 * 4, 123)}, pixels;
    auto key = ThumbnailCache::Key(lossless.wstring()); cache.Put(key, master);
    require(cache.Get(key, pixels) && pixels.data == master.data, "thumbnail cache reuses master pixels");
    auto time = std::filesystem::last_write_time(lossless);
    std::filesystem::last_write_time(lossless, time + std::chrono::seconds(1));
    require(ThumbnailCache::Key(lossless.wstring()) != key, "thumbnail cache key invalidated by file change");
    std::filesystem::last_write_time(lossless, time);
    for (int i=0; i<700; ++i) cache.Put(L"eviction-" + std::to_wstring(i), master);
    require(cache.Bytes() <= ThumbnailCache::Limit && !cache.Get(key, pixels) && cache.Get(L"eviction-699", pixels), "thumbnail LRU eviction and byte budget");
    auto start = std::chrono::steady_clock::now();
    for (int i=0; i<100; ++i) ReadWebP(lossless.wstring(), 256, 192);
    auto decoded = std::chrono::steady_clock::now();
    for (int i=0; i<100; ++i) cache.Get(L"eviction-699", pixels);
    auto cached = std::chrono::steady_clock::now();
    report << "INFO: synthetic WebP 100 decode+read us=" << std::chrono::duration_cast<std::chrono::microseconds>(decoded-start).count()
        << " cache-copy us=" << std::chrono::duration_cast<std::chrono::microseconds>(cached-decoded).count() << " (not real folder speedup)\n";
    auto ticket = cache.Ticket(); cache.Clear(); cache.Put(L"stale", master, ticket);
    require(cache.Bytes() == 0 && !cache.Get(L"stale", pixels), "explicit reload rejects old in-flight cache writes");
}
