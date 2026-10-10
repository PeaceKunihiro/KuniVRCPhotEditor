#include "ViewerRuntime.h"
bool CurrentJob(const Job& job) {
    if (job.kind == 3 || job.kind == 4) return true; // A click requests this exact file even if selection changes.
    return job.generation == app.currentGeneration &&
        (job.kind != 1 || job.thumbnailGeneration == app.currentThumbnailGeneration) &&
        (job.kind != 2 || job.selection == app.currentSelection);
}
void Worker(bool thumbnails) {
    WorkerLifetime lifetime;
    auto init = RoInitialize(RO_INIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> factory;
    auto factoryHr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    while (!app.stopping) {
        Job job;
        { std::unique_lock<std::mutex> lock(app.mutex);
          auto next = [&] { return std::find_if(app.jobs.begin(), app.jobs.end(), [&](const Job& j) { return j.kind<6 && (j.kind == 1) == thumbnails; }); };
          app.wake.wait(lock, [&] { return app.stopping || next() != app.jobs.end(); });
          if (app.stopping) break; auto i = next(); job = std::move(*i); app.jobs.erase(i); }
        if (!CurrentJob(job)) continue;
        Result result; result.job = job;
        try {
            if(job.kind==1||job.kind==2){result.job.sourceKey=ThumbnailCache::Key(job.path);if(!job.sourceKey.empty()&&job.sourceKey!=result.job.sourceKey)throw std::runtime_error("Source changed; reload before viewing");}
            if (job.kind == 4) {
                if(job.overwrite)result.text=SaveMetadataOverwrite(job.path,job.fingerprint,job.edit,job.autoBackup,job.backupLimit,job.preserve);
                else SaveMetadataCopy(job.path, job.destination, job.fingerprint, job.edit);
                result.summary = ReadPhotoSummary(job.destination);
            } else if (job.kind == 3) {
                std::wstring detail;
                auto hr = ActivatePhotos(job.path, detail);
                if (FAILED(hr)) result.error = detail + ErrorText(hr);
            } else if (job.kind == 0) {
                if (!fs::is_directory(job.path)) throw std::runtime_error("not directory");
                for (auto& entry : fs::directory_iterator(job.path)) {
                    if (app.stopping || job.generation != app.currentGeneration) break;
                    std::error_code error;
                    if (!entry.is_regular_file(error)) continue;
                    auto extension = entry.path().extension().wstring();
                    if (Same(extension, L".png") || Same(extension, L".jpg") || Same(extension, L".jpeg") || Same(extension, L".webp")) result.files.push_back(entry.path().wstring());
                }
                std::sort(result.files.begin(), result.files.end(), [](auto& a, auto& b) {
                    const wchar_t* left = wcsrchr(a.c_str(), L'\\'), *right = wcsrchr(b.c_str(), L'\\');
                    left = left ? left + 1 : a.c_str(); right = right ? right + 1 : b.c_str();
                    int comparison = _wcsicmp(left, right); return comparison ? comparison < 0 : wcscmp(left, right) < 0;
                });
            } else {
                if (job.kind == 2) {
                    for(unsigned elapsed=0;elapsed<app.metadataDelayMs && CurrentJob(job)&&!app.stopping;elapsed+=10)Sleep(10);
                    if(!CurrentJob(job)||app.stopping)continue;
                    result.summary = ReadPhotoSummary(job.path);
                    try { result.editable=InspectEditable(job.path); }
                    catch (const std::exception& e) { result.editError=winrt::to_hstring(e.what()).c_str(); }
                }
                std::wstring cacheKey;
                auto cacheTicket = app.cache.Ticket();
                bool cached = false;
                if (job.kind == 1) { cacheKey = ThumbnailCache::Key(job.path); cached = app.cache.Get(cacheKey, result.pixels); }
                if (!cached && Same(fs::path(job.path).extension().wstring(), L".webp")) {
                    auto image = ReadWebP(job.path, job.kind==1?256:0, job.kind==1?192:0);
                    if (job.kind == 1) result.pixels = { image.width, image.height, std::move(image.pixels) };
                    if (job.kind == 2) {
                        result.summary.values[2] = std::to_wstring(image.sourceWidth) + L" × " + std::to_wstring(image.sourceHeight) + L" px";
                        result.text = L"ファイル名: " + fs::path(job.path).filename().wstring() + L"\r\nフルパス: " + job.path + L"\r\n形式: WebP\r\n画像: " + result.summary.values[2];
                        if (image.animated) result.text += L"\r\nアニメーション: 合成済み先頭フレームの静止表示";
                        if (image.exif) result.text += L"\r\nEXIFあり（タグ展開・回転補正は未対応）";
                        if (image.icc) result.text += L"\r\nICCあり（色プロファイル適用は未対応）";
                        result.text += L"\r\n" + result.summary.notes + result.summary.xmpDetail;
                    }
                } else if (!cached) {
                winrt::check_hresult(factoryHr);
                ComPtr<IWICBitmapDecoder> decoder;
                winrt::check_hresult(factory->CreateDecoderFromFilename(job.path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder));
                ComPtr<IWICBitmapFrameDecode> frame; winrt::check_hresult(decoder->GetFrame(0, &frame));
                if (job.kind == 1) result.pixels = Decode(factory.Get(), frame.Get(), 256, 192);
                if (job.kind == 2) {
                    UINT width = 0, height = 0; winrt::check_hresult(frame->GetSize(&width, &height));
                    result.summary.values[2] = std::to_wstring(width) + L" × " + std::to_wstring(height) + L" px";
                    result.text = Info(decoder.Get(), frame.Get(), job.path) + L"\r\n" + result.summary.notes + result.summary.xmpDetail;
                }
                }
                if (job.kind == 1 && !cached && CurrentJob(job) && cacheKey == ThumbnailCache::Key(job.path)) app.cache.Put(cacheKey, result.pixels, cacheTicket);
            }
        } catch (const winrt::hresult_error& error) { result.error = ErrorText(error.code()); }
          catch (const fs::filesystem_error& error) { result.error = ErrorText(HRESULT_FROM_WIN32(error.code().value())); }
          catch (const std::exception& error) { result.error = winrt::to_hstring(error.what()).c_str(); }
          catch (...) { result.error = L"読込みエラー: パスまたはファイルを確認してください。"; }
        if (app.stopping || !CurrentJob(job)) continue;
        { std::unique_lock<std::mutex> lock(app.mutex);
          app.wake.wait(lock, [] { return app.stopping || app.results.size() < 32; });
          if (app.stopping) break;
          bool notify = app.results.empty(); app.results.push_back(std::move(result));
          if (notify) PostMessageW(app.window, WM_RESULTS, 0, 0); }
    }
    factory.Reset(); if (SUCCEEDED(init)) RoUninitialize();
}

Job ThumbnailJob(int index) {
    Job job{ 1, app.generation, 0, index, app.files[index], L"", app.thumbnailGeneration, app.activeThumbnailWidth };
    try{job.sourceKey=ThumbnailCache::Key(job.path);}catch(...){}
    return job;
}
