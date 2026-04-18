using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using LanFileSync.WinUI.Services;

namespace LanFileSync.WinUI.Views;

public sealed partial class SyncPage : Page
{
    private AppServices Services => App.CurrentServices;

    public SyncPage()
    {
        InitializeComponent();
        SyncSettingsRoot.DataContext = Services.State.Settings;
        LogsList.ItemsSource = Services.LogStore.Entries;
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }

    public AppState State => Services.State;

    private async void OnBrowseFolder(object sender, RoutedEventArgs e)
    {
        if (Services.MainWindow is null)
        {
            return;
        }

        var folder = await Services.FolderPickerService.PickFolderAsync(Services.MainWindow);
        if (!string.IsNullOrWhiteSpace(folder))
        {
            Services.State.Settings.LastSyncFolder = folder;
            UpdateCommandPreview();
        }
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        State.PropertyChanged += OnStateChanged;
        Services.State.Settings.PropertyChanged += OnSettingsChanged;
        UpdateUiState();
        UpdateCommandPreview();
    }

    private async void OnRunSync(object sender, RoutedEventArgs e)
    {
        var settings = Services.State.Settings;
        if (string.IsNullOrWhiteSpace(settings.LastServerHost))
        {
            await ShowMessageAsync("Missing server", "Enter the server host name or IP address.");
            return;
        }

        if (!Directory.Exists(settings.LastSyncFolder))
        {
            await ShowMessageAsync("Missing folder", "Choose a valid local folder before running sync.");
            return;
        }

        try
        {
            await Services.SettingsService.SaveAsync(settings);
            await Services.BackendProcessService.RunSyncAsync(
                settings.LastServerHost,
                settings.LastSyncFolder,
                settings.DefaultPort);
        }
        catch (Exception ex)
        {
            Services.LogStore.AddError("sync", ex.Message);
            await ShowMessageAsync("Sync failed", ex.Message);
        }

        UpdateUiState();
    }

    private void OnStateChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(AppState.IsSyncing) or nameof(AppState.SyncStateText))
        {
            UpdateUiState();
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        State.PropertyChanged -= OnStateChanged;
        Services.State.Settings.PropertyChanged -= OnSettingsChanged;
    }

    private void OnSettingsChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        UpdateCommandPreview();
    }

    private async Task ShowMessageAsync(string title, string message)
    {
        var dialog = new ContentDialog
        {
            Title = title,
            Content = message,
            CloseButtonText = "Close",
            XamlRoot = XamlRoot
        };

        await dialog.ShowAsync();
    }

    private void UpdateCommandPreview()
    {
        var settings = Services.State.Settings;
        SyncCommandPreviewText.Text =
            $"lan_filesync.exe sync {settings.LastServerHost} \"{settings.LastSyncFolder}\" {settings.DefaultPort}";
    }

    private void UpdateUiState()
    {
        SyncStatusText.Text = Services.State.SyncStateText;
        SyncProgressRing.IsActive = Services.State.IsSyncing;
        RunSyncButton.IsEnabled = !Services.State.IsSyncing;
    }
}
