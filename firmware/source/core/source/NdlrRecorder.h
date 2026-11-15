#ifndef SOURCE_NDLRRECORDER_H_
#define SOURCE_NDLRRECORDER_H_

#ifdef TRACKER_MINI

#include "Platform.h"

#include <atomic>

class NdlrRecorder
{
  public:
    const char* start(FileSystem& fs);
    const char* stop();
    const char* update();
    void cardRemoved();

    bool isRecording() const { return file != nullptr; }

    // Called from the audio interrupt with the stereo output for this block.
    void capture(const int16_t* left, const int16_t* right, uint32_t frames);

  private:
    enum class CaptureState : uint32_t { Idle, Recording, Overflow };
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<CaptureState>::is_always_lock_free);

    const char* finish(bool writeFailed);
    bool writeBuffered();
    bool writeHeader();
    uint32_t available() const;
    void close();

    std::atomic<CaptureState> state{CaptureState::Idle};
    std::atomic<uint32_t> writeFrame{0};
    std::atomic<uint32_t> readFrame{0};

    FileSystem* fileSystem = nullptr;
    FileSystem::FileHandle file = nullptr;
    uint32_t dataBytes = 0;
    unsigned nextTake = 1;
    char path[32]{};
    char message[32]{};
};

extern NdlrRecorder ndlrRecorder;

#endif
#endif
