#pragma once
#include "OcrRuntime.h"
#include <functional>
namespace person_ocr {
struct NativeTile {UINT x,y,width,height;};
std::vector<NativeTile> NativeTiles(UINT width,UINT height,UINT limit);
struct NativeTileStats {unsigned peakWorkers=0;size_t tiles=0;};
std::vector<TextLine> RecognizeNativeTiles(UINT width,UINT height,const std::vector<BYTE>& gray,const std::function<bool()>& cancelled,NativeTileStats* stats=nullptr);
void RunNativeTileTests(std::ostream& report);
}
