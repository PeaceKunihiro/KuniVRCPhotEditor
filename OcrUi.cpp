#include "ViewerRuntime.h"
namespace {
constexpr int Mode=1801,Apply=1802,Target=1803;
HWND mode{},fps{},seconds{},apply{},target{},status{},raw{},fpsLabel{},secondsLabel{};
std::vector<person_ocr::Window> windows;
std::wstring previousRaw,previousStatus;
void TextChanged(HWND h,const std::wstring& text,std::wstring& previous){if(text!=previous){SetWindowTextW(h,text.c_str());previous=text;}}
}
void CreateOcrUi(){
    person_ocr::LoadSettings(app.settings.parent_path());auto settings=person_ocr::GetSettings();
    mode=Control(L"BUTTON",settings.always?L"常にOCR":L"撮影時OCR",WS_TABSTOP,Mode);
    fpsLabel=Control(L"STATIC",L"読み取りFPS (5–15)",SS_LEFT,0);
    secondsLabel=Control(L"STATIC",L"バッファ秒 (1–30)",SS_LEFT,0);
    fps=Control(L"EDIT",std::to_wstring(settings.fps).c_str(),WS_BORDER|WS_TABSTOP|ES_NUMBER,0);
    seconds=Control(L"EDIT",std::to_wstring(settings.seconds).c_str(),WS_BORDER|WS_TABSTOP|ES_NUMBER,0);
    apply=Control(L"BUTTON",L"設定を適用",WS_TABSTOP,Apply);
    target=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP|WS_VSCROLL,Target);
    status=Control(L"EDIT",L"記録停止",ES_MULTILINE|ES_READONLY,0);
    raw=Control(L"EDIT",L"OCR結果",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER|WS_TABSTOP,0);
    SendMessageW(raw,EM_SETLIMITTEXT,65536,0);SendMessageW(fps,EM_SETLIMITTEXT,2,0);SendMessageW(seconds,EM_SETLIMITTEXT,2,0);
}
void LayoutOcrUi(RECT page,bool visible){
    int x=page.left,y=page.top,w=std::max(1L,page.right-page.left);
    MoveWindow(mode,x,y,std::min(w,130),26,TRUE);
    MoveWindow(target,x,y+30,w,180,TRUE);
    MoveWindow(fpsLabel,x,y+62,w/2,22,TRUE);MoveWindow(secondsLabel,x+w/2,y+62,w-w/2,22,TRUE);
    MoveWindow(fps,x,y+86,std::max(30,w/2-8),24,TRUE);MoveWindow(seconds,x+w/2,y+86,std::max(30,w/2),24,TRUE);
    MoveWindow(apply,x,y+114,std::min(w,120),26,TRUE);MoveWindow(status,x,y+144,w,48,TRUE);
    MoveWindow(raw,x,y+196,w,std::max(1L,page.bottom-y-196),TRUE);
    for(auto h:{mode,target,fpsLabel,secondsLabel,fps,seconds,apply,status,raw}){ShowWindow(h,visible?SW_SHOW:SW_HIDE);if(visible)SetWindowPos(h,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
    RefreshOcrUi();
}
void RefreshOcrUi(){
    if(!status)return;TextChanged(status,person_ocr::Status(),previousStatus);if(IsWindowVisible(raw))TextChanged(raw,person_ocr::Raw(),previousRaw);
    if(SendMessageW(target,CB_GETDROPPEDSTATE,0,0))return;
    auto next=person_ocr::Windows();bool changed=next.size()!=windows.size();if(!changed)for(size_t i=0;i<next.size();++i)if(next[i].handle!=windows[i].handle||next[i].title!=windows[i].title)changed=true;
    if(changed||SendMessageW(target,CB_GETCOUNT,0,0)==0){auto selected=SendMessageW(target,CB_GETCURSEL,0,0);HWND chosen=selected>0&&size_t(selected)<=windows.size()?windows[selected-1].handle:nullptr;windows=std::move(next);SendMessageW(target,CB_RESETCONTENT,0,0);SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"VRChatウィンドウ：自動選択"));int restore=0;for(size_t i=0;i<windows.size();++i){auto text=windows[i].title+L" (PID "+std::to_wstring(windows[i].pid)+L")";SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(windows[i].handle==chosen)restore=int(i+1);}SendMessageW(target,CB_SETCURSEL,restore,0);if(chosen&&!restore)person_ocr::SelectWindow(nullptr);}
}
bool OcrCommand(int id,int code){
    if(id!=Mode&&id!=Apply&&id!=Target)return false;
    try{if(id==Target&&code==CBN_SELCHANGE){auto index=SendMessageW(target,CB_GETCURSEL,0,0);person_ocr::SelectWindow(index>0&&size_t(index)<=windows.size()?windows[index-1].handle:nullptr);}
    else if(id==Mode||id==Apply){auto value=person_ocr::GetSettings();if(id==Mode)value.always=!value.always;else{value.fps=std::stoi(Text(fps));value.seconds=std::stoi(Text(seconds));}person_ocr::SetSettings(value);SetWindowTextW(mode,value.always?L"常にOCR":L"撮影時OCR");}RefreshOcrUi();}
    catch(const std::exception&){MessageBoxW(app.window,L"読み取りFPSは5～15、バッファ期間は1～30秒を指定してください。設定ファイルを保存できない場合も適用できません。",L"OCR設定",MB_OK|MB_ICONWARNING);}catch(const winrt::hresult_error& e){MessageBoxW(app.window,e.message().c_str(),L"OCR設定",MB_OK|MB_ICONWARNING);}return true;
}
