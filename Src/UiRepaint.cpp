#include "ViewerRuntime.h"
namespace {
unsigned depth=0;HWND root=nullptr;uint64_t suppressed=0;
void Guard(HWND);
LRESULT CALLBACK LayoutPaintGuard(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){
    if(depth&&(h==root||IsChild(root,h))){
        if(message==WM_PARENTNOTIFY&&LOWORD(wp)==WM_CREATE)Guard(reinterpret_cast<HWND>(lp));
        if(message==WM_WINDOWPOSCHANGING&&h!=root){auto pos=reinterpret_cast<WINDOWPOS*>(lp);pos->flags|=SWP_NOCOPYBITS|SWP_NOREDRAW;}
        if(message==WM_PAINT){++suppressed;ValidateRect(h,nullptr);return 0;}
        if(message==WM_ERASEBKGND||message==WM_NCPAINT||message==WM_SYNCPAINT){++suppressed;return message==WM_ERASEBKGND?1:0;}
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(h,LayoutPaintGuard,id);
    return DefSubclassProc(h,message,wp,lp);
}
void Guard(HWND h){
    if(!h)return;
    // Keep this guard before theme and control-specific WM_PAINT handlers.
    RemoveWindowSubclass(h,LayoutPaintGuard,5151);SetWindowSubclass(h,LayoutPaintGuard,5151,0);
    if(h!=root){auto style=GetWindowLongPtrW(h,GWL_STYLE);if(!(style&WS_CLIPSIBLINGS))SetWindowLongPtrW(h,GWL_STYLE,style|WS_CLIPSIBLINGS);}
}
}
bool UiRepaintActive(){return depth!=0;}
uint64_t UiRepaintSuppressed(){return suppressed;}
UiRepaintBatch::UiRepaintBatch(HWND window){
    if(!window||!IsWindow(window))return;joined=true;
    if(depth++){if(window!=root&&IsChild(window,root)){root=window;Guard(window);EnumChildWindows(window,[](HWND h,LPARAM){Guard(h);return TRUE;},0);}return;}root=window;Guard(window);EnumChildWindows(window,[](HWND h,LPARAM){Guard(h);return TRUE;},0);
}
UiRepaintBatch::~UiRepaintBatch(){
    if(!joined||--depth)return;auto window=root;root=nullptr;
    // Asynchronous invalidation coalesces repeated resize/scroll events. No global redraw lock.
    if(IsWindow(window))RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_FRAME);
}
