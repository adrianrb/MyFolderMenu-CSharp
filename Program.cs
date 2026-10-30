using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Security;
using System.Text.Json;
using System.Windows.Forms;
using Microsoft.Win32;

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

internal enum AppTheme
{
    Sistema,
    Claro,
    Escuro
}

internal sealed class AppSettings
{
    public bool HideFileExtensions { get; set; } = true;
    public AppTheme Theme { get; set; } = AppTheme.Sistema;
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
    private AppSettings _settings;

    public TrayApplicationContext()
    {
        _folderIcon = LoadApplicationIcon();
        _folderImage = _folderIcon.ToBitmap();
        _store.EnsureCreated();
        _folders = _store.Load();
        _settings = _store.LoadSettings();

        _menu = new ContextMenuStrip { ShowItemToolTips = true };
        _menu.Opening += (_, _) =>
        {
            _folders = _store.Load();
            _settings = _store.LoadSettings();
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
        ApplyTheme();
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
        ApplyMenuTheme(manage.DropDown);
        ApplyMenuTheme(remove.DropDown);

        _menu.Items.Add(new ToolStripMenuItem("Configurações...", null, (_, _) => ShowSettings()));
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
            ApplyMenuTheme(item.DropDown);
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
        var label = _settings.HideFileExtensions
            ? Path.GetFileNameWithoutExtension(path)
            : Path.GetFileName(path);

        if (string.IsNullOrEmpty(label))
        {
            label = Path.GetFileName(path);
        }

        var item = new ToolStripMenuItem(Escape(label))
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

        _store.Save(_folders, _settings);
        _folders = _store.Load();
    }

    private void RemoveFolder(string folder)
    {
        _folders.RemoveAll(item => string.Equals(item, folder, StringComparison.OrdinalIgnoreCase));
        _store.Save(_folders, _settings);
        _folders = _store.Load();
    }

    private void ShowSettings()
    {
        using var settingsForm = new SettingsForm(
            _settings,
            _folders,
            _store,
            ThemeManager.IsDark(_settings.Theme));

        if (settingsForm.ShowDialog() != DialogResult.OK)
        {
            return;
        }

        _folders = settingsForm.Folders;
        _settings = settingsForm.Settings;
        _store.Save(_folders, _settings);
        ApplyTheme();
    }

    private void ShowAbout()
    {
        using var about = new AboutForm(ThemeManager.IsDark(_settings.Theme));
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

    private static Icon LoadApplicationIcon()
    {
        try
        {
            var icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            if (icon is not null)
            {
                return icon;
            }
        }
        catch (Exception ex) when (ex is ArgumentException or IOException or System.ComponentModel.Win32Exception)
        {
        }

        return ShellIcons.GetFolderIcon();
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

    private void ApplyTheme()
    {
        ApplyMenuTheme(_menu);
    }

    private void ApplyMenuTheme(ToolStripDropDown menu)
    {
        ThemeManager.ApplyMenu(menu, ThemeManager.IsDark(_settings.Theme));
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
        public AboutForm(bool dark)
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
            ThemeManager.ApplyForm(this, dark);
        }
    }
}

internal sealed class SettingsForm : Form
{
    private readonly CheckBox _startWithWindows;
    private readonly CheckBox _hideFileExtensions;
    private readonly RadioButton _lightTheme;
    private readonly RadioButton _darkTheme;
    private readonly RadioButton _systemTheme;
    private readonly FolderStore _store;

    public SettingsForm(AppSettings settings, IEnumerable<string> folders, FolderStore store, bool dark)
    {
        Settings = new AppSettings
        {
            HideFileExtensions = settings.HideFileExtensions,
            Theme = settings.Theme
        };
        Folders = new List<string>(folders);
        _store = store;

        Text = "Configurações";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        StartPosition = FormStartPosition.CenterScreen;
        MaximizeBox = false;
        MinimizeBox = false;
        ShowInTaskbar = false;
        ClientSize = new Size(380, 340);
        AutoScaleMode = AutoScaleMode.Dpi;

        _startWithWindows = new CheckBox
        {
            Text = "Iniciar com o Windows",
            AutoSize = true,
            Checked = StartupManager.IsEnabled(),
            Location = new Point(24, 24)
        };

        _hideFileExtensions = new CheckBox
        {
            Text = "Não exibir a extensão do arquivo",
            AutoSize = true,
            Location = new Point(24, 58)
        };

        _lightTheme = new RadioButton
        {
            Text = "Claro",
            AutoSize = true,
            Location = new Point(20, 28)
        };

        _darkTheme = new RadioButton
        {
            Text = "Escuro",
            AutoSize = true,
            Location = new Point(20, 56)
        };

        _systemTheme = new RadioButton
        {
            Text = "Sistema",
            AutoSize = true,
            Location = new Point(20, 84)
        };

        ApplySettingsToControls(settings);

        var themeGroup = new GroupBox
        {
            Text = "Tema",
            Location = new Point(24, 98),
            Size = new Size(332, 118)
        };
        themeGroup.Controls.AddRange(new Control[] { _lightTheme, _darkTheme, _systemTheme });

        var export = new Button
        {
            Text = "Exportar configurações",
            Size = new Size(164, 32),
            Location = new Point(24, 232)
        };
        export.Click += (_, _) => ExportSettings();

        var import = new Button
        {
            Text = "Importar configurações",
            Size = new Size(164, 32),
            Location = new Point(192, 232)
        };
        import.Click += (_, _) => ImportSettings();

        var ok = new Button
        {
            Text = "OK",
            Size = new Size(90, 32),
            Location = new Point(ClientSize.Width - 214, ClientSize.Height - 56)
        };
        ok.Click += (_, _) => SaveAndClose();

        var cancel = new Button
        {
            Text = "Cancelar",
            DialogResult = DialogResult.Cancel,
            Size = new Size(90, 32),
            Location = new Point(ClientSize.Width - 114, ClientSize.Height - 56)
        };

        AcceptButton = ok;
        CancelButton = cancel;
        Controls.AddRange(new Control[]
        {
            _startWithWindows,
            _hideFileExtensions,
            themeGroup,
            export,
            import,
            ok,
            cancel
        });

        ThemeManager.ApplyForm(this, dark);
    }

    public AppSettings Settings { get; private set; }

    public List<string> Folders { get; private set; }

    private void ApplySettingsToControls(AppSettings settings)
    {
        _hideFileExtensions.Checked = settings.HideFileExtensions;
        _lightTheme.Checked = settings.Theme == AppTheme.Claro;
        _darkTheme.Checked = settings.Theme == AppTheme.Escuro;
        _systemTheme.Checked = settings.Theme == AppTheme.Sistema;
    }

    private AppSettings ReadSettingsFromControls()
    {
        return new AppSettings
        {
            HideFileExtensions = _hideFileExtensions.Checked,
            Theme = _darkTheme.Checked
                ? AppTheme.Escuro
                : _lightTheme.Checked
                    ? AppTheme.Claro
                    : AppTheme.Sistema
        };
    }

    private void ExportSettings()
    {
        using var dialog = new SaveFileDialog
        {
            Title = "Exportar configurações",
            Filter = "Configuração do MyFolderMenu (*.ini)|*.ini|Todos os arquivos (*.*)|*.*",
            FileName = "MyFolderMenu.ini",
            DefaultExt = "ini",
            AddExtension = true,
            OverwritePrompt = true,
            RestoreDirectory = true
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
        {
            return;
        }

        if (_store.TryExport(dialog.FileName, Folders, ReadSettingsFromControls()))
        {
            MessageBox.Show(
                $"Configurações exportadas para:\n{dialog.FileName}",
                "MyFolderMenu",
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
        }
        else
        {
            MessageBox.Show(
                $"Não foi possível exportar as configurações para:\n{dialog.FileName}",
                "MyFolderMenu",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }
    }

    private void ImportSettings()
    {
        using var dialog = new OpenFileDialog
        {
            Title = "Importar configurações",
            Filter = "Configuração do MyFolderMenu (*.ini)|*.ini|Todos os arquivos (*.*)|*.*",
            DefaultExt = "ini",
            CheckFileExists = true,
            RestoreDirectory = true
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
        {
            return;
        }

        if (!_store.TryImport(dialog.FileName, out var folders, out var settings))
        {
            MessageBox.Show(
                $"Arquivo de configuração inválido ou inacessível:\n{dialog.FileName}",
                "MyFolderMenu",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return;
        }

        Folders = folders;
        ApplySettingsToControls(settings);
        ThemeManager.ApplyForm(this, ThemeManager.IsDark(settings.Theme));
        MessageBox.Show(
            $"Configurações importadas de:\n{dialog.FileName}\n\nClique em OK para aplicar.",
            "MyFolderMenu",
            MessageBoxButtons.OK,
            MessageBoxIcon.Information);
    }

    private void SaveAndClose()
    {
        try
        {
            StartupManager.SetEnabled(_startWithWindows.Checked);
        }
        catch (Exception ex) when (ex is UnauthorizedAccessException or SecurityException or IOException or InvalidOperationException)
        {
            MessageBox.Show(
                $"Não foi possível alterar a inicialização com o Windows:\n\n{ex.Message}",
                "MyFolderMenu",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return;
        }

        Settings = ReadSettingsFromControls();
        DialogResult = DialogResult.OK;
        Close();
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

        Save(folders, new AppSettings());
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

    public AppSettings LoadSettings()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                return ParseSettings(File.ReadAllLines(FilePath));
            }

            return new AppSettings();
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return new AppSettings();
        }
    }

    public void Save(IEnumerable<string> folders, AppSettings settings)
    {
        try
        {
            Directory.CreateDirectory(DirectoryPath);
            WriteIni(FilePath, folders, settings);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
    }

    public bool TryExport(string path, IEnumerable<string> folders, AppSettings settings)
    {
        try
        {
            WriteIni(path, folders, settings);
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            return false;
        }
    }

    public bool TryImport(string path, out List<string> folders, out AppSettings settings)
    {
        folders = new List<string>();
        settings = new AppSettings();

        try
        {
            var lines = File.ReadAllLines(path);
            if (!HasFoldersSection(lines))
            {
                return false;
            }

            folders = ParseIni(lines);
            settings = ParseSettings(lines);
            return true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            return false;
        }
    }

    private static void WriteIni(string path, IEnumerable<string> folders, AppSettings settings)
    {
        var normalized = Normalize(folders);
        var lines = new List<string> { "[Folders]" };
        for (var index = 0; index < normalized.Count; index++)
        {
            lines.Add($"Folder{index + 1}={normalized[index]}");
        }

        lines.Add(string.Empty);
        lines.Add("[Settings]");
        lines.Add($"HideFileExtensions={(settings.HideFileExtensions ? 1 : 0)}");
        lines.Add($"Theme={ThemeManager.ToIniValue(settings.Theme)}");

        File.WriteAllLines(path, lines);
    }

    private static bool HasFoldersSection(IEnumerable<string> lines)
    {
        foreach (var raw in lines)
        {
            var line = raw.Trim();
            if (!line.StartsWith('[') || !line.EndsWith(']'))
            {
                continue;
            }

            var section = line[1..^1].Trim();
            if (section.Equals("Folders", StringComparison.OrdinalIgnoreCase)
                || section.Equals("Pastas", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        return false;
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

    private static AppSettings ParseSettings(IEnumerable<string> lines)
    {
        var settings = new AppSettings();
        var inSettingsSection = false;

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
                inSettingsSection = section.Equals("Settings", StringComparison.OrdinalIgnoreCase);
                continue;
            }

            if (!inSettingsSection)
            {
                continue;
            }

            var separator = line.IndexOf('=');
            if (separator <= 0)
            {
                continue;
            }

            var key = line[..separator].Trim();
            var value = line[(separator + 1)..].Trim();

            if (key.Equals("HideFileExtensions", StringComparison.OrdinalIgnoreCase))
            {
                settings.HideFileExtensions = value.ToLowerInvariant() switch
                {
                    "1" or "true" or "sim" or "yes" => true,
                    "0" or "false" or "nao" or "não" or "no" => false,
                    _ => settings.HideFileExtensions
                };
            }
            else if (key.Equals("Theme", StringComparison.OrdinalIgnoreCase))
            {
                settings.Theme = ThemeManager.FromIniValue(value);
            }
        }

        return settings;
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

internal static class ThemeManager
{
    private const int DwmwaUseImmersiveDarkMode = 20;

    private static readonly Color DarkWindow = Color.FromArgb(32, 32, 32);
    private static readonly Color DarkMenu = Color.FromArgb(37, 37, 38);
    private static readonly Color DarkMenuHover = Color.FromArgb(62, 62, 66);
    private static readonly Color DarkBorder = Color.FromArgb(72, 72, 76);
    private static readonly Color DarkButton = Color.FromArgb(48, 48, 52);

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    public static bool IsDark(AppTheme theme)
    {
        if (theme == AppTheme.Escuro)
        {
            return true;
        }

        if (theme == AppTheme.Claro)
        {
            return false;
        }

        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(
                @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            return key?.GetValue("AppsUseLightTheme") is int value && value == 0;
        }
        catch (Exception ex) when (ex is SecurityException or UnauthorizedAccessException or IOException)
        {
            return false;
        }
    }

    public static string ToIniValue(AppTheme theme)
    {
        return theme switch
        {
            AppTheme.Claro => "Claro",
            AppTheme.Escuro => "Escuro",
            _ => "Sistema"
        };
    }

    public static AppTheme FromIniValue(string? value)
    {
        return value?.Trim().ToLowerInvariant() switch
        {
            "claro" or "light" => AppTheme.Claro,
            "escuro" or "dark" => AppTheme.Escuro,
            _ => AppTheme.Sistema
        };
    }

    public static void ApplyMenu(ToolStripDropDown menu, bool dark)
    {
        menu.Renderer = new ToolStripProfessionalRenderer(new MenuColorTable(dark))
        {
            RoundedEdges = false
        };
        menu.BackColor = dark ? DarkMenu : SystemColors.Window;
        menu.ForeColor = dark ? Color.White : SystemColors.ControlText;

        if (menu is ToolStripDropDownMenu dropDownMenu)
        {
            dropDownMenu.ShowImageMargin = true;
        }
    }

    public static void ApplyForm(Form form, bool dark)
    {
        var background = dark ? DarkWindow : SystemColors.Control;
        form.BackColor = background;
        form.ForeColor = dark ? Color.White : SystemColors.ControlText;
        ApplyControlTheme(form.Controls, dark, background);
        ApplyTitleBar(form, dark);
    }

    private static void ApplyControlTheme(Control.ControlCollection controls, bool dark, Color background)
    {
        foreach (Control control in controls)
        {
            control.ForeColor = dark ? Color.White : SystemColors.ControlText;
            control.BackColor = background;

            if (control is Button button)
            {
                button.FlatStyle = FlatStyle.Flat;
                button.BackColor = dark ? DarkButton : SystemColors.Control;
                button.FlatAppearance.BorderColor = dark ? DarkBorder : SystemColors.ControlDark;
            }

            if (control.HasChildren)
            {
                ApplyControlTheme(control.Controls, dark, background);
            }
        }
    }

    private static void ApplyTitleBar(Form form, bool dark)
    {
        if (!OperatingSystem.IsWindowsVersionAtLeast(10, 0, 17763))
        {
            return;
        }

        try
        {
            var value = dark ? 1 : 0;
            DwmSetWindowAttribute(form.Handle, DwmwaUseImmersiveDarkMode, ref value, sizeof(int));
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
        }
    }

    private sealed class MenuColorTable : ProfessionalColorTable
    {
        private readonly bool _dark;

        public MenuColorTable(bool dark)
        {
            _dark = dark;
            UseSystemColors = false;
        }

        private Color Background => _dark ? DarkMenu : SystemColors.Window;
        private Color Selected => _dark ? DarkMenuHover : SystemColors.Highlight;

        public override Color ToolStripDropDownBackground => Background;
        public override Color ImageMarginGradientBegin => Background;
        public override Color ImageMarginGradientMiddle => Background;
        public override Color ImageMarginGradientEnd => Background;
        public override Color MenuBorder => _dark ? DarkBorder : SystemColors.ControlDark;
        public override Color MenuItemBorder => Selected;
        public override Color MenuItemSelected => Selected;
        public override Color MenuItemSelectedGradientBegin => Selected;
        public override Color MenuItemSelectedGradientEnd => Selected;
        public override Color MenuItemPressedGradientBegin => Selected;
        public override Color MenuItemPressedGradientMiddle => Selected;
        public override Color MenuItemPressedGradientEnd => Selected;
        public override Color SeparatorDark => _dark ? Color.FromArgb(78, 78, 82) : SystemColors.ControlDark;
        public override Color SeparatorLight => _dark ? Color.FromArgb(50, 50, 54) : SystemColors.ControlLight;
        public override Color CheckBackground => Background;
        public override Color CheckSelectedBackground => Selected;
    }
}

internal static class StartupManager
{
    private const string RunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "MyFolderMenu";

    public static bool IsEnabled()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath);
            return key?.GetValue(ValueName) is string value && !string.IsNullOrWhiteSpace(value);
        }
        catch (Exception ex) when (ex is SecurityException or UnauthorizedAccessException or IOException)
        {
            return false;
        }
    }

    public static void SetEnabled(bool enabled)
    {
        using var key = Registry.CurrentUser.CreateSubKey(RunKeyPath, true)
            ?? throw new InvalidOperationException("Não foi possível acessar a inicialização do Windows.");

        if (enabled)
        {
            key.SetValue(ValueName, $"\"{Application.ExecutablePath}\"", RegistryValueKind.String);
        }
        else
        {
            key.DeleteValue(ValueName, false);
        }
    }
}
