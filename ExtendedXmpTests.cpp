#include "ExtendedXmpTests.h"
#include "ExtendedXmp.h"
#include "MetadataModel.h"
#include <fstream>
#include <stdexcept>
#include <iterator>

void RunExtendedXmpTests(const std::filesystem::path& directory, std::ostream& report) {
    auto require = [&](bool ok, const char* label) { if (!ok) throw std::runtime_error(label); report << "PASS: " << label << '\n'; };
    auto bytes = [](const std::filesystem::path& path) { std::ifstream input(path, std::ios::binary); return std::string((std::istreambuf_iterator<char>(input)), {}); };
    auto be = [](uint32_t n) { std::string data; for (int shift : {24,16,8,0}) data += char((n >> shift) & 255); return data; };
    auto app1 = [](const std::string& data) { unsigned n = static_cast<unsigned>(data.size() + 2); return std::string("\xff\xe1",2) + char(n >> 8) + char(n & 255) + data; };
    const std::string extension = u8"<root xmlns:z='http://ns.adobe.com/xap/1.0/' xmlns:w='http://ns.vrchat.com/vrc/1.0/' z:Author='拡張撮影者' z:CreateDate='2026-10-10T01:02:03Z' w:WorldDisplayName='拡張ワールド' w:WorldID='wrld_extended'><padding>" + std::string(70000, 'x') + "</padding></root>";
    const auto guid = XmpMd5(extension);
    require(XmpMd5("abc") == "900150983CD24FB0D6963F7D28E17F72", "MD5 format conformance");
    const std::string standard = "<root xmlns:n='http://ns.adobe.com/xmp/note/' n:HasExtendedXMP='" + guid + "'/>";
    const char sig[] = "http://ns.adobe.com/xmp/extension/", stdSig[] = "http://ns.adobe.com/xap/1.0/";
    auto part = [&](uint32_t offset, uint32_t length) {
        return std::string(sig, sizeof(sig)) + guid + be(static_cast<uint32_t>(extension.size())) + be(offset) + extension.substr(offset,length);
    };
    auto first = part(0, 40000), second = part(40000, static_cast<uint32_t>(extension.size()-40000));
    auto source = directory / L"sample.jpg"; auto original = bytes(source);
    auto fixture = [&](const wchar_t* name, const std::string& xml, const std::vector<std::string>& parts) {
        auto content = original; std::string inserts = app1(std::string(stdSig,sizeof(stdSig)) + xml);
        for (auto& data : parts) inserts += app1(data); content.insert(2,inserts);
        auto path = directory / name; std::ofstream output(path,std::ios::binary); output.write(content.data(),content.size()); output.close(); return path;
    };
    auto path = fixture(L"extended-日本語.jpg",standard,{second,first}); auto before = bytes(path);
    auto summary = ReadPhotoSummary(path.wstring());
    require(summary.values[4] == L"拡張撮影者" && summary.values[5] == L"拡張ワールド" && summary.values[6] == L"wrld_extended" && summary.values[3].find(L"XMP") != std::wstring::npos && summary.notes.find(L"検証成功") != std::wstring::npos, "Extended XMP >64KB reverse chunks and namespace prefixes");
    require(bytes(source) == original && bytes(path) == before && summary.xmpDetail.find(L"拡張撮影者") != std::wstring::npos, "Extended XMP raw detail and original files unchanged");
    auto standardAuthor = "<root xmlns:n='http://ns.adobe.com/xmp/note/' xmlns:x='http://ns.adobe.com/xap/1.0/' n:HasExtendedXMP='" + guid + "' x:Author='standard'/>";
    auto conflict = fixture(L"extended-conflict.jpg", standardAuthor, {first,second}); summary = ReadPhotoSummary(conflict.wstring());
    require(summary.values[4] == L"standard" && summary.notes.find(L"異なる値") != std::wstring::npos && summary.values[5] == L"拡張ワールド", "standard XMP wins conflicts while extension fills missing fields");
    auto invalid = [&](const wchar_t* name, std::vector<std::string> parts, const char* label) {
        auto target = fixture(name,standardAuthor,parts); auto result = ReadPhotoSummary(target.wstring());
        require(result.values[4] == L"standard" && result.values[5].empty() && result.notes.find(L"Extended XMP読込みエラー") != std::wstring::npos,label);
    };
    invalid(L"extended-missing.jpg",{first},"missing extension chunk retains standard XMP");
    invalid(L"extended-duplicate.jpg",{first,first,second},"duplicate extension rejected");
    auto corrupted = second; corrupted.back() ^= 1;
    invalid(L"extended-md5.jpg",{first,corrupted},"Extended XMP MD5 mismatch rejected");
    auto mismatch = second; mismatch[sizeof(sig)] = mismatch[sizeof(sig)] == 'A' ? 'B' : 'A';
    invalid(L"extended-guid.jpg",{first,mismatch},"different GUID chunks not mixed");
    auto overrun = second; overrun.replace(sizeof(sig)+36,4,be(0xffffffff));
    invalid(L"extended-offset.jpg",{first,overrun},"extension offset overflow rejected");
    auto length = second; length.replace(sizeof(sig)+32,4,be(0x01000001));
    invalid(L"extended-limit.jpg",{first,length},"extension total byte limit enforced");
    auto shortHeader = std::string(sig,sizeof(sig)) + guid;
    invalid(L"extended-header.jpg",{shortHeader},"truncated Extended XMP header rejected");
    auto unrelated = fixture(L"extended-unreferenced.jpg","<root/>",{first,second});
    require(ReadPhotoSummary(unrelated.wstring()).values[4].empty(), "unreferenced extension ignored");
    std::wstring notes;
    auto overlap = part(39999,static_cast<uint32_t>(extension.size()-39999));
    require(RestoreExtendedXmp({standard},{first,overlap},notes).empty(),"overlapping extension chunks rejected");
    notes.clear(); auto disagree = second; disagree.replace(sizeof(sig)+32,4,be(static_cast<uint32_t>(extension.size()+1)));
    require(RestoreExtendedXmp({standard},{first,disagree},notes).empty(),"inconsistent extension lengths rejected");
}
