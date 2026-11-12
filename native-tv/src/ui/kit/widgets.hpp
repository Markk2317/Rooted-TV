#pragma once
#include "ui/kit/focus.hpp"
#include "ui/kit/geometry.hpp"
#include <string>
#include <vector>

// The TV design kit's components (spec section 8) as pure functions: state in (focus 0..1 and the
// other animated values the motion engine owns), Scene v2 nodes and hit rectangles out. Nothing
// here keeps state, so a view stays a thin read of its animated values.
//
// Components draw at their resting rect and scale by pushing a scene transform, so text and
// icons grow with the card as they do in the prototype. A component whose entrance has not
// started (alpha 0) or that is wholly off the canvas draws nothing.
namespace nativetv {
namespace kit {

// Text and icons come from the glyph and icon modules; the kit asks for them by style and name.
class Glyphs {
  public:
    virtual ~Glyphs() = default;
    // Advance of one line in canvas px, for tab and chip widths.
    virtual float measure(const std::string &text, const TypeStyle &style) const = 0;
    // `box` is the text's layout box; the line ends in an ellipsis where it overflows the width.
    virtual void text(Scene &scene, Rect box, const std::string &text, const TypeStyle &style,
                      Color color, Node::Align align = Node::Align::Left, int maxLines = 1) = 0;
    // A Material icon by file name ("settings", "hdmi"), square in `box`.
    virtual void icon(Scene &scene, Rect box, const std::string &name, Color tint) = 0;
};

// Text and icons as the legacy Text and Image nodes, which the renderer draws today. It ignores
// tracking and measures by a per-character estimate; the glyph-atlas implementation replaces it.
class LegacyGlyphs final : public Glyphs {
  public:
    float measure(const std::string &text, const TypeStyle &style) const override;
    void text(Scene &scene, Rect box, const std::string &text, const TypeStyle &style, Color color,
              Node::Align align, int maxLines) override;
    void icon(Scene &scene, Rect box, const std::string &name, Color tint) override;
};

struct Paint {
    Scene &scene;
    Glyphs &glyphs;
};

// A title's facts line ("Film  ·  2026  ·  ★ 6.4") with the star drawn as an icon; a line too
// wide for `box` loses whole items from its end.
void factsLine(Paint paint, Rect box, const std::string &facts, const TypeStyle &style, Color ink);

// Page entrance and exit: fades in while rising from `dy` px below (spec 5.6).
struct Enter {
    float alpha = 1, dy = 0;
};

// ---- Top bar --------------------------------------------------------------------------------

struct TopBar {
    std::string avatarInitial;
    std::vector<std::string> tabs;
    // The selected tab keeps the pill; -1 when another page owns the bar (Settings).
    int selected = 0;
    // The pill's two edges are animated separately so it stretches toward its target.
    float pillLeft = 0, pillRight = 0;
    float pillHot = 0;   // 0..1: focus is on the bar, so the pill turns from rest to focus fill
    float pillAlpha = 1; // fades out while Settings or Setup is showing
    float avatarFocus = 0, settingsFocus = 0, searchFocus = 0;
    bool settingsSelected = false;
    // Search leads the tabs' capsule where titles can be searched; `searchSelected` while its page
    // shows, as Google TV marks it with a grey slot.
    bool search = false, searchSelected = false;
    // The assistant leads the gear's capsule where a Gemini key is saved, as Google TV's Gemini.
    bool assistant = false;
    float assistantFocus = 0;
    float saverFocus = 0; // the screen saver's button, before the gear
    std::string clock;
    // After the clock and a divider, as Google TV ends its bar with its name.
    std::string wordmark = "Native Tv";
    Enter enter;
};
// Tab boxes for the bar's tabs, so the view can aim the pill's springs at them; `search` moves
// them along to make room for it.
std::vector<Rect> tabRects(const Glyphs &glyphs, const std::vector<std::string> &tabs,
                           bool search = false);
// Hits are "nav:avatar", "tool:2" (search), "nav:tab:<i>", "tool:3" (assistant) and "nav:settings".
void topBar(Paint paint, const TopBar &bar);
void tabPill(Paint paint, float left, float right, float hot, float alpha = 1);

struct RoundButton {
    Rect rest;
    std::string id;
    std::string icon;    // Material icon, or
    std::string text;    // a letter (the avatar)
    bool avatar = false; // gradient fill instead of `fill`
    Color fill = color::surface3; // opaque: the icon keeps its contrast over bright art
    Color ink = color::onSurface;
    Color focusFill = color::focusFill;
    std::string tooltip; // under the button while it has the focus, as on Google TV's bar
    float focus = 0; // fill fades to focus fill, ink to on-focus, scale to 1.12
    Enter enter;
};
void roundButton(Paint paint, const RoundButton &button);
// The profile's face where it has no picture: the avatar's warm-to-violet sweep.
Gradient avatarGradient();

// ---- Hero and pagination ---------------------------------------------------------------------

struct HeroSlot {
    std::string image;  // art; empty draws only the washes
    float opacity = 0;  // crossfade
    float kenBurns = 0; // 0..1 over the 14 s push
    float artAlpha = 1; // the picture alone: dimmed to a wallpaper behind a focused app
};
struct Hero {
    HeroSlot slot[2];    // outgoing and incoming art
    float wallAlpha = 1; // 0 off the Home page
    float stackY = 0;    // the rows' vertical offset (<= 0) that drives the parallax
    std::string trailer; // a film's clip, drawn over the art
    float trailerAlpha = 0;
    // A title: source row, title, and one line of facts, or, in the banner's own carousel, why it
    // is shown (Google TV's "Recommended for you"). An app: its icon beside the lines, and the key
    // hint under them. wordmark: the service's wordmark (an icon file, drawn white) in the source
    // row's place, as Google TV shows it over the banner's title.
    std::string source, title, facts, description, image, hint, tag, wordmark;
    std::string logo; // the title's logo, drawn in a preview's title place, its foot on the title's
    // Google TV's banner with the focus: the description, one line, under the line, fading in with
    // `expand`, and `button` under that with `buttonAlpha`; `drop` px it has moved down.
    std::string more, button;
    float expand = 0, buttonAlpha = 0, drop = 0;
    // A focused title of another row (Google TV's preview): its logo or title higher up, then its
    // service, `more` (its overview, two lines) and its facts.
    bool preview = false;
    float textAlpha = 1; // the text leaves while the rows below have the focus
    float rowsFocus = 0; // 1 while a row under the banner has the focus: the art's washes change
    // Source row, title and the lines under it enter one after another (spec 5.3).
    Enter text[3];
};
// The artwork, tints, legibility scrims and the veil that rises with the rows.
void heroWall(Paint paint, const Hero &hero);
// The wall of pages without a hero: two soft washes, the second in the page's tint.
void ambientWall(Paint paint, float amount, Color tint = color::ink);
// Source row, title and "tag | description", fading with the stack's parallax.
void heroText(Paint paint, const Hero &hero);
inline void hero(Paint paint, const Hero &state) {
    heroWall(paint, state);
    heroText(paint, state);
}

// Progress of the current slide, 0..1, from the time since it appeared.
constexpr float heroProgress(float elapsedMs) {
    return focus::clamp01(elapsedMs / motion::ms::heroAdvance);
}
struct Pagination {
    int count = 0;
    std::vector<float> current; // per slot, 0..1: the dot of the title on show grows and lights
    float progress = 0;
    float alpha = 1, y = 0; // y: parallax offset
};
void pagination(Paint paint, const Pagination &dots);

// ---- Home rows --------------------------------------------------------------------------------

struct Row {
    int index = 0;
    std::string label;
    float scrollX = 0; // the row's horizontal spring
    float dim = 0;     // 0..1: focus is in another row
    bool placed = false; // `y` holds the title's top; else the first screen's place for `index`
    float y = 0;
    float px = 24;     // the title's size: 24 over the banner, 29 risen, 52 for the focused row
    float grow = 0;    // 0..1: the focused row's title, white and large
    Enter enter;
};
// The label only; items scroll, the label stays.
void row(Paint paint, const Row &row);
// An item's box inside the scrolled row.
inline Rect rowItem(const Row &row, int index, float width, float gap) {
    return {layout::homeX + index * (width + gap) + row.scrollX, geometry::rowItemsY(row.index),
            width, 0};
}

struct PosterCard {
    Rect rest;
    std::string id, image, title;
    std::string subtitle; // the facts line that appears under the title on focus
    float focus = 0;
    // How far it was watched, 0 to 1, as a bar along the card's foot; below 0, none.
    float progress = -1;
    // Google TV's Continue watching: what OK does, in capitals over the art ("RESUME"), and two
    // lines under the card that the focus leaves in place (the title, then its service and how
    // much is left). With a caption the card prints no title of its own.
    std::string action, caption, captionLine;
    float captionAlpha = 1; // the lines fade in and out with the row that has them
    Enter enter;
};
void card(Paint paint, const PosterCard &card);

// Google TV's featured title on its Films and Shows tabs: an 824x384 card of the title's backdrop,
// its service's wordmark ("wordmark-netflix") at the top left, and at the foot its title and one
// line of its overview, lettered over the art. Focus as a card's.
struct FeatureCard {
    Rect rest;
    std::string id, image, title, line, wordmark;
    float focus = 0;
    Enter enter;
};
void featureCard(Paint paint, const FeatureCard &card);

struct AppCircle {
    Rect rest;
    std::string id, name;
    std::string icon;  // glyph on the circle
    std::string image; // the app's own artwork, masked to the circle
    Color color = color::surface3;
    bool named = false; // the name stays under the circle (the Apps page); else it shows on focus
    float names = 0;    // 0..1: every name shows, as in Google TV's focused Your apps row
    float nameWidth = 0; // the name's box; 0 is a box a little wider than the circle
    float focus = 0;
    Enter enter;
};
void appCircle(Paint paint, const AppCircle &circle);

struct InputTile {
    Rect rest;
    std::string id, name, sub;
    std::string icon = "hdmi";
    bool live = false, unused = false;
    float focus = 0;
    Enter enter;
};
void inputTile(Paint paint, const InputTile &tile);

// ---- Apps and Live ------------------------------------------------------------------------------

struct PageHead {
    std::string title, subtitle;
    Enter enter;
};
void pageHead(Paint paint, const PageHead &head);

struct AppTile {
    Rect rest;
    std::string id, name, icon;
    Color color = color::surface3;
    // A banner, as on Google TV and Apple TV: one flat colour (the app's own), its icon or art
    // (drawn over the tile by the page) large in the middle, and no name on the tile.
    bool centred = false;
    float focus = 0;
    Enter enter;
};
void appTile(Paint paint, const AppTile &tile);

struct StatCard {
    Rect rest;
    std::string id, icon, label, value, unit, caption, caption2;
    bool textValue = false;   // a word ("HDMI 1") rather than a number
    float bar = -1;           // 0..1 fill, or < 0 for none
    bool warn = false;        // the bar turns warn-coloured
    std::vector<float> spark; // 0..1 samples; drawn under the caption when not empty
    float sparkShift = 0;     // 0..1 of a sample: the line slides left between updates
    std::vector<bool> days;   // uptime strip: true = up, false = a grey gap day
    float focus = 0;
    Enter enter;
};
void statCard(Paint paint, const StatCard &card);
// A 3 px line over a faded fill, drawn as 6 px columns (the scene has no polyline).
void sparkline(Paint paint, Rect area, const std::vector<float> &samples, float shift);

// ---- Settings ----------------------------------------------------------------------------------

struct SettingsRow {
    Rect rest;
    std::string id, icon, title, value;
    bool toggle = false;
    // A row whose value OK steps to the next and Left and Right step through: its value has a chevron on each side.
    bool stepper = false;
    float on = 0; // toggle position 0..1
    float focus = 0;
    Enter enter;
};
void settingsRow(Paint paint, const SettingsRow &row);
// 68x38 in `rect`. `rowFocus` darkens the ring and knob once the row's fill turns light.
void toggle(Paint paint, Rect rect, float on, float rowFocus = 0);

struct DetailPanel {
    std::string icon, title, body, action; // an empty action leaves the OK hint out
    std::vector<std::string> chips;
    int bodyLines = 3; // the chips move down with a longer body
    Enter enter;
};
void detailPanel(Paint paint, const DetailPanel &panel);
// A pill with an optional accent dot; returns its width so chips can flow.
float pillChip(Paint paint, float x, float y, const std::string &label, bool dot = true,
               float alpha = 1);

// ---- Loading
// -------------------------------------------------------------------------------------

// The indeterminate circular progress indicator of Material and Google TV: a thin arc in the accent
// colour that turns at a constant rate, with an optional label under it. The scene has no arcs or
// rotation, so the arc is drawn as small dots along it that thin out toward the tail; the whole
// thing is under forty nodes and needs no layer of its own.
struct Loading {
    Rect area;         // the indicator is centred in it, and the label sits under
    std::string label; // "Loading..." or what is being read
    float phase = 0;   // 0..1 of a turn: spinnerPhase() in tuning.hpp turns it at a constant rate
    Enter enter;
};
void loadingIndicator(Paint paint, const Loading &loading);

// ---- Keyboard -----------------------------------------------------------------------------------

struct KeyCap {
    Rect rest;
    std::string id, label, icon;
    bool accent = false; // the Done key
    bool hint = false;   // quiet label (space)
    bool latched = false; // shift while capitals are on: accent ink on a tinted fill
    // Google TV's keyboard (Gboard for TV): its key colours and size, the search key as the accent,
    // and `corner`, the digit a letter of the top row also types, small in its top right.
    bool tv = false;
    std::string corner;
    float focus = 0;
    float press = 0; // 0 at rest, 1 at the bottom of the .94 dip
    Enter enter;
};
void keyCap(Paint paint, const KeyCap &key);

// Google TV's search field, the page of its Search tab: a wide dark pill with a light outline while
// it has the focus, the search icon and the words typed (or the placeholder) at 42 px.
struct SearchField {
    Rect rest;
    std::string id, text, placeholder;
    float focus = 0;
    Enter enter;
};
void searchField(Paint paint, const SearchField &field);

// A tile of Google TV's "Search by genre" grid: the genre's colour, darker at the top, and its name
// in the middle. `lit` (0..1) is the grid with the focus, when the tiles show at full strength;
// otherwise they are at 60% over the page. Focus as a card's, with a white glow.
struct GenreTile {
    Rect rest;
    std::string id, label;
    Color color;
    float lit = 0, focus = 0;
    Enter enter;
};
void genreTile(Paint paint, const GenreTile &tile);

// ---- Progress -----------------------------------------------------------------------------------

// The circular indeterminate progress indicator, for a value that is being read or a change that
// is on its way: twelve dots round a circle, the head one bright and the rest fading behind it,
// one turn in 1.25 s. `timeMs` is the frame's clock; the box is square.
void spinner(Paint paint, Rect box, double timeMs, Color ink = color::onSurface2, float alpha = 1);

// Static frames of the designed screens for capture review: "home", "apps", "live", "settings"
// and "setup". False for any other name.
bool designGallery(Paint paint, const std::string &name);
// The same over the legacy text and icon nodes.
bool designGallery(Scene &scene, const std::string &name);

} // namespace kit
} // namespace nativetv
