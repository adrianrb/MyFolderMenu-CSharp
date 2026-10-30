# AGENTS.md

## Status
- .NET 8 WinForms tray app (`MyFolderMenu`); git repo initialized, no remote configured.
- Current version is `0.1.1`, stored in both `version.txt` and `<Version>` in the csproj; keep them in sync.
- No tests, lint, CI, or solution file. The only verification is `dotnet.exe build`; never claim tests were run.

## Versioning / commits
- Version is `X.Y.Z`; update both `version.txt` and `<Version>` together.
- For each delivered build: increment `Z`, run the build, then commit.
- If the user explicitly says "melhoria": increment `Y`, reset `Z` to `0`, build, and commit.
- Do not bump or commit after a failed build; fix and rebuild the same increment.

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
- **No main form**: `Program` runs a `TrayApplicationContext` with `NotifyIcon` + `ContextMenuStrip`, so nothing appears in the taskbar. The only window is the modal `AboutForm` opened from "Sobre".
- Single instance is enforced with a `Local\MyFolderMenu` mutex; a second launch exits silently.
- Config: `%APPDATA%\MyFolderMenu\folders.ini`, section `[Folders]`, entries `Folder1=C:\...` (backslashes are literal). Created with Desktop, Downloads, and Documents; legacy `folders.json` is migrated once and then ignored.
- Root folders are reloaded and the menu rebuilt every time the tray menu opens. Submenus load lazily on `DropDownOpening`.
- Hidden/system entries are skipped. Submenus list subfolders first, then files; files open with the default Windows handler and use their real shell icons.
- Folders with no visible entries open in Explorer on click; folders with any entries expose "Abrir esta pasta" as the first submenu item.
- Keep the flat layout (`Program.cs` + `app.manifest`, no designer files) and Portuguese UI text, matching sibling `../EdgeAutoKill`.
