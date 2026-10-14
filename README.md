# Instant Clip (OBS plugin)

ShadowPlay-style "save the last N seconds" for OBS Studio, without an overlay. One replay buffer runs in the background; each clip length gets its own hotkey and dock button. Pressing one saves the buffer, then trims and remuxes it to the requested length in a single stream-copy pass.

## Installing

Download `instant-clip-x.y.z-windows-x64.zip` from [Releases](https://github.com/2lose/instant-clip/releases/latest), close OBS, and extract it into `C:\ProgramData\obs-studio\plugins\` so you end up with `C:\ProgramData\obs-studio\plugins\instant-clip\bin\64bit\instant-clip.dll`. Requires OBS Studio 32 on Windows x64. Later versions install themselves (see Updates).

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

The plugin checks the [latest release](https://github.com/2lose/instant-clip/releases/latest) 20 seconds after OBS starts, then every 6 hours (or on **Settings > Updates > Check now**). No account or token is needed. When there's a newer version the dock shows **Update x.y.z available · Install**. Install downloads the release zip, verifies it against its `.sha256`, and copies it into the plugin folder after OBS closes (the DLL is locked while OBS runs). The next launch says "Updated to x.y.z".

Publishing a version (maintainers, needs `gh` signed in):

```powershell
.\scripts\release.ps1 1.2.0 "What changed"
```

That bumps the version in `CMakeLists.txt`, builds, packages `instant-clip-1.2.0-windows-x64.zip` plus `.sha256`, commits, tags `v1.2.0`, pushes and creates the GitHub release.

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

## License

GPL-2.0 (see `LICENSE`), matching OBS Studio, which the plugin links against. Inter is © The Inter Project Authors, SIL Open Font License (see `data/fonts/Inter-LICENSE.txt`).
