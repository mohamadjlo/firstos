#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

enum class MimeType { Unknown, Image_PNG, Image_JPG, Executable_ELF };

class FileSignatureParser {
public:
    static MimeType detectMimeType(const std::filesystem::path& path);
    static const char* mimeTypeToString(MimeType type);
};
