#include "graphics/mp4.hpp"
#include <algorithm>

namespace nativetv {
namespace {
const uint32_t kSamplesMax = 200000;

struct Box {
    std::string type;
    size_t start = 0, size = 0; // the payload
};
uint32_t be16(const std::string &bytes, size_t at) {
    return (static_cast<uint8_t>(bytes[at]) << 8) | static_cast<uint8_t>(bytes[at + 1]);
}
uint32_t be32(const std::string &bytes, size_t at) {
    return (be16(bytes, at) << 16) | be16(bytes, at + 2);
}
uint64_t be64(const std::string &bytes, size_t at) {
    return (static_cast<uint64_t>(be32(bytes, at)) << 32) | be32(bytes, at + 4);
}
// The boxes between `start` and `end`. With `partialLast` a box that runs past the end stops the
// list instead of failing it: the media box of a file cut short.
bool children(const std::string &bytes, size_t start, size_t end, std::vector<Box> &boxes,
              bool partialLast = false) {
    boxes.clear();
    size_t at = start;
    while (at < end) {
        if (end - at < 8)
            return partialLast;
        uint64_t size = be32(bytes, at);
        size_t header = 8;
        if (size == 1) {
            if (end - at < 16)
                return partialLast;
            size = be64(bytes, at + 8);
            header = 16;
        } else if (size == 0)
            size = end - at;
        if (size < header)
            return false;
        if (size > end - at)
            return partialLast;
        boxes.push_back({bytes.substr(at + 4, 4), at + header, static_cast<size_t>(size) - header});
        at += static_cast<size_t>(size);
    }
    return true;
}
const Box *find(const std::vector<Box> &boxes, const char *type) {
    for (const Box &box : boxes)
        if (box.type == type)
            return &box;
    return nullptr;
}
// A full box's table: the entry count after version and flags, and room for `count` entries of
// `entry` bytes from `first`.
bool entries(const std::string &bytes, const Box &box, size_t entry, uint32_t &count, size_t &first) {
    if (box.size < 8)
        return false;
    count = be32(bytes, box.start + 4);
    first = box.start + 8;
    return entry && count <= (box.size - 8) / entry;
}
bool videoTrack(const std::string &bytes, const Box &trak, std::vector<Box> &stbl,
                uint32_t &timescale) {
    std::vector<Box> boxes;
    if (!children(bytes, trak.start, trak.start + trak.size, boxes))
        return false;
    const Box *mdia = find(boxes, "mdia");
    if (!mdia)
        return false;
    std::vector<Box> media;
    if (!children(bytes, mdia->start, mdia->start + mdia->size, media))
        return false;
    const Box *hdlr = find(media, "hdlr"), *mdhd = find(media, "mdhd"), *minf = find(media, "minf");
    if (!hdlr || hdlr->size < 12 || bytes.compare(hdlr->start + 8, 4, "vide") != 0 || !mdhd ||
        !minf)
        return false;
    const bool wide = mdhd->size >= 1 && bytes[mdhd->start] == 1;
    if (mdhd->size < (wide ? 24u : 16u))
        return false;
    timescale = be32(bytes, mdhd->start + (wide ? 20 : 12));
    std::vector<Box> information;
    if (!timescale || !children(bytes, minf->start, minf->start + minf->size, information))
        return false;
    const Box *table = find(information, "stbl");
    return table && children(bytes, table->start, table->start + table->size, stbl);
}
bool sampleEntry(const std::string &bytes, const Box &stsd, Mp4Video &video, std::string &error) {
    std::vector<Box> entriesFound;
    if (stsd.size < 8 || !children(bytes, stsd.start + 8, stsd.start + stsd.size, entriesFound) ||
        entriesFound.empty()) {
        error = "The video track describes no samples";
        return false;
    }
    const Box &entry = entriesFound[0];
    if ((entry.type != "avc1" && entry.type != "avc3") || entry.size < 78) {
        error = "The video is not H.264";
        return false;
    }
    video.width = static_cast<int>(be16(bytes, entry.start + 24));
    video.height = static_cast<int>(be16(bytes, entry.start + 26));
    std::vector<Box> extensions;
    if (!children(bytes, entry.start + 78, entry.start + entry.size, extensions)) {
        error = "The H.264 description is damaged";
        return false;
    }
    if (const Box *pasp = find(extensions, "pasp"))
        if (pasp->size >= 8 && be32(bytes, pasp->start) && be32(bytes, pasp->start + 4)) {
            video.aspectNum = static_cast<int>(std::min<uint32_t>(be32(bytes, pasp->start), 1000));
            video.aspectDen = static_cast<int>(std::min<uint32_t>(be32(bytes, pasp->start + 4), 1000));
        }
    const Box *avcc = find(extensions, "avcC");
    if (!avcc || avcc->size < 7) {
        error = "The H.264 description has no parameter sets";
        return false;
    }
    video.nalLength = (static_cast<uint8_t>(bytes[avcc->start + 4]) & 3) + 1;
    size_t at = avcc->start + 5;
    const size_t end = avcc->start + avcc->size;
    video.parameterSets.clear();
    for (int list = 0; list < 2; ++list) {
        if (at >= end)
            break;
        unsigned count = static_cast<uint8_t>(bytes[at++]) & (list ? 0xff : 0x1f);
        for (unsigned i = 0; i < count; ++i) {
            if (end - at < 2 || end - at - 2 < be16(bytes, at)) {
                error = "The H.264 parameter sets are damaged";
                return false;
            }
            const size_t length = be16(bytes, at);
            video.parameterSets.append("\0\0\0\1", 4);
            video.parameterSets.append(bytes, at + 2, length);
            at += 2 + length;
        }
    }
    if (video.parameterSets.empty()) {
        error = "The H.264 description has no parameter sets";
        return false;
    }
    return true;
}
} // namespace

bool parseMp4Video(const std::string &bytes, Mp4Video &video, std::string &error) {
    video = Mp4Video();
    std::vector<Box> top;
    children(bytes, 0, bytes.size(), top, true);
    const Box *moov = find(top, "moov");
    if (!moov) {
        error = "No movie index at the start of the file";
        return false;
    }
    std::vector<Box> tracks;
    if (!children(bytes, moov->start, moov->start + moov->size, tracks)) {
        error = "The movie index is damaged";
        return false;
    }
    std::vector<Box> stbl;
    uint32_t timescale = 0;
    bool found = false;
    for (const Box &box : tracks)
        if (box.type == "trak" && videoTrack(bytes, box, stbl, timescale)) {
            found = true;
            break;
        }
    if (!found) {
        error = "No video track";
        return false;
    }
    const Box *stsd = find(stbl, "stsd"), *stts = find(stbl, "stts"), *stsz = find(stbl, "stsz"),
              *stsc = find(stbl, "stsc"), *stco = find(stbl, "stco"), *co64 = find(stbl, "co64"),
              *stss = find(stbl, "stss");
    if (!stsd || !stts || !stsz || !stsc || (!stco && !co64)) {
        error = "The video track has no sample table";
        return false;
    }
    if (!sampleEntry(bytes, *stsd, video, error))
        return false;

    // Sizes: one for all, or one each.
    if (stsz->size < 12) {
        error = "The sample sizes are damaged";
        return false;
    }
    const uint32_t fixedSize = be32(bytes, stsz->start + 4);
    const uint32_t sampleCount = be32(bytes, stsz->start + 8);
    if (sampleCount > kSamplesMax || (!fixedSize && sampleCount > (stsz->size - 12) / 4)) {
        error = "The sample sizes are damaged";
        return false;
    }
    uint32_t chunkCount = 0, runCount = 0, timeCount = 0, keyCount = 0;
    size_t chunkFirst = 0, runFirst = 0, timeFirst = 0, keyFirst = 0;
    const bool wideOffsets = !stco;
    if (!entries(bytes, wideOffsets ? *co64 : *stco, wideOffsets ? 8 : 4, chunkCount, chunkFirst) ||
        !entries(bytes, *stsc, 12, runCount, runFirst) || !runCount ||
        !entries(bytes, *stts, 8, timeCount, timeFirst) ||
        (stss && !entries(bytes, *stss, 4, keyCount, keyFirst))) {
        error = "The sample table is damaged";
        return false;
    }
    std::vector<uint32_t> keys;
    for (uint32_t i = 0; i < keyCount; ++i)
        keys.push_back(be32(bytes, keyFirst + 4 * i));
    std::sort(keys.begin(), keys.end());

    std::vector<Mp4Sample> samples;
    samples.reserve(sampleCount);
    uint32_t run = 0, timeEntry = 0, timeLeft = timeCount ? be32(bytes, timeFirst) : 0;
    uint64_t ticks = 0;
    for (uint32_t chunk = 1; chunk <= chunkCount && samples.size() < sampleCount; ++chunk) {
        while (run + 1 < runCount && be32(bytes, runFirst + 12 * (run + 1)) <= chunk)
            ++run;
        const uint32_t perChunk = be32(bytes, runFirst + 12 * run + 4);
        uint64_t offset = wideOffsets ? be64(bytes, chunkFirst + 8 * (chunk - 1))
                                      : be32(bytes, chunkFirst + 4 * (chunk - 1));
        for (uint32_t i = 0; i < perChunk && samples.size() < sampleCount; ++i) {
            const size_t index = samples.size();
            Mp4Sample sample;
            sample.offset = offset;
            sample.size = fixedSize ? fixedSize : be32(bytes, stsz->start + 12 + 4 * index);
            sample.atMs = 1000.0 * static_cast<double>(ticks) / timescale;
            sample.key = !stss || std::binary_search(keys.begin(), keys.end(),
                                                     static_cast<uint32_t>(index + 1));
            while (!timeLeft && timeEntry + 1 < timeCount)
                timeLeft = be32(bytes, timeFirst + 8 * ++timeEntry);
            const uint32_t delta = timeCount ? be32(bytes, timeFirst + 8 * timeEntry + 4) : 0;
            if (timeLeft)
                --timeLeft;
            ticks += delta;
            offset += sample.size;
            samples.push_back(sample);
        }
    }
    // The clip ends at the first sample the bytes do not hold.
    for (const Mp4Sample &sample : samples) {
        if (!sample.size || sample.offset > bytes.size() || sample.size > bytes.size() - sample.offset)
            break;
        video.samples.push_back(sample);
    }
    if (video.samples.empty() || !video.samples[0].key) {
        error = "The file holds no whole video sample";
        return false;
    }
    const size_t kept = video.samples.size();
    video.durationMs = kept < samples.size() ? samples[kept].atMs
                                             : 1000.0 * static_cast<double>(ticks) / timescale;
    return true;
}

bool annexBSample(const std::string &bytes, const Mp4Video &video, size_t index, std::string &unit) {
    unit.clear();
    if (index >= video.samples.size() || video.nalLength < 1 || video.nalLength > 4)
        return false;
    const Mp4Sample &sample = video.samples[index];
    if (sample.offset > bytes.size() || sample.size > bytes.size() - sample.offset)
        return false;
    if (sample.key)
        unit = video.parameterSets;
    size_t at = static_cast<size_t>(sample.offset);
    const size_t end = at + sample.size;
    while (at < end) {
        if (end - at < static_cast<size_t>(video.nalLength))
            return false;
        size_t length = 0;
        for (int i = 0; i < video.nalLength; ++i)
            length = (length << 8) | static_cast<uint8_t>(bytes[at + i]);
        at += video.nalLength;
        if (length > end - at)
            return false;
        unit.append("\0\0\0\1", 4);
        unit.append(bytes, at, length);
        at += length;
    }
    return true;
}
} // namespace nativetv
