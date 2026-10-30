using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows.Forms;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        using var mutex = new Mutex(true, @"Local\MyFolderMenu", out var createdNew);
        if (!createdNew)
        {
            return;
        }

        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new TrayApplicationContext());
    }
}

internal sealed class TrayApplicationContext : ApplicationContext
{
    private const string AppName = "MyFolderMenu";

    private readonly NotifyIcon _notifyIcon;
    private readonly ContextMenuStrip _menu;
    private readonly FolderStore _store = new();
    private readonly Icon _folderIcon;
    private readonly Image _folderImage;
    private readonly Dictionary<string, Image> _fileImages = new(StringComparer.OrdinalIgnoreCase);
    private List<string> _folders;

    public TrayApplicationContext()
    {
        _folderIcon = ShellIcons.GetFolderIcon();
        _folderImage = _folderIcon.ToBitmap();
        _store.EnsureCreated();
        _folders = _store.Load();

        _menu = new ContextMenuStrip { ShowItemToolTips = true };
        _menu.Opening += (_, _) =>
        {
            _folders = _store.Load();
            BuildMenu();
        };

        _notifyIcon = new NotifyIcon
        {
            Icon = _folderIcon,
            Text = AppName,
            ContextMenuStrip = _menu,
            Visible = true
        };
        _notifyIcon.DoubleClick += (_, _) => OpenFirstFolder();

        BuildMenu();
    }

    private void BuildMenu()
    {
        ClearItems(_menu.Items);
        ClearFileImages();

        var available = _folders.Where(Directory.Exists).ToList();
        foreach (var folder in available)
        {
            _menu.Items.Add(CreateFolderItem(folder));
        }

        if (available.Count == 0)
        {
            _menu.Items.Add(new ToolStripMenuItem("Nenhuma pasta configurada") { Enabled = false });
        }

        _menu.Items.Add(new ToolStripSeparator());

        var manage = new ToolStripMenuItem("Pastas");
        manage.DropDownItems.Add(new ToolStripMenuItem("Adicionar pasta...", null, (_, _) => AddFolder()));

        var remove = new ToolStripMenuItem("Remover pasta");
        if (_folders.Count == 0)
        {
            remove.Enabled = false;
        }
        else
        {
            foreach (var folder in _folders)
            {
                var label = $"{GetFolderLabel(folder)}  —  {folder}";
                remove.DropDownItems.Add(new ToolStripMenuItem(Escape(label), null, (_, _) => RemoveFolder(folder)));
            }
        }

        manage.DropDownItems.Add(remove);
        _menu.Items.Add(manage);

        _menu.Items.Add(new ToolStripMenuItem("Abrir configuração", null, (_, _) => OpenConfig()));
        _menu.Items.Add(new ToolStripMenuItem("Sobre", null, (_, _) => ShowAbout()));
        _menu.Items.Add(new ToolStripSeparator());
        _menu.Items.Add(new ToolStripMenuItem("Sair", null, (_, _) => ExitApplication()));
    }

    private FolderMenuItem CreateFolderItem(string path)
    {
        var item = new FolderMenuItem(path)
        {
            Text = Escape(GetFolderLabel(path)),
            Image = _folderImage,
            ToolTipText = path
        };

        if (HasEntries(path))
        {
            item.DropDownItems.Add(new ToolStripMenuItem("Carregando...") { Enabled = false });
            item.DropDown.ShowItemToolTips = true;
            item.DropDownOpening += (_, _) => PopulateSubmenu(item);
        }
        else
        {
            item.Click += (_, _) => OpenPath(path);
        }

        return item;
    }

    private ToolStripMenuItem CreateFileItem(string path)
    {
        var item = new ToolStripMenuItem(Escape(Path.GetFileName(path)))
        {
            Image = GetFileImage(path),
            ToolTipText = path
        };
        item.Click += (_, _) => OpenPath(path);
        return item;
    }

    private Image GetFileImage(string path)
    {
        if (_fileImages.TryGetValue(path, out var image))
        {
            return image;
        }

        using var icon = ShellIcons.GetFileIcon(path);
        image = icon.ToBitmap();
        _fileImages[path] = image;
        return image;
    }

    private void ClearFileImages()
    {
        foreach (var image in _fileImages.Values)
        {
            image.Dispose();
        }

        _fileImages.Clear();
    }

    private void PopulateSubmenu(FolderMenuItem item)
    {
        if (item.Populated)
        {
            return;
        }

        item.Populated = true;
        ClearItems(item.DropDownItems);
        item.DropDownItems.Add(new ToolStripMenuItem("Abrir esta pasta", _folderImage, (_, _) => OpenPath(item.FolderPath)));
        item.DropDownItems.Add(new ToolStripSeparator());

        try
        {
            var directories = Directory.EnumerateDirectories(item.FolderPath)
                .Where(IsVisibleEntry)
                .OrderBy(GetFolderLabel, StringComparer.CurrentCultureIgnoreCase)
                .ToList();

            var files = Directory.EnumerateFiles(item.FolderPath)
                .Where(IsVisibleEntry)
                .OrderBy(path => Path.GetFileName(path), StringComparer.CurrentCultureIgnoreCase)
                .ToList();

            if (directories.Count == 0 && files.Count == 0)
            {
                item.DropDownItems.Add(new ToolStripMenuItem("(vazio)") { Enabled = false });
                return;
            }

            foreach (var directory in directories)
            {
                item.DropDownItems.Add(CreateFolderItem(directory));
            }

            if (directories.Count > 0 && files.Count > 0)
            {
                item.DropDownItems.Add(new ToolStripSeparator());
            }

            foreach (var file in files)
            {
                item.DropDownItems.Add(CreateFileItem(file));
            }
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or IOException)
        {
            item.DropDownItems.Add(new ToolStripMenuItem("(pasta indisponível)") { Enabled = false });
        }
    }

    private void AddFolder()
    {
        using var dialog = new FolderBrowserDialog
        {
            Description = "Escolha uma pasta para exibir no menu",
            UseDescriptionForTitle = true,
            ShowNewFolderButton = false
        };

        if (dialog.ShowDialog() != DialogResult.OK || string.IsNullOrWhiteSpace(dialog.SelectedPath))
        {
            return;
        }

        if (!_folders.Contains(dialog.SelectedPath, StringComparer.OrdinalIgnoreCase))
        {
            _folders.Add(dialog.SelectedPath);
        }

        _store.Save(_folders);
        _folders = _store.Load();
    }

    private void RemoveFolder(string folder)
    {
        _folders.RemoveAll(item => string.Equals(item, folder, StringComparison.OrdinalIgnoreCase));
        _store.Save(_folders);
        _folders = _store.Load();
    }

    private void OpenConfig()
    {
        try
        {
            _store.EnsureCreated();
            Process.Start(new ProcessStartInfo
            {
                FileName = "explorer.exe",
                Arguments = $"/select,\"{_store.FilePath}\"",
                UseShellExecute = true
            });
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            MessageBox.Show(
                $"Não foi possível abrir a configuração:\n{_store.FilePath}\n\n{ex.Message}",
                AppName,
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }
    }

    private static void ShowAbout()
    {
        using var about = new AboutForm();
        about.ShowDialog();
    }

    private void OpenFirstFolder()
    {
        var folder = _folders.FirstOrDefault(Directory.Exists);
        if (folder is not null)
        {
            OpenPath(folder);
        }
    }

    private static void OpenPath(string path)
    {
        try
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = path,
                UseShellExecute = true
            });
        }
        catch (Exception ex) when (ex is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            MessageBox.Show(
                $"Não foi possível abrir:\n{path}\n\n{ex.Message}",
                AppName,
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }
    }

    private static bool HasEntries(string path)
    {
        try
        {
            return Directory.EnumerateFileSystemEntries(path).Any(IsVisibleEntry);
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or IOException)
        {
            return false;
        }
    }

    private static bool IsVisibleEntry(string path)
    {
        try
        {
            var attributes = File.GetAttributes(path);
            return (attributes & (FileAttributes.Hidden | FileAttributes.System)) == 0;
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or IOException)
        {
            return false;
        }
    }

    private static string GetFolderLabel(string path)
    {
        var trimmed = path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        var name = Path.GetFileName(trimmed);
        return string.IsNullOrWhiteSpace(name) ? path : name;
    }

    private static string Escape(string text)
    {
        return text.Replace("&", "&&");
    }

    private static void ClearItems(ToolStripItemCollection items)
    {
        while (items.Count > 0)
        {
            var item = items[0];
            items.RemoveAt(0);
            item.Dispose();
        }
    }

    private void ExitApplication()
    {
        _notifyIcon.Visible = false;
        _notifyIcon.Dispose();
        _menu.Dispose();
        ClearFileImages();
        _folderImage.Dispose();
        _folderIcon.Dispose();
        ExitThread();
    }

    private sealed class FolderMenuItem : ToolStripMenuItem
    {
        public FolderMenuItem(string folderPath)
        {
            FolderPath = folderPath;
        }

        public string FolderPath { get; }

        public bool Populated { get; set; }
    }

    private sealed class AboutForm : Form
    {
        public AboutForm()
        {
            Text = "Sobre o MyFolderMenu";
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition = FormStartPosition.CenterScreen;
            MaximizeBox = false;
            MinimizeBox = false;
            ShowInTaskbar = false;
            ClientSize = new Size(430, 180);

            var title = new Label
            {
                Text = "MyFolderMenu",
                AutoSize = true,
                Location = new Point(24, 20)
            };

            var version = new Label
            {
                Text = $"Versão {typeof(TrayApplicationContext).Assembly.GetName().Version?.ToString(3) ?? "0.0.0"}",
                AutoSize = true,
                Location = new Point(24, 55)
            };

            var copyright = new Label
            {
                Text = "(2026) Copyleft by adrian.barbosa@gmail.com",
                AutoSize = true,
                Location = new Point(24, 85)
            };

            var ok = new Button
            {
                Text = "OK",
                DialogResult = DialogResult.OK,
                Size = new Size(90, 30),
                Location = new Point(ClientSize.Width - 114, ClientSize.Height - 52)
            };

            AcceptButton = ok;
            CancelButton = ok;
            Controls.AddRange(new Control[] { title, version, copyright, ok });
        }
    }
}

internal sealed class FolderStore
{
    public string DirectoryPath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "MyFolderMenu");

    public string FilePath => Path.Combine(DirectoryPath, "folders.ini");

    private string LegacyFilePath => Path.Combine(DirectoryPath, "folders.json");

    public void EnsureCreated()
    {
        if (File.Exists(FilePath))
        {
            return;
        }

        var folders = File.Exists(LegacyFilePath) ? LoadLegacyJson() : GetDefaultFolders();
        if (folders.Count == 0)
        {
            folders = GetDefaultFolders();
        }

        Save(folders);
    }

    public List<string> Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                return ParseIni(File.ReadAllLines(FilePath));
            }

            if (File.Exists(LegacyFilePath))
            {
                return LoadLegacyJson();
            }

            return GetDefaultFolders();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return GetDefaultFolders();
        }
    }

    public void Save(IEnumerable<string> folders)
    {
        try
        {
            Directory.CreateDirectory(DirectoryPath);
            var normalized = Normalize(folders);
            var lines = new List<string> { "[Folders]" };
            for (var index = 0; index < normalized.Count; index++)
            {
                lines.Add($"Folder{index + 1}={normalized[index]}");
            }

            File.WriteAllLines(FilePath, lines);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
    }

    private static List<string> ParseIni(IEnumerable<string> lines)
    {
        var folders = new List<string>();
        var inFoldersSection = false;

        foreach (var raw in lines)
        {
            var line = raw.Trim();
            if (line.Length == 0 || line.StartsWith(';') || line.StartsWith('#'))
            {
                continue;
            }

            if (line.StartsWith('[') && line.EndsWith(']'))
            {
                var section = line[1..^1].Trim();
                inFoldersSection = section.Equals("Folders", StringComparison.OrdinalIgnoreCase)
                    || section.Equals("Pastas", StringComparison.OrdinalIgnoreCase);
                continue;
            }

            if (!inFoldersSection)
            {
                continue;
            }

            var separator = line.IndexOf('=');
            if (separator <= 0)
            {
                continue;
            }

            var value = line[(separator + 1)..].Trim();
            if (value.Length >= 2 && value[0] == '"' && value[^1] == '"')
            {
                value = value[1..^1];
            }

            folders.Add(value);
        }

        return Normalize(folders);
    }

    private List<string> LoadLegacyJson()
    {
        try
        {
            if (!File.Exists(LegacyFilePath))
            {
                return new List<string>();
            }

            var config = JsonSerializer.Deserialize<MenuConfig>(File.ReadAllText(LegacyFilePath));
            return Normalize(config?.Folders);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return new List<string>();
        }
    }

    private static List<string> Normalize(IEnumerable<string>? folders)
    {
        var result = new List<string>();
        if (folders is null)
        {
            return result;
        }

        foreach (var raw in folders)
        {
            if (string.IsNullOrWhiteSpace(raw))
            {
                continue;
            }

            var expanded = Environment.ExpandEnvironmentVariables(raw.Trim());
            string fullPath;
            try
            {
                fullPath = Path.GetFullPath(expanded);
            }
            catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException)
            {
                fullPath = expanded;
            }

            if (!result.Contains(fullPath, StringComparer.OrdinalIgnoreCase))
            {
                result.Add(fullPath);
            }
        }

        return result;
    }

    private static List<string> GetDefaultFolders()
    {
        var folders = new List<string>();
        var desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
        var documents = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
        var profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);

        if (!string.IsNullOrWhiteSpace(desktop))
        {
            folders.Add(desktop);
        }

        if (!string.IsNullOrWhiteSpace(profile))
        {
            folders.Add(Path.Combine(profile, "Downloads"));
        }

        if (!string.IsNullOrWhiteSpace(documents))
        {
            folders.Add(documents);
        }

        return Normalize(folders);
    }

    private sealed class MenuConfig
    {
        public List<string> Folders { get; set; } = new();
    }
}

internal static class ShellIcons
{
    private const uint ShgfiIcon = 0x000000100;
    private const uint ShgfiSmallIcon = 0x000000001;
    private const uint ShgfiUseFileAttributes = 0x000000010;
    private const uint FileAttributeDirectory = 0x00000010;
    private const uint FileAttributeNormal = 0x00000080;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct ShFileInfo
    {
        public IntPtr hIcon;
        public int iIcon;
        public uint dwAttributes;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string szDisplayName;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)]
        public string szTypeName;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SHGetFileInfo(
        string pszPath,
        uint dwFileAttributes,
        ref ShFileInfo psfi,
        uint cbFileInfo,
        uint uFlags);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyIcon(IntPtr hIcon);

    public static Icon GetFolderIcon()
    {
        return TryGetIcon("folder", FileAttributeDirectory, true)
            ?? (Icon)SystemIcons.Application.Clone();
    }

    public static Icon GetFileIcon(string path)
    {
        return TryGetIcon(path, 0, false)
            ?? TryGetIcon($"file{Path.GetExtension(path)}", FileAttributeNormal, true)
            ?? (Icon)SystemIcons.Application.Clone();
    }

    private static Icon? TryGetIcon(string path, uint attributes, bool useFileAttributes)
    {
        var flags = ShgfiIcon | ShgfiSmallIcon;
        if (useFileAttributes)
        {
            flags |= ShgfiUseFileAttributes;
        }

        var info = new ShFileInfo();
        var result = SHGetFileInfo(
            path,
            attributes,
            ref info,
            (uint)Marshal.SizeOf<ShFileInfo>(),
            flags);

        if (result == IntPtr.Zero || info.hIcon == IntPtr.Zero)
        {
            return null;
        }

        try
        {
            return (Icon)Icon.FromHandle(info.hIcon).Clone();
        }
        finally
        {
            DestroyIcon(info.hIcon);
        }
    }
}
