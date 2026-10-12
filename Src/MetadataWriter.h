#pragma once
#include "MetadataEditor.h"
void ValidateMetadataDestination(const std::wstring& source, const std::wstring& destination);
void ValidateMetadataDestination(const EditableMetadata& metadata, const std::wstring& destination);
EditableMetadata InspectEditable(const std::wstring& path, PhotoSummary* summary = nullptr);
// Creates a verified copy; never replaces any existing destination.
void SaveMetadataCopy(const std::wstring& source, const std::wstring& destination,
    const std::wstring& expectedFingerprint, const MetadataEdit& edit, bool allowLockedRename = false);
std::wstring MetadataFingerprint(const std::wstring& path);
