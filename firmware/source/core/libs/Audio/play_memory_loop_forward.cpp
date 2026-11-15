#include "Audio.h"
#include "play_memory.h"
#include "utility/dspinst.h"
#include <cmath>

// Play dla wszystkich trybow loop jest wspolny stad tylko tu pelna implementacja a w innych
// wywolanie
AUDIO_CORE_FLASH void AudioPlayMemory::playLoopForward(uint8_t instrIdx, int8_t note)
{
    //	__disable_irq();
    AudioNoInterrupts();

    if (instrIdx > kSampleInstrumentsCount - 1)
        instrIdx = kSampleInstrumentsCount - 1;
    if (note > MAX_NOTE)
        note = MAX_NOTE;

    lastInstrIdxInPlay = currentInstrIdxInPlay;
    currentInstrIdxInPlay = instrIdx;
    currentInstrIdx = instrIdx;

    refreshStartParameters();
    if (previewLoop)
    {
        currentPlayMode = playModeLoopForward;
        if (currentSampleLength == 0)
        {
            previewLoop = false;
            AudioInterrupts();
            return;
        }
    }

    constrainCurrentTune(note);

    setStartPitch(note);

    applyFinetuneOnPitch(note);

    calculateGlidePitch(note);

    lastNote = note;

    uint16_t startPoint, endPoint, loopPoint1, loopPoint2;

    if (previewLoop)
    {
        startPoint = mtProject.instrument[instrIdx].startPoint;
        endPoint = mtProject.instrument[instrIdx].endPoint;
        loopPoint1 = startPoint;
        loopPoint2 = endPoint;
    }
    else if (pointsForceFlag)
    {
        startPoint = forcedStartPoint;
        endPoint = forcedEndPoint;
        loopPoint1 = forcedLoopPoint1;
        loopPoint2 = forcedLoopPoint2;
    }
    else
    {
        startPoint = mtProject.instrument[instrIdx].startPoint;
        endPoint = mtProject.instrument[instrIdx].endPoint;
        loopPoint1 = mtProject.instrument[instrIdx].loopPoint1;
        loopPoint2 = mtProject.instrument[instrIdx].loopPoint2;
    }

    if ((startPoint >= endPoint) || (startPoint > loopPoint1) || (startPoint > loopPoint2) ||
        (loopPoint1 > loopPoint2) || (loopPoint1 > endPoint) || (loopPoint2 > endPoint))
    {
        //		__enable_irq();
        AudioInterrupts();
        return;
    }

    if (samplePointsSource != nullptr)
    {
        pointsInSamples.start = samplePointsSource->pointsInSamples.start;
        pointsInSamples.end = samplePointsSource->pointsInSamples.end;
        pointsInSamples.loop1 = samplePointsSource->pointsInSamples.loop1;
        pointsInSamples.loop2 = samplePointsSource->pointsInSamples.loop2;
    }
    else
    {
        pointsInSamples.start = calculatePointPositionAndSnapToZero(
          startPoint, (uint32_t)currentSampleLength, (int16_t*)currentStartAddress);
        pointsInSamples.end = calculatePointPositionAndSnapToZero(
          endPoint, (uint32_t)currentSampleLength, (int16_t*)currentStartAddress);
        pointsInSamples.loop1 = calculatePointPositionAndSnapToZero(
          loopPoint1, (uint32_t)currentSampleLength, (int16_t*)currentStartAddress);
        pointsInSamples.loop2 = calculatePointPositionAndSnapToZero(
          loopPoint2, (uint32_t)currentSampleLength, (int16_t*)currentStartAddress);
    }

    if (!previewLoop && ((pointsInSamples.start >= currentSampleLength) ||
        (pointsInSamples.loop1 > currentSampleLength) ||
        (pointsInSamples.loop2 > currentSampleLength) ||
        (pointsInSamples.end > currentSampleLength)))
    {
        //		__enable_irq();
        AudioInterrupts();
        return;
    }

    if (previewLoop)
    {
        // Snap-to-zero can collapse a very short selection. Fall back to the
        // requested positions and guarantee at least one valid sample frame.
        if (pointsInSamples.end <= pointsInSamples.start ||
            pointsInSamples.start >= currentSampleLength ||
            pointsInSamples.end > currentSampleLength)
        {
            pointsInSamples.start = (uint64_t)startPoint * currentSampleLength / MAX_16BIT;
            pointsInSamples.end = (uint64_t)endPoint * currentSampleLength / MAX_16BIT;
            pointsInSamples.start = std::min(pointsInSamples.start, currentSampleLength - 1);
            pointsInSamples.end = std::max(pointsInSamples.start + 1, pointsInSamples.end);
        }
        pointsInSamples.loop1 = pointsInSamples.start;
        pointsInSamples.loop2 = pointsInSamples.end; // exclusive: include the last frame
    }
    else if (pointsInSamples.loop2 >= pointsInSamples.end)
    {
        assert(pointsInSamples.end != 0);
        pointsInSamples.loop2 = pointsInSamples.end - 1;
    }

    constrainsInSamples.loopPoint1 = pointsInSamples.loop1 - pointsInSamples.start;
    constrainsInSamples.loopPoint2 = pointsInSamples.loop2 - pointsInSamples.start;
    constrainsInSamples.loopLength = pointsInSamples.loop2 - pointsInSamples.loop1;
    constrainsInSamples.endPoint = pointsInSamples.end - pointsInSamples.start;

    next = currentStartAddress + pointsInSamples.start;
    length = currentSampleLength - pointsInSamples.start;
    iPitchCounter = reverseDirectionFlag ? constrainsInSamples.endPoint - 1 : 0;

    if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
    {
        voiceStreamManager.notifyNoteOn((uint8_t)voiceIndex,
                                        instrIdx,
                                        pointsInSamples.start,
                                        pointsInSamples.loop1,
                                        pointsInSamples.loop2,
                                        pointsInSamples.end,
                                        currentPlayMode,
                                        reverseDirectionFlag != 0,
                                        pitchControl);
    }

    playing = 1;
    //	__enable_irq();
    AudioInterrupts();
}

AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateLoopForward()
{
    if (reverseDirectionFlag)
        return updateLoopForwardReverse();
    else
        return updateLoopForwardNormal();
}

AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateLoopForwardNormal()
{
    audio_block_t* block = nullptr;
    int16_t* in = nullptr;
    int16_t* out = nullptr;
    int32_t castPitchControl;
    float pitchFraction;
    int32_t loopEndPoint = std::min((int32_t)constrainsInSamples.loopPoint2,
                                    (int32_t)std::min<int>(length, constrainsInSamples.endPoint));

    block = allocate();
    if (!block)
        return nullptr;

    if (!playing)
    {
        release(block);
        return nullptr;
    }
    else if (playing == 1)
    {
        out = block->data;
        in = (int16_t*)next;

        castPitchControl = pitchControl;
        pitchFraction = pitchControl - (int32_t)pitchControl;

        int32_t currentFractionPitchCounter = fPitchCounter * MAX_16BIT;
        int32_t currentFractionPitchControl = pitchFraction * MAX_16BIT;

        if (enableInterpolation)
            interpolationCondition = (((iPitchCounter + 128 * pitchControl) < length)) ? 0 : 1;
        else
            interpolationCondition = 1;

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
            if ((int32_t)length > iPitchCounter)
            {
                //*********************************** GLIDE HANDLE
                if (constrainsInSamples.pitchSmoothing)
                {
                    if (pitchSmoothingControlCounter <= constrainsInSamples.pitchSmoothing)
                    {
                        pitchControl += pitchSmoothingControl;
                        if (pitchControl > notes[MAX_NOTE - 1])
                        {
                            pitchControl = notes[MAX_NOTE - 1];
                            pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                        }
                        else if (pitchControl < notes[MIN_NOTE])
                        {
                            pitchControl = notes[MIN_NOTE];
                            pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                        }
                        castPitchControl = (int32_t)pitchControl;
                        pitchFraction = pitchControl - (int32_t)pitchControl;

                        currentFractionPitchControl = pitchFraction * MAX_16BIT;

                        pitchSmoothingControlCounter++;
                    }
                }
                //***********************************************
                //***************************************************************************************
                // poczatek przetwarzania pitchCountera
                currentSampelValue = readSampleAtRel(in, iPitchCounter);

                if (!enableInterpolation || (interpolationCondition && !previewLoop))
                    interpolationDif = 0;
                else
                {
                    int32_t nextSample = iPitchCounter + 1;
                    if (previewLoop && nextSample >= loopEndPoint)
                        nextSample = constrainsInSamples.loopPoint1;
                    interpolationDif = readSampleAtRel(in, nextSample) - currentSampelValue;
                }

                *out++ = currentSampelValue +
                         (int32_t)(((currentFractionPitchCounter >> 2) * interpolationDif) >> 14);
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;
                if (currentFractionPitchCounter >= MAX_16BIT)
                {
                    currentFractionPitchCounter -= MAX_16BIT;
                    iPitchCounter++;
                }
                //***************************************************************************************
                // koniec przetwarzania pitchCountera
                //***************************************************************************************
                // warunki pointow
                if (iPitchCounter >= loopEndPoint)
                {
                    if (previewLoop)
                    {
                        // A narrow selection may be shorter than a single pitch step.
                        iPitchCounter = constrainsInSamples.loopPoint1 +
                          (iPitchCounter - loopEndPoint) % constrainsInSamples.loopLength;
                    }
                    else
                    {
                        iPitchCounter = constrainsInSamples.loopPoint1 + (iPitchCounter - loopEndPoint);
                    }
                }
                //***************************************************************************************
                // warunki pętli
            }
            else
            {
                *out++ = 0;
                playing = 0;
            }
        }
        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;
        if (playing)
            updateStreamPlayhead(iPitchCounter, 1);
        else if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
            voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);

        return block;
    }

    return nullptr;
}
AUDIO_CORE_RAM audio_block_t* AudioPlayMemory::updateLoopForwardReverse()
{
    audio_block_t* block = nullptr;
    int16_t* in = nullptr;
    int16_t* out = nullptr;
    int32_t castPitchControl;
    float pitchFraction;
    int32_t loopStartPoint = std::max<int>((int32_t)constrainsInSamples.loopPoint1, 0);

    block = allocate();
    if (!block)
        return nullptr;

    if (!playing)
    {
        release(block);
        return nullptr;
    }
    else if (playing == 1)
    {
        out = block->data;
        in = (int16_t*)next;

        castPitchControl = (int32_t)-pitchControl;
        pitchFraction = -(pitchControl - (int32_t)pitchControl);

        if (enableInterpolation)
            interpolationCondition = ((int)(iPitchCounter - 128 * pitchControl) > 0) ? 0 : 1;
        else
            interpolationCondition = 1;

        int32_t currentFractionPitchCounter = fPitchCounter * MAX_16BIT;
        int32_t currentFractionPitchControl = pitchFraction * MAX_16BIT;

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
            if (iPitchCounter > 0)
            {
                //*********************************** GLIDE HANDLE
                if (constrainsInSamples.pitchSmoothing)
                {
                    if (pitchSmoothingControlCounter <= constrainsInSamples.pitchSmoothing)
                    {
                        pitchControl += pitchSmoothingControl;
                        if (pitchControl > notes[MAX_NOTE - 1])
                        {
                            pitchControl = notes[MAX_NOTE - 1];
                            pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                        }
                        else if (pitchControl < notes[MIN_NOTE])
                        {
                            pitchControl = notes[MIN_NOTE];
                            pitchSmoothingControlCounter = constrainsInSamples.pitchSmoothing;
                        }
                        castPitchControl = (int32_t)-pitchControl;
                        pitchFraction = -(pitchControl - (int32_t)pitchControl);
                        currentFractionPitchControl = pitchFraction * MAX_16BIT;

                        pitchSmoothingControlCounter++;
                    }
                }
                //***********************************************
                //***************************************************************************************
                // poczatek przetwarzania pitchCountera
                currentSampelValue = readSampleAtRel(in, iPitchCounter);

                if (interpolationCondition)
                    interpolationDif = 0;
                else
                    interpolationDif = (readSampleAtRel(in, iPitchCounter - 1) - currentSampelValue);

                *out++ =
                  (int32_t)currentSampelValue +
                  (int32_t)((((-currentFractionPitchCounter) >> 2) * interpolationDif) >> 14);
                iPitchCounter += castPitchControl;
                currentFractionPitchCounter += currentFractionPitchControl;

                if (currentFractionPitchCounter <= -MAX_16BIT)
                {
                    currentFractionPitchCounter += MAX_16BIT;
                    iPitchCounter--;
                }

                //***************************************************************************************
                // koniec przetwarzania pitchCountera
                if (iPitchCounter <= loopStartPoint)
                {
                    iPitchCounter =
                      constrainsInSamples.loopPoint2 - (loopStartPoint - iPitchCounter);
                }
                //***************************************************************************************
                // warunki pętli
            }
            else
            {
                *out++ = 0;
                playing = 0;
            }
        }
        next = currentStartAddress + pointsInSamples.start;
        fPitchCounter = (float)currentFractionPitchCounter / MAX_16BIT;
        if (playing)
            updateStreamPlayhead(iPitchCounter, -1);
        else if (currentIsStreamed && channel == 0 && voiceIndex >= 0)
            voiceStreamManager.notifyNoteOff((uint8_t)voiceIndex);

        return block;
    }

    return nullptr;
}
