#include "ViewerRuntime.h"
Pixels DisplayedThumbnail(int index) {
    if(IsFolderItem(index)||index<0||index>=static_cast<int>(app.itemSlots.size())||app.itemSlots[index]<0)return {};
    try { if(app.thumbnailKeys[index]!=ThumbnailCache::Key(app.files[index]))return {}; } catch (...) { return {}; }
    Pixels pixels;pixels.width=app.activeThumbnailWidth;pixels.height=app.activeThumbnailWidth*3/4;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=pixels.width;info.bmiHeader.biHeight=-static_cast<LONG>(pixels.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
    auto previous=SelectObject(dc,bitmap);auto bg=ThemeBackground();auto target=static_cast<BYTE*>(bits);for(size_t i=0;i<size_t(pixels.width)*pixels.height*4;i+=4){target[i]=GetBValue(bg);target[i+1]=GetGValue(bg);target[i+2]=GetRValue(bg);target[i+3]=255;}
    bool ok=ImageList_Draw(app.thumbs,app.itemSlots[index]+1,dc,0,0,ILD_NORMAL)!=FALSE;
    if(ok){auto data=static_cast<BYTE*>(bits);pixels.data.assign(data,data+size_t(pixels.width)*pixels.height*4);for(size_t i=3;i<pixels.data.size();i+=4)pixels.data[i]=255;}
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);return pixels;
}
HBITMAP Thumbnail(const Pixels& pixels, int width) {
    const int height = width * 3 / 4;
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr; HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return nullptr;
    auto target = static_cast<BYTE*>(bits);auto bg=ThemeBackground();int background[]{GetBValue(bg),GetGValue(bg),GetRValue(bg)};
    for(int i=0;i<width*height;++i)for(int c=0;c<3;++c)target[i*4+c]=BYTE(background[c]);
    for (int i = 0; i < width * height; ++i) target[i * 4 + 3] = 255;
    if (!pixels.width || !pixels.height) return bitmap;
    double scale = std::min({ 1.0, double(width) / pixels.width, double(height) / pixels.height });
    UINT drawWidth = std::max(1U, UINT(pixels.width * scale)), drawHeight = std::max(1U, UINT(pixels.height * scale));
    auto x = (width - drawWidth) / 2, y = (height - drawHeight) / 2;
    for (UINT row = 0; row < drawHeight; ++row) for (UINT col = 0; col < drawWidth; ++col) {
        double sx = std::max(0.0, (col + 0.5) * pixels.width / drawWidth - 0.5), sy = std::max(0.0, (row + 0.5) * pixels.height / drawHeight - 0.5);
        UINT x0 = UINT(sx), y0 = UINT(sy), x1 = std::min(x0 + 1, pixels.width - 1), y1 = std::min(y0 + 1, pixels.height - 1);
        double fx = sx - x0, fy = sy - y0; double channels[4]{};
        for (int c = 0; c < 4; ++c) channels[c] =
            (1 - fy) * ((1 - fx) * pixels.data[(size_t(y0) * pixels.width + x0) * 4 + c] + fx * pixels.data[(size_t(y0) * pixels.width + x1) * 4 + c]) +
            fy * ((1 - fx) * pixels.data[(size_t(y1) * pixels.width + x0) * 4 + c] + fx * pixels.data[(size_t(y1) * pixels.width + x1) * 4 + c]);
        auto dest = target + ((row + y) * width + col + x) * 4;
        for (int c = 0; c < 3; ++c) dest[c] = BYTE(std::clamp(channels[c] + background[c] * (255 - channels[3]) / 255, 0.0, 255.0));
    }
    return bitmap;
}
