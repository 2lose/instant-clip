# Instant Clip (OBS plugin)

ShadowPlay-style "save the last N seconds" for OBS Studio, without an overlay. One replay buffer runs in the background; each clip length gets its own hotkey and dock button. Pressing one saves the buffer, then trims and remuxes it to the requested length in a single stream-copy pass.

## Using it

1. In OBS: **Settings > Output > Replay Buffer**, enable it and set **Maximum Replay Time** to at least your longest clip length (the dock flags lengths the buffer can't hold).
2. **Docks > Clips** shows the dock.
3. Bind keys in **Settings > Hotkeys**. Look for "Clips: Save last 30s" and so on.
4. Start the buffer from the dock (or OBS's own button), then press a hotkey or click a length.

Clips are named `Clip 2026-10-13 21-14-05 (30s).mp4` and saved to OBS's recording folder unless you pick another one in the dock's settings.

### Tips

- **Keyframe interval:** fast mode can only start a clip on a keyframe, so a clip can run up to one keyframe interval longer than asked. Set the encoder's keyframe interval to 1 or 2 s (Settings > Output > Recording > Keyframe Interval). At "auto" it's often around 4 s.
- **Precise mode** re-encodes the video (using NVENC, AMF, QuickSync or x264, whichever is available) for frame-exact cuts. It's slower: about real-time divided by your encoder's speed.
- **Auto-remux:** the plugin writes MP4/MKV/MOV directly, so OBS's "Automatically remux to MP4" isn't needed for clips. If it's on and "Delete full-length original" is on too, the plugin also removes OBS's full-length remuxed copy of each replay once OBS has finished writing it.
- **Rapid presses** are queued: each press gets its own save, and the end of each clip is aligned to the moment that key was pressed.

## Updates

The plugin checks the latest release of the private repo `2lose/instant-clip` 20 seconds after OBS starts, then every 6 hours (or on **Settings > Updates > Check now**). When there's a newer version the dock shows **Update x.y.z available · Install**. Install downloads the release zip, verifies it against its `.sha256`, and copies it into the plugin folder after OBS closes (the DLL is locked while OBS runs). The next launch says "Updated to x.y.z".

Access to the private repo comes from a read-only token built into the plugin, so updates work on any PC without signing in. Order of use: a token pasted into Settings > Updates (stored with Windows DPAPI), then the built-in one, then the GitHub CLI sign-in.

The built-in token is read from `secrets/update-token.txt` (git-ignored) at build time and embedded AES-256 encrypted with a fresh key each build, so it never appears in the DLL as plain text. That stops casual extraction, not a determined one (the DLL has to carry the key), so the token must be limited to read-only access to this one repo:

1. GitHub > Settings > Developer settings > Fine-grained tokens > Generate new token.
2. Repository access: **Only select repositories** > `2lose/instant-clip`.
3. Permissions: **Contents: Read-only** (Metadata read-only is added automatically). Nothing else.
4. Save the token as the only line of `secrets/update-token.txt`.

When it expires, make a new one, replace the file and publish a release before the old one lapses; installed copies pick up the new token with that update.

Publishing a version (needs `gh` signed in):

```powershell
.\scripts\release.ps1 1.2.0 "What changed"
```

That bumps the version in `CMakeLists.txt`, builds, packages `instant-clip-1.2.0-windows-x64.zip` plus `.sha256`, commits, tags `v1.2.0`, pushes and creates the GitHub release.

## Feedback

The **Feedback** card in the dock opens a form (Bug / Idea / Other, message, optional contact). It posts to the relay in `relay/`, a Cloudflare Worker that holds the Discord webhook as a secret, checks a shared key, rate-limits each IP and forwards the message to Discord. The webhook URL never ships in the plugin. See `relay/README.md`.

## Build secrets

Releases embed three values, encrypted at build time from git-ignored files in `secrets/`. They are never committed:

| File | Purpose |
| --- | --- |
| `secrets/update-token.txt` | fine-grained, read-only token for update checks on this private repo |
| `secrets/feedback-relay-url.txt` | the relay's `/feedback` URL |
| `secrets/feedback-relay-key.txt` | shared key the relay checks (same value as its `RELAY_KEY` secret) |

`release.ps1` refuses to publish if any is missing; local builds without them just lose updates/feedback.

## Performance

Trimming, update checks and downloads run on worker threads in Windows background mode (lowest CPU priority, low I/O and memory priority), so they yield to OBS's encoders and the game. Precise mode uses at most half the CPU cores. The dock does no periodic work while hidden.

## Building (Windows x64)

The build links against the OBS you have installed instead of an OBS source build:

| Folder | What it is |
| --- | --- |
| `deps/obs-studio` | sparse checkout of `libobs` + `frontend/api` headers at your OBS version |
| `deps/ffmpeg` | FFmpeg headers matching OBS's FFmpeg |
| `deps/qt6` | Qt headers and `moc` from the obs-deps package OBS was built with |
| `deps/importlibs` | import libraries generated from the installed DLLs by `scripts/make-import-libs.ps1` |

With those in place and OBS closed:

```powershell
.\scripts\build.ps1
```

It installs to `%ProgramData%\obs-studio\plugins\instant-clip`. After updating OBS, re-run `scripts/make-import-libs.ps1` (and refresh the headers if the FFmpeg or Qt major version changed).

`trim-test` is a command-line harness for the trimmer, so you can test cuts without OBS:

```powershell
cmake --build build --config RelWithDebInfo --target trim-test
$env:PATH = "C:\Program Files\obs-studio\bin\64bit;$env:PATH"
.\build\RelWithDebInfo\trim-test.exe "Replay.mkv" out.mp4 mp4 30          # last 30 s
.\build\RelWithDebInfo\trim-test.exe "Replay.mkv" out.mp4 mp4 30 0 precise
.\build\RelWithDebInfo\trim-test.exe probe out.mp4                        # streams, timestamps, keyframes
```

## Code map

- `src/plugin-main.cpp`: module entry, dock registration
- `src/clip-manager.*`: settings, hotkeys, save-request queue, trim worker thread, cleanup of originals
- `src/trimmer.*`: FFmpeg trim/remux (fast stream copy and precise re-encode)
- `src/ui/`: the dock (custom-painted widgets, Inter fonts in `data/fonts`)

Inter is © The Inter Project Authors, SIL Open Font License (see `data/fonts/Inter-LICENSE.txt`).
