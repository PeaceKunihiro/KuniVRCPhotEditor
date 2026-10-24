#include "ViewerRuntime.h"
void CaptureV0412Ui(const fs::path&,HWND);
namespace {
struct PaintProbe {unsigned positions=0,partial=0,finished=0;bool force=false;};
LRESULT CALLBACK ProbePaint(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){auto probe=reinterpret_cast<PaintProbe*>(data);
    if(m==WM_PAINT){if(UiRepaintActive())++probe->partial;else ++probe->finished;}
    if(m==WM_WINDOWPOSCHANGED&&UiRepaintActive()&&!probe->force){++probe->positions;probe->force=true;InvalidateRect(h,nullptr,TRUE);UpdateWindow(h);probe->force=false;}
    return DefSubclassProc(h,m,wp,lp);
}
struct MenuProbe {HMENU menu{};HWND popup{};fs::path image;COLORREF edge=CLR_INVALID,separator=CLR_INVALID;std::exception_ptr failure;};
LRESULT CALLBACK ProbePopup(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){auto probe=reinterpret_cast<MenuProbe*>(data);if(m==WM_TIMER&&wp==57){KillTimer(h,57);
    try{EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL{wchar_t type[64]{};GetClassNameW(window,type,64);if(wcscmp(type,L"#32768")==0&&IsWindowVisible(window))*reinterpret_cast<HWND*>(data)=window;return TRUE;},reinterpret_cast<LPARAM>(&probe->popup));
        if(probe->popup){RECT rect{},separator{};GetWindowRect(probe->popup,&rect);bool hasSeparator=GetMenuItemRect(nullptr,probe->menu,1,&separator)!=FALSE;CaptureV0412Ui(probe->image,probe->popup);
            // Read the window's printed bitmap, independent of other apps covering the test window.
            ComPtr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICBitmapDecoder> decoder;winrt::check_hresult(factory->CreateDecoderFromFilename(probe->image.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));ComPtr<IWICBitmapFrameDecode> frame;winrt::check_hresult(decoder->GetFrame(0,&frame));auto pixels=Decode(factory.Get(),frame.Get(),UINT_MAX,UINT_MAX);
            auto pixel=[&](int x,int y){if(x<0||y<0||x>=int(pixels.width)||y>=int(pixels.height))return COLORREF(CLR_INVALID);auto at=(size_t(y)*pixels.width+x)*4;return RGB(pixels.data[at+2],pixels.data[at+1],pixels.data[at]);};probe->edge=pixel(0,pixels.height/2);if(hasSeparator)probe->separator=pixel((separator.left+separator.right)/2-rect.left,(separator.top+separator.bottom)/2-rect.top);
        }
    }catch(...){probe->failure=std::current_exception();}EndMenu();return 0;}
    return DefSubclassProc(h,m,wp,lp);
}
}
void RunV0515UiTests(const fs::path& directory,std::ostream& report){
    auto check=[&](bool ok,const char* text){if(!ok)throw std::runtime_error(text);report<<"PASS: v0515 "<<text<<std::endl;};
    auto pump=[](){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}};
    SetAppFont(L"",10,false);bool visible=IsWindowVisible(app.window);ShowWindow(app.window,SW_SHOWNOACTIVATE);app.layout=L"right";app.sideWidth=400;app.sideHeight=300;SendMessageW(app.placement,CB_SETCURSEL,0,0);TabCtrl_SetCurSel(app.metadataTabs,0);
    // Constant pixels make an uncovered control-colored stripe directly detectable.
    app.previewOriginalRequested=true;app.previewPixels={400,1000,std::vector<BYTE>(400*1000*4)};for(size_t i=0;i<app.previewPixels.data.size();i+=4){app.previewPixels.data[i]=25;app.previewPixels.data[i+1]=80;app.previewPixels.data[i+2]=185;app.previewPixels.data[i+3]=255;}++app.previewRevision;app.previewDedicated=true;
    PhotoSummary summary;summary.values[0]=L"paint-test.png";for(int i=1;i<11;++i)summary.values[i]=L"再描画確認";summary.values[9]=L"人物A\r\n人物B\r\n人物C";ShowSummary(summary);
    PaintProbe probe;SetWindowSubclass(app.simple,ProbePaint,5153,reinterpret_cast<DWORD_PTR>(&probe));SetWindowSubclass(app.preview,ProbePaint,5153,reinterpret_cast<DWORD_PTR>(&probe));auto suppressed=UiRepaintSuppressed();
    for(int i=0;i<5;++i){app.sideWidth=360+i*18;Layout();SendMessageW(app.side,WM_VSCROLL,i%2?SB_TOP:SB_LINEDOWN,0);UpdateWindow(app.side);}
    check(!UiRepaintActive()&&probe.positions>0&&UiRepaintSuppressed()>suppressed&&probe.partial==0,"forced synchronous paints are deferred until layout is complete");
    ResetSideScroll();Layout();RedrawWindow(app.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);auto dc=GetDC(app.preview);bool clean=true;for(int y=25;y<300;y+=19){auto pixel=GetPixel(dc,120,y);clean=clean&&pixel!=CLR_INVALID&&std::abs(int(GetRValue(pixel))-185)<=1&&std::abs(int(GetGValue(pixel))-80)<=1&&std::abs(int(GetBValue(pixel))-25)<=1;}ReleaseDC(app.preview,dc);
    check(clean&&probe.finished>0,"preview remains image-colored with no metadata stripes after scrolling and resizing");
    PaintProbe row;SetWindowSubclass(app.simpleValues[0],ProbePaint,5153,reinterpret_cast<DWORD_PTR>(&row));SendMessageW(app.side,WM_VSCROLL,SB_LINEDOWN,0);check(row.positions==0,"scrolling reuses measured rows without repositioning each metadata field");
    summary.values[0]=std::wstring(400,L'界');ShowSummary(summary);check(row.positions>0,"metadata changes invalidate the row measurement cache");RemoveWindowSubclass(app.simpleValues[0],ProbePaint,5153);summary.values[0]=L"paint-test.png";ShowSummary(summary);
    check((GetWindowLongPtrW(app.preview,GWL_STYLE)&WS_CLIPSIBLINGS)&&(GetWindowLongPtrW(app.simple,GWL_STYLE)&WS_CLIPSIBLINGS),"sibling clipping prevents overlap painting");
    RemoveWindowSubclass(app.simple,ProbePaint,5153);RemoveWindowSubclass(app.preview,ProbePaint,5153);
    for(auto placement:{L"left",L"right",L"bottom",L"none"}){app.layout=placement;Layout();check(!UiRepaintActive()&&bool(IsWindowVisible(app.side))==(std::wstring(placement)!=L"none"),"all placements restore normal visibility after batched painting");}
    app.layout=L"right";app.sideWidth=400;TabCtrl_SetCurSel(app.metadataTabs,0);ResetSideScroll();
    for(bool dark:{false,true}){app.darkTheme=dark;ApplyTheme();Layout();CaptureV0412Ui(directory/(dark?L"v0515-side-dark.png":L"v0515-side-light.png"),app.window);
        auto menu=CreatePopupMenu(),sub=CreatePopupMenu();AppendMenuW(menu,MF_STRING|MF_CHECKED,1,L"描画確認：チェック項目");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|MF_GRAYED,2,L"無効な項目");AppendMenuW(sub,MF_STRING,3,L"子項目");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(sub),L"サブメニュー");
        MENUITEMINFOW before{sizeof(before)};before.fMask=MIIM_FTYPE|MIIM_STATE|MIIM_DATA;GetMenuItemInfoW(menu,1,TRUE,&before);
        MenuProbe popup{menu,nullptr,directory/(dark?L"v0515-menu-dark.png":L"v0515-menu-light.png")};SetWindowSubclass(app.window,ProbePopup,5154,reinterpret_cast<DWORD_PTR>(&popup));SetTimer(app.window,57,220,nullptr);RECT origin{};GetWindowRect(app.window,&origin);TrackUiMenu(menu,TPM_RETURNCMD,origin.left+160,origin.top+120,app.window,nullptr);KillTimer(app.window,57);RemoveWindowSubclass(app.window,ProbePopup,5154);if(popup.failure){DestroyMenu(menu);std::rethrow_exception(popup.failure);}
        check(popup.popup!=nullptr,"native popup is captured with check, separator, disabled item and submenu");
        report<<"INFO: v0515 menu edge="<<popup.edge<<" separator="<<popup.separator<<std::endl;
        if(dark&&ThemeIsDark()){auto dim=[](COLORREF c){return c!=CLR_INVALID&&GetRValue(c)<=110&&GetGValue(c)<=110&&GetBValue(c)<=110;};check(dim(popup.edge)&&dim(popup.separator),"night menu outer frame and separator are dark");}
        MENUITEMINFOW original{sizeof(original)};original.fMask=MIIM_FTYPE|MIIM_STATE|MIIM_DATA;GetMenuItemInfoW(menu,1,TRUE,&original);check((original.fType&MFT_SEPARATOR)&&!(original.fType&MFT_OWNERDRAW)&&original.dwItemData==before.dwItemData&&original.fState==before.fState,"separator type, state and data restored after menu closes");DestroyMenu(menu);
    }
    app.darkTheme=false;app.layout=L"right";app.previewPixels={};++app.previewRevision;app.previewDedicated=false;app.previewOriginalRequested=false;ShowSummary({});ResetSideScroll();ApplyTheme();Layout();pump();if(!visible)ShowWindow(app.window,SW_HIDE);
}
int RepaintTest(){auto directory=app.settings.parent_path();std::ofstream report(directory/L"v0515-result.txt");try{RunV0515UiTests(directory,report);report<<"ALL PASSED\n";return 0;}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<std::endl;return 1;}}
