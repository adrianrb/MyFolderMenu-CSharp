🇺🇸 [English](README.md) | 🇧🇷 [Português (Brasil)](README.pt-br.md)

# MyFolderMenu

A lightweight, high-performance Windows system tray application (`.NET 8` WinForms) that provides quick access to your favorite folders and files without cluttering your taskbar or desktop.

---

## Features

- **System Tray Integration:** Runs discreetly in the tray with a custom icon extracted directly from the executable.
- **Lazy Loading Submenus:** Subfolders and files are loaded dynamically on demand (`DropDownOpening`), ensuring instant startup and low memory usage.
- **Smart Sorting:** Submenus list subdirectories first, followed by files, both sorted alphabetically.
- **Real Shell Icons:** Files and folders render with their authentic Windows shell icons and registered handlers.
- **File Extensions Toggle:** Option to hide or show file extensions (`.txt`, `.pdf`, etc.).
- **Theme Support:** Supports **Light**, **Dark**, and **System** themes, including native dark title bars on Windows 10/11.
- **Settings & Configuration:** Fully persistent settings and folder list stored in `%APPDATA%\MyFolderMenu\folders.ini`, with support for full export/import.
- **Windows Startup Integration:** Toggle "Start with Windows" directly from the settings dialog.
- **Single Instance:** Enforced via a local mutex (`Local\MyFolderMenu`) to prevent duplicate processes.

---

## Requirements

- **OS:** Windows 10 or Windows 11 (64-bit)
- **Runtime:** .NET 8 Desktop Runtime (x64)

---

## Installation & Running

1. Download or build the latest release package (`MyFolderMenu-win-x64`).
2. Extract the files to your preferred directory.
3. Run `MyFolderMenu.exe`.
4. Right-click the tray icon to access your folders, add new directories, or open **Settings...**.

---

## Configuration

On first launch, MyFolderMenu automatically initializes with your **Desktop**, **Downloads**, and **Documents** folders. 
You can customize your folders, toggle file extensions, and switch themes via the tray menu or by editing `%APPDATA%\MyFolderMenu\folders.ini`.

---

## License

Copyleft / Open Source. See repository for details.
