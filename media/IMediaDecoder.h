#pragma once
#include <string>
#include <vector>
#include <memory>

class IMediaDecoder {
public:
    virtual ~IMediaDecoder() = default;
    virtual bool load(const std::string& path) = 0;
    virtual std::vector<unsigned char> getNextFrame() = 0;
    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;
    virtual void cleanup() = 0;
};
