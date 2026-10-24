#pragma once
#define NOMINMAX
#include "MetadataModel.h"
#include "UiFont.h"
#include "Backdrop.h"
#include "UiRepaint.h"
#include "Stage3.h"
#include "OcrRuntime.h"
#include "MetadataTests.h"
#include "WebPImage.h"
#include "ThumbnailCache.h"
#include "DiskThumbnailCache.h"
#include "MdbStore.h"
#include "IdDisplayNames.h"
#include "MdbApply.h"
#include <map>
#include "WebPTests.h"
#include "ViewerSettings.h"
#include "ExtendedXmpTests.h"
#include "MetadataWriter.h"
#include "MetadataEditorUi.h"
#include "BackupManager.h"
#include <functional>
#include <windows.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <propkey.h>
#include <wincodec.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <sstream>
#include <vector>
#include <memory>
#include <unordered_set>
#include <cmath>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
using namespace winrt::Windows::Data::Json;
constexpr UINT WM_RESULTS = WM_APP + 1;
constexpr UINT WM_SELECTION = WM_APP + 2;
constexpr size_t HistoryLimit = 10;
constexpr int ThumbnailMin = 64, ThumbnailMax = 256, ThumbnailStep = 32;
constexpr UINT_PTR ZoomTimer = 1;
constexpr UINT_PTR ThumbnailTimer = 2;
constexpr UINT_PTR ActivityTimer = 3;
constexpr UINT_PTR RecordTimer=4;
constexpr int ThumbnailSlots = 512;
enum { PathEdit = 101, OpenPath, Browse, History, Placement, Images, Details, MetadataTabs, Options, Bookmarks, EditMetadata, SaveMetadata, CancelMetadata, SearchEdit, SearchButton, Help, PersonSettings, RecordToggle, RecordIndicator, ResetView, SortMenu, HistoryBack, HistoryForward };
struct Job { int kind = 0; unsigned generation = 0, selection = 0; int index = -1; std::wstring path, preserve; unsigned thumbnailGeneration = 0; int width = 128; MetadataEdit edit; std::wstring fingerprint, destination, sourceKey; bool userLoad=true, overwrite=false, autoBackup=true, metadataSearch=false; int backupLimit=10; unsigned searchGeneration=0; std::vector<std::wstring> files; std::vector<BackupCandidate> backups; std::wstring query; std::vector<EditableMetadata> metadata; std::vector<std::wstring> destinations; bool hideFileNames=false; uint64_t updateAttemptUtc=0,updateSuccessUtc=0;
    int categoryMode=0; int sortMode=0; int navigationIndex=-1; bool clearSearch=false,diskPrefetch=false,useMdb=false; unsigned diskGeneration=0,applyGeneration=0,applyDelayMs=0; };
struct Result { std::vector<std::wstring> directories; Job job; std::vector<std::wstring> files; Pixels pixels; std::wstring text, error; PhotoSummary summary; EditableMetadata editable; std::wstring editError; std::vector<BackupCandidate> backups; int failures=0; bool progress=false; std::vector<EditableMetadata> metadata; std::vector<MdbRow> mdbRows;MdbRecord mdbRecord;std::vector<MdbRecord> personRecords; };
struct State {
    bool mdbEnabled=false,categoryCommitting=false;int mdbMode=0,mdbCapacityMB=100,categoryMode=0,displayCategoryMode=0;
    IdNameStore idNames;
    std::vector<MdbRecord> personRecords;
    std::vector<std::pair<std::wstring,std::wstring>> personChoices;
    std::wstring priorityPerson;bool collapseReset=true;
    std::vector<int> wantedItems;
    MetadataDb mdb;std::vector<MdbRow> mdbRows;std::map<std::wstring,bool> mdbSelectionChanges;
    bool selectionCountsDirty=true;int selectedImageCount=0,selectedFolderCount=0;
    bool hideFileNames=false; uint64_t updateAttemptUtc=0,updateSuccessUtc=0;
    int sortMode=0; bool sorting=false,idLinks=true,showBackupImages=true;
    std::vector<std::wstring> directories,navigation; std::unordered_set<std::wstring> folderItems; int navigationIndex=-1;
    std::vector<std::wstring> batchFiles, restoreSelection, batchCompleted;
    std::vector<EditableMetadata> batchMetadata;
    bool batchLoading=false, updatingSelection=false,selectionPending=false;
    unsigned selectionRefreshes=0;
    std::wstring activityText; unsigned activityFrame=0;
    std::atomic<unsigned> batchReadActive{0},batchReadPeak{0};
    std::atomic<bool> cancelBatch{false};
    std::atomic<unsigned> thumbnailActive{0},thumbnailPeak{0};
    std::function<std::wstring()> chooseBatchFolder;
    std::function<bool(const Job&)> confirmBatch;
    HWND side{};bool sideBuilding=false,sideLayout=false,sideMeasuring=false,simpleLayoutDirty=true;int sideScroll=0,sideContent=0,sideWheel=0;std::array<HWND,4> verticalTabs{};
    std::wstring fontName;int fontPt=12;
    std::wstring backgroundPath;bool backgroundLoading=false;std::atomic<unsigned> backgroundGeneration{0};
    HWND window{}, path{}, history{}, list{}, preview{}, details{}, status{}, placement{};
    HWND searchEdit{}, searchButton{},recordTooltip{};
    std::vector<std::wstring> allFiles;
    std::wstring appliedQuery,searchQuery;
    bool metadataSearch=false, searching=false, autoBackup=true, darkTheme=false, backupBusy=false;
    int backupLimit=10; bool autoThumbnails=true; int sideWidth=400,sideHeight=300,manualThumbnailWidth=128; RECT splitter{}; bool draggingSplitter=false;
    unsigned searchGeneration=0;std::atomic<unsigned> currentSearchGeneration{0};
    std::thread searchWorker;
    std::function<int()> chooseSaveMode;
    HWND metadataTabs{}, simple{}, simpleNote{};
    std::array<HWND, 11> simpleLabels{}, simpleValues{};
    int simpleScroll = 0, simpleWheel = 0, simpleContentHeight = 0;
    HWND editButton{}, saveButton{}, cancelButton{}, editStatus{};
    MetadataEditorUi editorUi; EditableMetadata editable;
    bool editing=false, saving=false, metadataReady=false, transitionBypass=false;
    std::wstring editError; std::function<void()> afterSave;
    std::function<int()> confirmChanges;std::function<bool()> confirmHighOcr;
    std::function<std::wstring()> chooseSavePath;
    std::function<void(const std::wstring&)> photosRequest;
    std::atomic<unsigned> metadataDelayMs{0};
    PhotoSummary summary; std::function<void(const std::wstring&)> summaryLinkLauncher;
    HIMAGELIST thumbs{};
    std::vector<std::wstring> files, recent;
    std::vector<std::wstring> bookmarks;
    int cacheMB = 100, slotLimit = ThumbnailSlots;
    size_t imageBytes = 0;
    bool residentMode=false,residentOcr=false;
    bool bookmarkCache=false,diskCache=false; int diskCacheMiB=512,diskCacheMinutes=30,diskCacheVisits=5;
    size_t diskPrefetchCursor=0,diskPrefetchCount=0;std::atomic<unsigned> diskGeneration{0};
    bool saveHistory = true;
    bool thumbnailBudgetLimited = false;
    std::vector<bool> thumbnailDone, thumbnailFailed;
    // UI-thread-only invalidation: layout changes do not change filename labels.
    bool imageLabelsDirty = true;
    int imageLabelWidth = 0;
    HFONT imageLabelFont = nullptr;
    std::vector<int> dirtyImageLabels;
    std::vector<bool> thumbnailRequested;
    std::vector<int> itemSlots, slotOwners;
    std::vector<std::wstring> thumbnailKeys;
    bool previewFailed=false,previewDedicated=false,previewOriginal=false,previewOriginalRequested=false;
    std::wstring previewSourceKey;
    int wantedFirst = 0, wantedLast = -1;
    std::wstring folder, last, layout = L"right", settingsError;
    fs::path settings;
    bool settingsProtected = false, closing = false;
    unsigned generation = 0, selection = 0;
    unsigned thumbnailGeneration = 0;
    int thumbnailWidth = 128, activeThumbnailWidth = 128, zoomWheel = 0;
    bool folderPending = false;
    int selected = -1, wheel = 0, loaded = 0;
    Pixels previewPixels;
    uint64_t previewRevision=0;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::deque<Result> results;
    std::thread worker;
    std::vector<std::thread> thumbnailWorkers;
    ThumbnailCache cache;
    DiskThumbnailCache disk;
    std::atomic<unsigned> currentGeneration{0}, currentSelection{0};
    std::atomic<unsigned> currentThumbnailGeneration{0};
    std::atomic<bool> stopping{false};
    std::atomic<unsigned> activeWorkers{0};
};
extern State app;
enum class BrowserItemKind {Image,Folder};
BrowserItemKind ItemKind(int); bool IsFolderItem(int); bool ShowImage(const std::wstring&);
std::vector<std::wstring> VisibleImages();
void CommitNavigation(const std::wstring&,int);void UpdateNavigationControls();void NavigateHistory(int);void OpenBrowserItem(int);void ToggleBackupImages();
int SelectedImageCount();void CreateBrowserTooltips();
void DrawFolderIcon(HDC,int);bool DrawBrowserButton(DRAWITEMSTRUCT*);bool BrowserInput(const MSG&);
Job ThumbnailJob(int index);
void LaunchSelectedPhotos();
void RequestPhotos(int index);
Pixels DisplayedThumbnail(int index);
INT_PTR ShowCacheLimitDialog(int& mb);
void RefreshSummaryLinks();
HWND AppControl(int);HWND SideHit(POINT);
void CreateSideView();void LayoutSideView();bool SideWheel(HWND,WPARAM,LPARAM);void ResetSideScroll();void EnsureSideVisible(HWND);
void ShowFontDialog(const std::function<void(HWND)>& inspect={});void SetAppFont(const std::wstring&,int,bool persist=true);
void RunV0515UiTests(const fs::path&,std::ostream&);int RepaintTest();
bool ThemeIsDark();
void RunV0514UiTests(const fs::path&,std::ostream&);int SideViewTest();
void ScheduleOriginalPreview();
void LoadOriginalPreview();
void RunV0513UiTests(const fs::path&,std::ostream&);
int PreviewTest();
void ShowSummary(const PhotoSummary& summary, const std::wstring& message = L"");
std::wstring ErrorText(HRESULT hr);
std::wstring Text(HWND control);
void Status(const std::wstring& message);
void SetActivity(const std::wstring& text);
void StopActivity();
void RefreshPreview();
void BatchProgress(const Job& job,const std::wstring& text);
void QueueProgress(Result result);
void Counts(const std::wstring& message = L"");
bool Same(const std::wstring& a, const std::wstring& b);
std::wstring Normalize(const std::wstring& value);
void RefreshHistory();
int ReadThumbnailWidth(const JsonObject& object);
void LoadSettings();
void SaveSettings(bool notify=true);
void ToggleFileNames();
std::wstring FolderPickerStart();
Pixels Decode(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame, UINT maxWidth, UINT maxHeight);
void Metadata(IWICMetadataQueryReader* reader, const std::wstring& prefix, std::wstring& output, int depth, int& count, bool& unsupported, bool& failed);
std::wstring Info(IWICBitmapDecoder* decoder, IWICBitmapFrameDecode* frame, const std::wstring& path);
void Queue(Job job, bool priority = false);
bool IsPhotosId(const std::wstring& id);
HRESULT FindPhotosAppId(std::wstring& id);
HRESULT ActivatePhotos(const std::wstring& path, std::wstring& detail, HRESULT (*findApp)(std::wstring&) = FindPhotosAppId);
bool CurrentJob(const Job& job);
void Worker(bool thumbnails = false);
void Select(int index,bool reveal=true);
void ScheduleSelectionUpdate();
void FlushSelectionUpdate();
std::vector<std::wstring> SelectedFiles();
void BeginMetadataEdit();
void StartBatchSave();
bool HandleBatchResult(Result& result);
void RunBatchJob(const Job& job,Result& result,const std::function<void(size_t)>& afterFile = {},const std::function<void(size_t)>& beforePrepare = {});
std::wstring BatchResultCounts(size_t total,size_t success,bool failed);
LRESULT CALLBACK SearchEditProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
void SubmitBackupDeletion(const std::vector<BackupCandidate>& candidates, const std::vector<std::wstring>& selected);
void ResetViewer();
std::wstring QuickHelpText();
void ShowQuickHelp(const std::function<void(HWND)>& inspect = {});
void OpenFolder(const std::wstring& path, bool reload = false, const std::wstring& preserveOverride = L"",bool userLoad=true,int navigationIndex=-1);
HBITMAP Thumbnail(const Pixels& pixels, int width = 128);
void UpdateThumbnailSpacing();
void ArrangeImages();
void ResetThumbnailTracking();
void ScheduleThumbnails();
size_t ImageListBytes(HIMAGELIST images);
HIMAGELIST CreateThumbnails(int width, int& slots, size_t& retained);
void ApplyThumbnailSize(bool force = false);
int WheelThumbnailWidth(int width, int& remainder, int delta);
LRESULT CALLBACK ImagesProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR);
void Results();
void Layout();
void LayoutSimple();
LRESULT CALLBACK SimpleProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK SummaryValueProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR);
LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
void BrowseFolder();
void LoadAddress(const std::wstring& address);
bool AddBookmark(const std::wstring& address);
void RemoveBookmark(size_t index);
INT_PTR CALLBACK CacheDialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam);
void ChangeCacheLimit(int mb);
void ToggleHistorySaving();
HMENU BuildOptionsMenu();
void OptionsMenu();
HMENU BuildBookmarkMenu();
void BookmarkMenu();
HWND Control(LPCWSTR type, LPCWSTR text, DWORD style, int id);
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
void UpdateEditorControls(const MetadataEdit* snapshot=nullptr,const std::wstring* validation=nullptr);
void StartSave();
bool GuardTransition(std::function<void()> action);
int SelfTest();
int PhotosTest();
bool ThumbnailsReady();
int UiTest();
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show);

void SearchWorker();
void StartSearch(const std::wstring& query,const std::wstring& preserve=L"");
void CancelSearch();
void CommitDisplay(std::vector<std::wstring> files,const std::wstring& preserve);
bool SearchContains(const std::wstring& text,const std::wstring& query);
void ShowBackupDeletion();
void ChooseBackupDeletion(const std::vector<BackupCandidate>& candidates);
void NewOptionsAction(int action);
void ShowVersion(const std::function<void(HWND)>& inspect = {});
void ShowDiskCacheDialog(const std::function<void(HWND)>& inspect = {});
void ConfigureDiskCache();
void ScheduleBookmarkPrefetch();
bool SetBookmarkCache(bool enabled,bool notify=true);
void ApplyTheme();
void RunV0421ThemeTests(std::ostream&,const std::function<void(const wchar_t*)>& capture = {});
COLORREF ThemeBackground();
COLORREF ThemeForeground();
HBRUSH ThemeBrush();
LRESULT ThemeControlColor(HDC dc,HWND=nullptr);
LRESULT ThemeNotify(NMHDR* header);
bool ThemeDrawItem(DRAWITEMSTRUCT* draw);

bool TestBackupCheckboxes();

struct WorkerLifetime { ~WorkerLifetime(){--app.activeWorkers;} };

void CreateOcrUi();void LayoutOcrUi(RECT page,bool visible);void RefreshOcrUi();bool OcrCommand(int id,int code);
bool DrawOcrButton(DRAWITEMSTRUCT* draw);
bool OcrImageSource();
bool OcrTestSource();std::shared_ptr<const Pixels> OcrTestPreview();std::wstring OcrTestStatus();void StopOcrUi();void SetHighPrecisionVisible(bool visible);

bool TrayMessage(HWND,UINT,WPARAM,LPARAM);void TrayUpdate();void TrayShutdown();void RunV0413UiTests(const fs::path&,std::ostream&);

void SortFiles(std::vector<std::wstring>& files,int mode,const std::function<bool()>& active = {});
void RequestSort(int mode);

int ComfortTest();

int UpdateDialogTest();

int LicenseDialogTest();

int ThumbnailHeight(int);
void CreateRecordingTooltip();
void UpdateRecordingTooltip(bool recording,bool enabled);
bool RecordingTooltipNotify(NMHDR*);
int TileTooltipTest();
void RunV053UiTests(const fs::path&,std::ostream&,const std::function<void(const wchar_t*)>&);

void DrawTileSelection(HDC,int);

int BookmarkCacheTest();

void UpdateMdbDisplayPriority();
void ConfigureMdb();void ShowMdbDialog(const std::function<void(HWND)>& inspect = {});void MdbTick();
void RequestCategory(int);void StartCategory(const std::vector<std::wstring>&,const std::wstring&);void RunCategory(const Job&,Result&);
void BuildPersonView(const std::vector<MdbRecord>&,const IdNameTable&,const std::wstring&,Result&);
void ResetCategoryView();void CaptureCategoryState();void ApplyPersonPriority(const std::wstring&);
bool CategoryWheel(HWND,WPARAM,LPARAM);
void CreateCategoryBar();void LayoutCategoryBar(RECT&);void RefreshCategoryBar();
void SetAllCategories(bool);bool MdbGroupInput(HWND,UINT,WPARAM,LPARAM);
std::vector<int> MdbVisibleItems(int,bool&);bool WantedThumbnail(int);
void RunV0512Tests(const fs::path&,std::ostream&);void RunV0512UiTests(const fs::path&,std::ostream&);
bool MdbSelectionBusy();void BeginMdbInput(HWND,UINT,WPARAM,LPARAM);void EndMdbInput(HWND,UINT);
void SetupMdbGroups();void SyncMdbSelection();void NoteMdbSelection(int,UINT);std::wstring PathKeyForMdb(const std::wstring&);bool MdbDuplicateRow(int);void DrawMdbBadge(HDC,int);
void RunV0511Tests(const fs::path&,std::ostream&);
void RunMdbTests(const fs::path&,std::ostream&);void RunMdbUiTests(const fs::path&,std::ostream&);int MdbTest();int PeopleTest();

void DrawMdbGroupHeader(HDC,int);

void PaintMdbHeaders(HWND,HDC);

void RunV0516UiTests(const fs::path&,std::ostream&);int FontLayoutTest();

void RunV060UiTests(const fs::path&,std::ostream&);int BackdropTest();

HBITMAP EmptyThumbnailMask(HBITMAP);
