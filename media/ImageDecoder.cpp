#include "ImageDecoder.h"
#include <stdexcept>
#include <iostream>
#include <fstream>

bool ImageDecoder::load(const std::string& path) {
    try {
        if (path.empty()) {
            throw std::runtime_error("Empty path");
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open file: " + path);
        }

        // Read file bytes
        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size <= 0) {
            throw std::runtime_error("Empty or invalid image file");
        }

        pixel_data_.resize(size);
        if (!file.read(reinterpret_cast<char*>(pixel_data_.data()), size)) {
            throw std::runtime_error("Read error");
        }

        width_ = 64;
        height_ = 64;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ImageDecoder] Exception caught: " << e.what() << "\n";
        // Safe 1x1 red placeholder pixel (RGBA)
        pixel_data_ = { 255, 0, 0, 255 };
        width_ = 1;
        height_ = 1;
        return false;
    }
}

std::vector<unsigned char> ImageDecoder::getNextFrame() {
    return pixel_data_;
}

void ImageDecoder::cleanup() {
    pixel_data_.clear();
    width_ = 0;
    height_ = 0;
}
