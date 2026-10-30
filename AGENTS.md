# AGENTS.md

## Status
- .NET 8 WinForms tray app (`MyFolderMenu`); git repo initialized, no remote configured.
- Current version is `0.1.0`, stored in both `version.txt` and `<Version>` in the csproj; keep them in sync.
- No tests, lint, CI, or solution file. The only verification is `dotnet.exe build`; never claim tests were run.

## Environment (WSL)
- Native `dotnet` and `git` are **not installed** in WSL. Use Windows binaries: `dotnet.exe` (verified, SDK 10.0.300).
- Repo is on `/mnt/e/IAs, estudos etc/...`; Windows tools see it as `E:\IAs, estudos etc\...`.
- Targets `net8.0-windows` even though the installed SDK is 10.x.

## Commands (run in this directory)
- Build: `dotnet.exe build`
- Run: `dotnet.exe run` (blocks; the app has no window)
- Publish single-file win-x64: `dotnet.exe publish -c Release -r win-x64`
- Stop a running instance: `cmd.exe /c taskkill /IM MyFolderMenu.exe /F`
- Build output: `bin/Debug/net8.0-windows/win-x64/MyFolderMenu.exe`

## Architecture / gotchas
- **No `Form`**: `Program` runs a `TrayApplicationContext` with `NotifyIcon` + `ContextMenuStrip`, so nothing appears in the taskbar. Do not add a visible form.
- Single instance is enforced with a `Local\MyFolderMenu` mutex; a second launch exits silently.
- Config: `%APPDATA%\MyFolderMenu\folders.json` (`{ "Folders": [...] }`). On first run it is created with Desktop, Downloads, and Documents.
- Root folders are reloaded and the menu rebuilt every time the tray menu opens. Submenus load lazily on `DropDownOpening`.
- Hidden/system subdirectories are skipped. Folders without subfolders open in Explorer on click; those with subfolders expose "Abrir esta pasta" as the first submenu item.
- Keep the flat layout (`Program.cs` + `app.manifest`, no designer files) and Portuguese UI text, matching sibling `../EdgeAutoKill`.
