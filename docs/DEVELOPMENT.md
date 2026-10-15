# Development notes

Details for maintaining Instant Clip. For building and installing, see the [README](../README.md).

## Tips

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

That bumps the version in `buildspec.json`, builds, packages `instant-clip-1.2.0-windows-x64.zip` plus `.sha256`, commits, tags `v1.2.0`, pushes and creates the GitHub release.

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

Standard OBS plugin setup from [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate): `buildspec.json` pins the OBS version (32.2.2) and the matching prebuilt obs-deps/Qt packages. The first `cmake --preset windows-x64` downloads them into `.deps/` and builds libobs and obs-frontend-api; FFmpeg comes from the prebuilt package. To target a newer OBS, update the three versions and hashes in `buildspec.json` (OBS lists its deps versions and hashes in its own `CMakePresets.json`).

## Releasing (maintainer)

`scripts/release.ps1 <version> "<notes>"` is the only script, and only needed for publishing. It bumps the version in `buildspec.json`, encrypts the build secrets (`scripts/embed-secrets.ps1`), builds with the preset, packages the zip and `.sha256`, commits, tags, pushes and creates the GitHub release.

## Code map

- `src/plugin-main.cpp`: module entry, dock registration
- `src/clip-manager.*`: settings, hotkeys, save-request queue, trim worker thread, cleanup of originals
- `src/trimmer.*`: FFmpeg trim/remux (fast stream copy and precise re-encode)
- `src/ui/`: the dock (custom-painted widgets, Inter fonts in `data/fonts`)

Inter is © The Inter Project Authors, SIL Open Font License (see `data/fonts/Inter-LICENSE.txt`).
