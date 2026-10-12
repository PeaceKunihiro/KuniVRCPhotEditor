#define NOMINMAX
#include "ExtendedXmp.h"
#include "MetadataModel.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <stdexcept>

std::string XmpMd5(const std::string& bytes) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    unsigned char digest[16]{};
    auto status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_MD5_ALGORITHM, nullptr, 0);
    if (status >= 0) status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest, sizeof(digest), 0);
    if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("XMP MD5");
    std::string result; const char hex[] = "0123456789ABCDEF";
    for (auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
std::string RestoreExtendedXmp(const std::vector<std::string>& standard,
    const std::vector<std::string>& segments, std::wstring& notes) {
    try {
        std::string reference; size_t standardBytes = 0;
        for (const auto& xml : standard) {
            standardBytes += xml.size(); XmpFields fields; std::wstring error;
            if (!ParseXmp(xml, fields, error) || fields.extendedGuid.empty()) continue;
            auto guid = fields.extendedGuid;
            if (guid.size() != 32 || guid.find_first_not_of(L"0123456789ABCDEF") != std::wstring::npos) throw std::runtime_error("reference GUID");
            std::string ascii; for (auto c : guid) ascii += static_cast<char>(c);
            if (!reference.empty() && reference != ascii) throw std::runtime_error("conflicting references");
            reference = ascii;
        }
        if (reference.empty()) {
            if (!segments.empty()) notes += L"JPEG Extended XMP: 標準XMPから参照されていない拡張を無視しました。\r\n";
            return {};
        }
        const char signature[] = "http://ns.adobe.com/xmp/extension/";
        constexpr size_t header = sizeof(signature) + 32 + 4 + 4;
        struct Part { uint32_t offset; std::string bytes; };
        std::vector<Part> parts; uint32_t total = 0;
        auto read32 = [](const char* data) {
            const auto* b = reinterpret_cast<const unsigned char*>(data);
            return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
        };
        for (const auto& segment : segments) {
            if (segment.size() < sizeof(signature) + 32) throw std::runtime_error("truncated header");
            if (segment.compare(0, sizeof(signature), signature, sizeof(signature)) != 0) continue;
            if (segment.compare(sizeof(signature), 32, reference) != 0) continue;
            if (segment.size() <= header) throw std::runtime_error("empty chunk");
            auto length = read32(segment.data() + sizeof(signature) + 32);
            auto offset = read32(segment.data() + sizeof(signature) + 36);
            if (!length || uint64_t(length) + standardBytes > 16ULL * 1024 * 1024) throw std::runtime_error("XMP limit");
            if (total && total != length) throw std::runtime_error("inconsistent length"); total = length;
            if (offset >= total || segment.size() - header > total - offset) throw std::runtime_error("chunk bounds");
            parts.push_back({ offset, segment.substr(header) });
        }
        if (parts.empty()) throw std::runtime_error("missing extension");
        std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) { return a.offset < b.offset; });
        std::string restored; restored.reserve(total);
        for (const auto& part : parts) {
            if (part.offset != restored.size()) throw std::runtime_error("missing or overlapping chunks");
            restored += part.bytes;
        }
        if (restored.size() != total || XmpMd5(restored) != reference) throw std::runtime_error("length or MD5 mismatch");
        XmpFields fields; std::wstring error;
        if (!ParseXmp(restored, fields, error)) throw std::runtime_error("extension XML");
        notes += L"JPEG Extended XMP: 分割復元・GUID/MD5検証成功。\r\n";
        return restored;
    } catch (...) {
        notes += L"JPEG Extended XMP読込みエラー（参照・分割・長さ・MD5・XML）。標準XMPは保持します。\r\n";
        return {};
    }
}
