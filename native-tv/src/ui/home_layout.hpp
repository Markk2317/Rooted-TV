#pragma once
#include "domain/models.hpp"
#include "ui/kit/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// What Home's controller and view agree on: how wide a row's items are and the ids of the
// animated values. Ids stay under 16 characters so building one allocates nothing.
namespace nativetv {
namespace home {
constexpr size_t kGridColumns = static_cast<size_t>(kit::layout::tileCols);
constexpr float kGridPitch = kit::layout::tilePitchY;
constexpr size_t kSpotlightMax = 6;

// Hue in degrees, saturation and lightness in 0..1.
inline Color hsl(float hue, float saturation, float lightness) {
    float chroma = (1 - std::fabs(2 * lightness - 1)) * saturation, h = hue / 60;
    float x = chroma * (1 - std::fabs(std::fmod(h, 2.0f) - 1)), m = lightness - chroma / 2;
    float r = 0, g = 0, b = 0;
    if (h < 1) {
        r = chroma;
        g = x;
    } else if (h < 2) {
        r = x;
        g = chroma;
    } else if (h < 3) {
        g = chroma;
        b = x;
    } else if (h < 4) {
        g = x;
        b = chroma;
    } else if (h < 5) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }
    return {r + m, g + m, b + m, 1};
}
inline unsigned hashOf(const std::string &text) {
    unsigned hash = 2166136261u;
    for (unsigned char c : text)
        hash = (hash ^ c) * 16777619u;
    return hash;
}
inline bool isApps(const CardRow &row) {
    return row.key == "app";
}
// The Films and Shows tabs' featured titles: Google TV's large cards over the rows, with no title.
inline bool isFeature(const CardRow &row) {
    return row.key == "feature";
}
// Google TV's rows come in three sizes: the top picks' small cards, Your apps' circles, and the
// larger cards of every row of titles under them (Continue watching and the rest).
inline bool largeCards(const CardRow &row) {
    return !isApps(row) && !isFeature(row) && row.key != "trending" && row.key != "dev" &&
           row.key != "ch";
}
inline float itemWidth(const CardRow &row) {
    return isApps(row)      ? kit::layout::appD
           : isFeature(row) ? kit::layout::featureW
           : largeCards(row) ? kit::layout::cardLW
                             : kit::layout::cardW;
}
inline float itemHeight(const CardRow &row) {
    return isApps(row)      ? kit::layout::appD
           : isFeature(row) ? kit::layout::featureH
           : largeCards(row) ? kit::layout::cardLH
                             : kit::layout::cardH;
}
// `named`: a row of apps that carries every name (the Apps page), so the circles stand wider.
inline float itemGap(const CardRow &row, bool named = false) {
    return named && isApps(row) ? kit::layout::appGapNamed
           : isFeature(row)     ? kit::layout::featureGap
           : largeCards(row)    ? kit::layout::cardLGap
                                : kit::layout::cardGap;
}
inline std::string id(const char *name, size_t row, size_t column) {
    return std::string(name) + ":" + std::to_string(row) + "." + std::to_string(column);
}
inline std::string id(const char *name, size_t index) {
    return std::string(name) + ":" + std::to_string(index);
}
// The pointer's id for an item: "card:<row>:<column>".
inline std::string hit(size_t row, size_t column) {
    return "card:" + std::to_string(row) + ":" + std::to_string(column);
}

// How a row stands, from the animated values: `up` (0..1) once anything under the bar has the
// focus, `risen` (0..1) while the rows are over the banner, `grow` (0..1) for the focused row then,
// `names` (0..1) while every app's name shows under it.
struct RowLook {
    float up = 0, risen = 0, grow = 0, names = 0;
};
// The row's title: 24 px under the banner at rest, 36 px once a row or the banner has the focus,
// 63 px for the focused row once the rows have risen (Google TV's sizes at 1080p, measured on a
// Chromecast with Google TV).
inline float labelPx(const RowLook &look) {
    return kit::focus::mix(24, 36, look.up) + (63 - 36) * look.grow;
}
// From the title's top to the items: 55 px, 69 px, and 121 px for the focused risen row.
inline float labelArea(const RowLook &look) {
    return kit::focus::mix(55, 69, look.up) + (121 - 69) * look.grow;
}
// From the row's top to its items: the title's area, or nothing for the featured row.
inline float itemsOffset(const CardRow &row, const RowLook &look) {
    return isFeature(row) ? 0 : labelArea(look);
}
// From the title's top to the next row's title.
inline float rowSpan(const CardRow &row, const RowLook &look) {
    if (isFeature(row))
        return kit::layout::featureH + kit::layout::featureFoot;
    float foot = kit::focus::mix(kit::focus::mix(kit::layout::rowGap, kit::layout::rowGapUp, look.up),
                                 kit::layout::rowGapRisen, look.risen);
    if (isApps(row))
        foot += kit::layout::appNames * look.names;
    // Continue watching has two lines under its cards.
    if (row.key == "continue")
        foot += kit::layout::cardCaptionGap + 2 * kit::layout::cardCaptionLine;
    return labelArea(look) + itemHeight(row) + foot;
}
// Each row's title top, the first at `top`.
inline std::vector<float> rowTops(const std::vector<CardRow> &rows, const std::vector<RowLook> &looks,
                                  float top = kit::layout::rowsTop) {
    std::vector<float> tops;
    for (size_t i = 0; i < rows.size(); ++i) {
        tops.push_back(top);
        top += rowSpan(rows[i], i < looks.size() ? looks[i] : RowLook());
    }
    return tops;
}
// Row n's items, before the row's own scroll and the stack's offset; `itemsY` is the items' top.
inline Rect item(const CardRow &row, float itemsY, size_t column, bool named = false) {
    float width = itemWidth(row), gap = itemGap(row, named);
    return {kit::layout::homeX + static_cast<float>(column) * (width + gap), itemsY, width,
            itemHeight(row)};
}
// The apps grid's scroll target while focus is on `column`: three rows fit under the page
// title, so the grid follows once focus passes the second row, and stops when the last row is
// in view.
inline float gridScrollTarget(size_t column, size_t count) {
    size_t rows = (count + kGridColumns - 1) / kGridColumns, at = column / kGridColumns;
    float visible = 3;
    float room = rows > visible ? (static_cast<float>(rows) - visible) * kGridPitch : 0;
    float wanted = at > 1 ? static_cast<float>(at - 1) * kGridPitch : 0;
    return -(wanted > room ? room : wanted);
}
// The Apps page by kind: rows of named circles, one after another from layout::appsTop.
inline float kindPitch() {
    return labelArea(RowLook()) + kit::layout::appD + kit::layout::rowGap + kit::layout::appNames;
}
// Its scroll target while focus is on kind row `row` of `rows`: the rows keep their place until
// focus passes the second, then the page follows so the next row stays in view, and the last rows
// end flush with the screen.
inline float kindStackTarget(size_t row, size_t rows) {
    const size_t visible = static_cast<size_t>((kit::canvas::height - kit::layout::appsTop) / kindPitch());
    size_t last = rows > visible ? rows - visible : 0;
    size_t first = row > 1 ? row - 1 : 0;
    return -kindPitch() * static_cast<float>(std::min(first, last));
}
} // namespace home
} // namespace nativetv
