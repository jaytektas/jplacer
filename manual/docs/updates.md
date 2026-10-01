# Updates and betas

jplacer updates itself. Updates come from the project's
[GitHub releases](https://github.com/jaytektas/jplacer/releases).

## When it looks

- **When jplacer opens**, unless you have turned that off in [Preferences](preferences.md). It only
  speaks up when there is a newer version: no internet connection, for example, is not worth
  interrupting you for every time.
- **Help ▸ Check for Updates** (or **Check Now** in Preferences). Because you asked, you are told every
  answer: a newer version, that you are up to date, or that it could not check and why.

<!-- src: src/app/JPlacerApp.cpp (run checks at startup when updatesAtStartup); JFramework include/j/app/JAppUpdater.h (check, manual reports every outcome) -->

## Installing an update

When there is a newer version, jplacer says which version is available and which you have, and asks
whether to download and install it.

- **Update** downloads it, with a progress window you can cancel. When the download is complete,
  jplacer closes, puts the new version in place and starts it again.
- **Not now** leaves it for later. Tick **Don't ask about *version* again** first and that version is
  not offered when jplacer opens; a later version is, and **Check for Updates** still finds it.

Every download is checked against the checksum published with the release before anything is changed.
A download that does not match is not installed, and nothing is changed if a download fails.

<!-- src: JFramework include/j/app/JAppUpdater.h (offer, download, the checksum check, requestClose after staging) -->

Only the AppImage can update itself. A copy of jplacer built from source tells you an update is
available but cannot install it.

<!-- src: JFramework include/j/update/JSelfInstaller.h (canInstallHere) -->

## Beta versions

Beta versions are released before a full release so new features can be tried early. They are offered
only if you tick **Include beta versions** in Preferences. A beta is numbered below the release it
leads to (0.1.2-beta.1 comes before 0.1.2), so when that release comes out it is offered to you as
usual.

<!-- src: JFramework include/j/update/JVersion.h (pre-releases order before their release); JFramework include/j/app/JAppUpdater.h (betaSetting) -->
