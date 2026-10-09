#include "MetadataEditor.h"
#include <regex>
bool MetadataEdit::Changed() const {
    for (auto action : actions) if (action != EditAction::Keep) return true;
    return false;
}
std::wstring MetadataEdit::Validate() const {
    for (size_t i=0; i<values.size(); ++i) if (actions[i] == EditAction::Set) {
        if (values[i].empty()) return L"空欄で保存せず、値を削除する操作を指定してください。";
        if (values[i].size()>65536) return L"入力が長すぎます（各項目65536文字まで）。";
        for (size_t j=0;j<values[i].size();++j) {
            auto c=values[i][j];
            if ((c<32 && c!=L'\r' && c!=L'\n' && c!=L'\t') || c==0xfffe || c==0xffff) return L"XMLに保存できない文字があります。";
            if(c>=0xd800&&c<=0xdbff){if(j+1==values[i].size()||values[i][j+1]<0xdc00||values[i][j+1]>0xdfff)return L"Unicode文字が不正です。";++j;}
            else if(c>=0xdc00&&c<=0xdfff)return L"Unicode文字が不正です。";
        }
    }
    if (actions[0] == EditAction::Set && !ValidXmpDate(values[0])) return L"撮影日時は有効なXMP日時を入力してください（例: 2026-10-10T12:30:00+09:00）。";
    if (actions[3] == EditAction::Set && !std::regex_match(values[3], std::wregex(L"wrld_[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"))) return L"ワールドIDはwrld_に続くUUID形式で入力してください。";
    return {};
}
