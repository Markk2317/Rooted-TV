#pragma once
#include "domain/models.hpp"
#include "ui/kit/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// What Home's controller and view agree on: how wide a row's items are and the ids of the
// animated values. Ids stay under 16 characters so building one allocates nothing.
namespace nativetv {
namespace home {
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
// An app's own colour, as the TV gives it; without one a steady one per app, dark enough for
// white text.
inline Color appColor(const Card &card) {
    std::string value = card.data["color"].string();
    if (!value.empty() && value[0] == '#')
        value.erase(0, 1);
    if (value.size() == 6 && value.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
        return kit::rgb(static_cast<unsigned>(std::strtoul(value.c_str(), nullptr, 16)));
    return hsl(static_cast<float>(hashOf(card.id) % 360), .5f, .36f);
}
inline bool isApps(const CardRow &row) {
    return row.key == "app";
}
// Google TV's large cards over the rows, with no title: the Films and Shows tabs' featured titles,
// and the Apps tab's banners.
inline bool isFeature(const CardRow &row) {
    return row.key == "feature" || row.key == "appfeature";
}
// The Apps tab's bar that searches the apps, and its row of category buttons.
inline bool isSearch(const CardRow &row) {
    return row.key == "search";
}
inline bool isChips(const CardRow &row) {
    return row.key == "chips";
}
// The Apps tab's rows of wide app cards: a category's apps, and the stores to get more from.
inline bool isAppCards(const CardRow &row) {
    return row.key == "appkind" || row.key == "appstore";
}
// Google TV's rows come in three sizes: the top picks' small cards, Your apps' circles, and the
// larger cards of every row of titles under them (Continue watching and the rest), which the Apps
// tab's rows of apps share.
inline bool largeCards(const CardRow &row) {
    return !isApps(row) && !isFeature(row) && !isSearch(row) && !isChips(row) && row.key != "trending" &&
           row.key != "dev" && row.key != "ch";
}
inline float itemWidth(const CardRow &row) {
    return isApps(row)       ? kit::layout::appD
           : isFeature(row)  ? kit::layout::featureW
           : isSearch(row)   ? kit::layout::appsSearchW
           : isChips(row)    ? kit::layout::appsChipW
           : largeCards(row) ? kit::layout::cardLW
                             : kit::layout::cardW;
}
inline float itemHeight(const CardRow &row) {
    return isApps(row)       ? kit::layout::appD
           : isFeature(row)  ? kit::layout::featureH
           : isSearch(row)   ? kit::layout::appsSearchH
           : isChips(row)    ? kit::layout::appsChipH
           : largeCards(row) ? kit::layout::cardLH
                             : kit::layout::cardH;
}
inline float itemGap(const CardRow &row) {
    return isFeature(row)    ? kit::layout::featureGap
           : isChips(row)    ? kit::layout::appsChipGap
           : largeCards(row) ? kit::layout::cardLGap
                             : isApps(row) ? kit::layout::appGap
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
// `names` (0..1) while every app's name shows under it, `captions` (0..1) while its cards have
// their lines under them (the Films and Shows tabs' focused row, as Google TV's).
struct RowLook {
    float up = 0, risen = 0, grow = 0, names = 0, captions = 0;
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
// From the row's top to its items: the title's area, or nothing for a row with no title (the
// featured cards, the Apps tab's search bar).
inline float itemsOffset(const CardRow &row, const RowLook &look) {
    return isFeature(row) || isSearch(row) ? 0 : labelArea(look);
}
// Where a risen row's items stand: under the focused row's large title at rowsRisenTop. The Apps
// tab's search bar, which has no title, rises to there itself.
inline float risenItemsTop() {
    RowLook risen;
    risen.up = risen.risen = risen.grow = 1;
    return kit::layout::rowsRisenTop + labelArea(risen);
}
// The gap under a row's items: wider once anything under the bar has the focus, wider still once
// the rows have risen.
inline float rowFoot(const RowLook &look, float risen = kit::layout::rowGapRisen) {
    return kit::focus::mix(kit::focus::mix(kit::layout::rowGap, kit::layout::rowGapUp, look.up), risen,
                           look.risen);
}
// From the title's top to the next row's title (`next`: the row under it, if any). Google TV's Apps
// tab, as measured on a Chromecast with Google TV: its banners are spaced as rows are; its search
// bar stands 60 px under Your apps' circles (and their names, while they show) whatever the
// focus, with 62 px (48 once a row has the focus) to the categories' title; its focused category
// row and focused row of apps keep room for the line under the focused item.
inline float rowSpan(const CardRow &row, const RowLook &look, const CardRow *next = nullptr) {
    if (row.key == "feature")
        return kit::layout::featureH + kit::layout::featureFoot;
    if (row.key == "appfeature")
        return kit::layout::featureH + rowFoot(look);
    if (isSearch(row))
        return kit::layout::appsSearchH +
               kit::focus::mix(kit::layout::appsSearchFoot, kit::layout::appsSearchFootUp, look.up);
    if (isApps(row) && next && isSearch(*next))
        return labelArea(look) + kit::layout::appD + kit::layout::appsSearchGap +
               kit::layout::appsSearchNames * look.names;
    if (isChips(row))
        return labelArea(look) + kit::layout::appsChipH + rowFoot(look) + kit::layout::appsChipFoot * look.grow;
    if (isAppCards(row))
        return labelArea(look) + kit::layout::cardLH + rowFoot(look, kit::layout::appCardRowGap) +
               kit::layout::appCardCaptionFoot * look.captions;
    float foot = rowFoot(look);
    if (isApps(row))
        foot += kit::layout::appNames * look.names;
    // Continue watching has two lines under its cards, and so, while it has them, has the Films
    // and Shows tabs' focused row.
    const float captions = kit::layout::cardCaptionGap + 2 * kit::layout::cardCaptionLine;
    foot += captions * (row.key == "continue" ? 1 : look.captions);
    return labelArea(look) + itemHeight(row) + foot;
}
// Each row's title top, the first at `top`.
inline std::vector<float> rowTops(const std::vector<CardRow> &rows, const std::vector<RowLook> &looks,
                                  float top = kit::layout::rowsTop) {
    std::vector<float> tops;
    for (size_t i = 0; i < rows.size(); ++i) {
        tops.push_back(top);
        top += rowSpan(rows[i], i < looks.size() ? looks[i] : RowLook(), i + 1 < rows.size() ? &rows[i + 1] : nullptr);
    }
    return tops;
}
// Row n's items, before the row's own scroll and the stack's offset; `itemsY` is the items' top.
inline Rect item(const CardRow &row, float itemsY, size_t column) {
    if (isSearch(row))
        return {kit::layout::appsSearchX, itemsY, kit::layout::appsSearchW, kit::layout::appsSearchH};
    float width = itemWidth(row), gap = itemGap(row);
    return {kit::layout::homeX + static_cast<float>(column) * (width + gap), itemsY, width,
            itemHeight(row)};
}
} // namespace home
} // namespace nativetv
