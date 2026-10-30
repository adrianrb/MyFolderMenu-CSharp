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

        if (HasSubdirectories(path))
        {
            item.DropDownItems.Add(new ToolStripMenuItem("Carregando...") { Enabled = false });
            item.DropDown.ShowItemToolTips = true;
            item.DropDownOpening += (_, _) => PopulateSubmenu(item);
        }
        else
        {
            item.Click += (_, _) => OpenFolder(path);
        }

        return item;
    }

    private void PopulateSubmenu(FolderMenuItem item)
    {
        if (item.Populated)
        {
            return;
        }

        item.Populated = true;
        ClearItems(item.DropDownItems);
        item.DropDownItems.Add(new ToolStripMenuItem("Abrir esta pasta", _folderImage, (_, _) => OpenFolder(item.FolderPath)));
        item.DropDownItems.Add(new ToolStripSeparator());

        try
        {
            var directories = Directory.EnumerateDirectories(item.FolderPath)
                .Where(IsVisibleDirectory)
                .OrderBy(GetFolderLabel, StringComparer.CurrentCultureIgnoreCase)
                .ToList();

            if (directories.Count == 0)
            {
                item.DropDownItems.Add(new ToolStripMenuItem("(sem subpastas)") { Enabled = false });
                return;
            }

            foreach (var directory in directories)
            {
                item.DropDownItems.Add(CreateFolderItem(directory));
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

    private void OpenFirstFolder()
    {
        var folder = _folders.FirstOrDefault(Directory.Exists);
        if (folder is not null)
        {
            OpenFolder(folder);
        }
    }

    private static void OpenFolder(string path)
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

    private static bool HasSubdirectories(string path)
    {
        try
        {
            return Directory.EnumerateDirectories(path).Any(IsVisibleDirectory);
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or IOException)
        {
            return false;
        }
    }

    private static bool IsVisibleDirectory(string path)
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
}

internal sealed class FolderStore
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        WriteIndented = true
    };

    public string DirectoryPath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "MyFolderMenu");

    public string FilePath => Path.Combine(DirectoryPath, "folders.json");

    public void EnsureCreated()
    {
        if (!File.Exists(FilePath))
        {
            Save(GetDefaultFolders());
        }
    }

    public List<string> Load()
    {
        try
        {
            if (!File.Exists(FilePath))
            {
                return GetDefaultFolders();
            }

            var config = JsonSerializer.Deserialize<MenuConfig>(File.ReadAllText(FilePath), SerializerOptions);
            return Normalize(config?.Folders);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return GetDefaultFolders();
        }
    }

    public void Save(IEnumerable<string> folders)
    {
        try
        {
            Directory.CreateDirectory(DirectoryPath);
            var config = new MenuConfig { Folders = Normalize(folders) };
            File.WriteAllText(FilePath, JsonSerializer.Serialize(config, SerializerOptions));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
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
        var info = new ShFileInfo();
        var result = SHGetFileInfo(
            "folder",
            FileAttributeDirectory,
            ref info,
            (uint)Marshal.SizeOf<ShFileInfo>(),
            ShgfiIcon | ShgfiSmallIcon | ShgfiUseFileAttributes);

        if (result == IntPtr.Zero || info.hIcon == IntPtr.Zero)
        {
            return (Icon)SystemIcons.Application.Clone();
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
