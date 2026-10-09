#include "MetadataTests.h"
#include "MetadataModel.h"
#include <fstream>
#include <vector>
#include <stdexcept>
#include <iterator>

namespace {
void Require(bool value, const char* label, std::ostream& report) {
    if (!value) throw std::runtime_error(label); report << "PASS: " << label << '\n';
}
uint32_t TestCrc(const std::string& data) {
    uint32_t value = 0xffffffff;
    for (unsigned char byte : data) { value ^= byte; for (int i = 0; i < 8; ++i) value = (value >> 1) ^ ((value & 1) ? 0xedb88320 : 0); }
    return value ^ 0xffffffff;
}
std::string Be32(uint32_t number) {
    std::string value; for (int shift : { 24,16,8,0 }) value += static_cast<char>((number >> shift) & 255); return value;
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary); return std::string((std::istreambuf_iterator<char>(input)), {});
}
void Fixture(const std::filesystem::path& source, const std::filesystem::path& target, const std::string& xml, bool png, bool text = false, bool corrupt = false) {
    auto bytes = Bytes(source);
    std::string metadata;
    if (png) {
        std::string type = text ? "tEXt" : "iTXt", data = "XML:com.adobe.xmp";
        data += '\0'; if (!text) data.append(4, '\0'); data += xml;
        metadata = Be32(static_cast<uint32_t>(data.size())) + type + data + Be32(TestCrc(type + data));
        if (corrupt) metadata.back() ^= 1;
        bytes.insert(bytes.size() - 12, metadata);
    } else {
        const char key[] = "http://ns.adobe.com/xap/1.0/\0";
        std::string data(key, sizeof(key) - 1); data += xml;
        unsigned length = static_cast<unsigned>(data.size() + 2);
        metadata = std::string("\xff\xe1", 2) + static_cast<char>(length >> 8) + static_cast<char>(length & 255) + data;
        bytes.insert(2, metadata);
    }
    std::ofstream output(target, std::ios::binary); output.write(bytes.data(), bytes.size()); if (!output) throw std::runtime_error("metadata fixture write");
}
}
void RunMetadataTests(const std::filesystem::path& directory, std::ostream& report) {
    auto require = [&](bool ok, const char* label) { Require(ok, label, report); };
    require(ValidXmpDate(L"2024-02-29T23:59:59.123456+09:00") && !ValidXmpDate(L"2023-02-29T12:00:00Z") && !ValidXmpDate(L"2026-10-09T24:00:00Z") && !ValidXmpDate(L"2026-10-09T12:00:00+14:30"), "XMP calendar and timezone validation");
    std::wstring name = L"VRChat-2026-10-09-21-30-45.123456_1920x1080.png";
    require(FilenameDate(name) == L"2026-10-09 21:30:45.123456" && FilenameDate(L"VRChat-2026-02-30-21-30-45.1_1920x1080.png").empty() && FilenameDate(L"VRChat_2026-10-09-21-30-45.1_1920x1080.png").empty() && FilenameDate(L"VRChat-2026-10-09-21-30-45.1_0x1080.png").empty(), "strict VRChat filename date inference");
    require(ChooseDate(L"2025-01-02T03:04:05Z", name, L"created", L"modified").find(L"2025-01-02") == 0 && ChooseDate(L"bad", name, L"created", L"modified").find(L"推測") != std::wstring::npos && ChooseDate(L"", L"normal.png", L"created", L"modified").find(L"created") == 0 && ChooseDate(L"", L"normal.png", L"", L"modified").find(L"modified") == 0, "date source priority and invalid XMP fallback");
    const std::string xml = u8"<x:xmpmeta xmlns:x='adobe:ns:meta/'><r:RDF xmlns:r='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><r:Description xmlns:p='http://ns.adobe.com/xap/1.0/' xmlns:w='http://ns.vrchat.com/vrc/1.0/' p:CreateDate='2026-10-09T12:34:56+09:00' w:WorldID='wrld_test'><p:Author>撮影者 &amp; テスト</p:Author><w:WorldDisplayName>日本語ワールド</w:WorldDisplayName></r:Description></r:RDF></x:xmpmeta>";
    XmpFields fields; std::wstring error;
    require(ParseXmp(xml, fields, error) && fields.author == L"撮影者 & テスト" && fields.worldName == L"日本語ワールド" && fields.worldId == L"wrld_test", "XMP attributes, entities and alternate namespace prefixes");
    std::string arrayXml = "<root xmlns:xmp='http://ns.adobe.com/xap/1.0/' xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'><xmp:Author><rdf:Seq><rdf:li>A</rdf:li><rdf:li>B</rdf:li></rdf:Seq></xmp:Author></root>";
    require(ParseXmp(arrayXml, fields, error) && fields.author == L"A\r\nB", "XMP author array retained");
    require(ParseXmp("<root xmlns:xmp='urn:other'><xmp:Author>wrong</xmp:Author></root>", fields, error) && fields.author.empty(), "unrelated namespace not used as photographer");
    require(!ParseXmp("<root><broken></root>", fields, error) && !ParseXmp("<!DOCTYPE root [<!ENTITY value 'test'>]><root>&value;</root>", fields, error), "malformed XML and DTD rejected");
    for (bool png : { true, false }) {
        auto source = directory / (png ? L"sample-日本語.png" : L"sample.jpg");
        auto original = Bytes(source);
        auto target = directory / (png ? L"xmp-日本語.png" : L"xmp-日本語.jpg");
        Fixture(source, target, xml, png);
        auto summary = ReadPhotoSummary(target.wstring());
        require(summary.values[0] == target.filename().wstring() && summary.values[1].find(L" MB") != std::wstring::npos && summary.values[3].find(L"XMP") != std::wstring::npos && summary.values[4] == L"撮影者 & テスト" && summary.values[5] == L"日本語ワールド" && summary.values[6] == L"wrld_test" && summary.values[7].empty(), "PNG/JPEG simple summary from embedded XMP");
        require(Bytes(source) == original && summary.notes.empty() && !summary.xmpDetail.empty(), "original fixture unchanged and raw XMP retained");
    }
    Fixture(directory / L"sample-日本語.png", directory / L"xmp-text.png", xml, true, true);
    require(ReadPhotoSummary((directory / L"xmp-text.png").wstring()).values[4] == L"撮影者 & テスト", "PNG tEXt XMP extraction");
    Fixture(directory / L"sample-日本語.png", directory / L"xmp-crc.png", xml, true, false, true);
    require(!ReadPhotoSummary((directory / L"xmp-crc.png").wstring()).notes.empty(), "corrupt PNG XMP checksum reported");
    auto missing = ReadPhotoSummary((directory / L"sample.jpg").wstring());
    require(missing.values[4].empty() && missing.values[5].empty() && missing.values[6].empty() && missing.values[7].empty() && missing.values[3].find(L"ファイル作成日時") != std::wstring::npos, "missing metadata blank and filesystem date fallback");
}
