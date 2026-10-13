#include "ViewerRuntime.h"
#include "WebPMetadata.h"
#include "WebPXmpCompatibility.h"
#include "PngCrc.h"
#include <fstream>

void RunV0422Tests(const fs::path& root,std::ostream& report){
    auto require=[&](bool ok,const char* label){if(!ok)throw std::runtime_error(label);report<<"PASS: "<<label<<'\n';};
    auto read=[](const fs::path& path){std::ifstream f(path,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});};
    auto write=[](const fs::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f.write(bytes.data(),bytes.size());};
    auto folder=root/L"v0422";fs::create_directories(folder);
    std::vector<std::string> parts,packets;std::vector<bool> mask;InspectWebPChunks(read(root/L"webp-lossy.webp"),parts,mask,packets);
    parts.push_back(WebPChunk("KUPE","unknown-container-marker"));mask.push_back(false);
    stage3::Record record;record.memberStatus=L"partial";record.candidateStatus=L"observed";record.members={{L"同名",L"usr_11111111-1111-1111-1111-111111111111"},{L"同名",L"usr_22222222-2222-2222-2222-222222222222"}};stage3::Candidate candidate;candidate.person=record.members[1];candidate.raw=L"OCR evidence";candidate.method=L"normalized-exact";candidate.count=2;record.candidates={candidate};
    auto xml=stage3::RecordXml(record,true);auto at=xml.find("</rdf:RDF>");xml.insert(at,R"(<rdf:Description xmlns:xmp="http://ns.adobe.com/xap/1.0/" xmlns:vrc="http://ns.vrchat.com/vrc/1.0/" xmlns:foo="urn:test:unknown" xmlns:dc="http://purl.org/dc/elements/1.1/"><xmp:CreateDate>2026-10-13T22:05:23+09:00</xmp:CreateDate><xmp:Author>撮影者</xmp:Author><vrc:WorldDisplayName>ワールド</vrc:WorldDisplayName><vrc:WorldID>wrld_aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa</vrc:WorldID><foo:Property foo:attr="unknown-attribute">unknown-property-marker</foo:Property><dc:subject><rdf:Bag><rdf:li>tag</rdf:li></rdf:Bag></dc:subject></rdf:Description>)");
    auto clean=folder/L"clean.webp";write(clean,WriteWebPXmp(parts,mask,xml));auto expected=ReadPhotoSummary(clean.wstring());
    for(size_t count:{size_t(1),size_t(2),size_t(3)}){
        auto source=folder/(L"nul-"+std::to_wstring(count)+L".webp");auto raw=WriteWebPXmp(parts,mask,xml+std::string(count,'\0'));write(source,raw);auto fingerprint=MetadataFingerprint(source.wstring());
        PhotoSummary summary;auto metadata=InspectEditable(source.wstring(),&summary);auto direct=ReadPhotoSummary(source.wstring());
        bool equal=true;for(size_t i=3;i<11;++i)equal&=summary.values[i]==expected.values[i]&&direct.values[i]==expected.values[i];
        require(equal&&metadata.members.size()==2&&metadata.candidates.size()==1&&metadata.candidates[0].id==record.members[1].id&&summary.notes.find(L"末尾NUL")!=std::wstring::npos,"v0.4.22 single/multiple trailing NULs preserve summary and identities");
        MetadataEdit edit;edit.actions[1]=EditAction::Set;edit.values[1]=L"編集済み";auto output=folder/(L"edited-"+std::to_wstring(count)+L".webp");fs::remove(output);SaveMetadataCopy(source.wstring(),output.wstring(),fingerprint,edit);
        auto edited=ReadPhotoSummary(output.wstring());auto inspected=InspectEditable(output.wstring());auto before=ReadWebP(source.wstring(),128,96),after=ReadWebP(output.wstring(),128,96);
        require(edited.values[4]==L"編集済み"&&edited.values[9]==expected.values[9]&&edited.values[10]==expected.values[10]&&inspected.candidates[0].id==record.members[1].id&&edited.xmpDetail.find(L"unknown-property-marker")!=std::wstring::npos&&read(output).find("unknown-container-marker")!=std::string::npos&&before.pixels==after.pixels,"v0.4.22 edited copy preserves unknown XML, chunks, people and pixels");
        require(MetadataFingerprint(source.wstring())==fingerprint&&read(source)==raw,"v0.4.22 copy editing leaves original bytes unchanged");
    }
    auto source=folder/L"nul-1.webp";auto fingerprint=MetadataFingerprint(source.wstring());auto ledger=folder/L"backups.json";fs::remove(ledger);MetadataEdit edit;edit.actions[1]=EditAction::Set;edit.values[1]=L"上書き済み";
    SaveMetadataOverwrite(source.wstring(),fingerprint,edit,true,10,ledger);auto entries=ReadBackupLedger(ledger);
    require(entries.size()==1&&entries[0].sourceFingerprint==fingerprint&&ReadPhotoSummary(source.wstring()).values[4]==L"上書き済み","v0.4.22 NUL WebP overwrite creates verified backup before replacement");
    auto backup=ReadPhotoSummary(entries[0].path);require(backup.values[4]==expected.values[4]&&backup.values[9]==expected.values[9]&&backup.values[10]==expected.values[10]&&backup.values[8].find(L"【AutoBackup】")!=std::wstring::npos&&backup.xmpDetail.find(L"unknown-property-marker")!=std::wstring::npos,"v0.4.22 backup retains pre-edit metadata and AutoBackup tag");
    auto reject=[&](const std::string& packet,const wchar_t* name){auto path=folder/name;write(path,WriteWebPXmp(parts,mask,packet));bool refused=false;try{InspectEditable(path.wstring());}catch(...){refused=true;}require(refused,"v0.4.22 malformed or oversized WebP XML is rejected");};
    auto internal=xml;internal.insert(internal.find("撮影者"),1,'\0');reject(internal+std::string(1,'\0'),L"internal-nul.webp");
    reject("<!DOCTYPE x [<!ENTITY bad 'text'>]>"+xml+std::string(1,'\0'),L"dtd.webp");reject("<broken>"+std::string(2,'\0'),L"broken.webp");reject(xml+std::string(1,char(0xff))+std::string(1,'\0'),L"bad-utf8.webp");
    reject(xml+std::string(16*1024*1024,'\0'),L"oversized.webp");
    std::string utf16="\xff\xfe";for(char c:std::string("<x/>")){utf16+=c;utf16+='\0';}require(WebPXmpForParsing(utf16).size()==utf16.size(),"v0.4.22 UTF-16 bytes are not trimmed as UTF-8 terminators");
    auto png=read(root/L"writer-alpha.png");auto pngChunk=[&](const std::string& type,const std::string& data){std::string out;auto be=[&](uint32_t n){for(int i=24;i>=0;i-=8)out+=char(n>>i);};be(static_cast<uint32_t>(data.size()));out+=type+data;be(PngCrc(type+data));return out;};
    auto pngSource=folder/L"nul.png";png.insert(png.size()-12,pngChunk("tEXt",std::string("XML:com.adobe.xmp")+'\0'+xml+'\0'));write(pngSource,png);bool refused=false;try{InspectEditable(pngSource.wstring());}catch(...){refused=true;}require(refused&&ReadPhotoSummary(pngSource.wstring()).notes.find(L"XML読込みエラー")!=std::wstring::npos,"v0.4.22 WebP NUL compatibility is not applied to PNG");
    auto jpeg=read(root/L"sample.jpg");auto app1=std::string("http://ns.adobe.com/xap/1.0/")+char(0)+xml+char(0);auto length=app1.size()+2;std::string segment="\xff\xe1";segment+=char(length>>8);segment+=char(length);segment+=app1;jpeg.insert(2,segment);auto jpegSource=folder/L"nul.jpg";write(jpegSource,jpeg);
    require(ReadPhotoSummary(jpegSource.wstring()).notes.find(L"XML読込みエラー")!=std::wstring::npos,"v0.4.22 WebP NUL compatibility is not applied to JPEG");
    // Opt-in real samples are read-only; all edited outputs remain under self-test-data.
    wchar_t samplePath[32768]{};auto pathLength=GetEnvironmentVariableW(L"KUNI_WEBP_TEST_SAMPLES",samplePath,32768);
    if(pathLength){require(pathLength<32768,"v0.4.22 sample path fits buffer");auto outputs=folder/L"real";fs::create_directories(outputs);size_t count=0;
        for(const auto& entry:fs::directory_iterator(samplePath)){auto pngPath=entry.path();if(pngPath.extension()!=L".png")continue;auto webpPath=pngPath.parent_path()/(pngPath.stem().wstring()+L"_.webp");auto original=ReadPhotoSummary(pngPath.wstring());PhotoSummary converted;auto metadata=InspectEditable(webpPath.wstring(),&converted);bool same=true;for(size_t i=3;i<11;++i)same&=original.values[i]==converted.values[i];require(same&&converted.notes.find(L"XML読込みエラー")==std::wstring::npos,"v0.4.22 provided converted WebP summary matches original PNG");
            MetadataEdit change;change.actions[1]=EditAction::Set;change.values[1]=L"検証用コピー";auto output=outputs/webpPath.filename();fs::remove(output);SaveMetadataCopy(webpPath.wstring(),output.wstring(),metadata.fingerprint,change);auto reread=InspectEditable(output.wstring());auto display=ReadPhotoSummary(output.wstring());bool kept=true;for(size_t i=5;i<11;++i)kept&=display.values[i]==converted.values[i];auto a=ReadWebP(webpPath.wstring(),128,96),b=ReadWebP(output.wstring(),128,96);require(kept&&reread.values[1]==change.values[1]&&a.pixels==b.pixels&&MetadataFingerprint(webpPath.wstring())==metadata.fingerprint,"v0.4.22 provided WebP copy edit retains metadata, pixels and original fingerprint");++count;
        }require(count>0,"v0.4.22 opt-in samples were found");report<<"INFO: v0.4.22 real PNG/WebP pairs="<<count<<'\n';
    }
}
