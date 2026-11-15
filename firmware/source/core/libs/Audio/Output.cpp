#include "Output.h"

#ifdef TRACKER_MINI
#include "NdlrRecorder.h"
#endif

void AudioOutputStream::begin(void)
{
}

AUDIO_CORE_RAM void AudioOutputStream::update()
{
    if (outBuffers[0] == nullptr)
    {
        return;
    }

    audio_block_t* blockLeft = receiveReadOnly(kOutChannels * source[0]); // input 0 = left channel
    audio_block_t* blockRight =
      receiveReadOnly(kOutChannels * source[1] + 1); // input 1 = right channel

    if (blockLeft)
    {
        memcpy(outBuffers[0], blockLeft->data, sizeof(blockLeft->data));
    }
    else
    {
        memset(outBuffers[0], 0, sizeof(blockLeft->data));
    }
    if (blockRight)
    {
        memcpy(outBuffers[1], blockRight->data, sizeof(blockRight->data));
    }
    else
    {
        memset(outBuffers[1], 0, sizeof(blockLeft->data));
    }

#ifdef TRACKER_MINI
    ndlrRecorder.capture(static_cast<const int16_t*>(outBuffers[0]),
                         static_cast<const int16_t*>(outBuffers[1]), AUDIO_BLOCK_SAMPLES);
#endif

    if (blockLeft)
        release(blockLeft);
    if (blockRight)
        release(blockRight);
}

void AudioOutputStream::selectSource(int channel, int idx)
{
    assert(channel < kOutChannels);
    assert(idx < kSources);

    source[channel] = idx;
}

AUDIO_CORE_FLASH void AudioOutputStream::setOutBuffers(void* left, void* right)
{
    outBuffers[0] = left;
    outBuffers[1] = right;
}
