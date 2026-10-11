#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MetadataEditor.h"
#include "Stage3.h"
std::vector<std::wstring> GroupIds(const std::wstring& tags);
std::wstring GroupUrl(const std::wstring& id);
MetadataEdit BuildAutomaticEdit(const EditableMetadata&,const stage3::Record&,const stage3::Config&);
void RunV048Tests(const std::filesystem::path&,std::ostream&);

#include <functional>
namespace stage3 {void RunMonitorUiTests(const std::filesystem::path&,std::ostream&,const std::function<void(HWND)>&);}
