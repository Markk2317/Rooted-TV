# Strings

Text shown on the phone and computer dashboard is written in English where it is
used, with a key next to it. The key names the string, so the same words are used
wherever it appears, and `scripts/check-strings.py` catches the ones that drift
apart. A translation is a file that maps the same keys to another language, in
`rooted-tv/assets/i18n/<lang>.json`. English needs no file, so reading the markup
or the script shows the real words. The dashboard is in [Spanish](#spanish) too, and
the native TV client in [Italian](#the-tv-app).

The pages the TV's own browser shows (`rooted-tv/assets/google-tv/`) are English only.
So are Home Assistant entity names, MQTT topics and
the logs, and the "Can't reach the TV" page of the dashboard's service worker
(`assets/pwa/sw.js`), which has to work with no TV to fetch a dictionary from.

`scripts/check-strings.py` enforces what follows, and runs in CI.

## Adding a string

In markup, key the element that holds the text, and nothing but the text:

```html
<div class="prot-title" data-t="server.checkDaily">Check daily</div>
<button data-t="server.checkNow">Check now</button>
```

An attribute takes `data-t-` and its name, with the English in the attribute:

```html
<div role="group" aria-label="Theme" data-t-aria-label="theme">
```

`title`, `aria-label` and `placeholder` are covered.

In the page's script, call `t()` with a literal key and literal English, one
literal however long rather than pieces joined with `+`. Values go in through
named placeholders:

```js
t('server.install', 'Install v{version}', { version: v })
```

`t()` is in `rooted-tv/web/ui/i18n.js`. It returns the translation when the page
is in another language and the dictionary has one for this English, and the English
otherwise, with the placeholders filled either way. The TV pages' `t()` is in
`rooted-tv/assets/google-tv/shared/common.js` and only fills the English.

A new string shows in English in every language until it is translated, which is
not a fault: see [Keeping a translation current](#keeping-a-translation-current).

## Keys

Lowercase words joined by dots, camelCase within a word:
`server.tvUpdates.block.desc`. Start with the tab or page (`server.`, `adv.`),
or `common.` for words used across it such as `common.on`. The server's own
messages start with `srv.`.

One key, one English text. Where the same words appear in several places, such
as a description on both dashboards, use the same key, so the check notices when
one copy is edited and the others are not. Different wording needs a different
key.

## Rules

- **Whole sentences.** Never build a sentence from pieces such as
  `n + t('x', ' of ') + total`. Other languages order words differently. Put the
  sentence in one string with placeholders: `'{n} of {total} on'`.
- **A value stays as it is.** Anything compared with what the TV reports, sent
  to the server or the TV, or used as a state is never put through `t()`. Keep
  state in a variable or `dataset`, never read it back from displayed text.
- **Script-owned text is keyed in the script.** An element whose text the script
  sets gets no `data-t`, and holds a placeholder such as `&mdash;` in the
  markup until the script fills it.
- **No local variable named `t`** in a page that uses `t()`. It would hide
  `t()` in that function.
- **Names, units and products stay.** Text that reads the same in every language,
  such as a product name, carries `translate="no"` in the markup.

## Messages from the server

Text the server sends a page - an error, a screen saver's name, an agreement's
description - is written with `say::msg()`, from `rooted-tv/src/domain/say.hpp`:

```cpp
say::msg("srv.install.busy", "an install is already in progress")
say::msg("srv.saver.stageFailed", "could not stage the screen saver: {error}", variables)
```

The key and the English follow the rules for `t()`: both literal, the English one
literal rather than pieces joined with `+`. A message kept in a table, such as a
screen saver's name, has its key beside its English, `{"clock", "srv.saver.clock",
"Clock", ...}`, and is passed to `say::msg()` where it is used, not when the table
is built: the translation is looked up from what `say::msg()` made for the request.
The check reads those pairs too.

`say::msg()` returns the English, so the server, Home Assistant, MQTT and the log
all carry on in English. When a page is in another language it sends
`X-Rooted-Tv-Lang: es` on its own requests, and `HttpExchange::respond()` swaps the
messages in the JSON it answers with for their translations, from the same file
the page uses. The daemon reads `assets/i18n/` once, at start.

Only fields a page shows are changed (`error`, `label`, `title` and the rest of
`displayField()` in `rooted-tv/src/domain/say.cpp`), and only strings
`say::msg()` made, so a raw value that reads the same as a message, such as a TV
state, is left alone. Anything that is not JSON, `/api/logs` and the answers to
requests with no language header go out as they are. A name that is a value in
a message, such as a service's in "{name} is running", is translated in the
message too when it is a message itself.

Errors a page cannot cause, such as a malformed request, stay in English without
`say::msg()`. Names that come from the TV itself, such as its picture modes and
inputs, and the text of apps in the Homebrew Channel catalogue, are shown as they
are given.

## The language

The page follows `?lang=es`, then the choice stored by the **Language** list on the
[Server tab](../dashboard/server.md#rooted-tv) (Auto, English or Español, in the
browser's `localStorage` as `lang`), then the browser's own language. A language with
no file is English. Changing the list stores the choice, drops `?lang=` from the
address and reloads, since `i18n.js` reads the dictionary once, before the page's
scripts run, so the first paint is in one language.

## Translations

A file is an object of keys, each with the translation and the English it was made
from:

```json
{ "server.health.title": { "text": "Estado del servidor", "from": "Server health" } }
```

When the English in the code no longer equals `from`, the page and the server show
the English, never a translation of the old words. `from` is compared as written,
so a changed apostrophe or a trailing space counts.

`scripts/check-strings.py` fails on:

- a dictionary that does not parse, or an entry without a non-empty `text` and a `from`;
- a key nothing in the code uses (an orphan: the string was removed or renamed,
  and the entry is dead weight on every TV);
- a translation whose `{placeholders}`, markup tags or entities differ from the
  English it was made from, which would print `{n}` or break the page;
- a language the loader lists in `LANGS` without a file, or a file it does not list,
  which would never be read.

It reports, and does not fail on, translations whose `from` is out of date and keys a
language lacks (`--missing` lists them). Editing English is therefore never blocked
on a translation: the new English shows until someone redoes the entry. The count
is printed on every run, so a drop in coverage is visible in the CI log.

## Keeping a translation current

Each entry records the English it was translated from. When that changes, the entry
is **outdated**: `check-strings.py` lists it with the new English. Translate the new
English, put the new English in `from`, and the entry is current. An entry for a
string that was removed is deleted.

## Adding a language

1. `./scripts/check-strings.py --template > rooted-tv/assets/i18n/<lang>.json` writes
   every key with its English as `from` and an empty `text`. Entries left empty are
   rejected, so leave a key out to show its English.
2. Fill in `text`, keeping each `{placeholder}` as it is.
3. Add the two-letter code to `LANGS` and the language's name in its own words to
   `NAMES` in `rooted-tv/web/ui/i18n.js`, and the file to `FILES` in
   `rooted-tv/lifecycle/deploy.sh`. The Server tab's list is built from `LANGS`
   and `NAMES`.
4. Check the page in that language with `?lang=<code>` for text that does not fit
   and for English that was missed.

## Spanish

`rooted-tv/assets/i18n/es.json` covers every string the check finds. About four in five
are [Glasshouse's](https://github.com/rorygallagher2024/glasshouse) Spanish (MIT), used where
the English is unchanged. The rest, for English that Rooted TV changed or added (its own name,
the LG launcher, the Black screen saver, the newer privacy and Home messages), were
machine-translated into neutral Latin-American Spanish, in the same terms and tone as
Glasshouse's: the TV is "la TV", a screen saver a "salvapantallas", Rooted TV Home stays "Home"
and LG's is "el Inicio de LG". **Those have not been read by a native speaker.** A correction is
a pull request that edits the `text` of the entry and leaves its `from` alone.

Not translated: the native TV client, whose three Google Sans Flex cuts cover every
character Spanish needs but whose screens and glyph atlas have not been tried with it;
the pages the TV's own browser shows; names the TV reports; the Homebrew Channel
catalogue's descriptions; and the date formats, which follow the browser's own
language rather than the dashboard's.

## The TV app

The native TV client speaks the TV's own menu language, as Google TV speaks its
device's. The daemon reads webOS's `localeInfo` (`locales.UI`, such as `it-IT`) at
start and again at most once a minute as the client asks for `/api/caps`, keeps it
in `locale` beside the server and names its language there (`"language": "it"`).
The client then fetches `/assets/i18n/it.json`, from which `say::msg()` in the
client returns the translations (`say::showIn()`), and sends `X-Rooted-Tv-Lang: it`
on its requests, so the server's messages it shows, such as a row's title, come
back translated as a dashboard's do. TMDB is asked for titles, overviews and genres
in the same language. With no file for the TV's language, or English, the client
shows English.

The client's strings are keyed like the server's: `msg("tv.home.topPicks",
"Top picks for you")` in `rooted-tv/src/ui/` and the models it shares in
`rooted-tv/src/domain/`, and `check-strings.py` reads them there. A language the TV
client alone speaks is listed in `TV_ONLY` in that script rather than in the
dashboard's `LANGS`, so its file can hold the client's words without offering a
half-translated dashboard.

Italian (`it.json`) covers the screens Google TV has: Home and its tabs, the banner,
a title's details, Search, the Apps tab, Quick settings, the profiles, the recent
apps and the keyboard. Its words are Google TV's own where Google TV has the same
string (taken from the Italian resources of a Chromecast with Google TV's launcher
and from its screens), such as "Scelti per te", "Le tue app" and "Lista di titoli".
Rooted TV's own settings, setup and tools are still English.

## Converting part of a page

Key every piece of text in it, then add the element's id to `CONVERTED` in
`scripts/check-strings.py`. From then on, text added there without a key fails
the check.
