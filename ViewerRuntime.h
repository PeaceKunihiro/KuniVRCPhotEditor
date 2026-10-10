#pragma once
#define NOMINMAX
#include "MetadataModel.h"
#include "Stage3.h"
#include "MetadataTests.h"
#include "WebPImage.h"
#include "ThumbnailCache.h"
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
enum { PathEdit = 101, OpenPath, Browse, History, Placement, Images, Details, MetadataTabs, Options, Bookmarks, EditMetadata, SaveMetadata, CancelMetadata, SearchEdit, SearchButton, Help, PersonSettings, RecordToggle, RecordIndicator };
struct Job { int kind = 0; unsigned generation = 0, selection = 0; int index = -1; std::wstring path, preserve; unsigned thumbnailGeneration = 0; int width = 128; MetadataEdit edit; std::wstring fingerprint, destination, sourceKey; bool overwrite=false, autoBackup=true, metadataSearch=false; int backupLimit=10; unsigned searchGeneration=0; std::vector<std::wstring> files; std::vector<BackupCandidate> backups; std::wstring query; std::vector<EditableMetadata> metadata; std::vector<std::wstring> destinations; };
struct Result { Job job; std::vector<std::wstring> files; Pixels pixels; std::wstring text, error; PhotoSummary summary; EditableMetadata editable; std::wstring editError; std::vector<BackupCandidate> backups; int failures=0; bool progress=false; std::vector<EditableMetadata> metadata; };
struct State {
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
    HWND window{}, path{}, history{}, list{}, preview{}, details{}, status{}, placement{};
    HWND searchEdit{}, searchButton{};
    std::vector<std::wstring> allFiles;
    std::wstring appliedQuery;
    bool metadataSearch=false, searching=false, autoBackup=true, darkTheme=false, backupBusy=false;
    int backupLimit=10;
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
    std::function<int()> confirmChanges;
    std::function<std::wstring()> chooseSavePath;
    std::function<void(const std::wstring&)> photosRequest;
    std::atomic<unsigned> metadataDelayMs{0};
    PhotoSummary summary;
    HIMAGELIST thumbs{};
    std::vector<std::wstring> files, recent;
    std::vector<std::wstring> bookmarks;
    int cacheMB = 100, slotLimit = ThumbnailSlots;
    size_t imageBytes = 0;
    bool saveHistory = true;
    bool thumbnailBudgetLimited = false;
    std::vector<bool> thumbnailDone, thumbnailFailed;
    std::vector<bool> thumbnailRequested;
    std::vector<int> itemSlots, slotOwners;
    std::vector<std::wstring> thumbnailKeys;
    bool previewFailed=false;
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
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::deque<Result> results;
    std::thread worker;
    std::vector<std::thread> thumbnailWorkers;
    ThumbnailCache cache;
    std::atomic<unsigned> currentGeneration{0}, currentSelection{0};
    std::atomic<unsigned> currentThumbnailGeneration{0};
    std::atomic<bool> stopping{false};
    std::atomic<unsigned> activeWorkers{0};
};
extern State app;
Job ThumbnailJob(int index);
void LaunchSelectedPhotos();
void RequestPhotos(int index);
Pixels DisplayedThumbnail(int index);
INT_PTR ShowCacheLimitDialog(int& mb);
void ShowSummary(const PhotoSummary& summary, const std::wstring& message = L"");
std::wstring ErrorText(HRESULT hr);
std::wstring Text(HWND control);
void Status(const std::wstring& message);
void SetActivity(const std::wstring& text);
void StopActivity();
void BatchProgress(const Job& job,const std::wstring& text);
void Counts(const std::wstring& message = L"");
bool Same(const std::wstring& a, const std::wstring& b);
std::wstring Normalize(const std::wstring& value);
void RefreshHistory();
int ReadThumbnailWidth(const JsonObject& object);
void LoadSettings();
void SaveSettings();
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
void OpenFolder(const std::wstring& path, bool reload = false, const std::wstring& preserveOverride = L"");
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
void UpdateEditorControls();
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
void ShowVersion();
void ApplyTheme();
COLORREF ThemeBackground();
COLORREF ThemeForeground();
HBRUSH ThemeBrush();
LRESULT ThemeControlColor(HDC dc);
LRESULT ThemeNotify(NMHDR* header);
bool ThemeDrawItem(DRAWITEMSTRUCT* draw);

bool TestBackupCheckboxes();

struct WorkerLifetime { ~WorkerLifetime(){--app.activeWorkers;} };
