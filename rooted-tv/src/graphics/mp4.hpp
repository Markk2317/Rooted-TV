#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rootedtv {
// The H.264 track of an MP4 file (Apple's movie previews, or the fragmented MP4 of Apple TV's
// HLS trailers): where each sample lies, when it shows and whether decoding can start at it. Samples that end past the bytes given are left out, so the
// first megabytes of a "fast start" file (its index before the media) play as a shorter clip.
struct Mp4Sample {
    uint64_t offset = 0;
    uint32_t size = 0;
    double atMs = 0; // decode time; the decoder hands pictures back in display order, each taking
                     // the next of these times, which are evenly spaced
    bool key = false;
};
struct Mp4Video {
    int width = 0, height = 0;          // the coded picture
    int aspectNum = 1, aspectDen = 1;   // pixel aspect ratio (pasp): Apple's 640x458 is 4:3
    int nalLength = 4;                  // bytes of length before each NAL unit in a sample
    std::string parameterSets;          // the SPS and PPS, as Annex B
    std::vector<Mp4Sample> samples;
    double durationMs = 0;              // of the samples kept
};
// False, with the reason, when the bytes hold no AVC video track.
bool parseMp4Video(const std::string &bytes, Mp4Video &video, std::string &error);
// One sample as an Annex B access unit (start codes in place of the lengths), led by the parameter
// sets on a key frame. False when the sample's NAL lengths run past it.
bool annexBSample(const std::string &bytes, const Mp4Video &video, size_t index, std::string &unit);
} // namespace rootedtv
