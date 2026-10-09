#pragma once
#include "MetadataEditor.h"
EditableMetadata InspectEditable(const std::wstring& path);
// Creates a verified copy; never replaces any existing destination.
void SaveMetadataCopy(const std::wstring& source, const std::wstring& destination,
    const std::wstring& expectedFingerprint, const MetadataEdit& edit);
std::wstring MetadataFingerprint(const std::wstring& path);
