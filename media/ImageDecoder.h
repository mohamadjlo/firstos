#pragma once
#include "IMediaDecoder.h"
#include <vector>
#include <string>

class ImageDecoder : public IMediaDecoder {
private:
    std::vector<unsigned char> pixel_data_;
    int width_{0};
    int height_{0};

public:
    ImageDecoder() = default;
    ~ImageDecoder() override = default;

    bool load(const std::string& path) override;
    std::vector<unsigned char> getNextFrame() override;
    int getWidth() const override { return width_; }
    int getHeight() const override { return height_; }
    void cleanup() override;
};
