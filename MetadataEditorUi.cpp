#define NOMINMAX
#include "MetadataEditorUi.h"
#include <algorithm>
namespace { std::wstring Text(HWND h){std::wstring s(GetWindowTextLengthW(h)+1,L'\0');GetWindowTextW(h,s.data(),static_cast<int>(s.size()));s.resize(wcslen(s.c_str()));return s;} }
LRESULT CALLBACK MetadataEditorUi::Proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto self=reinterpret_cast<MetadataEditorUi*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<MetadataEditorUi*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->panel=window;}
    if(self){
        if(message==WM_SIZE){self->Layout();return 0;}
        if(message==WM_COMMAND){
            if(self->loading)return 0;
            if(HIWORD(wp)==EN_CHANGE)for(size_t i=0;i<6;++i)if(reinterpret_cast<HWND>(lp)==self->inputs[i])SendMessageW(self->choices[i],CB_SETCURSEL,1,0);
            SendMessageW(GetParent(window),WM_COMMAND,MAKEWPARAM(1200,EN_CHANGE),0);return 0;
        }
        if(message==WM_MOUSEWHEEL||message==WM_VSCROLL){int position=self->scroll;
            if(message==WM_MOUSEWHEEL)position-=GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*48;
            else {SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(window,SB_VERT,&info);switch(LOWORD(wp)){case SB_LINEUP:position-=24;break;case SB_LINEDOWN:position+=24;break;case SB_PAGEUP:position-=info.nPage;break;case SB_PAGEDOWN:position+=info.nPage;break;case SB_THUMBTRACK:position=info.nTrackPos;break;}}
            RECT r{};GetClientRect(window,&r);self->scroll=std::clamp(position,0,std::max(0,680-int(r.bottom)));self->Layout();return 0;}
    }return DefWindowProcW(window,message,wp,lp);
}
void MetadataEditorUi::Create(HWND parent){WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Proc;wc.lpszClassName=L"KuniMetadataEditor";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);RegisterClassW(&wc);
    panel=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"",WS_CHILD|WS_VSCROLL|WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,wc.hInstance,this);
    const wchar_t* names[]{L"撮影日時（XMP）",L"撮影者",L"ワールド名",L"ワールドID",L"モデル（1行1名）",L"タグ（1行1タグ）"};
    auto control=[&](const wchar_t* type,const wchar_t* text,DWORD style){auto h=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,panel,nullptr,wc.hInstance,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return h;};
    for(size_t i=0;i<6;++i){labels[i]=control(L"STATIC",names[i],0);inputs[i]=control(L"EDIT",L"",WS_BORDER|WS_TABSTOP|(i>=4?ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL:ES_AUTOHSCROLL));SetWindowLongPtrW(inputs[i],GWLP_ID,1000+i);SendMessageW(inputs[i],EM_SETLIMITTEXT,65536,0);choices[i]=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP);SetWindowLongPtrW(choices[i],GWLP_ID,1100+i);
        for(auto s:{L"変更しない",L"値を設定",L"値を削除する"})SendMessageW(choices[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s));if(i==5)SendMessageW(choices[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"追加"));SendMessageW(choices[i],CB_SETCURSEL,0,0);}
    referenceLabel=control(L"STATIC",L"同室者・モデル候補の記録（読取り専用）",0);reference=control(L"EDIT",L"",WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP);SetWindowLongPtrW(reference,GWLP_ID,1010);
    message=control(L"STATIC",L"入力後に「値を設定」を選択。空欄と削除は別操作です。",0);
}
void MetadataEditorUi::Load(const EditableMetadata& metadata){loading=true;batch=false;mixed.fill(false);original=metadata.values;SetWindowTextW(reference,metadata.recordReference.empty()?L"自動記録なし":metadata.recordReference.c_str());scroll=0;const wchar_t* names[]{L"撮影日時（XMP）",L"撮影者",L"ワールド名",L"ワールドID",L"モデル（1行1名）",L"タグ（1行1タグ）"};for(size_t i=0;i<6;++i){SetWindowTextW(labels[i],names[i]);SetWindowTextW(inputs[i],metadata.values[i].c_str());SendMessageW(choices[i],CB_SETCURSEL,0,0);}loading=false;Enable(true);Layout();}
void MetadataEditorUi::LoadBatch(const std::vector<EditableMetadata>& metadata){if(metadata.empty())return;Load(metadata.front());batch=true;loading=true;for(size_t i=0;i<6;++i){for(auto& item:metadata)if(item.values[i]!=original[i])mixed[i]=true;if(mixed[i]){SetWindowTextW(inputs[i],L"");SetWindowTextW(labels[i],(Text(labels[i])+L"（複数の値）").c_str());}}if(std::any_of(metadata.begin(),metadata.end(),[&](auto& m){return m.recordReference!=metadata.front().recordReference;}))SetWindowTextW(reference,L"複数の記録があります。個別に選択して確認してください。");SetWindowTextW(labels[0],L"撮影日時（一括変更不可）");loading=false;Enable(true);Layout();}
void MetadataEditorUi::Place(int x,int y,int w,int h,bool visible){MoveWindow(panel,x,y,std::max(1,w),std::max(1,h),TRUE);ShowWindow(panel,visible?SW_SHOW:SW_HIDE);}
void MetadataEditorUi::Layout(){if(!panel)return;RECT r{};GetClientRect(panel,&r);int w=std::max(140L,r.right);for(int i=0;i<6;++i){int y=i*64+(i==5?32:0)-scroll;MoveWindow(labels[i],6,y+3,std::max(1,w-146),20,TRUE);MoveWindow(choices[i],w-134,y,128,160,TRUE);MoveWindow(inputs[i],6,y+26,w-12,i>=4?60:26,TRUE);}MoveWindow(referenceLabel,6,450-scroll,w-12,30,TRUE);MoveWindow(reference,6,484-scroll,w-12,130,TRUE);MoveWindow(message,6,630-scroll,w-12,38,TRUE);SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,679,static_cast<UINT>(r.bottom),scroll};SetScrollInfo(panel,SB_VERT,&info,TRUE);}
MetadataEdit MetadataEditorUi::Read() const {MetadataEdit edit;for(size_t i=0;i<6;++i){edit.values[i]=Text(inputs[i]);auto selected=SendMessageW(choices[i],CB_GETCURSEL,0,0);edit.actions[i]=selected==1?EditAction::Set:selected==2?EditAction::Remove:selected==3?EditAction::Append:EditAction::Keep;if(!batch&&!mixed[i]&&((edit.actions[i]==EditAction::Set&&edit.values[i]==original[i])||(edit.actions[i]==EditAction::Remove&&original[i].empty())))edit.actions[i]=EditAction::Keep;}if(batch)edit.actions[0]=EditAction::Keep;return edit;}
void MetadataEditorUi::Enable(bool enabled){for(auto h:inputs)EnableWindow(h,enabled);for(auto h:choices)EnableWindow(h,enabled);if(batch){EnableWindow(inputs[0],FALSE);EnableWindow(choices[0],FALSE);}}
