#include "ViewerRuntime.h"
namespace {
struct TipState {bool initialized=false,recording=false,enabled=true;} tipState;
TOOLINFOW RecordingTool(){TOOLINFOW tool{sizeof(tool)};tool.hwnd=app.window;tool.uId=reinterpret_cast<UINT_PTR>(GetDlgItem(app.window,RecordToggle));tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.lpszText=LPSTR_TEXTCALLBACKW;return tool;}
}
void CreateRecordingTooltip(){
    app.recordTooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,app.window,nullptr,GetModuleHandleW(nullptr),nullptr);
    auto tool=RecordingTool();SendMessageW(app.recordTooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
    SendMessageW(app.recordTooltip,TTM_SETDELAYTIME,TTDT_INITIAL,3000);
    SendMessageW(app.recordTooltip,TTM_SETDELAYTIME,TTDT_RESHOW,3000);
    SendMessageW(app.recordTooltip,TTM_SETDELAYTIME,TTDT_AUTOPOP,10000);
    tipState={};UpdateRecordingTooltip(stage3::Recording(),!stage3::Stopping());
}
void UpdateRecordingTooltip(bool recording,bool enabled){
    if(!app.recordTooltip)return;
    enabled=enabled&&IsWindowEnabled(app.window);
    if(tipState.initialized&&tipState.recording==recording&&tipState.enabled==enabled)return;
    tipState={true,recording,enabled};
    SendMessageW(app.recordTooltip,TTM_POP,0,0);
    auto tool=RecordingTool();SendMessageW(app.recordTooltip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));
}
bool RecordingTooltipNotify(NMHDR* header){
    if(header->hwndFrom!=app.recordTooltip||header->code!=TTN_GETDISPINFOW)return false;
    auto info=reinterpret_cast<NMTTDISPINFOW*>(header);
    bool enabled=tipState.enabled&&IsWindowEnabled(app.window)&&IsWindowEnabled(GetDlgItem(app.window,RecordToggle));
    info->lpszText=const_cast<wchar_t*>(!enabled?L"":tipState.recording?L"写真へ追加情報の記録を停止します":L"写真へ追加情報の記録を開始します");
    return true;
}
