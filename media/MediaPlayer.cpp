#include "MediaPlayer.h"
#include "ImageDecoder.h"
#include "c_abi.h"
#include <iostream>
#include <chrono>

MediaPlayer::MediaPlayer() : decoder_(std::make_unique<ImageDecoder>()) {
    render_thread_ = std::thread(&MediaPlayer::playbackLoop, this);
}

MediaPlayer::~MediaPlayer() {
    thread_running_ = false;
    state_cv_.notify_all();
    if (render_thread_.joinable()) {
        render_thread_.join();
    }
}

void MediaPlayer::playbackLoop() {
    while (thread_running_) {
        std::unique_lock<std::mutex> lock(state_mutex_);
        state_cv_.wait(lock, [this]() {
            return state_ == PlaybackState::PLAYING || !thread_running_;
        });

        if (!thread_running_) break;

        // Simulate frame playback tick while playing
        if (state_ == PlaybackState::PLAYING && decoder_) {
            auto frame = decoder_->getNextFrame();
            (void)frame;
        }
        lock.unlock();

        std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 fps
    }
}

bool MediaPlayer::loadFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    current_file_ = path;
    if (!decoder_) {
        decoder_ = std::make_unique<ImageDecoder>();
    }
    bool ok = decoder_->load(path);
    state_ = PlaybackState::STOPPED;
    return ok;
}

void MediaPlayer::play() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = PlaybackState::PLAYING;
    state_cv_.notify_one();
}

void MediaPlayer::pause() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = PlaybackState::PAUSED;
}

void MediaPlayer::stop() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_ = PlaybackState::STOPPED;
}

PlaybackState MediaPlayer::getState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

const std::string& MediaPlayer::getCurrentFile() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_file_;
}

// C-ABI Exports
extern "C" {

MediaPlayerHandle MediaPlayer_Create(void) {
    return static_cast<MediaPlayerHandle>(new MediaPlayer());
}

void MediaPlayer_Destroy(MediaPlayerHandle handle) {
    if (handle) {
        delete static_cast<MediaPlayer*>(handle);
    }
}

void MediaPlayer_LoadFile(MediaPlayerHandle handle, const char* path) {
    if (handle && path) {
        static_cast<MediaPlayer*>(handle)->loadFile(path);
    }
}

}
