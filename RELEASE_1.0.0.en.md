🇺🇸 [English](RELEASE_1.0.0.en.md) | 🇧🇷 [Português (Brasil)](RELEASE_1.0.0.md)

# MyFolderMenu 1.0.0

**Tag:** `v1.0.0`  
**Date:** 2026-10-30  
**Platform:** Windows x64  
**Runtime:** .NET 8 Desktop Runtime

A system tray application to quickly access your favorite folders and files, without a main window or taskbar clutter.

## Highlights

- Configurable folder menu directly from the tray icon.
- Lazy-loaded submenus with folders first and files second.
- Files opened using default Windows handlers with real shell icons.
- Settings for Windows startup, file extension visibility, and themes.
- Light, Dark, and System themes, including dark title bars.
- Full configuration export and import.
- Single instance enforcement and INI-backed persistence.

## Changes in This Version

- Application icon aligned with the tray icon.
- Version promoted to `1.0.0`.
- Consolidated theme, extension, and startup settings.
- Folder and preference export/import via the settings dialog.

## Installation

1. Download `MyFolderMenu-1.0.0-win-x64.zip`.
2. Extract the contents to your preferred directory.
3. Run `MyFolderMenu.exe`.
4. Optionally, open **Settings...** and check **Start with Windows**.

The application is portable and does not include an installer.

## Requirements

- Windows 10 or Windows 11, 64-bit.
- .NET 8 Desktop Runtime (x64).

## Configuration

- On first launch, Desktop, Downloads, and Documents folders are added.
- Use **Folders** in the tray menu to add or remove directories.
- Configuration file is located at `%APPDATA%\MyFolderMenu\folders.ini`.
- Hiding file extensions is enabled by default.
- Exporting/importing includes folders, theme, and extension preference. **Start with Windows** is user-specific and not included in the file.

## Files

- `MyFolderMenu-1.0.0-win-x64.zip`
- SHA-256: `c8f8b0d56c25f433340e915b3d022f63dcc55020c42cdd8abc23e39adec9c961`

## Notes

- The executable is not digitally signed; Windows SmartScreen may display a warning on first run.
- Requires .NET 8 Desktop Runtime installed.
