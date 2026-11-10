#include "FileSignatureParser.h"

MimeType FileSignatureParser::detectMimeType(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return MimeType::Unknown;

    std::vector<unsigned char> magic(4, 0);
    file.read(reinterpret_cast<char*>(magic.data()), 4);
    if (file.gcount() < 4) return MimeType::Unknown;

    if (magic[0] == 0x89 && magic[1] == 0x50 && magic[2] == 0x4E && magic[3] == 0x47)
        return MimeType::Image_PNG;
    if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF)
        return MimeType::Image_JPG;
    if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F')
        return MimeType::Executable_ELF;

    return MimeType::Unknown;
}

const char* FileSignatureParser::mimeTypeToString(MimeType type) {
    switch (type) {
        case MimeType::Image_PNG: return "image/png";
        case MimeType::Image_JPG: return "image/jpeg";
        case MimeType::Executable_ELF: return "application/x-executable";
        case MimeType::Unknown:
        default: return "application/octet-stream";
    }
}
