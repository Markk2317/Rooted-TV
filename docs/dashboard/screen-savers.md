---
description: "Replace LG’s screen saver on a rooted webOS TV with a clock, starfield, fireworks or the TV’s own readings."
---

# Screen savers

The **Screen savers** tab of the **Display** section, `/?tab=screensaver`, provides eighteen alternatives to LG's default:

* Clock
* Clock with date, with seconds, the weekday and the date
* Black screen, nothing on screen (on an OLED the pixels are off)
* Starfield
* Fireworks
* Bokeh, soft circles of light drifting in and out
* Harmonograph, pendulum ribbons of light weaving and fading on black
* Filament, swarms of light circling and trailing across black
* Currents, streams of light flowing along a slowly changing wind
* Veils, translucent ribbons of light rippling slowly
* Incense, thin streams of coloured smoke rising and curling
* Murmuration, flocks of light swooping after their leaders
* Vortex, whirlwinds of light spiralling up swaying funnels
* Neo, columns of falling glyphs
* Tesseract, a four-dimensional cube turning in fine lines of light
* Panel vitals, showing the TV's own panel hours and refresher countdown
* Your photos, the owner's own photos with the time, the date and the weather, as Google TV's ambient mode (below)
* Google TV, Google TV's ambient mode: the owner's photos or the stills of Rooted TV Home's titles, with their caption, the time and the weather (below)

Each mode but the black screen offers dim and bright variants, and visual elements continuously drift across the screen to prevent OLED burn-in or image retention.

**Your photos** shows the photos added under **Your photos** on this tab, one after another every 20 seconds with a crossfade and a slow push-in, and the time, the date and the weather in a corner that changes every minute, so no text stays on the same pixels; dim keeps the photos a little darker. **Add photos** takes JPEG, PNG or WebP files from the phone or computer, fits each to 1920 × 1080 and sends it as a JPEG; the TV keeps up to 60 photos and 60 MB in `/var/lib/tvweb/screensaver-photos`, and **Remove all** deletes them. **Weather for** takes a town or city, looked up on [Open-Meteo](https://open-meteo.com), which needs no key; the weather there is read at most every half hour while the screen saver runs, and an empty name shows none. Google Photos cannot be read directly: since 2025 Google lets an app see only the photos picked in Google's own picker, which a TV's sign-in cannot open.

**Google TV** draws Google TV's ambient mode: one full-screen picture every 30 seconds, slowly panning and crossfading into the next, its caption in the lower left (a capitalised overline over the name) and, on one line in the lower right as on a Chromecast with Google TV, the temperature, a colour picture of the weather (sun, moon, cloud, fog, drizzle, rain, snow or storm, by day or night) and the time, in Google Sans Flex. The pictures are the ones under **Your photos** where some are added, else the stills (TMDB's 1920 × 1080 backdrops) of the titles on Rooted TV Home's banner and its trending, film and series rows, each captioned with its kind, year and name; with neither (no photos and no TMDB key) it shows a line saying so. The weather is the one **Weather for** names. The text moves to another spot within 16 pixels every minute; dim keeps the pictures a little darker.

**Start after** sets how long the TV sits idle before its screen saver starts: 3, 10, 20 or 30 minutes, the four times LG's own menu offers (any other value stops the screen saver coming at all, so no other is offered). It is LG's `option.screenSaverTimer` setting, shown only on a TV that reports it, and the same row is on the TV app's Screen Saver Options page.

A firmware update restores the LG default.

On webOS 3.4 LG's screen saver is a native program rather than QML. There a small launcher takes that program's place and has the TV's own QML runner draw the chosen saver, with LG's manifest left as it is, so they are on by default. As the TV runs no root hook at boot (see [Installing](../getting-started/install.md)), the chosen saver comes back after a power-on once the Rooted TV tile has been opened; until then the TV shows LG's.

On webOS 12 and later they are experimental: the tab shows a switch above the list that turns them on, after a warning, and until then only LG's default can be chosen. See [Experimental features](../administration/experimental.md) for why.

**Start screen saver** asks the TV for its screen saver. Some TVs accept the request and then refuse it; a G3 on webOS 9.2.2 refused every one, its own idle screen saver included. There the dashboard opens the screen saver itself, unless something is playing full screen or playback cannot be checked. Any key on the remote, including Home and the coloured buttons, or a second press of the button closes it. A failed close leaves it tracked so another press can retry. The TV does not know about a screen saver opened this way: its power state stays On, in the dashboards and in Home Assistant, and watch time counts the app beneath it. The Screen Saver sensor does show it.

