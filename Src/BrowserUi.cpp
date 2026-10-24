#include "ViewerRuntime.h"
#include <windowsx.h>
#pragma comment(lib,"msimg32.lib")
namespace {
struct Artwork {HBITMAP bitmap=nullptr,night=nullptr;UINT width=0,height=0;RECT source{};~Artwork(){if(bitmap)DeleteObject(bitmap);if(night)DeleteObject(night);}};
Artwork& Art(int resource){
    static Artwork icons[10];auto& art=icons[resource-2001];if(art.bitmap)return art;
    auto module=GetModuleHandleW(nullptr);auto found=FindResourceW(module,MAKEINTRESOURCEW(resource),RT_RCDATA);if(!found)return art;
    auto data=LockResource(LoadResource(module,found));auto length=SizeofResource(module,found);ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(data),length))||FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))||FAILED(decoder->GetFrame(0,&frame)))return art;
    auto pixels=Decode(factory.Get(),frame.Get(),256,256);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=pixels.width;info.bmiHeader.biHeight=-LONG(pixels.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;
    art.bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(art.bitmap){memcpy(bits,pixels.data.data(),pixels.data.size());if(resource>=2006){void* inverted=nullptr;art.night=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&inverted,nullptr,0);if(art.night){auto dest=static_cast<BYTE*>(inverted);for(size_t i=0;i<pixels.data.size();i+=4){BYTE alpha=pixels.data[i+3];for(int c=0;c<3;++c)dest[i+c]=BYTE(std::max(0,int(alpha)-int(pixels.data[i+c]))*235/255);dest[i+3]=alpha;}}}art.width=pixels.width;art.height=pixels.height;art.source={0,0,LONG(art.width),LONG(art.height)};
        if(resource!=2001){RECT bounds{LONG(art.width),LONG(art.height),0,0};for(UINT y=0;y<art.height;++y)for(UINT x=0;x<art.width;++x)if(pixels.data[(size_t(y)*art.width+x)*4+3]){bounds.left=std::min(bounds.left,LONG(x));bounds.top=std::min(bounds.top,LONG(y));bounds.right=std::max(bounds.right,LONG(x+1));bounds.bottom=std::max(bounds.bottom,LONG(y+1));}if(bounds.right>bounds.left&&bounds.bottom>bounds.top)art.source=bounds;}
    }return art;
}
void PaintArt(HDC target,RECT rect,int resource,BYTE opacity=255){auto& art=Art(resource);if(!art.bitmap)return;auto source=art.source;int sw=source.right-source.left,sh=source.bottom-source.top;double scale=std::min(double(rect.right-rect.left)/sw,double(rect.bottom-rect.top)/sh);int w=int(sw*scale),h=int(sh*scale);HDC dc=CreateCompatibleDC(target);auto old=SelectObject(dc,resource>=2006&&ThemeIsDark()&&art.night?art.night:art.bitmap);BLENDFUNCTION blend{AC_SRC_OVER,0,opacity,AC_SRC_ALPHA};AlphaBlend(target,rect.left+(rect.right-rect.left-w)/2,rect.top+(rect.bottom-rect.top-h)/2,w,h,dc,source.left,source.top,sw,sh,blend);SelectObject(dc,old);DeleteDC(dc);}
}
BrowserItemKind ItemKind(int index){return index>=0&&index<int(app.files.size())&&app.folderItems.count(app.files[index])?BrowserItemKind::Folder:BrowserItemKind::Image;}
int SelectedImageCount(){if(app.selectionCountsDirty){app.selectedImageCount=app.selectedFolderCount=0;std::unordered_set<std::wstring> seen;int i=-1;if(app.list)while((i=ListView_GetNextItem(app.list,i,LVNI_SELECTED))>=0){if(IsFolderItem(i))++app.selectedFolderCount;else if(seen.insert(PathKeyForMdb(app.files[i])).second)++app.selectedImageCount;}app.selectionCountsDirty=false;}return app.selectedImageCount;}
namespace {
LRESULT CALLBACK ArrowKeys(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR){if(message==WM_KEYDOWN&&wp==VK_RETURN){if(!(lp&(1LL<<30)))SendMessageW(window,BM_CLICK,0,0);return 0;}if(message==WM_NCDESTROY)RemoveWindowSubclass(window,ArrowKeys,id);return DefSubclassProc(window,message,wp,lp);}
}
void CreateBrowserTooltips(){auto tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,app.window,nullptr,GetModuleHandleW(nullptr),nullptr);for(int id:{HistoryBack,HistoryForward,OpenPath,Bookmarks,Options,Help,SortMenu,Browse}){auto button=AppControl(id);SetWindowSubclass(button,ArrowKeys,423,0);TOOLINFOW info{sizeof(info)};info.uFlags=TTF_IDISHWND|TTF_SUBCLASS;info.hwnd=app.window;info.uId=reinterpret_cast<UINT_PTR>(button);info.lpszText=const_cast<wchar_t*>(id==OpenPath?L"ロード":id==HistoryBack?L"戻る (Alt+←)":id==HistoryForward?L"進む (Alt+→)":id==Bookmarks?L"ブックマーク":id==Options?L"オプション":id==Help?L"ヘルプ":id==SortMenu?L"並べ替え":L"フォルダー");SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info));}}
bool IsFolderItem(int index){return ItemKind(index)==BrowserItemKind::Folder;}
bool ShowImage(const std::wstring& path){return app.showBackupImages||!SearchContains(fs::path(path).filename().wstring(),L"AB_");}
std::vector<std::wstring> VisibleImages(){std::vector<std::wstring> images;std::unordered_set<std::wstring> seen;for(size_t i=0;i<app.files.size();++i)if(!IsFolderItem(int(i))&&seen.insert(PathKeyForMdb(app.files[i])).second)images.push_back(app.files[i]);return images;}
void CommitNavigation(const std::wstring& path,int position){
    if(position>=0&&position<int(app.navigation.size())&&Same(app.navigation[position],path))app.navigationIndex=position;
    else if(app.navigationIndex<0||app.navigationIndex>=int(app.navigation.size())||!Same(app.navigation[app.navigationIndex],path)){
        app.navigation.resize(size_t(app.navigationIndex+1));app.navigation.push_back(path);if(app.navigation.size()>HistoryLimit)app.navigation.erase(app.navigation.begin());app.navigationIndex=int(app.navigation.size())-1;
    }UpdateNavigationControls();
}
void UpdateNavigationControls(){bool ready=!app.folder.empty()&&!app.folderPending&&!app.saving&&!app.backupBusy&&!app.sorting&&!app.batchLoading;EnableWindow(AppControl(HistoryBack),ready&&app.navigationIndex>0);EnableWindow(AppControl(HistoryForward),ready&&app.navigationIndex>=0&&app.navigationIndex+1<int(app.navigation.size()));}
void NavigateHistory(int direction){if(!IsWindowEnabled(app.window))return;UpdateNavigationControls();auto button=AppControl(direction<0?HistoryBack:HistoryForward);if(!IsWindowEnabled(button))return;int position=app.navigationIndex+(direction<0?-1:1);OpenFolder(app.navigation[position],false,L"",true,position);}
void OpenBrowserItem(int index){if(app.folderPending)return;if(index<0||index>=int(app.files.size()))return;if(IsFolderItem(index))OpenFolder(app.files[index]);else RequestPhotos(index);}
void ToggleBackupImages(){if(app.folderPending||app.saving||app.backupBusy||app.sorting||app.batchLoading)return;FlushSelectionUpdate();if(!GuardTransition([]{ToggleBackupImages();}))return;auto preserve=app.selected>=0?app.files[app.selected]:L"";app.restoreSelection=SelectedFiles();auto query=app.searching?app.searchQuery:app.appliedQuery;CancelSearch();app.showBackupImages=!app.showBackupImages;SaveSettings();if(query.empty()){app.appliedQuery.clear();CommitDisplay(app.allFiles,preserve);}else StartSearch(query,preserve);}
void DrawFolderIcon(HDC dc,int index){RECT r{};if(ListView_GetItemRect(app.list,index,&r,LVIR_ICON)){InflateRect(&r,-4,-4);auto label=r;if(app.hideFileNames)r.bottom=std::max(r.top+1,r.bottom-18);PaintArt(dc,r,2001);if(app.hideFileNames){label.top=label.bottom-18;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ThemeForeground());auto name=fs::path(app.files[index]).filename().wstring();DrawTextW(dc,name.c_str(),-1,&label,DT_SINGLELINE|DT_CENTER|DT_END_ELLIPSIS|DT_NOPREFIX);}}}
bool DrawBrowserButton(DRAWITEMSTRUCT* draw){
    int resource=draw->CtlID==OpenPath?2004:draw->CtlID==HistoryBack?2002:draw->CtlID==HistoryForward?2003:draw->CtlID==Bookmarks?2006:draw->CtlID==Options?2007:draw->CtlID==Help?2008:draw->CtlID==SortMenu?2009:draw->CtlID==Browse?2010:0;
    if(!resource)return false;auto r=draw->rcItem;
    auto brush=CreateSolidBrush(draw->itemState&ODS_SELECTED?(ThemeIsDark()?RGB(65,75,85):RGB(215,225,235)):ThemeBackground());if(UsesBackdrop(draw->hwndItem))PaintBackdrop(draw->hwndItem,draw->hDC,r);else FillRect(draw->hDC,&r,brush);DeleteObject(brush);FrameRect(draw->hDC,&r,GetSysColorBrush(COLOR_GRAYTEXT));InflateRect(&r,-4,-4);PaintArt(draw->hDC,r,resource,draw->itemState&ODS_DISABLED?70:255);if(draw->itemState&ODS_FOCUS)DrawFocusRect(draw->hDC,&r);return true;
}
bool BrowserInput(const MSG& message){if(message.hwnd!=app.window&&!IsChild(app.window,message.hwnd))return false;if(!IsWindowEnabled(app.window))return false;int direction=0;if(message.message==WM_SYSKEYDOWN&&(message.lParam&(1LL<<29))){if(message.wParam==VK_LEFT)direction=-1;if(message.wParam==VK_RIGHT)direction=1;}if(message.message==WM_KEYDOWN){if(message.wParam==VK_BROWSER_BACK)direction=-1;if(message.wParam==VK_BROWSER_FORWARD)direction=1;}if(message.message==WM_XBUTTONUP)direction=GET_XBUTTON_WPARAM(message.wParam)==XBUTTON1?-1:1;if(!direction)return false;if(!(message.message!=WM_XBUTTONUP&&(message.lParam&(1LL<<30))))NavigateHistory(direction);return true;}

std::wstring FolderPickerStart(){for(auto& candidate:std::vector<std::wstring>{Text(app.path),app.folder})try{if(!candidate.empty()){auto normalized=Normalize(candidate);std::error_code error;if(fs::is_directory(normalized,error))return normalized;}}catch(...){}return {};}
void ToggleFileNames(){
    FlushSelectionUpdate();int focused=ListView_GetNextItem(app.list,-1,LVNI_FOCUSED);POINT scroll{};ListView_GetOrigin(app.list,&scroll);auto previous=app.hideFileNames;auto generation=app.thumbnailGeneration;
    app.hideFileNames=!previous;ApplyThumbnailSize(true);
    if(generation==app.thumbnailGeneration){app.hideFileNames=previous;return;}
    Layout();if(focused>=0)ListView_EnsureVisible(app.list,focused,FALSE);else ListView_Scroll(app.list,0,scroll.y);
    ScheduleThumbnails();
}

void DrawTileSelection(HDC dc,int index){if(!app.hideFileNames||!(ListView_GetItemState(app.list,index,LVIS_SELECTED)&LVIS_SELECTED))return;RECT rect{};if(!ListView_GetItemRect(app.list,index,&rect,LVIR_ICON))return;InflateRect(&rect,-1,-1);auto brush=CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));FrameRect(dc,&rect,brush);InflateRect(&rect,-1,-1);FrameRect(dc,&rect,brush);DeleteObject(brush);}

void DrawMdbBadge(HDC dc,int index){if(!MdbDuplicateRow(index))return;RECT r{};if(!ListView_GetItemRect(app.list,index,&r,LVIR_ICON))return;int side=std::clamp(int((r.right-r.left)/4),16,32);r.left=r.right-side-2;r.top=r.bottom-side-2;r.right-=2;r.bottom-=2;PaintArt(dc,r,2005);}
