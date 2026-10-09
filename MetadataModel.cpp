#define NOMINMAX
#include "MetadataModel.h"
#include <xmllite.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <regex>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace {
constexpr size_t XmpLimit = 16 * 1024 * 1024;
const std::wstring XmpUri = L"http://ns.adobe.com/xap/1.0/";
const std::wstring VrcUri = L"http://ns.vrchat.com/vrc/1.0/";
std::wstring* Target(XmpFields& fields, const std::wstring& uri, const std::wstring& name) {
    if (uri == XmpUri && name == L"CreateDate") return &fields.date;
    if (uri == XmpUri && name == L"Author") return &fields.author;
    if (uri == VrcUri && name == L"WorldDisplayName") return &fields.worldName;
    if (uri == VrcUri && name == L"WorldID") return &fields.worldId;
    return nullptr;
}
std::wstring Trim(const std::wstring& value) {
    auto start = value.find_first_not_of(L" \t\r\n");
    return start == std::wstring::npos ? L"" : value.substr(start, value.find_last_not_of(L" \t\r\n") - start + 1);
}
std::wstring Utf8(const std::string& value) {
    if (value.size() > XmpLimit) throw std::runtime_error("XMP size limit");
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count && !value.empty()) throw std::runtime_error("XMP encoding");
    std::wstring text(count, L'\0'); MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), text.data(), count); return text;
}
bool Calendar(int year, int month, int day, int hour, int minute, int second) {
    if (year < 1 || year > 9999 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) return false;
    constexpr int days[]{ 31,28,31,30,31,30,31,31,30,31,30,31 };
    int maximum = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ++maximum;
    return day >= 1 && day <= maximum;
}
std::wstring FileDate(const FILETIME& value) {
    if (!value.dwHighDateTime && !value.dwLowDateTime) return L"";
    FILETIME local{}; SYSTEMTIME time{};
    if (!FileTimeToLocalFileTime(&value, &local) || !FileTimeToSystemTime(&local, &time)) return L"";
    wchar_t date[64]; swprintf_s(date, L"%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond); return date;
}
uint32_t BigEndian(const unsigned char* bytes) {
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | bytes[3];
}
uint32_t Crc(const std::string& value) {
    uint32_t crc = 0xffffffff;
    for (unsigned char byte : value) { crc ^= byte; for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0); }
    return crc ^ 0xffffffff;
}
void Read(std::ifstream& input, char* data, size_t length) {
    if (!input.read(data, static_cast<std::streamsize>(length))) throw std::runtime_error("truncated metadata");
}
std::vector<std::string> Packets(const std::wstring& path, std::wstring& notes) {
    std::ifstream input(fs::path(path), std::ios::binary);
    if (!input) throw std::runtime_error("metadata file read");
    unsigned char signature[8]{}; Read(input, reinterpret_cast<char*>(signature), 8);
    std::vector<std::string> packets;
    size_t total = 0;
    auto add = [&](std::string packet) { total += packet.size(); if (total > XmpLimit) throw std::runtime_error("XMP size limit"); packets.push_back(std::move(packet)); };
    const unsigned char png[]{ 137,80,78,71,13,10,26,10 };
    auto size = fs::file_size(path);
    if (std::equal(std::begin(png), std::end(png), signature)) {
        for (;;) {
            unsigned char header[8]; Read(input, reinterpret_cast<char*>(header), 8);
            size_t length = BigEndian(header); std::string type(reinterpret_cast<char*>(header + 4), 4);
            auto offset = static_cast<uint64_t>(input.tellg());
            if (offset > size || uint64_t(length) + 4 > size - offset) throw std::runtime_error("PNG chunk length");
            if (type == "IEND") break;
            if (type != "iTXt" && type != "tEXt" && type != "zTXt") { input.seekg(length + 4, std::ios::cur); continue; }
            if (length > XmpLimit) { notes += L"PNGテキストが読込み上限を超えています。\r\n"; input.seekg(length + 4, std::ios::cur); continue; }
            std::string data(length, '\0'); Read(input, data.data(), length);
            unsigned char checksum[4]; Read(input, reinterpret_cast<char*>(checksum), 4);
            auto separator = data.find('\0');
            if (separator == std::string::npos || data.substr(0, separator) != "XML:com.adobe.xmp") continue;
            if (Crc(type + data) != BigEndian(checksum)) throw std::runtime_error("PNG XMP checksum");
            auto position = separator + 1;
            if (type == "tEXt") add(data.substr(position));
            else if (type == "zTXt") notes += L"圧縮PNG XMP（zTXt）はこの版の直接抽出では未対応です。\r\n";
            else {
                if (position + 2 > data.size()) throw std::runtime_error("PNG iTXt fields");
                unsigned char compressed = data[position++], method = data[position++];
                if (compressed > 1 || method != 0) throw std::runtime_error("PNG iTXt compression");
                for (int i = 0; i < 2; ++i) { auto end = data.find('\0', position); if (end == std::string::npos) throw std::runtime_error("PNG iTXt fields"); position = end + 1; }
                if (compressed) notes += L"圧縮PNG XMP（iTXt）はこの版の直接抽出では未対応です。\r\n";
                else add(data.substr(position));
            }
        }
    } else if (std::string(reinterpret_cast<char*>(signature), 4) == "RIFF") {
        auto little = [](const unsigned char* b) { return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24); };
        uint64_t end = uint64_t(little(signature + 4)) + 8;
        char type[4]; Read(input, type, 4);
        if (std::string(type, 4) != "WEBP" || end != size || end < 12) throw std::runtime_error("WebP RIFF size");
        while (uint64_t(input.tellg()) < end) {
            if (end - uint64_t(input.tellg()) < 8) throw std::runtime_error("WebP chunk header");
            unsigned char header[8]; Read(input, reinterpret_cast<char*>(header), 8);
            uint64_t length = little(header + 4), padded = length + (length & 1);
            if (padded > end - uint64_t(input.tellg())) throw std::runtime_error("WebP chunk length");
            if (std::string(reinterpret_cast<char*>(header), 4) == "XMP ") {
                if (length > XmpLimit) throw std::runtime_error("WebP XMP limit");
                std::string packet(size_t(length), '\0'); Read(input, packet.data(), packet.size()); add(std::move(packet));
                if (length & 1) input.seekg(1, std::ios::cur);
            } else input.seekg(static_cast<std::streamoff>(padded), std::ios::cur);
        }
    } else if (signature[0] == 0xff && signature[1] == 0xd8) {
        input.seekg(2);
        for (;;) {
            unsigned char byte = 0; Read(input, reinterpret_cast<char*>(&byte), 1);
            if (byte != 0xff) throw std::runtime_error("JPEG marker");
            do { Read(input, reinterpret_cast<char*>(&byte), 1); } while (byte == 0xff);
            if (byte == 0xda || byte == 0xd9) break;
            if (byte == 0x01 || (byte >= 0xd0 && byte <= 0xd7)) continue;
            unsigned char lengthBytes[2]; Read(input, reinterpret_cast<char*>(lengthBytes), 2);
            size_t length = (size_t(lengthBytes[0]) << 8) | lengthBytes[1];
            if (length < 2) throw std::runtime_error("JPEG segment"); length -= 2;
            if (byte != 0xe1) { input.seekg(length, std::ios::cur); continue; }
            std::string data(length, '\0'); Read(input, data.data(), length);
            const char standard[] = "http://ns.adobe.com/xap/1.0/\0";
            if (data.compare(0, sizeof(standard) - 1, standard, sizeof(standard) - 1) == 0) add(data.substr(sizeof(standard) - 1));
            else if (data.find("http://ns.adobe.com/xmp/extension/") == 0) notes += L"JPEG Extended XMPはこの版では未対応です。\r\n";
        }
    }
    return packets;
}
}

bool ParseXmp(const std::string& xml, XmpFields& fields, std::wstring& error) {
    if (xml.empty() || xml.size() > XmpLimit) { error = L"XMPのサイズが不正です。"; return false; }
    ComPtr<IStream> stream; stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(xml.data()), static_cast<UINT>(xml.size())));
    ComPtr<IXmlReader> reader;
    HRESULT hr = CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.GetAddressOf()), nullptr);
    if (SUCCEEDED(hr)) hr = reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
    if (SUCCEEDED(hr)) hr = reader->SetProperty(XmlReaderProperty_MaxElementDepth, 64);
    if (SUCCEEDED(hr)) hr = reader->SetInput(stream.Get());
    XmpFields parsed;
    struct Element { std::wstring name, uri, text; };
    std::vector<Element> stack;
    XmlNodeType type{};
    auto finish = [&] {
        auto element = std::move(stack.back()); stack.pop_back();
        if (auto target = Target(parsed, element.uri, element.name)) {
            auto value = Trim(element.text); if (!value.empty()) { if (!target->empty() && *target != value) *target += L"\r\n" + value; else *target = value; }
        }
        if (!stack.empty()) { stack.back().text += element.text; if (element.name == L"li" && element.uri == L"http://www.w3.org/1999/02/22-rdf-syntax-ns#") stack.back().text += L"\r\n"; }
    };
    while (SUCCEEDED(hr) && (hr = reader->Read(&type)) == S_OK) {
        if (type == XmlNodeType_Element) {
            LPCWSTR name = nullptr, uri = nullptr; reader->GetLocalName(&name, nullptr); reader->GetNamespaceUri(&uri, nullptr);
            stack.push_back({ name ? name : L"", uri ? uri : L"", L"" });
            bool empty = reader->IsEmptyElement() != FALSE;
            if (reader->MoveToFirstAttribute() == S_OK) {
                do {
                    LPCWSTR attribute = nullptr, namespaceUri = nullptr, value = nullptr;
                    reader->GetLocalName(&attribute, nullptr); reader->GetNamespaceUri(&namespaceUri, nullptr); reader->GetValue(&value, nullptr);
                    if (auto target = Target(parsed, namespaceUri ? namespaceUri : L"", attribute ? attribute : L"")) *target = value ? value : L"";
                } while (reader->MoveToNextAttribute() == S_OK);
                reader->MoveToElement();
            }
            if (empty) finish();
        } else if (type == XmlNodeType_Text || type == XmlNodeType_CDATA || type == XmlNodeType_Whitespace) {
            LPCWSTR value = nullptr; reader->GetValue(&value, nullptr); if (!stack.empty() && value) stack.back().text += value;
        } else if (type == XmlNodeType_EndElement && !stack.empty()) finish();
    }
    if (FAILED(hr) || !stack.empty()) { error = L"XMPのXML読込みエラー。元の値は詳細で確認してください。"; return false; }
    fields = std::move(parsed); return true;
}
bool ValidXmpDate(const std::wstring& text) {
    static const std::wregex pattern(LR"(^([0-9]{4})-([0-9]{2})-([0-9]{2})(?:T([0-9]{2}):([0-9]{2}):([0-9]{2})(?:\.[0-9]{1,9})?(?:Z|([+-])([0-9]{2}):([0-9]{2}))?)?$)");
    std::wsmatch m; if (!std::regex_match(text, m, pattern)) return false;
    if (!Calendar(std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]), m[4].matched ? std::stoi(m[4]) : 0, m[5].matched ? std::stoi(m[5]) : 0, m[6].matched ? std::stoi(m[6]) : 0)) return false;
    if (m[7].matched) { int hour = std::stoi(m[8]), minute = std::stoi(m[9]); if (hour > 14 || minute > 59 || (hour == 14 && minute != 0)) return false; }
    return true;
}
std::wstring FilenameDate(const std::wstring& filename) {
    static const std::wregex pattern(LR"(^VRChat-([0-9]{4})-([0-9]{2})-([0-9]{2})-([0-9]{2})-([0-9]{2})-([0-9]{2})\.([0-9]{1,6})_([0-9]{1,10})x([0-9]{1,10})$)");
    std::wsmatch m; auto stem = fs::path(filename).stem().wstring();
    if (!std::regex_match(stem, m, pattern) || !Calendar(std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]), std::stoi(m[4]), std::stoi(m[5]), std::stoi(m[6])) || std::stoull(m[8]) == 0 || std::stoull(m[9]) == 0) return L"";
    return m[1].str() + L"-" + m[2].str() + L"-" + m[3].str() + L" " + m[4].str() + L":" + m[5].str() + L":" + m[6].str() + L"." + m[7].str();
}
std::wstring ChooseDate(const std::wstring& xmp, const std::wstring& filename, const std::wstring& created, const std::wstring& modified) {
    if (ValidXmpDate(xmp)) return xmp + L"（XMP）";
    auto inferred = FilenameDate(filename); if (!inferred.empty()) return inferred + L"（ファイル名から推測）";
    if (!created.empty()) return created + L"（ファイル作成日時・ローカル時刻）";
    if (!modified.empty()) return modified + L"（ファイル更新日時・ローカル時刻）";
    return L"";
}
PhotoSummary ReadPhotoSummary(const std::wstring& path) {
    PhotoSummary result; result.values[0] = fs::path(path).filename().wstring();
    std::error_code error; auto bytes = fs::file_size(path, error);
    if (!error) { std::wostringstream value; value << std::fixed << std::setprecision(2) << double(bytes) / 1000000 << L" MB"; result.values[1] = value.str(); }
    WIN32_FILE_ATTRIBUTE_DATA attributes{}; std::wstring created, modified;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) { created = FileDate(attributes.ftCreationTime); modified = FileDate(attributes.ftLastWriteTime); }
    XmpFields fields;
    try {
        for (auto& packet : Packets(path, result.notes)) {
            std::wstring parseError; XmpFields parsed;
            try { result.xmpDetail += L"\r\nXMP（直接抽出）\r\n" + Utf8(packet) + L"\r\n"; } catch (...) { result.notes += L"XMPの文字コード読込みエラー。\r\n"; }
            if (ParseXmp(packet, parsed, parseError)) {
                if (!parsed.date.empty()) fields.date = parsed.date;
                if (!parsed.author.empty()) fields.author = parsed.author;
                if (!parsed.worldName.empty()) fields.worldName = parsed.worldName;
                if (!parsed.worldId.empty()) fields.worldId = parsed.worldId;
            } else result.notes += parseError + L"\r\n";
        }
    } catch (...) { result.notes += L"XMP読込みエラー（破損・読込み不可・サイズ上限）。\r\n"; }
    if (!fields.date.empty() && !ValidXmpDate(fields.date)) result.notes += L"xmp:CreateDateが不正なため別の日時を使用しました。\r\n";
    result.values[3] = ChooseDate(fields.date, result.values[0], created, modified);
    result.values[4] = fields.author; result.values[5] = fields.worldName; result.values[6] = fields.worldId;
    return result;
}
