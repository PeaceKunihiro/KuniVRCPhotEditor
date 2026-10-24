#include "ViewerRuntime.h"
#include "TestOcr.h"
namespace {
constexpr int Mode=1801,Apply=1802,Target=1803,Output=1804,TestRun=1805,FpsField=1806,High=1807;
HWND high{},mode{},fps{},seconds{},apply{},target{},status{},raw{},fpsLabel{},secondsLabel{},output{},testRun{};
std::vector<person_ocr::Window> windows;
std::wstring previousRaw,previousStatus,lastImage;
int source=0;bool names=false,imageRunning=false;HWND testWindow{},normalWindow{};ULONGLONG refreshed=0;
void TextChanged(HWND h,const std::wstring& text,std::wstring& previous){if(text!=previous){SetWindowTextW(h,text.c_str());previous=text;}}
void RecognizeImage(){int count=SelectedImageCount();if(count!=1||app.selected<0||app.selected>=int(app.files.size())||IsFolderItem(app.selected)){test_ocr::Stop();lastImage.clear();TextChanged(status,L"画像認識：画像を1件選択してください",previousStatus);return;}lastImage=app.files[app.selected];test_ocr::Image(lastImage,true);}
void FillTargets(){windows=source==1?test_ocr::Windows():person_ocr::Windows();auto missing=[&](HWND h){return h&&std::none_of(windows.begin(),windows.end(),[&](auto& w){return w.handle==h;});};if(source==0&&missing(normalWindow)){normalWindow=nullptr;person_ocr::SelectWindow(nullptr);}if(source==1&&missing(testWindow)){test_ocr::Stop();testWindow=nullptr;}SendMessageW(target,CB_RESETCONTENT,0,0);for(auto text:{L"VRChatウィンドウ：自動選択",L"テストキャプチャ（対象を選択）",L"画像認識（選択画像）"})SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));int selected=source;for(size_t i=0;i<windows.size();++i){auto text=(source==1?L"テスト：":L"VRChat：")+windows[i].title+L" (PID "+std::to_wstring(windows[i].pid)+L")";SendMessageW(target,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(windows[i].handle==(source==1?testWindow:normalWindow))selected=int(i+3);}SendMessageW(target,CB_SETCURSEL,selected,0);}
}
bool OcrImageSource(){return source==2;}
bool OcrTestSource(){return source!=0;}
std::shared_ptr<const Pixels> OcrTestPreview(){auto p=test_ocr::Preview();static std::shared_ptr<const Pixels> fallback;static uint64_t revision=UINT64_MAX;if(!p&&source==2&&!app.previewPixels.data.empty()){if(!fallback||revision!=app.previewRevision){fallback=std::make_shared<Pixels>(app.previewPixels);revision=app.previewRevision;}return fallback;}fallback.reset();return p;}
void SetHighPrecisionVisible(bool visible){
    if(stage3::Recording())throw std::runtime_error("Stop recording before changing OCR display");
    auto value=person_ocr::GetSettings();if(value.showHighPrecision==visible)return;
    bool modeChange=!visible&&value.highPrecision;bool active=source!=0&&test_ocr::Active();bool image=source==2&&imageRunning;
    if(modeChange&&active)test_ocr::Stop();
    value.showHighPrecision=visible;
    try{person_ocr::SetSettings(value);}catch(...){if(modeChange&&active){if(image)RecognizeImage();else if(source==1&&testWindow)test_ocr::Capture(testWindow,value.fps,value.seconds);}throw;}
    if(modeChange&&active){if(image)RecognizeImage();else if(source==1&&testWindow)test_ocr::Capture(testWindow,value.fps,value.seconds);}
    SetWindowTextW(high,person_ocr::GetSettings().highPrecision?L"高精度：ON":L"高精度：OFF");
    Layout();RefreshPreview();
}
std::wstring OcrTestStatus(){return test_ocr::Status();}
void StopOcrUi(){test_ocr::Stop();}
void CreateOcrUi(){
    source=0;names=false;imageRunning=false;testWindow=nullptr;normalWindow=nullptr;refreshed=0;lastImage.clear();previousRaw.clear();previousStatus.clear();
    person_ocr::LoadSettings(app.settings.parent_path());auto settings=person_ocr::GetSettings();
    mode=Control(L"BUTTON",settings.always?L"常にOCR":L"撮影時OCR",WS_TABSTOP,Mode);
    high=Control(L"BUTTON",settings.highPrecision?L"高精度：ON":L"高精度：OFF",WS_TABSTOP|BS_OWNERDRAW,High);
    fpsLabel=Control(L"STATIC",L"読み取りFPS (5–15)",SS_LEFT,0);
    secondsLabel=Control(L"STATIC",L"バッファ秒 (1–30)",SS_LEFT,0);
    fps=Control(L"EDIT",std::to_wstring(settings.fps).c_str(),WS_BORDER|WS_TABSTOP|ES_NUMBER,FpsField);
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
    int bh=UiControlHeight(),line=UiLineHeight()+4,gap=4,top=y;
    auto place=[&](HWND h,int left,int at,int width,int height){MoveWindow(h,left,at,std::max(1,width),height,TRUE);};
    int modeW=std::max(130,UiTextWidth(L"撮影時OCR")+20),highW=std::max(120,UiTextWidth(L"高精度：OFF")+20);
    place(mode,x,top,std::min(w,modeW),bh);
    bool showHigh=person_ocr::GetSettings().showHighPrecision;
    if(showHigh){if(modeW+gap+highW>w){top+=bh+gap;place(high,x,top,std::min(w,highW),bh);}else place(high,x+modeW+gap,top,highW,bh);}
    top+=bh+gap;place(target,x,top,w,UiScale(250));top+=bh+gap;
    place(fpsLabel,x,top,w/2,line);place(secondsLabel,x+w/2,top,w-w/2,line);top+=line+gap;
    place(fps,x,top,w/2-gap,bh);place(seconds,x+w/2,top,w-w/2,bh);top+=bh+gap;
    int applyW=std::max(120,UiTextWidth(L"設定を適用")+20),outputW=std::max(145,UiTextWidth(L"OCR結果：認識")+20);
    place(apply,x,top,std::min(w,applyW),bh);
    if(applyW+gap+outputW>w){top+=bh+gap;place(output,x,top,std::min(w,outputW),bh);}else place(output,x+applyW+gap,top,outputW,bh);
    top+=bh+gap;place(testRun,x,top,std::min(w,std::max(180,UiTextWidth(L"テスト開始／再実行")+20)),bh);top+=bh+gap;
    place(status,x,top,w,UiScale(60));top+=UiScale(60)+gap;
    place(raw,x,top,w,std::max(1L,page.bottom-top));
    for(auto h:{mode,high,target,fpsLabel,secondsLabel,fps,seconds,apply,output,testRun,status,raw}){bool show=visible&&(h!=high||person_ocr::GetSettings().showHighPrecision);ShowWindow(h,show?SW_SHOW:SW_HIDE);EnableWindow(high,person_ocr::GetSettings().showHighPrecision);if(show)SetWindowPos(h,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
    EnableWindow(testRun,source!=0);RefreshOcrUi();
}
void RefreshOcrUi(){
    if(!status)return;
    if(!SendMessageW(target,CB_GETDROPPEDSTATE,0,0)&&GetTickCount64()-refreshed>2000){refreshed=GetTickCount64();auto next=source==1?test_ocr::Windows():person_ocr::Windows();bool changed=next.size()!=windows.size();if(!changed)for(size_t i=0;i<next.size();++i)if(next[i].handle!=windows[i].handle||next[i].title!=windows[i].title)changed=true;if(changed)FillTargets();}
    bool one=app.list&&SelectedImageCount()==1&&app.selected>=0&&app.selected<int(app.files.size())&&!IsFolderItem(app.selected);
    if(source==2){auto path=one?app.files[app.selected]:L"";if(path!=lastImage){if(one&&imageRunning)RecognizeImage();else{test_ocr::Stop();lastImage=path;}}}
    auto text=source?test_ocr::Status():(stage3::Recording()&&!stage3::RecordingOcr()?L"OCR停止：ログ情報のみ記録中":person_ocr::Status());if(!source&&!stage3::Recording()){auto monitor=stage3::MonitorStatus();if(!monitor.empty())text=monitor;}if(source==2&&!one)text=L"画像認識：画像を1件選択してください";else if(source==2&&!imageRunning)text=L"画像認識：停止（1FPS固定）";
    if(source&&stage3::Recording())text+=L"\r\n通常記録は別処理で継続";
    TextChanged(status,text,previousStatus);
    if(IsWindowVisible(raw)){auto result=source?test_ocr::Raw():person_ocr::Raw();if(names)result=source?test_ocr::FilterNames(result):person_ocr::Names();TextChanged(raw,result,previousRaw);}
    SetWindowTextW(testRun,source==1&&test_ocr::Active()?L"テスト停止":source==2?(imageRunning?L"画像認識：停止":L"画像認識：実行"):L"テスト開始");EnableWindow(testRun,source!=0);
    if(source==2){if(Text(fpsLabel)!=L"読み取りFPS (1固定)")SetWindowTextW(fpsLabel,L"読み取りFPS (1固定)");if(Text(fps)!=L"1")SetWindowTextW(fps,L"1");}else if(Text(fpsLabel)==L"読み取りFPS (1固定)"){SetWindowTextW(fpsLabel,L"読み取りFPS (5–15)");SetWindowTextW(fps,std::to_wstring(person_ocr::GetSettings().fps).c_str());}
    EnableWindow(mode,source==0);EnableWindow(seconds,source==0);EnableWindow(secondsLabel,source==0);EnableWindow(fps,source!=2);EnableWindow(fpsLabel,source!=2);EnableWindow(apply,source!=2);
}
bool OcrCommand(int id,int code){
    if(id!=High&&id!=Mode&&id!=Apply&&id!=Target&&id!=Output&&id!=TestRun)return false;
    try{
        if(id==High){auto value=person_ocr::GetSettings();if(!value.showHighPrecision)return true;if(!value.highPrecision){bool approved=app.confirmHighOcr?app.confirmHighOcr():MessageBoxW(app.window,L"高精度OCRは原寸画像を分割して認識します。CPU・メモリー負荷が増え、VRChatのFPSが低下する場合があります。有効にしますか？",L"高精度OCR",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)==IDYES;if(!approved)return true;}bool active=source==1&&test_ocr::Active();bool image=source==2&&imageRunning;test_ocr::Stop();value.highPrecision=!value.highPrecision;person_ocr::SetSettings(value);SetWindowTextW(high,value.highPrecision?L"高精度：ON":L"高精度：OFF");InvalidateRect(high,nullptr,TRUE);previousRaw=L"\x1";if(active)test_ocr::Capture(testWindow,value.fps,value.seconds);if(image)RecognizeImage();}
        else if(id==Output){names=!names;SetWindowTextW(output,names?L"OCR結果：名前":L"OCR結果：認識");previousRaw=L"\x1";}
        else if(id==TestRun){if(source==2){if(imageRunning){imageRunning=false;test_ocr::PauseImage();}else{imageRunning=true;RecognizeImage();}}else if(source==1){if(test_ocr::Active())test_ocr::Stop();else if(testWindow){auto s=person_ocr::GetSettings();test_ocr::Capture(testWindow,s.fps,s.seconds);}else MessageBoxW(app.window,L"対象ウィンドウをプルダウンから選択してください。",L"テストキャプチャ",MB_OK);}}
        else if(id==Target&&code==CBN_SELCHANGE){auto index=SendMessageW(target,CB_GETCURSEL,0,0);if(index<0)return true;if(index<3){test_ocr::Stop();imageRunning=false;source=int(index);testWindow=nullptr;lastImage.clear();if(source==0){normalWindow=nullptr;person_ocr::SelectWindow(nullptr);}FillTargets();}
            else if(size_t(index-3)<windows.size()){auto h=windows[index-3].handle;if(source==1){testWindow=h;auto s=person_ocr::GetSettings();test_ocr::Capture(h,s.fps,s.seconds);}else{test_ocr::Stop();lastImage.clear();imageRunning=false;source=0;normalWindow=h;person_ocr::SelectWindow(h);}}}
        else if(id==Mode||id==Apply){auto value=person_ocr::GetSettings();if(id==Mode)value.always=!value.always;else{value.fps=std::stoi(Text(fps));value.seconds=std::stoi(Text(seconds));}person_ocr::SetSettings(value);SetWindowTextW(mode,value.always?L"常にOCR":L"撮影時OCR");if(source==1&&test_ocr::Active()){test_ocr::Capture(testWindow,value.fps,value.seconds);}}
        RefreshOcrUi();InvalidateRect(app.preview,nullptr,FALSE);
    }catch(const std::exception&){MessageBoxW(app.window,L"FPSは5～15、期間は1～30秒です。対象ウィンドウ・設定保存先も確認してください。",L"OCR設定",MB_OK|MB_ICONWARNING);}
    catch(const winrt::hresult_error& e){MessageBoxW(app.window,e.message().c_str(),L"OCR設定",MB_OK|MB_ICONWARNING);}return true;
}

bool DrawOcrButton(DRAWITEMSTRUCT* draw){if(draw->CtlID!=High)return false;int saved=SaveDC(draw->hDC);bool on=person_ocr::GetSettings().highPrecision;auto color=on?RGB(255,100,100):RGB(80,175,255);if(draw->itemState&ODS_SELECTED)color=on?RGB(220,75,75):RGB(55,145,225);auto brush=CreateSolidBrush(color);FillRect(draw->hDC,&draw->rcItem,brush);DeleteObject(brush);FrameRect(draw->hDC,&draw->rcItem,GetSysColorBrush(COLOR_GRAYTEXT));auto font=SendMessageW(draw->hwndItem,WM_GETFONT,0,0);if(font)SelectObject(draw->hDC,reinterpret_cast<HGDIOBJ>(font));SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,RGB(0,0,0));auto rect=draw->rcItem;auto text=Text(draw->hwndItem);DrawTextW(draw->hDC,text.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(draw->itemState&ODS_FOCUS){InflateRect(&rect,-3,-3);DrawFocusRect(draw->hDC,&rect);}RestoreDC(draw->hDC,saved);return true;}
