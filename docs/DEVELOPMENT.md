# Development notes

Details for maintaining Instant Clip. For building and installing, see the [README](../README.md).

## Tips

- **Keyframe interval:** fast mode can only start a clip on a keyframe, so a clip can run up to one keyframe interval longer than asked. Set the encoder's keyframe interval to 1 or 2 s (Settings > Output > Recording > Keyframe Interval). At "auto" it's often around 4 s.
- **Precise mode** re-encodes the video (using NVENC, AMF, QuickSync or x264, whichever is available) for frame-exact cuts. It's slower: about real-time divided by your encoder's speed.
- **Auto-remux:** the plugin writes MP4/MKV/MOV directly, so OBS's "Automatically remux to MP4" isn't needed for clips. If it's on and "Delete full-length original" is on too, the plugin also removes OBS's full-length remuxed copy of each replay once OBS has finished writing it.
- **Rapid presses** are queued: each press gets its own save, and the end of each clip is aligned to the moment that key was pressed.

## Updates

The plugin checks the [latest release](https://github.com/2lose/instant-clip/releases/latest) 20 seconds after OBS starts, then every 6 hours (or on **Settings > Updates > Check now**). The repo is public, so no account or token is involved. When there's a newer version the dock shows **Update x.y.z available · Install**. Install downloads the release zip, verifies it against its `.sha256`, and copies it into the plugin folder after OBS closes (the DLL is locked while OBS runs). The next launch says "Updated to x.y.z".

Publishing a version (needs `gh` signed in):

```powershell
.\scripts\release.ps1 1.2.0 "What changed"
```

That bumps the version in `buildspec.json`, builds, packages `instant-clip-1.2.0-windows-x64.zip` plus `.sha256`, commits, tags `v1.2.0`, pushes and creates the GitHub release.

## Feedback

The **Feedback** card in the dock opens a form (Bug / Idea / Other, message, optional contact). It posts to the relay in `relay/`, a Cloudflare Worker that holds the Discord webhook as a secret, checks a shared key, rate-limits each IP (3/min) and all senders together (200/day), and forwards the message to Discord. The webhook URL never ships in the plugin. See `relay/README.md`.

## Build secrets

Releases embed the feedback relay's two values, encrypted at build time from git-ignored files in `secrets/`. They are never committed:

| File | Purpose |
| --- | --- |
| `secrets/feedback-relay-url.txt` | the relay's `/feedback` URL |
| `secrets/feedback-relay-key.txt` | shared key the relay checks (same value as its `RELAY_KEY` secret) |

`release.ps1` refuses to publish if either is missing; local builds without them just hide the Feedback form.

## Performance

Trimming, update checks and downloads run on worker threads in Windows background mode (lowest CPU priority, low I/O and memory priority), so they yield to OBS's encoders and the game. Precise mode uses at most half the CPU cores. The dock does no periodic work while hidden.

## Building (Windows x64)

Standard OBS plugin setup from [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate): `buildspec.json` pins the OBS version (32.2.2) and the matching prebuilt obs-deps/Qt packages. The first `cmake --preset windows-x64` downloads them into `.deps/` and builds libobs and obs-frontend-api; FFmpeg comes from the prebuilt package. To target a newer OBS, update the three versions and hashes in `buildspec.json` (OBS lists its deps versions and hashes in its own `CMakePresets.json`).

## Releasing (maintainer)

`scripts/release.ps1 <version> "<notes>"` is the only script, and only needed for publishing. It bumps the version in `buildspec.json`, encrypts the build secrets (`scripts/embed-secrets.ps1`), builds with the preset, packages the zip and `.sha256`, commits, tags, pushes and creates the GitHub release.

## Code map

- `src/plugin-main.cpp`: module entry, dock registration
- `src/clip-manager.*`: settings, hotkeys, save-request queue, trim worker thread, cleanup of originals
- `src/trimmer.*`: FFmpeg trim/remux (fast stream copy and precise re-encode)
- `src/ui/`: the dock (custom-painted widgets, Inter fonts in `data/fonts`)

Inter is © The Inter Project Authors, SIL Open Font License (see `data/fonts/Inter-LICENSE.txt`).
