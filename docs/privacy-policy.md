---
description: "Rooted TV's privacy policy: what the app on an LG webOS TV reads from Google when a profile signs in for YouTube, where it is kept, and which services the TV contacts."
---

# Privacy policy

*Last updated: 13 November 2026.*

Rooted TV is free, open-source software that runs on its owner's own LG webOS TV. The project runs no server for it: there is no Rooted TV account, no analytics and no advertising, and nothing Rooted TV handles is sent to the project. The TV contacts only the services listed below, itself and directly, and only those the owner sets up.

## Signing in with Google for YouTube

A profile on Rooted TV Home can sign in to a Google account, through Google's sign-in for TVs (a code entered at google.com/device), to show the newest videos of the channels it subscribes to on YouTube.

* **Access asked for.** Read-only access to YouTube (`youtube.readonly`), and nothing else: no email address, contacts, files or other Google data.
* **What is read.** The signed-in account's YouTube channel name, to show which account is signed in; the channels it subscribes to; and the recent uploads of those channels, with their titles, thumbnails and lengths.
* **Where it is kept.** On the TV only, in `/var/lib/tvweb`: the sign-in in `youtube-accounts.json` and the row's videos in `youtube-rows.json`. The dashboard on the home network shows which account is signed in.
* **What it is used for.** Only the row of that profile's subscriptions on Rooted TV Home, refreshed every hour while Home is shown. It is not sold, shared or used for advertising, not sent to Gemini or any other AI service, and not used to train any model.
* **Removing it.** **Sign out** on the dashboard's Home tab withdraws the access at Google and removes the account from the TV. The access can also be removed at any time from the Google Account's [third-party connections](https://myaccount.google.com/connections). [Uninstalling](getting-started/managing.md#uninstalling) Rooted TV deletes `/var/lib/tvweb` with everything in it.

Rooted TV's use and transfer of information received from Google APIs adheres to the [Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy), including the Limited Use requirements.

## Other services the TV contacts

Each is asked by the TV directly, only once the owner has set it up, and its own privacy policy covers what it receives.

* **TMDB**, with a TMDB key: titles, artwork, cast and genres, in the TV's language and country.
* **Apple TV's trailer pages**: a title's trailer, where it has one.
* **YouTube**, with a YouTube Data API key: the trending videos.
* **Gemini**, with a Gemini key: to choose Top picks, the titles a profile watched, liked or saved and the time played on each streaming service in the last 30 days; for the assistant, the question asked. YouTube data from the sign-in above is never part of it.
* **GitHub**: the Server tab's check for a newer release of Rooted TV, when asked or once a day if switched on.

## What stays on the TV

Settings, keys, profiles and what was watched are kept on the TV in `/var/lib/tvweb`. The dashboard serves them to browsers on the home network, with no login by default: [Security](administration/security.md) covers the access token and closing the dashboard to the network.

## Contact and changes

Questions go to the project's [issues on GitHub](https://github.com/Markk2317/Rooted-TV/issues). Changes to this policy are made on this page, whose history is kept in the repository.
