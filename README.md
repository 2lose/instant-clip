# YO.

This shit is definitely vibe coded.. I will not lie to u.. i didnt code a single piece of this shit. jus bein honest

# Instant Clip

Instant Clip is an OBS plugin that helps you make the most of your replay buffer by letting you clip any length within your replay buffer's interval. For example, you can clip the last 30s out of your 5m replay buffer cache, or clip the whole 5m.

Each clip length gets its own hotkey and a button in the **Clips** dock. No overlay needed.

## Building from source

If you've edited the source, you can build the plugin yourself. It uses the standard OBS plugin build setup (CMake presets), so there are no scripts to run.

**Requirements**

- Windows 10 or 11 (64-bit)
- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (Community or Build Tools) with the **Desktop development with C++** workload
- [Git](https://git-scm.com)

**Build**

In a terminal in the project folder:

```
cmake --preset windows-x64
cmake --build --preset windows-x64
```

Or open the folder in Visual Studio or VS Code (with the CMake Tools extension), pick the **Windows x64** preset and build.

The first configure downloads the OBS source code and its dependencies (about 1 GB, into `.deps\`) and takes a few minutes. After that, builds are quick.

**Install your build into OBS**

Close OBS, then run:

```
cmake --install build_x64 --config RelWithDebInfo
```

This copies the plugin to `C:\ProgramData\obs-studio\plugins\instant-clip`. If you get an access error, run the terminal as administrator.

Builds from source don't include the update checker's access token or the feedback form's relay, so those two features stay off. Everything else works the same. See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for more.

## Installing

1. Download `instant-clip-x.y.z-windows-x64.zip` from the [latest release](../../releases/latest).
2. Close OBS.
3. Extract the zip into `C:\ProgramData\obs-studio\plugins\`. You should end up with `C:\ProgramData\obs-studio\plugins\instant-clip\bin\64bit\instant-clip.dll`.
4. Open OBS and turn on the dock from **Docks > Clips**.
5. In **Settings > Output > Replay Buffer**, enable the replay buffer and set its maximum time to your longest clip (for example 5 minutes).
6. Bind a key for each clip length in **Settings > Hotkeys** (look for "Clips: Save last 30s" and so on).

Start the replay buffer, then press a hotkey or click a length in the dock to save a clip. New versions install themselves: the dock lets you know when one is ready.

---

The dock uses the [Inter](https://rsms.me/inter/) typeface by The Inter Project Authors, under the SIL Open Font License.
