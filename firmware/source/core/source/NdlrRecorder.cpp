#ifdef TRACKER_MINI

#include "NdlrRecorder.h"

#include "config.h"
#include "mtStructs.h"
#include "wavHeaderReader.h"

#include <algorithm>

namespace
{
constexpr char kRecordingDir[] = "Recordings";
constexpr uint32_t kSampleRate = 44100;
constexpr uint32_t kFrameBytes = 2 * sizeof(int16_t);
constexpr uint32_t kBufferFrames = NDLR_RECORDING_MEMORY_SIZE / kFrameBytes;
constexpr uint32_t kWriteFrames = 8192 / kFrameBytes;
constexpr uint32_t kMaxDataBytes = (UINT32_MAX - sizeof(strWavFileHeader)) & ~(kFrameBytes - 1);
static_assert(sizeof(strWavFileHeader) == 44);
static_assert((kBufferFrames & (kBufferFrames - 1)) == 0);
static_assert(kBufferFrames >= kWriteFrames);

struct StereoFrame
{
    int16_t left;
    int16_t right;
};
static_assert(sizeof(StereoFrame) == kFrameBytes);

// Separate from sample RAM and the scratch buffer used by the SD sample player.
__attribute__((section(".noinit.$EXTERNAL_RAM"), aligned(32))) StereoFrame buffer[kBufferFrames];
}

NdlrRecorder ndlrRecorder;

const char* NdlrRecorder::start(FileSystem& fs)
{
    if (isRecording())
        return "Already recording";

    if (!fs.isSdCardInserted())
        return "REC needs an SD card";
    if (!fs.exists(kRecordingDir) && !fs.mkdir(0, kRecordingDir))
        return "Cannot open Recordings";

    for (unsigned tries = 0; tries < 9999; ++tries)
    {
        const unsigned take = nextTake;
        nextTake = take == 9999 ? 1 : take + 1;
        snprintf(path, sizeof(path), "%s/NDLR%04u.wav", kRecordingDir, take);
        if (fs.exists(path))
            continue;

        const auto [opened, handle] = fs.openFile(path, FileSystem::kWriteCreateNew);
        if (!opened)
            return "Cannot create REC file";
        fileSystem = &fs;
        file = handle;
        dataBytes = 0;
        if (!writeHeader())
        {
            close();
            fs.remove(path);
            return "Cannot write REC file";
        }

        readFrame.store(0, std::memory_order_relaxed);
        writeFrame.store(0, std::memory_order_relaxed);
        state.store(CaptureState::Recording, std::memory_order_release);
        return "Recording stereo mix";
    }
    return "No free NDLR take name";
}

AUDIO_CORE_RAM void NdlrRecorder::capture(const int16_t* left, const int16_t* right, uint32_t frames)
{
    if (state.load(std::memory_order_acquire) != CaptureState::Recording)
        return;

    const uint32_t head = writeFrame.load(std::memory_order_relaxed);
    const uint32_t tail = readFrame.load(std::memory_order_acquire);
    if (frames > kBufferFrames - (head - tail))
    {
        state.store(CaptureState::Overflow, std::memory_order_release);
        return;
    }

    for (uint32_t i = 0; i < frames; ++i)
        buffer[(head + i) & (kBufferFrames - 1)] = { left[i], right[i] };

    writeFrame.store(head + frames, std::memory_order_release);
}

uint32_t NdlrRecorder::available() const
{
    return writeFrame.load(std::memory_order_acquire) - readFrame.load(std::memory_order_relaxed);
}

bool NdlrRecorder::writeBuffered()
{
    const uint32_t tail = readFrame.load(std::memory_order_relaxed);
    const uint32_t index = tail & (kBufferFrames - 1);
    const uint32_t frames = std::min({ available(), kWriteFrames, kBufferFrames - index });
    const size_t request = frames * kFrameBytes;
    const size_t bytes = std::min<size_t>(request, kMaxDataBytes - dataBytes);
    if (bytes == 0)
        return false;

    const size_t written = fileSystem->writeFile(buffer + index, bytes, file);
    if (written <= bytes)
        dataBytes += written & ~(kFrameBytes - 1);

    // The audio interrupt may reuse this region once the SD write has returned.
    readFrame.store(tail + frames, std::memory_order_release);
    return written == request;
}

bool NdlrRecorder::writeHeader()
{
    const strWavFileHeader header = {
        0x46464952, dataBytes + 36, WAVEID,
        0x20746d66, 16, wavFormatPCM, 2, kSampleRate,
        kSampleRate * kFrameBytes, kFrameBytes, 16,
        0x61746164, dataBytes
    };
    return fileSystem->writeFile(&header, sizeof(header), file) == sizeof(header);
}

void NdlrRecorder::close()
{
    fileSystem->closeFile(file);
    file = nullptr;
}

const char* NdlrRecorder::finish(bool writeFailed)
{
    const auto previous = state.exchange(CaptureState::Idle, std::memory_order_acq_rel);
    while (!writeFailed && available() != 0)
        writeFailed = !writeBuffered();

    if (writeFailed && fileSystem->seekFile(sizeof(strWavFileHeader) + dataBytes, file))
        fileSystem->endFile(file);

    const bool valid = dataBytes != 0 && fileSystem->seekFile(0, file) && writeHeader();
    close();
    if (!valid)
    {
        fileSystem->remove(path);
        return writeFailed || dataBytes != 0 ? "REC save failed" : "No audio recorded";
    }

    if (writeFailed || previous == CaptureState::Overflow)
        return "REC stopped: partial WAV saved";

    snprintf(message, sizeof(message), "Saved %s", path + sizeof(kRecordingDir));
    return message;
}

const char* NdlrRecorder::stop()
{
    return isRecording() ? finish(false) : nullptr;
}

const char* NdlrRecorder::update()
{
    if (!isRecording())
        return nullptr;
    if (state.load(std::memory_order_acquire) == CaptureState::Overflow)
        return finish(false);
    if (available() >= kWriteFrames && !writeBuffered())
        return finish(true);
    return nullptr;
}

void NdlrRecorder::cardRemoved()
{
    if (!isRecording())
        return;
    state.store(CaptureState::Idle, std::memory_order_release);
    close();
}

#endif
