#include "ViewerRuntime.h"
#include <dwmapi.h>
#pragma comment(lib,"dwmapi.lib")
namespace {
thread_local std::vector<HWND> menuWindows;
thread_local bool watching=false;
struct DwmStyle {BOOL dark=FALSE;DWORD border=0xFFFFFFFF;bool savedDark=false,savedBorder=false;};
thread_local std::map<HWND,DwmStyle> dwmStyles;
void SetMenuDwmStyle(HWND h){
    DwmStyle style;style.savedDark=SUCCEEDED(DwmGetWindowAttribute(h,20,&style.dark,sizeof(style.dark)));style.savedBorder=SUCCEEDED(DwmGetWindowAttribute(h,34,&style.border,sizeof(style.border)));dwmStyles[h]=style;
    if(style.savedDark){BOOL dark=TRUE;DwmSetWindowAttribute(h,20,&dark,sizeof(dark));}
    if(style.savedBorder){DWORD border=RGB(64,64,64);DwmSetWindowAttribute(h,34,&border,sizeof(border));}
}
void RestoreMenuDwmStyle(HWND h){auto found=dwmStyles.find(h);if(found==dwmStyles.end())return;auto& style=found->second;if(style.savedDark)DwmSetWindowAttribute(h,20,&style.dark,sizeof(style.dark));if(style.savedBorder)DwmSetWindowAttribute(h,34,&style.border,sizeof(style.border));}

void PaintMenuFrame(HWND h,HDC target=nullptr){
    if(!ThemeIsDark())return;RECT window{},client{};GetWindowRect(h,&window);GetClientRect(h,&client);POINT origin{};ClientToScreen(h,&origin);
    int width=window.right-window.left,height=window.bottom-window.top;if(width<4||height<4)return;
    int left=std::max(2,int(origin.x-window.left)),top=std::max(2,int(origin.y-window.top));
    int right=std::max(2,int(window.right-origin.x-client.right)),bottom=std::max(2,int(window.bottom-origin.y-client.bottom));
    auto dc=target?target:GetWindowDC(h);if(!dc)return;
    RECT edges[]{{0,0,width,top},{0,height-bottom,width,height},{0,top,left,height-bottom},{width-right,top,width,height-bottom}};
    for(auto& edge:edges)FillRect(dc,&edge,ThemeBrush());
    RECT border{0,0,width,height};auto brush=CreateSolidBrush(RGB(64,64,64));FrameRect(dc,&border,brush);DeleteObject(brush);
    if(!target)ReleaseDC(h,dc);
}
LRESULT CALLBACK MenuFrameProc(HWND h,UINT m,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    auto result=DefSubclassProc(h,m,wp,lp);
    if(m==WM_NCPAINT||m==WM_PAINT||m==WM_WINDOWPOSCHANGED||m==WM_NCACTIVATE)PaintMenuFrame(h);
    else if(m==WM_PRINT&&(lp&PRF_NONCLIENT))PaintMenuFrame(h,reinterpret_cast<HDC>(wp));
    if(m==WM_NCDESTROY){RemoveWindowSubclass(h,MenuFrameProc,id);menuWindows.erase(std::remove(menuWindows.begin(),menuWindows.end(),h),menuWindows.end());dwmStyles.erase(h);}
    return result;
}
LRESULT CALLBACK MenuHook(int code,WPARAM wp,LPARAM lp){
    if(code>=0){auto message=reinterpret_cast<CWPSTRUCT*>(lp);if(message->message==WM_CREATE||message->message==WM_NCPAINT||message->message==WM_WINDOWPOSCHANGED){wchar_t type[64]{};GetClassNameW(message->hwnd,type,64);
        if(wcscmp(type,L"#32768")==0&&std::find(menuWindows.begin(),menuWindows.end(),message->hwnd)==menuWindows.end()&&SetWindowSubclass(message->hwnd,MenuFrameProc,5152,0)){menuWindows.push_back(message->hwnd);SetMenuDwmStyle(message->hwnd);}
    }}return CallNextHookEx(nullptr,code,wp,lp);
}
}
MenuThemeScope::MenuThemeScope(){if(watching||!ThemeIsDark())return;hook=SetWindowsHookExW(WH_CALLWNDPROC,MenuHook,nullptr,GetCurrentThreadId());if(hook)watching=true;}
MenuThemeScope::~MenuThemeScope(){if(!hook)return;UnhookWindowsHookEx(hook);for(auto window:menuWindows)if(IsWindow(window)){RestoreMenuDwmStyle(window);RemoveWindowSubclass(window,MenuFrameProc,5152);}menuWindows.clear();dwmStyles.clear();watching=false;}

void RefreshMenuFrame(HWND h){if(h&&IsWindow(h))PaintMenuFrame(h);}
