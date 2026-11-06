#pragma once
#include "ui/kit/focus.hpp"
#include <algorithm>
#include <string>
#include <vector>

// Layout boxes at rest on the 1920x1080 canvas (spec section 3), as plain functions of the
// layout tokens. tests/kit.cpp checks them against design/metrics.json, the kit's record of the
// same boxes (measured from the CSS prototype at first, kept by hand since).
namespace nativetv {
namespace kit {
namespace geometry {
constexpr float clamp(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

// Top bar.
constexpr Rect avatar() {
    return {canvas::safeX, layout::navTop + (layout::navH - layout::circle) / 2, layout::circle,
            layout::circle};
}
// As on Google TV since its 2025 redesign, a capsule after the avatar holds search, where it is
// offered, and the tabs; the gear has a capsule of its own after it.
constexpr float capsuleLeft() {
    return canvas::safeX + layout::circle + layout::avatarGap;
}
// A capsule round the bar's items from `left` to `right`, a little taller than the tab pill.
constexpr Rect capsule(float left, float right) {
    return {left, layout::navTop - layout::capsuleInset, right - left,
            layout::navH + 2 * layout::capsuleInset};
}
// Search sits in the capsule's rounded end, as far from its edge as from its top.
constexpr Rect searchButton() {
    return {capsuleLeft() + 2 * layout::capsulePad,
            layout::navTop + (layout::navH - layout::circle) / 2, layout::circle, layout::circle};
}
constexpr float tabsLeft(bool search = false) {
    return capsuleLeft() + layout::capsulePad +
           (search ? layout::circle + 2 * layout::capsulePad : 0);
}
constexpr float tabWidth(float textWidth) {
    return textWidth + 2 * layout::tabPad;
}
// Tabs touch; each is its text plus the pad on both sides.
inline std::vector<Rect> tabRects(const std::vector<float> &textWidths, bool search = false) {
    std::vector<Rect> rects;
    float x = tabsLeft(search);
    for (float width : textWidths) {
        rects.push_back({x, layout::navTop, tabWidth(width), layout::navH});
        x += tabWidth(width);
    }
    return rects;
}
// The pill spans two independently animated edges.
constexpr Rect pill(float left, float right) {
    return {left, layout::navTop, right - left, layout::navH};
}
// The gear's capsule follows the tabs' capsule, which ends a pad after the last tab.
constexpr Rect settingsButton(float tabsRight) {
    return {tabsRight + layout::capsulePad + layout::capsuleGap + 2 * layout::capsulePad,
            layout::navTop + (layout::navH - layout::circle) / 2, layout::circle, layout::circle};
}
// The clock ends where the divider before the wordmark begins; `wordmarkWidth` is the
// wordmark's text, which ends at the right margin.
constexpr Rect clock(float wordmarkWidth) {
    return {canvas::width - canvas::safeX - wordmarkWidth - 2 * layout::wordmarkGap - 2 -
                layout::clockW,
            layout::navTop + (layout::navH - type::clock.line) / 2, layout::clockW,
            type::clock.line};
}

// Hero block, bottom-left of Home.
constexpr Rect heroSource() {
    return {canvas::safeX, layout::heroSourceTop, layout::heroW, layout::heroSourceH};
}
constexpr Rect heroTitle() {
    return {canvas::safeX, heroSource().y + layout::heroSourceH + layout::heroTitleGap,
            layout::heroW, layout::heroTitleH};
}
constexpr Rect heroSubtitle() {
    return {canvas::safeX, heroTitle().y + layout::heroTitleH + layout::heroSubGap, layout::heroW,
            layout::heroSubH};
}
// Two lines of description, or an app's key hint, under the facts.
constexpr Rect heroDescription() {
    return {canvas::safeX, heroSubtitle().y + layout::heroSubH + layout::heroDescGap,
            layout::heroDescW, layout::heroDescH};
}
// Pagination slots end at the right margin.
constexpr Rect dotSlot(int index, int count = layout::dotSlots) {
    return {canvas::width - canvas::safeX - count * layout::dotSlotW + index * layout::dotSlotW,
            layout::dotsTop, layout::dotSlotW, layout::dotSlotH};
}

// Home rows: label, then items 36 px below, 212 px per row.
constexpr float rowLabelY(int row) {
    return layout::rowsTop + layout::rowPitch * row;
}
constexpr float rowItemsY(int row) {
    return rowLabelY(row) + layout::rowLabelH + layout::rowLabelGap;
}
constexpr Rect rowLabel(int row) {
    return {canvas::safeX, rowLabelY(row), canvas::width - canvas::safeX, layout::rowLabelH};
}
constexpr Rect card(int row, int index) {
    return {canvas::safeX + index * (layout::cardW + layout::cardGap), rowItemsY(row),
            layout::cardW, layout::cardH};
}
constexpr Rect inputTile(int row, int index) {
    return card(row, index);
}
constexpr Rect appCircle(int row, int index) {
    return {canvas::safeX + index * (layout::appD + layout::appGap), rowItemsY(row), layout::appD,
            layout::appD};
}
// Width of a row's content, without the trailing gap.
constexpr float rowContentWidth(int count, float itemWidth, float gap) {
    return count <= 0 ? 0 : count * (itemWidth + gap) - gap;
}
// Horizontal scroll target (<= 0): focus stays in the 4th slot once it passes it.
inline float rowScrollTarget(int focus, int count, float itemWidth, float gap) {
    float pitch = itemWidth + gap, keep = static_cast<float>(std::min(focus, layout::rowKeep));
    float room = std::max(0.0f, rowContentWidth(count, itemWidth, gap) -
                                    (canvas::width - 2 * canvas::safeX));
    return -clamp((focus - keep) * pitch, 0, room);
}
// Vertical snap: the whole stack moves one row pitch per content row; the bar keeps it at 0.
constexpr float stackTarget(int contentRow) {
    return contentRow <= 0 ? 0 : -layout::rowPitch * contentRow;
}
// Everything the stack offset `y` (<= 0) drives while the rows scroll over the hero.
// scrimOpacity multiplies alpha::scrimStrong, so the veil tops out at .86 * .84.
struct HeroParallax {
    float textY, textAlpha, artY, scrimOpacity;
};
constexpr float scrimMaxOpacity = .84f;
constexpr HeroParallax heroParallax(float y) {
    return {y * .5f, 1 - focus::clamp01(-y / 150), y * .12f,
            focus::clamp01(-y / layout::rowPitch) * scrimMaxOpacity};
}

// Page heads (Live, Settings, the catalogue). The Apps page has none: it starts at layout::appsTop.
constexpr Rect pageTitle(float textWidth) {
    return {canvas::safeX, layout::pageTitleTop, textWidth, type::headline.line};
}
constexpr Rect pageSubtitle(float textWidth) {
    return {canvas::safeX, layout::pageSubTop, textWidth, type::body.line};
}
constexpr Rect appTile(int index) {
    return {canvas::safeX + (index % layout::tileCols) * layout::tilePitchX,
            layout::appsTop + (index / layout::tileCols) * layout::tilePitchY, layout::tileW,
            layout::tileH};
}
// The Homebrew catalogue's banners: 16:9 tiles, six to a row, three rows above its strip.
constexpr Rect banner(int index) {
    return {canvas::safeX + (index % layout::tileCols) * (layout::bannerW + layout::bannerGapX),
            layout::bannerTop + (index / layout::tileCols) * (layout::bannerH + layout::bannerGapY),
            layout::bannerW, layout::bannerH};
}
constexpr Rect statCard(int index) {
    return {canvas::safeX + (index % layout::statCols) * (layout::statW + layout::statGapX),
            layout::gridTop + (index / layout::statCols) * (layout::statH + layout::statGapY),
            layout::statW, layout::statH};
}

// Settings: a clipped list and a detail panel.
constexpr Rect settingsViewport() {
    return {canvas::safeX, layout::listTop, layout::listW, layout::listH};
}
constexpr float settingsRowPitch() {
    return layout::srowH + layout::srowGap;
}
// List content starts 8 px inside the viewport.
constexpr Rect settingsRow(int index) {
    return {canvas::safeX, layout::listTop + layout::srowGap + index * settingsRowPitch(),
            layout::listW, layout::srowH};
}
// Scroll target (<= 0): the list follows once focus passes the 4th row.
inline float settingsScrollTarget(int focus, int count) {
    float content = count * settingsRowPitch() + 2 * layout::srowGap;
    float room = std::max(0.0f, content - (layout::listH + 2 * layout::srowGap));
    return -clamp((focus - 3) * settingsRowPitch(), 0, room);
}
constexpr Rect detailPanel() {
    return {layout::panelX, layout::panelY, layout::panelW, layout::panelH};
}

// Keyboard: key rows start at (originX, originY); key widths vary, the gap does not.
constexpr float keyRowPitch() {
    return layout::keyH + layout::keyGap;
}
inline float keyWidth(const std::string &key) {
    if (key == "shift" || key == "delete")
        return layout::keyWide;
    if (key == "space")
        return layout::keySpace;
    if (key == "done")
        return layout::keyDone;
    return layout::keyW;
}
inline std::vector<Rect> keyRow(float originX, float originY, int row,
                                const std::vector<std::string> &keys) {
    std::vector<Rect> rects;
    float x = originX, y = originY + row * keyRowPitch();
    for (const auto &key : keys) {
        rects.push_back({x, y, keyWidth(key), layout::keyH});
        x += keyWidth(key) + layout::keyGap;
    }
    return rects;
}
} // namespace geometry
} // namespace kit
} // namespace nativetv
