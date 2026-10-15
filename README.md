# Instant Clip

> **Disclaimer:** This plugin was coded by Claude, Anthropic's AI, using Claude Code.

Instant Clip is an OBS plugin that helps you make the most of your replay buffer by letting you clip any length within your replay buffer's interval. For example, you can clip the last 30s out of your 5m replay buffer cache, or clip the whole 5m.

Each clip length gets its own hotkey and a button in the **Clips** dock. No overlay needed.

## Building from source

If you've edited the source, you can build and install the plugin yourself.

**Requirements**

- Windows 10 or 11 (64-bit)
- [OBS Studio](https://obsproject.com) 32 or newer, installed
- [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) with the **Desktop development with C++** workload
- [Git](https://git-scm.com)

**Steps**

1. Clone the repo and open PowerShell in the project folder.
2. Close OBS (Windows locks the plugin file while OBS is running).
3. Run:

   ```powershell
   .\scripts\build.ps1
   ```

The first build downloads the headers matching your installed OBS (about 300 MB, saved in `deps\`), then compiles the plugin and installs it into OBS. Later builds skip the download.

To build without installing, use `.\scripts\build.ps1 -NoInstall`. The plugin is then at `build\RelWithDebInfo\instant-clip.dll`.

Builds from source don't include the update checker's access token or the feedback form's relay, so those two features stay off. Everything else works the same. See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for details.

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
