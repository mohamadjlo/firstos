#pragma once
#include <filesystem>
#include <vector>
#include <string>
#include <mutex>
#include <future>
#include <atomic>

class FileExplorer {
private:
    std::filesystem::path current_path_;
    std::vector<std::filesystem::path> current_directory_files_;
    std::vector<std::filesystem::path> filtered_files_;
    mutable std::mutex data_mutex_;
    std::future<void> search_task_;
    std::atomic<bool> search_active_{false};

public:
    FileExplorer();
    ~FileExplorer();

    void setCurrentDirectory(const std::filesystem::path& path);
    const std::filesystem::path& getCurrentDirectory() const;

    void filterFilesAsync(const std::string& query);
    void filterFilesSync(const std::string& query);
    std::vector<std::filesystem::path> getFilteredFiles() const;
    std::vector<std::filesystem::path> getCurrentFiles() const;

    void openTerminalHere(const std::filesystem::path& current_path);
};
