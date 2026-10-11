#include "ViewerRuntime.h"
#include "TestOcr.h"
namespace {
constexpr int Mode=1801,Apply=1802,Target=1803,Output=1804,TestRun=1805;
HWND mode{},fps{},seconds{},apply{},target{},status{},raw{},fpsLabel{},secondsLabel{},output{},testRun{};
std::vector<person_ocr::Window> windows;
std::wstring previousRaw,previousStatus,lastImage;
int source=0;bool names=false;HWND testWindow{},normalWindow{};ULONGLONG refreshed=0;
void TextChanged(HWND h,const std::wstring& text,std::wstring& previous){if(text!=previous){SetWindowTextW(h,text.c_str());previous=text;}}
void RecognizeImage(){int count=ListView_GetSelectedCount(app.list);if(count!=1||app.selected<0||app.selected>=int(app.files.size())){test_ocr::Stop();lastImage.clear();TextChanged(status,L"画像認識：画像を1件選択してください",previousStatus);return;}lastImage=app.files[app.selected];test_ocr::Image(lastImage);}
void FillTargets(){windows=source==1?test_ocr::Windows():person_ocr::Windows();auto missing=[&](HWND h){return h&&std::none_of(windows.begin(),windows.end(),[&](auto& w){return w.handle==h;});};if(source==0&&missing(normalWindow)){normalWindow=nullptr;person_ocr::SelectWindow(nullptr);}if(source==1&&missing(testWindow)){test_ocr::Stop();testWindow=nullptr;}SendMessageW(target,CB_RESETCONTENT,0,0);for(auto text:{L"VRChatウィンドウ：自動選択",L"テストキャプチャ（対象を選択）",L"画像認識（選択画像）"})SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));int selected=source;for(size_t i=0;i<windows.size();++i){auto text=(source==1?L"テスト：":L"VRChat：")+windows[i].title+L" (PID "+std::to_wstring(windows[i].pid)+L")";SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(windows[i].handle==(source==1?testWindow:normalWindow))selected=int(i+3);}SendMessageW(target,CB_SETCURSEL,selected,0);}
}
bool OcrTestSource(){return source!=0;}
std::shared_ptr<const Pixels> OcrTestPreview(){return test_ocr::Preview();}
std::wstring OcrTestStatus(){return test_ocr::Status();}
void StopOcrUi(){test_ocr::Stop();}
void CreateOcrUi(){
    source=0;names=false;testWindow=nullptr;normalWindow=nullptr;refreshed=0;lastImage.clear();previousRaw.clear();previousStatus.clear();
    person_ocr::LoadSettings(app.settings.parent_path());auto settings=person_ocr::GetSettings();
    mode=Control(L"BUTTON",settings.always?L"常にOCR":L"撮影時OCR",WS_TABSTOP,Mode);
    fpsLabel=Control(L"STATIC",L"読み取りFPS (5–15)",SS_LEFT,0);
    secondsLabel=Control(L"STATIC",L"バッファ秒 (1–30)",SS_LEFT,0);
    fps=Control(L"EDIT",std::to_wstring(settings.fps).c_str(),WS_BORDER|WS_TABSTOP|ES_NUMBER,0);
    seconds=Control(L"EDIT",std::to_wstring(settings.seconds).c_str(),WS_BORDER|WS_TABSTOP|ES_NUMBER,0);
    apply=Control(L"BUTTON",L"設定を適用",WS_TABSTOP,Apply);
    output=Control(L"BUTTON",L"OCR結果：認識",WS_TABSTOP,Output);
    testRun=Control(L"BUTTON",L"テスト開始／再実行",WS_TABSTOP,TestRun);
    target=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_TABSTOP|WS_VSCROLL,Target);
    status=Control(L"EDIT",L"記録停止",ES_MULTILINE|ES_READONLY,0);
    raw=Control(L"EDIT",L"OCR結果",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_BORDER|WS_TABSTOP,0);
    SendMessageW(raw,EM_SETLIMITTEXT,65536,0);SendMessageW(fps,EM_SETLIMITTEXT,2,0);SendMessageW(seconds,EM_SETLIMITTEXT,2,0);FillTargets();
}
void LayoutOcrUi(RECT page,bool visible){
    int x=page.left,y=page.top,w=std::max(1L,page.right-page.left);
    MoveWindow(mode,x,y,std::min(w,130),26,TRUE);
    MoveWindow(target,x,y+30,w,250,TRUE);
    MoveWindow(fpsLabel,x,y+62,w/2,22,TRUE);MoveWindow(secondsLabel,x+w/2,y+62,w-w/2,22,TRUE);
    MoveWindow(fps,x,y+86,std::max(30,w/2-8),24,TRUE);MoveWindow(seconds,x+w/2,y+86,std::max(30,w/2),24,TRUE);
    int buttonWidth=std::min(w,145),resultY=w<280?144:114;
    MoveWindow(apply,x,y+114,std::min(w,120),26,TRUE);MoveWindow(output,w<280?x:x+128,y+resultY,buttonWidth,26,TRUE);
    MoveWindow(testRun,x,y+resultY+30,std::min(w,180),26,TRUE);MoveWindow(status,x,y+resultY+60,w,60,TRUE);
    MoveWindow(raw,x,y+resultY+124,w,std::max(1L,page.bottom-y-resultY-124),TRUE);
    for(auto h:{mode,target,fpsLabel,secondsLabel,fps,seconds,apply,output,testRun,status,raw}){ShowWindow(h,visible?SW_SHOW:SW_HIDE);if(visible)SetWindowPos(h,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
    EnableWindow(testRun,source!=0);RefreshOcrUi();
}
void RefreshOcrUi(){
    if(!status)return;
    if(!SendMessageW(target,CB_GETDROPPEDSTATE,0,0)&&GetTickCount64()-refreshed>2000){refreshed=GetTickCount64();auto next=source==1?test_ocr::Windows():person_ocr::Windows();bool changed=next.size()!=windows.size();if(!changed)for(size_t i=0;i<next.size();++i)if(next[i].handle!=windows[i].handle||next[i].title!=windows[i].title)changed=true;if(changed)FillTargets();}
    bool one=app.list&&ListView_GetSelectedCount(app.list)==1&&app.selected>=0&&app.selected<int(app.files.size());
    if(source==2){auto path=one?app.files[app.selected]:L"";if(path!=lastImage){if(one)RecognizeImage();else{test_ocr::Stop();lastImage.clear();}}}
    auto text=source?test_ocr::Status():person_ocr::Status();if(!source&&!stage3::Recording()){auto monitor=stage3::MonitorStatus();if(!monitor.empty())text=monitor;}if(source==2&&!one)text=L"画像認識：画像を1件選択してください";
    if(source&&stage3::Recording())text+=L"\r\n通常記録は別処理で継続";
    TextChanged(status,text,previousStatus);
    if(IsWindowVisible(raw)){auto result=source?test_ocr::Raw():person_ocr::Raw();if(names)result=source?test_ocr::FilterNames(result):person_ocr::Names();TextChanged(raw,result,previousRaw);}
    SetWindowTextW(testRun,source==1&&test_ocr::Active()?L"テスト停止":source==2?L"画像認識を再実行":L"テスト開始");EnableWindow(testRun,source!=0);
    EnableWindow(mode,source==0);EnableWindow(seconds,source==0);EnableWindow(secondsLabel,source==0);EnableWindow(fps,source!=2);EnableWindow(fpsLabel,source!=2);EnableWindow(apply,source!=2);
}
bool OcrCommand(int id,int code){
    if(id!=Mode&&id!=Apply&&id!=Target&&id!=Output&&id!=TestRun)return false;
    try{
        if(id==Output){names=!names;SetWindowTextW(output,names?L"OCR結果：名前":L"OCR結果：認識");previousRaw=L"\x1";}
        else if(id==TestRun){if(source==2)RecognizeImage();else if(source==1){if(test_ocr::Active())test_ocr::Stop();else if(testWindow){auto s=person_ocr::GetSettings();test_ocr::Capture(testWindow,s.fps,s.seconds);}else MessageBoxW(app.window,L"対象ウィンドウをプルダウンから選択してください。",L"テストキャプチャ",MB_OK);}}
        else if(id==Target&&code==CBN_SELCHANGE){auto index=SendMessageW(target,CB_GETCURSEL,0,0);if(index<0)return true;if(index<3){test_ocr::Stop();source=int(index);testWindow=nullptr;lastImage.clear();if(source==0){normalWindow=nullptr;person_ocr::SelectWindow(nullptr);}FillTargets();if(source==2)RecognizeImage();}
            else if(size_t(index-3)<windows.size()){auto h=windows[index-3].handle;if(source==1){testWindow=h;auto s=person_ocr::GetSettings();test_ocr::Capture(h,s.fps,s.seconds);}else{test_ocr::Stop();lastImage.clear();source=0;normalWindow=h;person_ocr::SelectWindow(h);}}}
        else if(id==Mode||id==Apply){auto value=person_ocr::GetSettings();if(id==Mode)value.always=!value.always;else{value.fps=std::stoi(Text(fps));value.seconds=std::stoi(Text(seconds));}person_ocr::SetSettings(value);SetWindowTextW(mode,value.always?L"常にOCR":L"撮影時OCR");if(source==1&&test_ocr::Active()){test_ocr::Capture(testWindow,value.fps,value.seconds);}}
        RefreshOcrUi();InvalidateRect(app.preview,nullptr,FALSE);
    }catch(const std::exception&){MessageBoxW(app.window,L"FPSは5～15、期間は1～30秒です。対象ウィンドウ・設定保存先も確認してください。",L"OCR設定",MB_OK|MB_ICONWARNING);}
    catch(const winrt::hresult_error& e){MessageBoxW(app.window,e.message().c_str(),L"OCR設定",MB_OK|MB_ICONWARNING);}return true;
}
