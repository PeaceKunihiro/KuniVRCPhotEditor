#include "ViewerRuntime.h"
int ThumbnailHeight(int width){return app.hideFileNames?width:width*3/4;}
Pixels DisplayedThumbnail(int index) {
    if(IsFolderItem(index)||index<0||index>=static_cast<int>(app.itemSlots.size())||app.itemSlots[index]<0)return {};
    try { if(app.thumbnailKeys[index]!=ThumbnailCache::Key(app.files[index]))return {}; } catch (...) { return {}; }
    if(HasBackdrop()){Pixels cached;if(app.cache.Get(app.thumbnailKeys[index],cached))return cached;}
    Pixels pixels;pixels.width=app.activeThumbnailWidth;pixels.height=ThumbnailHeight(app.activeThumbnailWidth);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=pixels.width;info.bmiHeader.biHeight=-static_cast<LONG>(pixels.height);info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return {};}
    auto previous=SelectObject(dc,bitmap);auto bg=ThemeBackground();auto target=static_cast<BYTE*>(bits);for(size_t i=0;i<size_t(pixels.width)*pixels.height*4;i+=4){target[i]=HasBackdrop()?0:GetBValue(bg);target[i+1]=HasBackdrop()?0:GetGValue(bg);target[i+2]=HasBackdrop()?0:GetRValue(bg);target[i+3]=HasBackdrop()?0:255;}
    bool ok=ImageList_Draw(app.thumbs,app.itemSlots[index]+1,dc,0,0,ILD_NORMAL)!=FALSE;
    if(ok){auto data=static_cast<BYTE*>(bits);pixels.data.assign(data,data+size_t(pixels.width)*pixels.height*4);if(!HasBackdrop())for(size_t i=3;i<pixels.data.size();i+=4)pixels.data[i]=255;}
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);return pixels;
}
HBITMAP Thumbnail(const Pixels& pixels, int width) {
    const bool transparent=HasBackdrop();
    const int height = ThumbnailHeight(width);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr; HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return nullptr;
    auto target = static_cast<BYTE*>(bits);auto bg=ThemeBackground();int background[]{GetBValue(bg),GetGValue(bg),GetRValue(bg)};
    for(int i=0;i<width*height;++i)for(int c=0;c<3;++c)target[i*4+c]=transparent?0:BYTE(background[c]);
    for (int i = 0; i < width * height; ++i) target[i * 4 + 3] = transparent?0:255;
    if (!pixels.width || !pixels.height) return bitmap;
    bool tile=app.hideFileNames;
    double scale=tile?std::max(double(width)/pixels.width,double(height)/pixels.height):std::min({1.0,double(width)/pixels.width,double(height)/pixels.height});
    UINT drawWidth=tile?UINT(width):std::max(1U,UINT(pixels.width*scale)),drawHeight=tile?UINT(height):std::max(1U,UINT(pixels.height*scale));
    double spanX=tile?width/scale:pixels.width,spanY=tile?height/scale:pixels.height;
    double sourceLeft=(pixels.width-spanX)/2,sourceTop=(pixels.height-spanY)/2;
    auto x = (width - drawWidth) / 2, y = (height - drawHeight) / 2;
    for (UINT row = 0; row < drawHeight; ++row) for (UINT col = 0; col < drawWidth; ++col) {
        double sx=std::clamp(sourceLeft+(col+0.5)*spanX/drawWidth-0.5,0.0,double(pixels.width-1)),sy=std::clamp(sourceTop+(row+0.5)*spanY/drawHeight-0.5,0.0,double(pixels.height-1));
        UINT x0 = UINT(sx), y0 = UINT(sy), x1 = std::min(x0 + 1, pixels.width - 1), y1 = std::min(y0 + 1, pixels.height - 1);
        double fx = sx - x0, fy = sy - y0; double channels[4]{};
        for (int c = 0; c < 4; ++c) channels[c] =
            (1 - fy) * ((1 - fx) * pixels.data[(size_t(y0) * pixels.width + x0) * 4 + c] + fx * pixels.data[(size_t(y0) * pixels.width + x1) * 4 + c]) +
            fy * ((1 - fx) * pixels.data[(size_t(y1) * pixels.width + x0) * 4 + c] + fx * pixels.data[(size_t(y1) * pixels.width + x1) * 4 + c]);
        auto dest = target + ((row + y) * width + col + x) * 4;
        for (int c = 0; c < 3; ++c) dest[c] = BYTE(std::clamp(transparent?(channels[3]>0?channels[c]*255/channels[3]:0):channels[c]+background[c]*(255-channels[3])/255, 0.0, 255.0));
        if(transparent)dest[3]=BYTE(std::clamp(channels[3],0.0,255.0));
    }
    return bitmap;
}

// ComCtl32 treats an all-zero-alpha image as legacy RGB; an explicit empty mask avoids black tiles.
HBITMAP EmptyThumbnailMask(HBITMAP bitmap){
    if(!HasBackdrop())return nullptr;DIBSECTION image{};if(!GetObjectW(bitmap,sizeof(image),&image)||!image.dsBm.bmBits)return nullptr;auto bytes=static_cast<const BYTE*>(image.dsBm.bmBits);size_t count=size_t(image.dsBm.bmWidthBytes)*image.dsBm.bmHeight;for(size_t i=3;i<count;i+=4)if(bytes[i])return nullptr;
    int stride=((image.dsBm.bmWidth+15)/16)*2;std::vector<BYTE> mask(size_t(stride)*image.dsBm.bmHeight,255);return CreateBitmap(image.dsBm.bmWidth,image.dsBm.bmHeight,1,1,mask.data());
}
