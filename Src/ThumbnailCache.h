#pragma once
#include <windows.h>
#include <filesystem>
#include <list>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
struct Pixels { UINT width = 0, height = 0; std::vector<BYTE> data; };
// One cache is shared by the thumbnail workers. Files are opened read-only.
class ThumbnailCache {
    struct Entry { std::wstring key; Pixels pixels; };
    std::list<Entry> entries;
    std::mutex mutex;
    size_t bytes = 0;
    uint64_t epoch = 0;
    size_t limit = Limit;
public:
    static constexpr size_t Limit = 128 * 1024 * 1024;
    std::atomic<unsigned> hits{0}, misses{0};
    static std::wstring Key(const std::wstring& path) {
        namespace fs = std::filesystem;
        auto key = fs::absolute(path).lexically_normal().wstring();
        return key + L"|" + std::to_wstring(fs::file_size(path)) + L"|" + std::to_wstring(fs::last_write_time(path).time_since_epoch().count());
    }
    bool Get(const std::wstring& key, Pixels& pixels) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto i = entries.begin(); i != entries.end(); ++i) if (i->key == key) {
            pixels = i->pixels; entries.splice(entries.begin(), entries, i); ++hits; return true;
        }
        ++misses; return false;
    }
    uint64_t Ticket() { std::lock_guard<std::mutex> lock(mutex); return epoch; }
    void Put(const std::wstring& key, const Pixels& pixels, uint64_t ticket = UINT64_MAX) {
        std::lock_guard<std::mutex> lock(mutex);
        if (ticket != UINT64_MAX && ticket != epoch) return;
        for (auto i = entries.begin(); i != entries.end(); ++i) if (i->key == key) return;
        if (pixels.data.size() > limit) return;
        while (!entries.empty() && bytes + pixels.data.size() > limit) { bytes -= entries.back().pixels.data.size(); entries.pop_back(); }
        entries.push_front({ key, pixels }); bytes += pixels.data.size();
    }
    void Clear() { std::lock_guard<std::mutex> lock(mutex); ++epoch; entries.clear(); bytes = 0; }
    size_t Bytes() { std::lock_guard<std::mutex> lock(mutex); return bytes; }
    void SetLimit(size_t value) {
        std::lock_guard<std::mutex> lock(mutex); limit = value; ++epoch;
        while (!entries.empty() && bytes > limit) { bytes -= entries.back().pixels.data.size(); entries.pop_back(); }
    }
};
