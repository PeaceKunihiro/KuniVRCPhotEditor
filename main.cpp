#include "ViewerRuntime.h"
#include "resource.h"
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES }; InitCommonControlsEx(&controls);
    LoadSettings();
    if(std::wstring(command).find(L"--stage3-evaluate")==0){int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(count!=6){LocalFree(args);return 2;}auto log=fs::path(args[2]),photos=fs::path(args[3]),samples=fs::path(args[4]),output=fs::path(args[5]);LocalFree(args);int code=0;std::thread probe([&]{auto init=RoInitialize(RO_INIT_MULTITHREADED);std::ofstream report(output);try{winrt::check_hresult(init);stage3::EvaluateReferences(log,photos,samples,report);report<<"EVALUATION COMPLETED\n";}catch(const winrt::hresult_error& e){report<<"FAILED HRESULT="<<std::hex<<static_cast<unsigned>(e.code())<<'\n';code=1;}catch(const std::exception& e){report<<"FAILED: "<<e.what()<<'\n';code=1;}if(SUCCEEDED(init))RoUninitialize();});probe.join();winrt::uninit_apartment();return code;}
    if(std::wstring(command)==L"--ocr-test"){auto folder=app.settings.parent_path()/L"self-test-data";fs::create_directories(folder);std::ofstream report(folder/L"ocr-result.txt");int code=0;try{person_ocr::RunCaptureTests(report);}catch(const std::exception& e){report<<"FAIL: "<<e.what()<<'\n';code=1;}winrt::uninit_apartment();return code;}
    if (std::wstring(command) == L"--self-test") { auto result = SelfTest(); winrt::uninit_apartment(); return result; }
    if (std::wstring(command) == L"--photos-test") { auto result = PhotosTest(); winrt::uninit_apartment(); return result; }
    bool shutdownTest=std::wstring(command)==L"--shutdown-test";
    bool uiTest=shutdownTest||std::wstring(command)==L"--ui-test";
    if (uiTest) {
        app.settings = app.settings.parent_path() / L"self-test-data" / L"ui-settings.json";
        fs::create_directories(app.settings.parent_path());
        app.last.clear(); app.recent.clear(); app.settingsError.clear(); app.settingsProtected = false;
        app.autoBackup=true;app.backupLimit=10;app.darkTheme=false;app.metadataSearch=false;
        app.layout = L"right"; app.thumbnailWidth = app.activeThumbnailWidth = 128; app.cacheMB = 100; app.saveHistory = true; app.bookmarks.clear();
    }
    WNDCLASSW preview{}; preview.style=CS_DBLCLKS; preview.lpfnWndProc = PreviewProc; preview.hInstance = instance; preview.lpszClassName = L"KuniPreview"; preview.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&preview);
    WNDCLASSW simple{}; simple.lpfnWndProc = SimpleProc; simple.hInstance = instance; simple.lpszClassName = L"KuniSimple"; simple.hCursor = LoadCursorW(nullptr, IDC_ARROW); simple.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); RegisterClassW(&simple);
    WNDCLASSW main{}; main.hIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED)); main.lpfnWndProc = WindowProc; main.hInstance = instance; main.lpszClassName = L"KuniVRCPhotEditor"; main.hCursor = LoadCursorW(nullptr, IDC_ARROW); main.hbrBackground = GetSysColorBrush(COLOR_BTNFACE); RegisterClassW(&main);
    HWND window = CreateWindowExW(0, main.lpszClassName, L"KuniVRCPhotEditor v0.4.3 — 画像メタデータビューアー（開発版）", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 800, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED)));
    SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(main.hIcon));
    if(shutdownTest){SendMessageW(window,WM_CLOSE,0,0);int result=app.closing&&app.activeWorkers==0?0:1;winrt::uninit_apartment();return result;}
    if(uiTest){int result=UiTest();SendMessageW(window,WM_CLOSE,0,0);if(!app.closing||app.activeWorkers)result=1;winrt::uninit_apartment();return result;}
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {

        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && message.hwnd == app.path) { LoadAddress(Text(app.path)); continue; }
        if (message.message == WM_MOUSEWHEEL) {
            // Route by pointer location even when another control has keyboard focus.
            POINT point{ static_cast<short>(LOWORD(message.lParam)), static_cast<short>(HIWORD(message.lParam)) };
            HWND hovered = WindowFromPoint(point);
            if (hovered == app.simple || IsChild(app.simple, hovered)) { SendMessageW(app.simple, WM_MOUSEWHEEL, message.wParam, message.lParam); continue; }
            if (hovered == app.list || hovered == app.preview || hovered == app.details) {
                SendMessageW(hovered, WM_MOUSEWHEEL, message.wParam, message.lParam); continue;
            }
        }
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    winrt::uninit_apartment(); return static_cast<int>(message.wParam);
}
