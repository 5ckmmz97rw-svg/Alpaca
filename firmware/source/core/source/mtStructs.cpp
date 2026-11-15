#include "mtStructs.h"

template<int synthCount>
void ProjectValues<synthCount>::setDefaultTrackName(int trackIdx)
{
    assert(trackIdx < kMaxAllTracksInTrackers);

    if (trackIdx < kSampleTracks)
    {
        sprintf(TrackNames[trackIdx], "Track %d", trackIdx + 1);
    }
    else
    {
        sprintf(midiTrackNames[trackIdx - kSampleTracks], "Midi %d", trackIdx + 1);
    }
}

template void ProjectValues<kLegacySynthInstrumentsCount>::setDefaultTrackName(int);
#ifdef TRACKER_MINI
template void ProjectValues<kSynthInstrumentsCount>::setDefaultTrackName(int);
#endif
