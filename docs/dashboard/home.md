---
description: "Native Tv Home, an experimental replacement for LG's home screen on webOS 9 and 10, laid out as Google TV's home with rows of apps, inputs and live channels."
---

# Home

**Experimental.** Native Tv Home is a home screen of Native Tv's own, shown in place of LG's when the remote's Home or Back button is pressed and when the TV switches on. The **Native Tv Home** tab, `/?tab=home`, under Apps, switches it on and off and arranges it. It has been tried on webOS 9 and 10. On webOS 11 it is offered once `"allowExperimentalHome": true` is set in `config.json`. Elsewhere LG's Home stays the TV's Home and Native Tv Home is the TV app's own, opened with Back from the app's Settings: the tab is still there for its layout, its profiles and the TMDB and YouTube keys below, without the switch, and without the warning about replacing LG's Home. It is left out only where the TV app's graphics probe fails.

On webOS 3.4, where LG's Home is a ribbon the TV's compositor draws over the card in front rather than an app, **Home button** on the tab sets the remote's Home button to open Native Tv Home. The server follows the compositor's log for the ribbon being shown and brings the TV app forward on its Home: launched over the ribbon, the app takes the screen and the ribbon goes, about a second after the press from cold; where the TV app is in front already, the server closes the ribbon with Back and the app goes to its Home. LG's ribbon shows for that moment first. The LG Home tile, and the Home key on the dashboard's remote, still open LG's Home. The switch is off until turned on. While it is on, the server closes the TV app once an app that is not LG's own (Netflix, Prime Video, YouTube, the browser) has been in front for 3 seconds: the C6 has 1.1 GB, and Netflix and Prime Video opened over the TV app, which holds 60 to 90 MB on Home, were restarted by the TV for memory. The Home button starts the app again, in under a second to its window. After a power cut or a reboot the server starts only once the Native Tv tile has been opened, so until then the Home button opens LG's ribbon, unless **Start after a reboot** is on (below).

**When the TV is switched on**, on by default where the Home button switch is offered, opens Native Tv Home as the TV comes on: the server follows the system log for the boot manager opening the last input (`NL_APP_LAUNCH_BEGIN` with `caller_id` `com.webos.bootManager`), which it does at every wake from Quick Start's standby, and launches the TV app over it about half a second later. LG's own **Home Auto Launch** does not show LG's Home on such a wake. After a cold start the boot manager's opening comes before the server runs, so a server that starts within 8 minutes of the boot opens Native Tv Home if an input is still in front.

**Start after a reboot**, off until switched on, starts the server after a reboot or a power cut, which webOS 3.4 otherwise leaves to the Native Tv tile: it registers a persistent activity, `com.tvweb.dashboard.boot`, with the TV's activity manager, which keeps it across reboots and calls the Homebrew Channel's `autostart` every 5 minutes. That runs the Homebrew Channel's boot hooks, Native Tv's among them, once per boot, so the server is up within 5 minutes of the TV starting. The server registers it again at every start and every half hour while the switch is on, and switching it off cancels it.

The screen is laid out as Google TV's home, after Google's TV Design Kit and the bar of its 2025 redesign: the profile's avatar, a capsule holding search (where titles can be searched) and the tabs, the screen saver's button and the gear in a capsule of their own, and the clock and the Native Tv name at the right, a featured carousel across the top (the kit's Immersive layout: its artwork fills the top right and fades into the black, with the text at the left), and rows of cards below it. Until Home has its first content, a boot screen of slowly moving colours shows the Native Tv mark. The tabs are:

* **Home** has **Your apps**, a row of round icons (the [themed icon](apps.md) where there is one, else the largest of the app's own), with the name under the one in focus: as Google TV's, a streaming service's circle is its logo on its own colour (Netflix's red wordmark on white, Prime Video's on blue), and LG's own apps a Material Symbol on a tonal circle. With a TMDB key set (below), **Top picks for you** comes first and Your apps under it, as on Google TV since 2025. The carousel across the top has titles of its own, as Google TV's does: the first six of the week's trending films and series, each with **Trending │** and the first sentence of its summary under its title, and over it the white wordmark of the service that has it in the TV's country (Netflix, Prime Video or Disney+) where a Popular on row lists it; Top picks for you starts after them, so the two never show the same title. The focus starts on the carousel itself, which then also shows two lines of the title's summary and a **Watch on …** button (**More info** where no service has it), with the rows lower: Left and Right show the title before or after, OK opens its details, Down reaches Top picks for you and Up the bar, which darkens the page under it while it has the focus. The carousel moves on by itself every 12 seconds while no key is pressed, with a dot for each title, and the rows under it hold still while it does. As on Google TV, with the focus on Top picks for you the bar leaves and the carousel shows the focused title (its service's wordmark, its title, facts and summary); on Your apps the carousel's text leaves and its picture stays, dimmed, behind the row, which then shows every app's name; further down the rows rise to the top, the focused row's name large, over a near-black page. A row moves one item per press and keeps the focused item first. Back goes from the rows to the carousel, then to the bar. The carousel's artwork is TMDB's 1920 × 1080 picture, the screen's own size. For a profile that has liked titles or put them on its watchlist (below), Top picks for you is its own, as Google TV's are an account's: up to ten titles like those, one from each in turn, newest first, ahead of the week's trending ones, leaving out what it already marked. Rows of **Trending films this week** and **Trending series this week** follow, as 16:9 cards. As on Google TV, a card shows the studio's artwork with the title on it, from TMDB, in English or else in the TV's country's language, and no title of its own; a title TMDB has no such artwork for shows its plain picture with its title and, on focus, its facts. Only what is trending this week is listed, and only titles that can be opened somewhere (a Netflix title, or a YouTube trailer). Once the server knows the TV's country, **Popular on Netflix**, **Popular on Prime Video** and **Popular on Disney+** follow Your apps: what is popular on each service in that country with a subscription, films and series in turn, from TMDB's watch-provider data (JustWatch's). A service has a row only where its app is installed and in Your apps. The services the profile has watched in the last 30 days come first, the most played first (the time a picture played full screen in their apps), and the others take the order of their apps in Your apps, so each profile's arrangement of Your apps also arranges and hides them. They list what the service offers, not what an account has watched: Netflix, Prime Video and Disney+ give no other app their own suggestions. With Netflix links on (below), **Available on Netflix** lists the trending titles that have a Netflix link, and OK opens the title in Netflix itself; it is left out while Popular on Netflix is shown. With a YouTube key, **Trending on YouTube** is added. A profile signed in with Google (below) has **From your YouTube subscriptions**, the newest videos of the channels it subscribes to, in YouTube's place among the services. A focused app shows its icon and name in the carousel's place, on a tint of its colour, with the picture dimmed behind it.
* **Films** and **Shows** (with a TMDB key set, as Google TV's Films and Shows tabs): across the top, up to eight of the week's trending films (or series) as large cards, each with its title and the start of its summary over its picture and, where a Popular on row lists it, the white wordmark of the service that has it in the TV's country; under them, rows from TMDB: Popular films, Family films, Top rated films, Action and adventure, Comedies and Animated films on Films, and Popular shows, Animated shows, Top rated shows, Crime shows, Comedy shows and Documentaries on Shows. A tab opens with the focus on its first card. Down from the featured cards raises the focused row to the top with its name large, as Home's lower rows do, with a line under each of its cards (the service that has it where a Popular on row lists it, its year and rating) and the focused title's name over its line; OK opens a title's details.
* **Apps**, as Google TV's Apps tab: across the top, large banners of the TV's apps of films, music and games (up to six, in the order of Your apps), each the app's icon on its own colour carried across the banner; under them **Your apps**, every app on LG's Home as a round icon, with every name under them while the row has the focus; a **Search apps** bar; **App categories**, a button for each kind that has apps (Movies & TV, Music & audio, Games, Smart home, LG & TV, Tools, Other), which opens a page of that kind's apps; **Get more apps**, in the Play Store's place: **LG Content Store**, which opens LG's store even while it is hidden from Your apps, and, where the Homebrew Channel is installed, **Homebrew Channel**, which opens the Homebrew Channel's catalogue to install apps from (as Settings → Apps → Install homebrew apps does; Back returns to the tab); then a row of each kind's apps as wide cards, in the order chosen under **Kinds**, with the focused app's name under it. Google TV's banners and rows are the Play Store's suggestions; here they are the TV's own apps, which is all Native Tv can open. As on the Films and Shows tabs, the focus starts on the first banner and a row under them rises to the top with its name large; OK on a banner, an app or a card opens the app, and on the search bar opens Search, where the TV's apps whose name has the words come first, then the apps of LG Content Store and of the Homebrew Channel that have them, which can be installed from there (below). Holding OK on a kind's row lifts it so Up and Down move it among the kinds' rows, OK saves the kinds' order and Back puts it back. This tab has its own hidden apps, apart from the Home tab's **Your apps** row: an app hidden on one is still on the other, and a kind whose apps are all hidden has no button or row. Holding OK on an app of Your apps starts [editing the apps](#editing-the-apps). LG's own promotions (LG Channels, Sports, Always Ready, Alexa, Camera) start hidden on both. An app hidden under [Hide home screen system apps](apps.md) is gone from both, since the TV no longer lists it; the two lists only choose among the apps the TV still shows. A layout saved before this tab had a list of its own starts it as a copy of the Home row's, so what was hidden stays hidden here until it is changed. With [History](advanced.md) on, new apps are placed by how much each is used; otherwise they follow LG's order. **Notifications** and **Search** open LG's notification list and search, which LG's Home reaches from its sidebar. The last tile, **LG Home**, opens LG's Home screen and cannot be hidden per tile; **Show the LG Home tile** leaves it out. The **Native Tv** tile, on this tab and in the apps row of the Home tab, opens the TV app's Settings instead of launching an app, since Native Tv Home is that app and already in front.
* **Inputs** (left out when **Show inputs** is off): every HDMI input. An input with devices on it has a tile for each device the TV knows over HDMI-CEC, saying which device it is behind ("HDMI 2, through STEREO 70s"); an input with nothing connected is greyed out. Choosing a tile switches the TV to that input. Nothing is sent to the devices themselves.
* **Live** (**Show Live** is off by default; with it on, the tab appears once the TV has channels): the channels added here, in the order they are listed, each with a LIVE badge and how far through the programme on now it is, where the TV's guide has it. Choosing one tunes it.

![Native Tv Home on a B3: the featured title, Your apps and Top picks for you](../screenshots/tv-home.jpg)

![The Apps tab on a B3 with the apps sorted by kind](../screenshots/tv-apps.png)

With a TMDB key, **OK** on a film or series opens its details, laid out as
Google TV's: the title in large type, its rating, genre, year and length, two
lines of its overview and its buttons, then rows that rise to the top as the
focus goes down: **Cast & crew** (the director first, then the cast, with their
photos from TMDB), **If you liked** the title, its similar titles, and a row for
each of its first two genres (**Drama films and shows**, films and series in
turn, from the genres of Search's grid, without the titles of the rows above
it), over a dark blur of the backdrop. **Watch on Netflix**
launches the title in Netflix when it is there (see below); **Trailer** opens
YouTube when a trailer is available. As Google TV's watch options, the details lead with **Watch on Netflix**, **Watch on Prime Video** or **Watch on Disney+** for each of those services that has the title with a subscription in the TV's country (TMDB's watch providers, JustWatch's data) and whose app is installed, the service of the row the title came from first. Each opens the service's app on the title, as Google TV's Watch does, through LG's own catalogue: the one LG's Search answers from, which knows each service's titles in the TV's country and the link its app takes. Prime Video and Disney+ then play a film (a series opens on its page), and Netflix opens its page with Play in focus; Prime Video and Disney+ ask whose profile it is first. A title the catalogue does not list opens as before: in Netflix with Netflix links on, in Prime Video with `"primeDeepLinks": true` in `config.json` where Wikidata has the title's Prime Video ID (its GTI, from which a film plays), and otherwise on the app's own Home. The server finds a title in LG's catalogue by its name in the TV's country, in English and in its original language, and remembers the answer for a day. The trending rows list only titles that open in Netflix or have a trailer.
After them come **Watchlist**, **Watched it?** and the thumbs, as on Google TV: Watchlist
adds the title to the profile's watchlist (it then reads **On watchlist**, and
OK takes it off), Watched it? marks it watched (it then reads **Watched**, with a
tick, and OK takes the mark off), thumbs up likes it and thumbs down says it is not
for the profile. A change shows at once, is saved for the profile in use, and goes back
with a message if the server refuses it. A title opened from Home that plays to 90% in
its app is marked watched by itself. As on Google TV, a watched title leaves Continue
watching, Top picks for you and Because you liked, and stays in the other rows. Home then has **Continue watching**, right after Your apps: the titles the profile opened from Home's details or rows in Netflix, Prime Video or Disney+, newest first, with a bar of how far it got, which is the time a picture then played full screen in the app against the title's runtime (two hours for a film without one, 45 minutes for an episode): the app's menus, its title pages and a pause do not count. It counts again whenever that app comes back in front within 30 minutes, until Home opens another title. On a TV whose player reports nothing, the time the app stays in front counts instead. A title shows after two minutes and goes at 90% or after 30 days; OK opens it in its app again, where the app's own Resume is. What is started inside an app, not from Home, has no title, since no app tells another what it plays (the TV's player says only that a picture plays, pauses or stops): two minutes or more of it make that service's own card, with the last frame the server captured while it played (the first after 45 seconds, then every three minutes, kept on the TV) and **Watched for** the time played, for a week or until a later play in that app; OK opens the app, whose own row resumes it. A play of a title Home opened has no card of its own. Home also has **Your watchlist**, after
Your apps and the services' rows, and **Because you liked** the title liked
last, its similar titles as TMDB gave them when it was liked; a title marked not
for the profile is left out of every row. Each profile has its own; they are
kept in `home-titles.json` on the TV.
**Full overview** shows the complete summary, a page at a time with Up and Down.
A title from those rows opens on top of the one before, up to eight
deep, and Back returns through them to the original row or search result, with
the focus where it was.
The remote's **Red** key opens a focused title in Netflix immediately, without the details; on a service's row, in that service's app.

![A title's details on a B3: year, rating, genres, overview, cast, the Trailer button and More like this](../screenshots/tv-details.jpg)

**Search** leads the bar's capsule before the tabs and is a tab of its own, as on Google TV: resting on it (Left from the first tab) opens its page, a wide search field over a **Search by genre** grid of eighteen genres (Action to Thriller; TMDB has no game show genre, so War takes Google TV's Game show place). Down from the field raises the grid under its title as the bar leaves. OK on the field opens the typing page: the field at the top left and Google TV's TV keyboard (Gboard's layout: the top row's letters show their digits, **?123** for symbols, the caret keys, space, - and _, and a search key), with up to eight titles suggested over the keys after two characters and a 350 ms pause (Up reaches them, OK searches one). The search key, a suggestion or a genre opens the results page: the words searched, **Edit search** (back to the keyboard with them), a row of the TV's apps whose name has the words (OK opens the app), a row of **LG Content Store**'s apps and one of the **Homebrew Channel**'s, as Google TV's results list the Play Store's, and a row of films and one of series, a genre's being its most popular. Down raises the rows as Home's do, with the focused row's captions; OK opens a title's details, and Back steps back through the rows, the keyboard and the Search tab, which forgets the words, as Google TV's does.

**Installing apps**, as Google TV installs from the Play Store: OK on an app of the LG Content Store row opens its page, laid out as the Play Store's page of an app on Google TV: its name in very large type, who makes it, its kind and price, its stars (LG's rating out of ten, as five), two lines of its description, its version and size, its icon on its colour at the top right, and its screenshots in a row under the page, which rises to them with Down. **Install** has the TV's install service fetch the app from LG and install it, as LG Content Store's own button does: the button reads **Downloading** with how much has come, then **Installing…**, then **Open**, and the app joins Your apps and the Apps tab. One app installs at a time. A paid app is refused with a message, since buying needs LG's account in LG Content Store itself. OK on an app of the Homebrew Channel row opens the Homebrew catalogue on that app, where OK installs it with the usual preview. The search and the pages come from LG's own search, which the server asks through the TV's `sdx` service (LG's Search app does the same), so the TV's LG credentials stay inside LG's service.

**Ask Gemini** has no button on the bar, as the bar of a Chromecast with Google TV has none; its question box is on the dashboard's Native Tv Home tab, and on the TV its page (route `assistant`) is opened by nothing but scripts. With the owner's own key, the same keyboard takes a question ("something funny on Prime tonight", "open Rocky", "turn the volume down"), **Ask** sends it, and the answer shows over the films and series it names, which open as from Search. Gemini can search TMDB, choose the titles to show, open a title in the streaming app of the TV that has it in the TV's country, and set the volume, mute, switch off the screen, change the HDMI input, open an installed app, and play or pause; each of these goes through the server's own routes, with their checks. It answers in the language of the question. The Native Tv Home tab has the same question box, for asking from a phone. The remote's microphone is not used.
Ask Gemini's keyboard is the one Setup uses, with Shift, a **123?** key for
symbols, Delete, keys that move the caret and a space bar, and **Clear** under
it; a physical keyboard works on both. Search and title details are hidden without a TMDB key.
With `"netflixDeepLinks": true` in `config.json`, details can also show
**Open on Netflix**. The server uses TMDB's Wikidata identifier and
[Wikidata's Netflix ID (P1874)](https://www.wikidata.org/wiki/Property:P1874)
to find the title. Netflix must be installed. Its native launch receives
`m=https://www.netflix.com/title/<id>&source_type=4`. A missing or ambiguous
mapping leaves the action out; saved mappings work offline. Availability
still depends on the Netflix account and region. The option defaults off.

The Magic Remote's pointer works the same way: pointing at a card or a tab focuses it, and clicking acts. A tab opens on the click, the carousel opens what it is showing, and its dots move the spotlight on. Hovering does not move the rows down the screen as the keys do, so a card stays under the pointer; where a row slides a card aside to make room for the focused one, the click still opens that focused card.

The **screen saver** button, in a capsule after the tabs (Up to the bar, then Right past them), starts the TV's screen saver at once, as Google TV's does; the **gear** after it opens **Quick settings**, Google TV's quick settings panel: a dark blue panel that slides in from the right over a dimmed Home, with the date and the clock at the top beside two round buttons, **All settings** (the gear), which opens Native Tv's [Settings](../apps/tv-app.md) as Google TV's opens its own, and the **profile**. Under them, tiles: **Volume** (Left and Right change it, OK mutes) and **OLED light** (**Backlight** on an LCD; Left and Right change it by 5) across the panel, with a bar; then **Picture mode**, **Sound output**, **Input** and **Sleep timer**, each with what is set now under its name, where OK opens the choices; **Screensaver** and **Screen off**; and **Recent apps** as a wide card. Up and Down move a line, keeping to the column, and Left and Right move between the tiles of a line. The choices replace the panel's page with a list, as Google TV's sleep timer does. The panel shows what it read last time at once, with a turning circle where a value has not been read yet, and reads the TV again behind it. Its changes go to the TV as the Control tab's do, so they need `allowControl`, which is on by default. Every change shows the moment the key is pressed and is sent behind it: a held key sends the newest value at most every 120 ms, a late answer never moves the value back, and one read after the last answer settles it on what the TV says (a volume the TV capped shows as capped). A change the TV refuses, or does not answer within eight seconds, goes back with a short message that gives the reason. Back goes up a level and then closes the panel, which slides out and leaves Home as it was, with the gear in focus; the pointer works too, and a click beside the panel closes it.

**Profiles** give each person a Home layout of their own: the rows and tiles that are hidden or moved, the tabs and the clock. The TV starts with one profile, named after the TV (B3, C4, or LGTV where the model is not known) until it is renamed. A coloured avatar with the profile's first letter shows before the tabs, and the focus reaches it with Left from the first tab, also while there is one profile; it opens **Choose a profile**, laid out as Google TV's account chooser: round avatars with the profiles' names, where OK on a profile switches to it and **Add profile** adds one (up to five) and switches to it, with a fresh layout. The chosen avatar is in use at once, with a turning circle until the server answers, and a refusal gives the old one back with the reason. **Profile** in Quick settings opens the same page. **Manage profile**, under the avatars, opens a settings panel for the profile in use at the right. **Rename** in it uses the same on-screen keyboard, with a 20-character limit and the avatar shown as it will look; Back cancels; Save returns to the panel with the new name on it at once, and a refusal puts the old name back with the reason. Six colours under it set the profile's colour (OK on one; the avatar changes at once and goes back with a message if the server refuses). Removing a profile is on the browser dashboard's Home tab, under **Profiles**; removing asks for a second press, and the last profile cannot be removed. The TV app's Home section has a **Profile** row whose OK steps to the next profile, or adds one where there is only the first. Everything else on both dashboards' Home tabs, and arranging on the TV, changes the profile in use. Adding a profile does not copy the layout in use, and what Home shows from TMDB or YouTube is the same for every profile, apart from which services' rows show and in what order.

The highlight follows the remote immediately; the carousel's title and artwork
follow once navigation settles briefly. Each row keeps the card it had the focus
on, as Google TV's do: Up or Down lands on that card again, and on the first
card of a row not visited yet. Once the rows have risen over the carousel, the
focused card's artwork gives way to its colour, darkening toward the bottom
left, as Google TV shows a page full of rows. With the focus out of the bar,
the selected tab keeps a faint grey pill and its name dims with the others. Returning from an app keeps the current
tab and selection while Home refreshes its content in the background.

A tap of **Back** only goes back: from any other tab (Films, Shows, Apps, Inputs or Live) to the first tab with the focus in its content, then from a lower row to the first row, then to the bar, where it stops. Holding **Back** (650 ms) is the only way to **Recent apps**, from any tab: the apps the TV lists as recent, newest first, each with a snapshot of how it was left (its splash screen, for an app left before the TV last restarted). Holding OK opens nothing. OK opens one. Down from a card reaches its **Close**, and up from the cards reaches **Close all**; closing an app takes it off the list and stops it. Apps the TV keeps running in the background without a card, such as YouTube when it is kept ready, are not listed. Back again closes the panel. On a TV that gives no list, Back moves to the first tile instead.

<span id="editing-the-apps"></span>**Editing the apps** is done from the remote, as in Google TV's edit mode. Holding **OK** on an app (650 ms), in **Your apps** on Home or on the Apps tab, starts it: the row reads "Moving apps", a **Hide from Home** button (**Hide from Apps** on the Apps tab) appears at the top right and Left and Right swap the app with its neighbour. With the apps sorted by kind an app swaps only with the apps of its own kind and the apps stay by kind; otherwise saving makes the order **Your order**. **OK** or **Back** saves, **Up** goes to the button, and OK on it takes the app off that list (it stays installed and on LG's Home, and on the other list: Home's row and the Apps tab each have their own hidden apps); **Red** does the same without the button. The apps hidden from the list being edited follow the others, dimmed, while moving: Right at the end moves on to them, the button reads **Show on Home** (**Show on Apps**) on one, and OK puts it back. They can also be shown again in the TV app's Home section and on the browser dashboard. The LG Home tile cannot be hidden this way. Holding OK anywhere else on Home does nothing, and on one of the Apps tab's rows of a kind it lifts the row to move the kind. Where holding does nothing, OK acts the moment it is pressed. Some LG remotes send the release of a key only with the next key, so until a release has arrived in time, a press on an app that the remote neither releases nor repeats within about half a second counts as one press, and holding is told by the repeats the remote sends while the key is down.

![Editing the apps on Home: the Moving apps row and the Hide from Home button](../screenshots/tv-home-edit.jpg)

![Moving a kind on the Apps page: the row is lifted and Up and Down move it](../screenshots/tv-apps-arrange.png)

Keys on Native Tv Home:

| Key | On a tab's content | On the bar |
|---|---|---|
| Arrows | Move the focus | Move between the tabs, avatar, search and the gear |
| OK | Opens the focused app, input, channel or title | Opens the tab or the page |
| Hold OK | An app of Your apps: starts editing the apps; a kind's row on the Apps tab: lifts it; anything else: nothing | Nothing |
| Red | Opens the focused title in its app at once, as OK does for an app, input or channel | Nothing |
| Back | Other tab: first tab. First tab: lower row to the first row, then to the bar | Nothing |
| Hold Back | Opens Recent apps | Opens Recent apps |

While apps are being edited or a kind's row is lifted, OK saves, Back saves (editing) or puts the row back (a lifted kind), and holding Back is ignored.

OK acts once when released. Back also cancels a panel still loading. A close
that cannot be confirmed shows an error and keeps the cards for retry.

With **Picture details** switched on, on the tab or in the TV app's Home section, a line in the top right of the carousel describes the picture of what was last on. It is off by default. For an HDMI input it gives the signal the device is sending (resolution, refresh rate, chroma and bit depth, HDR10, HLG or Dolby Vision, and VRR and low latency when on), read while Native Tv Home is on screen, and the picture mode the TV gives that signal. LG keeps a mode for each input and dynamic range, so HDMI 2 and HDMI 4 can differ, and an input switching from SDR to HDR10 shows its HDR mode. An input with nothing sending shows no line. For an app, the plate gives the last video it played, noted while it played, as Last video (a Dolby Vision film on Netflix shows Dolby Vision and its mode). A channel's signal is not read, so it shows only the mode it gets in SDR, which nearly all broadcasts are. An app that has played nothing since the server started shows no plate.

App video tracking runs while Native Tv Home and **Picture details** are on and the TV is out of standby, including before Home's first visit after a server restart. Switching either off, or the TV going into standby, stops it; videos already noted are kept. Returning to Home applies **Picture details** as soon as its layout arrives; a delayed picture reply cannot bring the plate back or replace a different source's details.

The separate native TV client renders the picture plate and recent-app cards
in OpenGL ES using the existing Home APIs. It preserves the previous video
source when opened. Source tracking still requires the Home service to be
enabled; standalone native launch does not enable Home replacement.

The background is black. The browser dashboard can add an image for each time of day (morning 06:00, afternoon 11:00, evening 17:00, night 21:00): choose a **Time of day**, select a JPEG, PNG or WebP file up to 25 MB, then press **Upload image**. The browser fits it within 1920 × 1080 pixels, the size Native Tv Home is drawn at, and prepares a JPEG before sending it to the TV. Images fill the screen and may be cropped. **Remove uploaded images** puts black back. None are included with Native Tv.

Uploads are stored in `/var/lib/tvweb/home-wallpaper-uploads`, outside the deploy bundle. A failed or interrupted upload leaves the previous image intact. The directory is removed when Native Tv is uninstalled; the layout backup does not include image files.

The TV's screen saver, LG's or one chosen on the [Screensaver](screen-savers.md) tab, is left as it is set.

While Home is hidden, periodic source, guide, picture and catalogue refreshes
pause, along with routine rendering. Returning refreshes
the view immediately. The five-second recovery heartbeat continues while the
browser runs, including behind overlays.

**Show app names**, on both dashboards, shows names beneath the app icons by
default. Switching it off leaves icons only. App names remain available to
assistive technology. The choice
takes effect on Native Tv Home within a few seconds.

**Clock format**, also on both dashboards, chooses **12-hour** time with AM/PM
or **24-hour** time, the default. It applies to Home's clock and programme start
times; midnight is 12 AM and noon is 12 PM. The choice is saved on the TV and
does not change LG's clock settings. Resetting the layout keeps the choice.

**Show inputs**, **Show Live** and **Show the LG Home tile**, also on both
dashboards, choose the Inputs tab, the Live tab and the tile that ends the
Apps row. **Show inputs** and **Show the LG Home tile** are on by default;
**Show Live** is off by default, so the Live tab stays out until it is
switched on. Switching one off leaves only its own part out: the inputs, the
added channels and their individual hide settings are kept. Holding the
remote's Home button still opens LG's Home. **Reset the layout** switches
**Show inputs** and **Show the LG Home tile** back on and **Show Live** back
off.

The titles come from [The Movie Database](https://www.themoviedb.org/), with the
owner's own free API key (the **API Key** of 32 letters and digits, from themoviedb.org under Settings, API; the longer **API Read Access Token** beside it is refused),
pasted into **Films and series** on the Native Tv Home tab of the dashboard and saved there: it is kept on the TV
and never shown again, and **Remove** takes it away. The titles are fetched once a day, in the morning. The trending ones are kept
only if they have a YouTube trailer or, with `"netflixDeepLinks": true`, a Netflix link, and the Films and Shows tabs' rows keep TMDB's lists as they are; the pictures are kept on the TV, so browsing the rows uses no internet. Without a key, the Home tab shows only what is on the
TV. TMDB asks that its data is credited: this product uses the TMDB API but is
not endorsed or certified by TMDB.

A YouTube Data API key (free, made in the Google Cloud console with the YouTube Data API v3 enabled), pasted into **Trending on YouTube** below it on the same tab or set as `"youtubeKey"` in `config.json`, adds **Trending on YouTube**, the
videos trending in the TV's country (in the US until the server knows it); OK plays the video in YouTube's app. YouTube's own
personal suggestions cannot be read by anything but its app, so the row is the
public list.

**Film trailers**, on the same tab once a TMDB key is saved, is on by default (`"homeTrailers"` in `config.json`). As on Google TV, a film's details page plays its trailer behind the page, without sound, 3.5 seconds after it opens (once the clip is on the TV); 4 seconds after the trailer starts, or after the last key, the page lowers to the title, at half its size, and its facts at the foot of the screen, and any key brings it back up without doing anything else. The trailer fades back to the artwork when the clip ends or the focus goes down to the rows. Home's carousel shows no trailers. The trailers are Apple's: a film's iTunes id comes from Wikidata, or, for a film Wikidata does not link (most new ones), from Apple's charts of the films bought and rented now, matched by title and year. Apple TV's page of the film, in the TV's country and then the US, names its trailer in several sizes, in the country's language where Apple has one, and the server takes the widest the TV app decodes in time, 894 × 372 for a widescreen film, where the iTunes Store's preview, which it falls back to, is 640 pixels wide. A film still only in cinemas, and every series, has none, and keeps its artwork. The server fetches the first 7 MB of each (about 30 seconds of Apple TV's), the carousel's films' when the top picks are fetched and any other film's when its details ask for it, keeps six on the TV and drops the one played longest ago; the TV app plays them with the TV's own H.264 decoder.

**Top picks with Gemini**, on unless switched off, has Gemini choose up to 12 of Top picks for you once a Gemini key is saved: the names, years and kinds of the titles the profile watched from Home, liked and saved, and how long it played each service's app in the last 30 days, go with the titles on offer (the week's trending ones, films, series and the popular ones on the TV's services, at most 90, none the profile has), and Gemini answers with its picks, best first, and why ("Because you watched The Dark Knight"), which the banner shows before the title's facts. Gemini is asked to favour the services the profile watches most. The answer is kept a day per profile in `home-picks.json`, or until the profile's titles or the order of its services change; a failed request is tried again after an hour, and until then the row is as before.

**Continue watching from the screen**, off until switched on, puts the title of what is started inside Netflix, Prime Video or Disney+ on Continue watching in place of the service's own card: when playback in one of them, in front, is paused (after 4 seconds) or stopped (after 2.5 seconds) once it has played two minutes, the server captures the screen at 960 × 540 and Gemini reads the title off the app's pause screen or the title's page. The title found on TMDB goes first in Continue watching with the time played, and the time played after it is added as playback pauses and stops. A title that left the front without a pause or stop is not read. Needs a Gemini key and a TMDB key.

On the banner, a title with a logo on TMDB (its own lettering on a transparent ground, in English or the TV's country's language) shows the logo in place of its name, as Google TV does, set at 720 × 140 at most with its foot on the name's line; a dark logo, which would not read over the art, leaves the name.

**Gemini assistant**, on the same tab, takes a Gemini API key (free at aistudio.google.com, under Get API key: the older `AIza…` keys or the `AQ.…` keys AI Studio has issued since 2026), kept on the TV as `geminiKey` and never shown again; `geminiModel` in `config.json` picks a model other than `gemini-flash-latest`. With a key, the tab also has a box to ask the assistant from the phone, and the TV's bar the ✦.

**Your YouTube subscriptions**, below it, signs the Home profile in use into a
Google account with read-only access to YouTube (`youtube.readonly`), through
Google's sign-in for TVs: **Sign in with Google** shows a code to enter at
google.com/device on a phone or computer, and the page follows along until it
is entered. It needs an OAuth client of the owner's own, made in the Google
Cloud console as **TVs and Limited Input devices**, whose client ID and secret
are saved on the tab (or as `youtubeClientId` and `youtubeClientSecret` in
`config.json`); the app should be published (In production), since Google
signs out a client still in testing after seven days, and Google calls it
unverified at sign-in. Each profile has its own account, and **Sign out**
withdraws it at Google too. The row lists the newest videos of the 25
subscribed channels with something new first, three of each, without Shorts
(a minute or less) and premieres still to come, refreshed hourly while Home
is shown; OK plays a video in YouTube's app. YouTube's own recommendations,
history and Watch later are not readable this way.

## Switching it on and off

**Use Native Tv Home** asks for a second press, then puts Native Tv Home in LG's Home's place: the TV then opens it wherever it would open LG's Home, with the Home button, with Back out of the last app, and when it wakes on Home. LG's Home is not altered: Native Tv Home is laid over it while switched on. With Quick Start+ off, so that every switch-on starts the TV from cold, the one TV setting changed is LG's **Home Auto Launch**, switched off so that the TV does not show LG's Home as it starts; it is left as it is while Quick Start+ is on, which is followed whenever the TV is switched on or off, and **Use LG Home** puts it back as it was. **Use LG Home** takes it away again, and LG's Home is back after about three seconds. The TV app's own **Home** section offers the same, driven by the remote.

While a switch is in progress, further changes are refused with a retry message. A switch-off that is cut short is shown as **Retry restoring LG Home** on both dashboards, and is finished when the server next starts.

**Power-on screen**, on both dashboards while Native Tv Home is on, chooses
**Native Tv Home** or **Follow LG setting**. Native Tv Home holds LG's Home
auto launch off and opens Native Tv once it has taken Home's place, including
when LG was previously set to Recent Input. The TV's last input remains on
screen while a full shutdown starts up; Quick Start+ is not changed. An app or
input chosen during startup cancels the automatic opening. Follow LG setting
uses the earlier behavior described below. Switching Native Tv Home off puts
LG's saved auto-launch setting back.

Two ways lead back to LG's Home without switching anything off:

* the **LG Home** tile;
* holding the remote's **Home** button for a second or more; a short press on LG's Home then brings Native Tv Home back.

LG's Home opens about two seconds after either, and Native Tv Home takes its place again as soon as something else is opened.

Native Tv Home is the Native Tv app on the TV's home screen (the **App on the TV's home screen** switch on the Server tab), started by a screenless app in LG's Home's place, so it is offered only where that app is there and can draw: **Use Native Tv Home** is refused, with the reason, where the app is not on the home screen or its start-up check fails, and a server that starts with Home on and finds either leaves LG's Home in place. If the app is taken off the home screen while Home is on, or keeps stopping (three times within two minutes, or three times LG's Home is still in front 15 seconds after it came), LG's Home takes its place back by itself and Native Tv Home stays off it until the server restarts or **Use Native Tv Home** is pressed again. Home stays switched on in the meantime. The reason is in the server's log and in `nativeHome` of `/api/home`.

After the TV starts from cold, Native Tv Home takes LG's Home's place about 27 seconds in (a G3), 37 seconds on a G5. The same message is shown without a key press as soon as the boot hook starts, which is the earliest Native Tv's own code can run. Home pressed in that time leaves the TV on what it is showing, with that message; once Native Tv Home is in place, Home opens it as usual. Home is held like this for at most 90 seconds after the TV starts, so a TV where Native Tv no longer runs still reaches LG's Home. Anything else that opens LG's Home in that time, such as Back out of an app, shows LG's Home until Native Tv Home replaces it.

With Quick Start+ off, until Native Tv Home is in place the TV shows its last input, as LG's Home Auto Launch is off. Where it was on, Native Tv Home opens once in place, after a power-on with the remote or the TV's own button, as LG's Home would have; it is not opened after a restart from the menu, where LG's would not be either, or where another input or app has been chosen since the TV started. Switched back on within a few seconds, before the TV has powered off, it opens a second or so after the last input. With Quick Start+ on, switching on from standby is not a cold start: Native Tv Home is still in place, and the TV opens it at once.

**Hide LG's Home while the TV starts**, on webOS 9 only and off unless chosen, gives LG's Home an empty layout while Native Tv Home has its place, so that anything opening it in those first seconds shows a black screen instead. It asks for a second press to switch on, and takes effect at once. The layout is kept on the TV, not in Native Tv: if Native Tv stops running without switching Native Tv Home off, for example after a firmware update removes root or `/var/lib/tvweb` is deleted, LG's Home stays black at every start. Over SSH, `tvwebctl restore-nativetv-home` puts LG's Home back while Native Tv is installed. Where it is not:

```sh
rm -rf /mnt/lg/cmn_data/manifest /tmp/checkpoint/*com.webos.app.home* /mnt/lg/cmn_data/checkpoint/*com.webos.app.home* && reboot
```

LG's Home then starts on its default layout, without one set with [LG launcher layout](apps.md#lg-launcher-layout). A TV that already had the empty layout from an earlier build keeps the option on.

The Native Tv server answers about ten seconds later; Native Tv Home opened in between shows the Native Tv mark on black with a slowly turning ring, and fades into the home screen once the server answers. After 30 seconds without an answer it says so under the mark. It does the same whenever the server is not running, as during an update. Should the server stop, or fail three times in ten minutes, LG's Home is given its place back until the server is running again.

Over SSH, `tvwebctl restore-nativetv-home` does what **Use LG Home** does. Uninstalling Native Tv from the Homebrew Channel switches Native Tv Home off before deleting the server; a failed switch-off keeps the server for another attempt.

Earlier builds installed a separate Native Tv Home app and changed the TV's Home and start-up settings to it. The first start of a newer server puts back any of those settings that still name that app, gives the Home button back to LG's filters, and removes the app.

## Arranging

Apps and inputs come from the TV, so they are shown or hidden rather than added. Channels are the other way round: none is on Home until it is added, and one no longer wanted is removed. The Channels row on this tab lists those added, each with **Remove**, and below them **Add channels** finds the rest of the TV's channels by name or number. The TV app's Home section does the same with OK. A change made on either dashboard shows on Native Tv Home within a few seconds, while it is on screen.

Each row has an **Order**: **Your order**, as arranged, or **Last opened**, with the tile opened last first. Last opened counts an app, input or channel however it was opened, from Native Tv Home or not: an input switched to from the remote counts for every device on it, and the channel Live TV was left on counts as opened. Tiles not yet opened follow in the row's own order, and **LG Home** stays last. What was opened is recorded only for a row set to Last opened, only while Native Tv Home is on, and forgotten when the row goes back to Your order. On this tab, a Last opened row's tiles cannot be moved; the TV app's Home section has the same choice at the top of each row's section.

The Apps row has a third order, **By kind**: the Apps screen becomes one row per kind of app, and Your apps on Home follows the same grouping. Each app's kind is fixed by what it is (Netflix and LG Channels are Movies & TV, Spotify is Music & audio, the Web Browser and the Homebrew Channel are Tools, LG's other apps LG & TV, anything unknown Other). The order of the kinds is set under **Kinds**, here with Earlier and Later, in the TV app's Home section, or on the Apps screen itself by holding OK on a row. Tiles keep their own order within a kind.

Tiles are moved here, and apps also on Native Tv Home itself ([editing the apps](#editing-the-apps)); the rows are fixed tabs on Native Tv Home. Each app has two switches: **Shown on Home** for the Home tab's Your apps row and **Shown on the Apps tab** for the Apps tab; the order and the By kind grouping are shared. **Reset the layout** puts the rows, apps and inputs back in their first order, every row in Your order, with LG's promotions hidden again on both, and leaves the channels as they are.

The browser dashboard locks editing until each save and refresh finishes. A failed move keeps the accepted order.

Home can show a recent catalogue from the same browser session while a fresh
one loads. Without a usable cached catalogue, a failed first read asks the
server for LG Home and retries while visible; input waits for usable tiles.
The view sends heartbeats only after rendering. Reset leaves the Apps row
following its default order. A server redeploy reloads the view.

