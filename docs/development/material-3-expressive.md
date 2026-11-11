---
description: "What Material 3 Expressive specifies for type, colour, shape, spacing, focus, motion and icons, what the Native Tv TV client uses today, and where the two differ."
---

# Material 3 Expressive

The TV client draws every screen from tokens in `native-tv/design/` (`tokens.json` and `metrics.json`; `tools/gen_tokens.py` writes `native-tv/src/ui/kit/tokens.hpp`). The tokens come from Google's TV Design Kit, which is Material 3 at 2 px per dp: the 1920×1080 canvas is 960×540 dp.

Material 3 Expressive is Google's May 2025 update to Material 3. Google's [Android TV design guide](https://developer.android.com/design/ui/tv/guides/foundations/design-for-tv) and `androidx.tv:tv-material` 1.1.0 do not document any adoption of it for TV: the library's theme has colour, shapes and typography, and no motion scheme. This page records what Expressive specifies, what the client does, and the differences. It was checked on 31 October 2026.

A TV screen takes its type, colour, shape, spacing and motion from the tokens, not from literals in a screen's code. A value that departs from the tables below is a decision, and is written here with its reason.

## Typography

| | Material 3 Expressive | Native Tv |
|---|---|---|
| Scale | 30 styles: 15 baseline and 15 emphasized, in five roles (display, headline, title, body, label) of three sizes. A Major Second scale (1.125) on a 14 sp base. | 40 named styles on 16 distinct sizes (`type` in `tokens.hpp`). Each is a baseline style at 2 px per sp: `title` 32/48 is Title Medium (16/24 sp), `body` 28/40 is Body Medium, `labelM` 24/32 is Label Medium, `display` 72/88 is Display Small, `headline` 56/72 is Headline Medium. `numeral` (64/72), `numeralText` (44/68) and `fieldText` (36/48) are off the scale. |
| Emphasis | An emphasized style keeps the size, line height and tracking of its baseline style and raises the weight one step: 400 to 500 for display, headline, title large and body; 500 to 700 (600 on the variable axis) for title medium, title small and label. Components do not use them by default. | Two weights, 400 and 500. Titles and labels use 500, which is their baseline weight. A selected item changes fill and colour, not weight. There is no 600 or 700 cut. |
| Typeface | Roboto for both the brand (display, headline) and the plain (body, label) typeface. Roboto Flex is the variable swap, "not yet part of the M3 typescale". No M3 page names Google Sans Flex as a default. | Google Sans Flex in three static cuts made by `tools/gen_fonts.py`: Display (optical size 36, from 48 px up) and Regular and Medium (optical size 18). No variable axis is used at run time. |
| Tracking | Non-zero on most styles: Label Medium and Body Large 0.5 sp, Title Medium 0.15 sp, Display Large −0.25 sp. | 0 on most styles, 0.01 em on `labelM`, `labelS` and `cap`, 0.08 em on `cardAction` (Google TV's capitals over a Continue watching card), −0.01 and −0.02 em on the large numerals. |
| Line height | 1.2 times the size for display, headline and title; about 1.5 for body and label. | The baseline line heights at 2 px per sp. |
| TV | No minimum text size is documented for TV. | The smallest styles are `quickTileSub` and `quickCardSub`, 18 px (9 sp), the second line of Google TV's quick settings tiles and cards; `cardAction` is 20 px capitals (10 sp), as Google TV sets its RESUME over a card. |

## Colour

| | Material 3 Expressive | Native Tv |
|---|---|---|
| Roles | 26 documented roles on five key colours (a sixth palette, error, in Compose), with fixed roles, surface-dim and surface-bright. Expressive changed two things: three contrast levels (standard, medium, high) and more colourful light-theme `on-*-container` roles. The dark theme is unchanged. Material Color Utilities also offers schemes such as TonalSpot, Vibrant and Expressive. | 28 colour tokens and 15 with alpha. Page `ink` #0C0B0E; `surface1` to `surface3` #16151A, #201F26, #2C2B34; `onSurface` to `onSurface3` #F1EEF4, #B9B4C2, #8A8495; `accent` #A8C7FA on `onAccent` #062E6F; `focusFill` #F1EEF4 on `onFocus` #131118; `warn` #FFB784; `bad` #FF8A80; the bar's selected tab `tabSelected` #E5F1FF (also the banner's button) under `onFocus`, on a `capsule` of white at 12%; `artDim`, black at 18%, over the banner's art while the bar has the focus. No secondary or tertiary role. One dark scheme, no light theme, no dynamic colour. |
| Dark baseline | Surface is neutral tone 6 (#141218); containers lowest to highest are tones 4, 10, 12, 17 and 22. | `ink` is darker than tone 6, for the OLED panel. |
| Art colour | Not specified. | Home tints its wall and circles from an app's or title's colour (`tintOf`, `appColor` in `home_motion.cpp` and `home_view.cpp`), and falls back to a hue derived from the id. |
| Quick settings | Not specified. | Google TV's quick settings colours, from its launcher's theme: the panel #131D23 and its lists #1E232C; a tile #E4F3FF multiplied by a 5% #E8EAED tint (`quickTile`, #CFDEED at 4.7%, #1C262C on the panel); focused, the tint is #E4F3FF over #E4F3FF, #CCE8FF (`quickFocus`), with #0E0E0F text and icons at 80% black (#292E33). Text at rest is #DFF3FF at 54% (`quickInk`), the date at 39%; the page behind is dimmed by 60% black. Each measured on the Chromecast's screen to the pixel. |
| Contrast | 4.5:1 for small text, 3:1 for large text and graphics. | Measured on `ink`: `onSurface` 17.1, `onSurface2` 9.7, `onSurface3` 5.4, `accent` 11.4. `onSurface3` on `surface3` is 3.9. A row label dimmed to 50% (`dimmedRowAlpha`) is 3.2, which meets the large-text limit only. |
| State layers | Hover 8%, focus 10%, pressed 10%, dragged 16%. | None. Focus swaps the fill to `focusFill` and draws a ring. |

## Shape

The Material 3 Expressive scale has ten steps, in dp: none 0, XS 4, S 8, M 12, L 16, L-increased 20, XL 28, XL-increased 32, XXL 48 and full (a pill). Expressive added L-increased, XL-increased and XXL.

The client's radii are px: `sm` 8, `md` 12, `lg` 20, `xl` 28, `key` 18 and `pill`. They carry the same numbers as the dp steps, so at 2 px per dp they are 4, 6, 10, 14 and 9 dp. Of these only 4 dp is a step of the scale.

| | Material 3 Expressive | Native Tv |
|---|---|---|
| State | A list item goes from 4 dp at rest to 16 dp when focused or selected (12 dp hovered); a pressed button turns more square. | Focus never changes a shape: it scales the item and draws a ring. |
| Library | 35 shapes (Cookie, Clover, Sunny, Pill, Burst and others) for decorative elements such as avatars and image crops, used sparingly; any shape morphs into any other. | App icons are circles and tabs are pills. No library shape and no morph. |
| Containment | Cards 12 dp; dialogs and sheets 28 dp; no large corners on dense components. | Poster cards, input tiles and app tiles `md`; stat cards, settings rows and dialogs `lg`; detail panels `xl`. Quick settings takes Google TV's: the panel 40 px (`quickR`, 20 dp), its tiles and cards 32 px (16 dp), the items of its lists 24 px (12 dp). |

## Spacing and layout

| | Material 3 Expressive and Android TV | Native Tv |
|---|---|---|
| Scale | An 8 dp scale with `space100` = 8 dp, and nested 2, 4, 6 and 10 dp. | `space` 4, 8, 12, 16, 24, 32, 48, 64 px, which is 2 to 32 dp. |
| TV canvas | 960×540 dp, shipped as 1080p. Overscan margin 48 dp at the sides and 27 dp top and bottom (the same page also gives 58 and 28 dp). A 12-column grid of 52 dp columns with 20 dp gutters. | 1920×1080 px. Safe margin 80 px at the sides (40 dp) and 48 px top and bottom (24 dp). Rows run to the right edge of the screen. |
| Home | Not specified. | Measured on a Chromecast with Google TV (HD, Android 14, Google TV build of September 2025) at 1080p, from its screenshots, its layout tree (`uiautomator dump`) and screen recordings (`adb`), after a first pass on a Google TV Streamer screenshot and a capture-card video: a 116 px margin on Home and its tabs (`homeX`; Native Tv's own pages keep 80). The bar is centred on y 108: an 80 px avatar, a capsule 88 px high from x 220 (white at 12 %) holding search as a 96 × 72 slot and the tabs (32 px Medium, 34 px each side), a second capsule of 64 px buttons 80 px apart (the assistant's ✦ where a Gemini key is saved, the screen saver, the gear), each showing its name under it when focused, and at the right margin the clock, a 2 × 30 px divider and the wordmark at 40 px in white at 60 %. The selected tab is a light pill (#E5F1FF, #CDDFED with the bar focused) with dark text; the other tab names are at 60 % until the bar has the focus. While it does, the banner's art darkens by 18 % (`artDim`) and its lines and the rows keep their light; the Apps page darkens all of it by 32 %. The banner: the service's white wordmark (Netflix, Prime Video, Disney+) with its foot at y 321, the title in Google Sans Flex 90/112 within 968 px, set smaller when longer (Google TV's long titles come down to about 75 px), and one 32 px Medium line saying why it is shown ("Trending") with the dots on it (16 px, all one size, the current one white, the others at 18 %). Its art fills the screen; a scrim in the art's own colour (the left third's mean, very dark) covers the left and fades out by x 1075, and the page colour #0E0E10 rises from y 550 to solid at y 850. Rows on the first screen: titles 24 px grey, Top picks for you with 306 × 172 cards (radius 24, 346 px pitch) from y 655, Your apps with 152 px circles at a 182 px pitch from y 906. Every other row has 391 × 220 cards at a 432 px pitch; Continue watching's say RESUME in 20 px capitals on a dark pill over the art, have an 8 px white progress bar 23 px in from the edges, and two 24 px lines 22 px under the card (the title, then the service and the time left), which the focus leaves in place. Focus: a card scales 1.05 with a 3 px light ring 4 px off it and no shadow; an app circle scales 1.06 with a 4 px white ring 3 px off it; a row scrolls one item per press with the focused item in the first slot. The rows' titles are 36 px as soon as anything under the bar has the focus, with the items 69 px under them. The vertical states: the banner focused (where Home opens) moves its lines down 125 px, adds its logline (one 28 px line, 23 px under its line) and a "Watch now" button 50 px under that (an 80 px #E5F1FF pill, its text 40 px in from each end, grown 10 % with the focus), and drops the rows and the dots 320 px, the first row's title to y 920; the top picks focused take the bar away and preview the focused title in the banner: its logo from TMDB where it has a light one (720 × 176 at most) or its title, with the foot at y 373, then its service (32 px Medium) 20 px under it, two lines of its overview (28 px) 24 px under that and its facts 16 px lower, with the rows 47 px lower; from Your apps down the rows rise to the top, the focused row's title at 63 px from y 118, the others' at 36 px, the page near-black. Banner slides cross-fade in 400 ms every 12 s, without the push-in. A title's details, as Google TV's: the title in Google Sans Flex Display 122/120 within 1688 px (two lines when longer) in a 240 px box from y 347, its facts in 30 px Medium 32 px apart from y 587 (the rating's star an icon), two lines of overview (28 px, 1116 px wide) from y 683, and its actions from y 835 as 80 px pills 24 px apart (white at 10 % with grey text, light and grown 10 % with the focus; text 32 px with a 32 px icon, 44 px in from each end; the thumbs 116 px wide). Under them, from y 973, its rows with 36 px titles: Cast & crew (248 px photos 40 px apart, the name and part in 24 px centred 18 px under each) and If you liked the title (391 × 220 cards, the focused one's title and every one's facts under them); the focus going down raises the focused row's title to y 118 at 63 px, as Home's rows do. |
| Apps | Not specified. | No page head. Six columns of 156 px circles at a 293×240 px pitch from y 160, or one 277 px row per kind from y 160; three rows fit and a fourth shows its top. |
| Editing apps | Not specified. | A 380×56 px pill button (Hide from Home) at the top right from y 136, the Apps page 64 px lower while it is shown, the key hints on a band of ink from y 900, and an app hidden from Home dimmed to 40%. |
| Live | Not specified. | Four columns of 422×292 px stat cards, 24 px apart. |
| Quick settings | Not specified. | Google TV's quick settings panel, read from its launcher (`com.google.android.apps.tv.launcherx` 1.0.1009811201: `DashboardActivity`, its layouts and theme decoded from the APK) and from the panel's layout tree (`dumpsys activity top`): a 544×1016 px panel (272 dp wide, 16 dp from the top and the foot, 24 dp from the right). The date (24 px) and the clock (40 px) at x 30, y 52 and 92 of the panel; 56 px round buttons (the gear, the profile) at y 52, the label of the focused one under it at y 114. Tiles 242×108 px on a 254×120 px pitch from (24, 170), two columns; a tile across both is 496 px. Icon 32 px in a 40 px box at x 20, title at x 68 (24 px), its second line at y 60 (18 px at 60%). The wide card 496×112 px, 54 px under the last tile, a 64 px circle at x 22 and its title at x 102. The mark at (36, 966). A list replaces the page: its title centred at y 32 (28 px), items 496×100 px on a 116 px pitch from y 113, text at x 24 (22 px). |

## Focus

| | Android TV and tv-material 1.1.0 | Native Tv |
|---|---|---|
| Indicators | Scale, border, glow and colour. Scales 1.025, 1.05 or 1.1; defaults 1.1 for buttons, cards and chips, 1.05 for list and navigation-drawer items. | Scale 1.1 for cards, app tiles, input tiles and keys; 1.12 for app circles; 1.035 for stat cards; 1.02 for settings rows; 1.025 for Quick settings' tiles, cards and list items and 1.1 for its round buttons, the values of Google TV's quick settings animators. |
| Border | 3 dp on cards (6 px at 1080p), 2 dp on most others. Material Web's focus ring is 3 px wide with a 2 px outward offset. | A 3 px ring in `focusRingColor` #F1EEF4, outside the edge, no offset; a shadow of 22 px offset, 56 px blur, 62% black. |
| Timing | A tween: 300 ms focus, 500 ms unfocus, 120 ms press, easing (0, 0, 0.2, 1). | A tween: 170 ms, easing (0.23, 1, 0.32, 1). Quick settings: 200 ms, easing (0.2, 0.1, 0, 1), Google TV's for its quick settings tiles, with their 4 dp lift as a shadow. |

## Motion

Expressive replaces easing and duration with springs. A spatial spring (position, size, rotation, corner radius) overshoots; an effects spring (colour, opacity) never does. Values are damping ratio and stiffness, mass 1, from the Compose source (`ExpressiveMotionTokens.kt`, `StandardMotionTokens.kt`).

| Token | Expressive | Standard |
|---|---|---|
| Fast spatial | 0.6 / 800 | 0.9 / 1400 |
| Default spatial | 0.8 / 380 | 0.9 / 700 |
| Slow spatial | 0.8 / 200 | 0.9 / 300 |
| Fast, default and slow effects | 1.0 / 3800, 1.0 / 1600, 1.0 / 800 | the same |

| Native Tv | Damping ratio / stiffness | Drives |
|---|---|---|
| `spring::row` | 0.96 / 230 | Row scroll |
| `spring::page` | 1.0 / 170 | Page stack, grid scroll |
| `spring::list` | 1.0 / 210 | Settings and drawer lists |
| `spring::pillLead`, `pillTrail` | 0.9 / 460, 0.9 / 250 | The tab pill's two edges |

Focus, entrances and the hero are tweens (`ms::focus` 170, `ms::base` 320, `ms::hero` 900), not springs. A film's trailer fades in over the hero's art over `ms::hero` once the hero has rested on it for `ms::trailerDelay` (3500), the pause Google TV leaves before its previews start. The springs are at or near critical damping, so a position never overshoots. The client has no reduced-motion setting; Material asks for fades instead of slides and scaling, and no parallax or shape morphing, when the platform sets one.

A tab change slides the page's content (not its art) `layout::pageSlide` 105 px away from the tab chosen as it fades over `ms::pageOut`, and the new page in from the other side over `ms::page` with the decelerate curve, as Google TV's tabs do.

Quick settings comes in and goes out as Google TV's panel leaves (its exit animation, `Ap1`): sliding its own width to the right while it fades, over `ms::quick` 500 ms on `ease::quick` (0.4, 1, 0.12, 1), a curve that covers 90% of the way in the first third. A tween, not a spring, as Google TV's is.

A row's items enter one after another, `stagger::homeCell` 26 ms apart (the tenth and later together), the first `stagger::homeBase` 80 ms after the row starts. The values were 38 and 120 ms: the apps row, 14 circles wide, took 0.78 s from the start of its entrance to the end of its last circle's, which read as slow; it takes 0.63 s now. Home draws nothing until the apps' icons are in (300 ms at most), so a circle never enters as a colour and changes to its art.

## Icons

| | Material 3 Expressive | Native Tv |
|---|---|---|
| Family | Material Symbols, a variable font: weight 100 to 700, fill 0 to 1, grade, optical size 20 to 48; Outlined, Rounded and Sharp. | Material Symbols Rounded, weight 400, 24 px optical size (Apache-2.0), as SVGs in `native-tv/assets/icons/material`, as Google TV draws its own; play, star and the saved bookmark filled. The Home Assistant mark and the assistant's sparkle stay Material Design Icons. |
| Size | 24 dp with a 20 dp live area. Icon buttons from 32 dp (20 dp icon) to 136 dp (40 dp icon). | Rasterised from SVG into an alpha atlas at 18, 24, 26, 28, 34, 38, 40 and 52 px (30 and 56 px on demand) and tinted when drawn. |
| State | A toggle goes from outlined to filled when selected. | A selected item changes colour; the glyph stays. |

The sleep timer's moon (`bedtime.svg`), Google TV's icon for it in quick settings, is drawn for the client: Material Symbols' own is not among the SVGs.

App icons are Google TV's circles: the brand's logo large on the brand's own colour (Netflix's red wordmark on white, Prime Video's on blue), and LG's own apps a Material Symbol in a tonal colour on its lighter container.

## Not adopted

* Tracking on the baseline values.
* The emphasized weights (600 and 700 cuts).
* The shape steps 4, 16, 32 and 48, and a shape that changes with focus.
* Springs that overshoot, and effects springs for colour and opacity.
* A reduced-motion setting.
* Material Symbols and the filled and outlined state.
* Secondary and tertiary colour roles, and state layers.
* A focus border of 3 dp (6 px) on cards.

Each of these changes the golden images of every screen that uses it, so each is decided on its own.

## Sources

* Typography: [type scale tokens](https://m3.material.io/styles/typography/type-scale-tokens), [applying type](https://m3.material.io/styles/typography/applying-type), [fonts](https://m3.material.io/styles/typography/fonts).
* Colour: [colour system](https://m3.material.io/styles/color/system/overview), [state layers](https://m3.material.io/foundations/interaction/states/state-layers).
* Shape: [corner radius scale](https://m3.material.io/styles/shape/corner-radius-scale), [shape morph](https://m3.material.io/styles/shape/shape-morph).
* Spacing: [spacing](https://m3.material.io/styles/spacing/overview).
* Motion: [how it works](https://m3.material.io/styles/motion/overview/how-it-works), [specs](https://m3.material.io/styles/motion/overview/specs), [applying transitions](https://m3.material.io/styles/motion/transitions/applying-transitions).
* Icons: [applying icons](https://m3.material.io/styles/icons/applying-icons).
* TV: [layouts](https://developer.android.com/design/ui/tv/guides/styles/layouts), [focus system](https://developer.android.com/design/ui/tv/guides/styles/focus-system), [typography](https://developer.android.com/design/ui/tv/guides/styles/typography).
* Overview: [Building with Material 3 Expressive](https://m3.material.io/blog/building-with-m3-expressive).
