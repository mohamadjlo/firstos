#pragma once
#include "IMediaDecoder.h"
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <string>

enum class PlaybackState { STOPPED, PLAYING, PAUSED };

class MediaPlayer {
private:
    PlaybackState state_{PlaybackState::STOPPED};
    mutable std::mutex state_mutex_;
    std::condition_variable state_cv_;
    std::thread render_thread_;
    std::atomic<bool> thread_running_{true};
    std::unique_ptr<IMediaDecoder> decoder_;
    std::string current_file_;

    void playbackLoop();

public:
    MediaPlayer();
    ~MediaPlayer();

    bool loadFile(const std::string& path);
    void play();
    void pause();
    void stop();

    PlaybackState getState() const;
    const std::string& getCurrentFile() const;
};
