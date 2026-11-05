# Privacy Policy

**Plasma Touchpad** (the on-screen touchpad, its System Settings module and
its dashboard widgets)

Effective date: November 5, 2026

Plasma Touchpad is free, open-source software that runs entirely on your own
computer. This policy explains what information it handles, especially when
you choose to sign in to YouTube from the dashboard Videos widget.

## The short version

- Plasma Touchpad has **no servers**. The developer does not collect,
  receive, store or see any of your data.
- If you sign in to YouTube, the app reads **only your list of channel
  subscriptions and your channel name**, and keeps them **on your computer**.
- Your data is never sold, shared, or used for advertising.
- You can sign out at any time, which deletes this data from your computer
  and tells Google to revoke the app's access.

## Information the app uses

### Touch input and settings

The touchpad turns touches on your screen into pointer movement. Touch events
are processed in memory, as they happen, and are not recorded. Your settings
are saved in a local configuration file (`~/.config/plasma-touchpadrc`).

### YouTube account (optional)

The Videos widget can show the latest videos from the channels you subscribe
to. This only happens if you click **Sign In to YouTube** and grant access.

When you sign in, Google's sign-in page opens in your own default web
browser. The app never sees your Google password. It asks Google for one
permission, **"View your YouTube account"**
(`https://www.googleapis.com/auth/youtube.readonly`), and uses it only to
read:

| Data | Why it's used | Where it's kept |
|---|---|---|
| Your list of channel subscriptions (channel IDs and names) | To show recent videos from those channels | `~/.config/plasma-touchpadrc` on your computer |
| Your YouTube channel name | To show which account is signed in, in the widget's settings | `~/.config/plasma-touchpadrc` on your computer |
| An OAuth refresh token from Google | To stay signed in without asking you each time | KWallet, the encrypted KDE password store on your computer |

The app does not read or change your watch history, playlists, comments,
likes, email, contacts or any other account data. It cannot post, subscribe,
unsubscribe or upload anything on your behalf.

The subscription list and channel name are refreshed from YouTube every few
hours while the widget is in use. If they haven't been refreshed for 30 days,
they are deleted automatically. They are also deleted when you sign out.

### News and video feeds

The News widget downloads the RSS or Atom feeds you configure (by default from
NPR, Google News, 9to5Linux and Linux Today). The Videos widget downloads
each subscribed channel's public YouTube feed and its video thumbnails. These
requests go straight from your computer to those sites. Like any web request,
they reveal your IP address to that site, and they are covered by that site's
own privacy policy.

### Viewing articles and videos in the widget

If you choose to open articles or videos inside a widget (the default), they
are shown in a built-in web view. Like a browser, it may keep cookies, cache
and site data in a local profile on your computer
(`~/.local/share/plasmashell/QtWebEngine/plasma-touchpad-dashboard`). Videos
are played with YouTube's embedded player, which is provided by YouTube and
covered by Google's privacy policy. You can switch either widget to **Open in
the browser** in its settings.

## How Google user data is used

Plasma Touchpad's use and transfer of information received from Google APIs
adheres to the
[Google API Services User Data Policy](https://developers.google.com/terms/api-services-user-data-policy),
including the Limited Use requirements. Specifically:

- Google user data is used only to provide the Videos widget feature
  described above.
- It is not transferred to anyone. It goes only from Google to your computer.
- It is not used for advertising, and it is not sold.
- No person, including the developer, reads it.
- It is not used to develop, improve or train AI or machine-learning models.

Plasma Touchpad uses YouTube API Services. By signing in, you also agree to
the [YouTube Terms of Service](https://www.youtube.com/t/terms), and Google's
handling of your data is covered by the
[Google Privacy Policy](https://policies.google.com/privacy).

## Removing your data and revoking access

- **Sign out:** In the Videos widget's settings, click **Sign Out**. This
  asks Google to revoke the app's access, and deletes the refresh token,
  subscription list and channel name from your computer.
- **Revoke from Google:** You can also remove access at any time from your
  [Google Account's third-party connections](https://myaccount.google.com/connections).
  The app then forgets the account the next time it tries to use it.
- **Uninstall:** Removing the app and deleting `~/.config/plasma-touchpadrc`,
  the `plasma-touchpad` folder in KWallet, and the web profile folder named
  above removes everything it stored.

## Children

Plasma Touchpad is not directed at children under 13, and it collects no
information about anyone.

## Changes to this policy

If this policy changes, the new version will be published at this address,
with a new effective date. You can see every past version in the project's
history on GitHub.

## Contact

Dave Carey, <carvsdriver@gmail.com>
Project: <https://github.com/carvsdriver/kde-plasma-touchpad>
