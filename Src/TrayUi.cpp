#include "ViewerRuntime.h"
#include "AppVersion.h"
#include "resource.h"
namespace {
constexpr UINT Callback=WM_APP+21;
bool hidden=false,registered=false,maximized=false,red=false;HICON normal{},recording{};
std::function<bool(DWORD,NOTIFYICONDATAW&)> notify;
UINT ExplorerMessage(){static UINT id=RegisterWindowMessageW(L"TaskbarCreated");return id;}
bool Notify(DWORD command,NOTIFYICONDATAW& data){return notify?notify(command,data):Shell_NotifyIconW(command,&data)!=FALSE;}
HICON RecordIcon(HICON original){
    int size=GetSystemMetrics(SM_CXSMICON);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),size,-size,1,32,BI_RGB};void* pixels{};
    HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);DeleteDC(dc);return nullptr;}
    auto prior=SelectObject(dc,bitmap);memset(pixels,0,size*size*4);DrawIconEx(dc,0,0,original,size,size,0,nullptr,DI_NORMAL);
    auto bytes=static_cast<unsigned char*>(pixels);int radius=std::max(3,size/4),cx=size-radius-1,cy=size-radius-1;
    for(int y=0;y<size;++y)for(int x=0;x<size;++x)if((x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius){auto p=bytes+4*(y*size+x);p[0]=50;p[1]=50;p[2]=245;p[3]=255;}
    SelectObject(dc,prior);std::vector<unsigned char> maskBits(((size+15)/16)*2*size,0);HBITMAP mask=CreateBitmap(size,size,1,1,maskBits.data());ICONINFO icon{TRUE,0,0,mask,bitmap};auto result=CreateIconIndirect(&icon);DeleteObject(mask);DeleteObject(bitmap);DeleteDC(dc);return result;
}
NOTIFYICONDATAW Data(HWND window){
    if(!normal){normal=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));recording=RecordIcon(normal);}
    NOTIFYICONDATAW data{sizeof(data)};data.hWnd=window;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=Callback;
    red=stage3::Recording();data.hIcon=red&&recording?recording:normal;wcscpy_s(data.szTip,stage3::Stopping()?L"KuniVRCPhotEditor：停止処理中":red?L"KuniVRCPhotEditor：記録中":L"KuniVRCPhotEditor：記録停止中");return data;
}
void Remove(HWND window){if(registered){auto data=Data(window);Notify(NIM_DELETE,data);registered=false;}}
void Restore(HWND window){hidden=false;Remove(window);ShowWindow(window,maximized?SW_SHOWMAXIMIZED:SW_RESTORE);SetForegroundWindow(window);}
bool Register(HWND window){auto data=Data(window);if(!Notify(NIM_ADD,data))return false;registered=true;data.uVersion=NOTIFYICON_VERSION_4;if(!Notify(NIM_SETVERSION,data)){Remove(window);return false;}return true;}
}
void TrayUpdate(){if(!hidden||!registered)return;auto data=Data(app.window);if(!Notify(NIM_MODIFY,data))Restore(app.window);}
void TrayShutdown(){hidden=false;Remove(app.window);if(recording)DestroyIcon(recording);recording=nullptr;normal=nullptr;}
bool TrayMessage(HWND window,UINT message,WPARAM wp,LPARAM lp){
    if(message==ExplorerMessage()){if(hidden){registered=false;if(!Register(window))Restore(window);}return true;}
    if(message==WM_SIZE){if(wp==SIZE_MINIMIZED&&!app.closing){if(hidden)return true;if(Register(window)){hidden=true;ShowWindow(window,SW_HIDE);}else ShowWindow(window,maximized?SW_SHOWMAXIMIZED:SW_RESTORE);return true;}if(wp==SIZE_MAXIMIZED)maximized=true;else if(wp==SIZE_RESTORED&&!hidden)maximized=false;return false;}
    if(message!=Callback)return false;UINT event=LOWORD(lp);
    if(event==WM_LBUTTONDBLCLK||event==NIN_SELECT||event==NIN_KEYSELECT)Restore(window);
    else if(event==WM_CONTEXTMENU||event==WM_RBUTTONUP){POINT point{};GetCursorPos(&point);auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"開く");AppendMenuW(menu,MF_STRING|(stage3::Stopping()?MF_GRAYED:0),2,stage3::Recording()?L"記録停止":L"記録開始");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,3,L"終了");SetForegroundWindow(window);auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);if(action==1)Restore(window);else if(action==2){stage3::ToggleRecording();TrayUpdate();}else if(action==3){Restore(window);SendMessageW(window,WM_CLOSE,0,0);}}
    return true;
}
void RunV0413UiTests(const fs::path&,std::ostream& report){
    auto check=[&](bool ok,const char* text){if(!ok)throw std::runtime_error(text);report<<"PASS: v0.4.13 "<<text<<'\n';};
    auto legacy=ParseViewerSettings("{\"version\":3,\"autoStart\":true}");check(!legacy.residentMode&&!legacy.residentOcr,"resident defaults OFF even with legacy autoStart");legacy.residentMode=true;legacy.residentOcr=true;auto saved=ParseViewerSettings(SerializeViewerSettings(legacy));check(saved.residentMode&&saved.residentOcr,"resident settings roundtrip");
    check(Text(app.window)==L"KuniVRCPhotEditor v" KUNI_VERSION_TEXT,"simple production title");auto menu=BuildOptionsMenu();check(GetMenuState(menu,13,MF_BYCOMMAND)!=UINT(-1)&&GetMenuState(menu,14,MF_BYCOMMAND)!=UINT(-1),"resident options present");DestroyMenu(menu);
    auto pump=[](auto done){auto deadline=GetTickCount64()+15000;while(!done()){if(GetTickCount64()>deadline)throw std::runtime_error("v0.4.13 asynchronous stop timeout");MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(10);}};
    int adds=0,deletes=0;HICON last{};notify=[&](DWORD command,NOTIFYICONDATAW& data){if(command==NIM_ADD)++adds;if(command==NIM_DELETE)++deletes;last=data.hIcon;return true;};
    ShowWindow(app.window,SW_SHOW);SendMessageW(app.window,WM_SIZE,SIZE_MINIMIZED,0);check(hidden&&registered&&!IsWindowVisible(app.window)&&last==normal,"minimize registers stopped tray icon and hides");
    SendMessageW(app.window,ExplorerMessage(),0,0);check(adds==2&&registered&&hidden,"Explorer restart re-registers hidden window");
    SendMessageW(app.window,Callback,0,MAKELPARAM(NIN_SELECT,1));check(!hidden&&!registered&&IsWindowVisible(app.window)&&deletes==1,"tray click restores window and removes icon");
    notify=[](DWORD,NOTIFYICONDATAW&){return false;};SendMessageW(app.window,WM_SIZE,SIZE_MINIMIZED,0);check(!hidden&&IsWindowVisible(app.window),"registration failure keeps window accessible");notify={};
    check(recording&&recording!=normal,"recording icon has separate red badge");
    notify=[&](DWORD,NOTIFYICONDATAW& data){last=data.hIcon;return true;};
    app.residentOcr=false;stage3::ToggleRecording();check(stage3::Recording()&&!stage3::RecordingOcr(),"manual recording starts without OCR");SendMessageW(app.window,WM_SIZE,SIZE_MINIMIZED,0);check(hidden&&last==recording,"active recording uses red tray icon");stage3::ToggleRecording();pump([]{return !stage3::Recording();});TrayUpdate();check(last==normal,"completed stop restores original tray icon");SendMessageW(app.window,Callback,0,MAKELPARAM(NIN_SELECT,1));
    ShowWindow(app.window,SW_SHOWMAXIMIZED);SendMessageW(app.window,WM_SIZE,SIZE_MINIMIZED,0);SendMessageW(app.window,Callback,0,MAKELPARAM(NIN_SELECT,1));check(IsZoomed(app.window),"maximized window restores maximized");ShowWindow(app.window,SW_RESTORE);notify={};
    auto oldMode=app.residentMode,oldOcr=app.residentOcr;NewOptionsAction(13);NewOptionsAction(14);check(app.residentMode!=oldMode&&app.residentOcr!=oldOcr,"option toggles apply");NewOptionsAction(13);NewOptionsAction(14);
    SendMessageW(GetDlgItem(app.window,1803),CB_SETCURSEL,2,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(1803,CBN_SELCHANGE),0);
    int image=int(app.directories.size());if(image<int(app.files.size())){ListView_SetItemState(app.list,-1,0,LVIS_SELECTED);app.selected=image;ListView_SetItemState(app.list,image,LVIS_SELECTED,LVIS_SELECTED);FlushSelectionUpdate();int launched=0;auto previous=app.photosRequest;app.photosRequest=[&](const std::wstring&){++launched;return true;};SendMessageW(app.preview,WM_LBUTTONDBLCLK,0,0);check(launched==1,"selected-image OCR stopped preview double-click launches Photos");
        SendMessageW(app.window,WM_COMMAND,1805,0);SendMessageW(app.preview,WM_LBUTTONDBLCLK,0,0);check(launched==2,"selected-image OCR running preview double-click launches Photos");SendMessageW(app.window,WM_COMMAND,1805,0);
        auto original=app.files[image];app.files[image]=original+L".missing";SendMessageW(app.preview,WM_LBUTTONDBLCLK,0,0);app.files[image]=original;check(launched==2,"missing selected image does not launch Photos");
        if(image+1<int(app.files.size())){ListView_SetItemState(app.list,image+1,LVIS_SELECTED,LVIS_SELECTED);SendMessageW(app.preview,WM_LBUTTONDBLCLK,0,0);check(launched==2,"multiple selected images do not launch Photos from OCR preview");}
        ListView_SetItemState(app.list,-1,0,LVIS_SELECTED);SendMessageW(app.preview,WM_LBUTTONDBLCLK,0,0);check(launched==2,"empty selection does not launch Photos");app.photosRequest=previous;}
    SendMessageW(GetDlgItem(app.window,1803),CB_SETCURSEL,0,0);SendMessageW(app.window,WM_COMMAND,MAKEWPARAM(1803,CBN_SELCHANGE),0);
}

