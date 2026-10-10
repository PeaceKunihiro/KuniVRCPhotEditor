#pragma once
#include "MetadataWriter.h"
#include <filesystem>
#include <vector>
struct BackupEntry { std::wstring path, source, fingerprint; unsigned long long created=0; };
struct BackupCandidate { std::wstring path, sourceKey; unsigned long long bytes=0; FILETIME modified{}; bool managed=false; };
std::vector<BackupEntry> ReadBackupLedger(const std::filesystem::path& ledger);
void WriteBackupLedger(const std::filesystem::path& ledger,const std::vector<BackupEntry>& entries);
std::vector<BackupCandidate> FindBackups(const std::wstring& folder,const std::filesystem::path& ledger);
bool RecycleBackup(const std::wstring& path, std::wstring* error = nullptr);
std::wstring SaveMetadataOverwrite(const std::wstring& source,const std::wstring& fingerprint,
    const MetadataEdit& edit,bool backup,int limit,const std::filesystem::path& ledger);
std::wstring PruneBackups(const std::wstring& folder,int limit,const std::filesystem::path& ledger);
