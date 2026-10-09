#include "ViewerRuntime.h"
bool IsPhotosId(const std::wstring& id) {
    const std::wstring family = L"Microsoft.Windows.Photos_8wekyb3d8bbwe!";
    return id.size() > family.size() && _wcsnicmp(id.c_str(), family.c_str(), family.size()) == 0;
}
HRESULT FindPhotosAppId(std::wstring& id) {
    ComPtr<IShellItem> apps;
    auto hr = SHCreateItemInKnownFolder(FOLDERID_AppsFolder, 0, nullptr, IID_PPV_ARGS(&apps));
    if (FAILED(hr)) return hr;
    ComPtr<IEnumShellItems> items;
    hr = apps->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items));
    if (FAILED(hr)) return hr;
    for (;;) {
        ComPtr<IShellItem> item; ULONG fetched = 0;
        hr = items->Next(1, &item, &fetched);
        if (hr != S_OK) return FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        ComPtr<IShellItem2> properties;
        if (FAILED(item.As(&properties))) continue;
        PWSTR value = nullptr;
        if (SUCCEEDED(properties->GetString(PKEY_AppUserModel_ID, &value))) {
            std::wstring candidate(value); CoTaskMemFree(value);
            if (IsPhotosId(candidate)) { id = candidate; return S_OK; }
        }
    }
}
HRESULT ActivatePhotos(const std::wstring& path, std::wstring& detail, HRESULT (*findApp)(std::wstring&)) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return HRESULT_FROM_WIN32(GetLastError());
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return E_INVALIDARG;
    std::wstring id;
    auto hr = findApp(id);
    if (FAILED(hr)) { detail = L"Windowsフォトを検出できません。Microsoftフォトのインストール状態を確認してください。\r\n"; return hr; }
    try {
        // Await on the MTA worker. The package target does not change associations.
        auto file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(path).get();
        winrt::Windows::System::LauncherOptions options;
        options.TargetApplicationPackageFamilyName(id.substr(0, id.find(L'!')));
        options.DisplayApplicationPicker(false);
        return winrt::Windows::System::Launcher::LaunchFileAsync(file, options).get() ? S_OK : E_FAIL;
    } catch (const winrt::hresult_error& error) { return error.code(); }
}

void RequestPhotos(int index){
    if(index<0||index>=static_cast<int>(app.files.size()))return;
    if(app.editing&&app.editorUi.Read().Changed())MessageBoxW(app.window,L"Windowsフォトへ渡すのは保存済み画像です。未保存の編集内容は反映されません。",L"未保存の変更",MB_OK|MB_ICONINFORMATION);
    Counts(L"Windowsフォトを起動しています…");
    if(app.photosRequest)app.photosRequest(app.files[index]);else Queue({3,app.generation,0,index,app.files[index],L""},true);
}
void LaunchSelectedPhotos(){RequestPhotos(app.selected);}

