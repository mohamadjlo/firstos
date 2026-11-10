#include "FileExplorer.h"
#include "FileSignatureParser.h"
#include "c_abi.h"

#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>

FileExplorer::FileExplorer() : current_path_(".") {
    setCurrentDirectory(current_path_);
}

FileExplorer::~FileExplorer() {
    if (search_task_.valid()) {
        search_task_.wait();
    }
}

void FileExplorer::setCurrentDirectory(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    current_path_ = path;
    current_directory_files_.clear();

    std::error_code ec;
    if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            current_directory_files_.push_back(entry.path());
        }
    }
    filtered_files_ = current_directory_files_;
}

const std::filesystem::path& FileExplorer::getCurrentDirectory() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return current_path_;
}

void FileExplorer::filterFilesSync(const std::string& query) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (query.empty()) {
        filtered_files_ = current_directory_files_;
        return;
    }

    std::vector<std::filesystem::path> local_results;
    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    for (const auto& file : current_directory_files_) {
        std::string fname = file.filename().string();
        std::string lower_fname = fname;
        std::transform(lower_fname.begin(), lower_fname.end(), lower_fname.begin(), ::tolower);

        if (lower_fname.find(lower_query) != std::string::npos) {
            local_results.push_back(file);
        }
    }
    filtered_files_ = std::move(local_results);
}

void FileExplorer::filterFilesAsync(const std::string& query) {
    if (search_active_) {
        if (search_task_.valid()) {
            search_task_.wait();
        }
    }

    search_task_ = std::async(std::launch::async, [this, query]() {
        search_active_ = true;
        filterFilesSync(query);
        search_active_ = false;
    });
}

std::vector<std::filesystem::path> FileExplorer::getFilteredFiles() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return filtered_files_;
}

std::vector<std::filesystem::path> FileExplorer::getCurrentFiles() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return current_directory_files_;
}

void FileExplorer::openTerminalHere(const std::filesystem::path& current_path) {
    pid_t pid = fork();
    if (pid == 0) {
        if (chdir(current_path.c_str()) != 0) {
            _exit(1);
        }
        char* args[] = { const_cast<char*>("/bin/terminal"), nullptr };
        execvp(args[0], args);
        _exit(1);
    }
}

// C-ABI Exports
extern "C" {

ExplorerHandle Explorer_Create(void) {
    return static_cast<ExplorerHandle>(new FileExplorer());
}

void Explorer_Destroy(ExplorerHandle handle) {
    if (handle) {
        delete static_cast<FileExplorer*>(handle);
    }
}

void Explorer_SetSearchFilter(ExplorerHandle handle, const char* query) {
    if (handle && query) {
        static_cast<FileExplorer*>(handle)->filterFilesAsync(query);
    }
}

}
