using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using LanFileSync.WinUI.Services;

namespace LanFileSync.WinUI.Views;

public sealed partial class ServePage : Page
{
    private AppServices Services => App.CurrentServices;

    public ServePage()
    {
        InitializeComponent();
        SettingsRoot.DataContext = Services.State.Settings;
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
            Services.State.Settings.LastServerFolder = folder;
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

    private async void OnStartServer(object sender, RoutedEventArgs e)
    {
        var settings = Services.State.Settings;
        if (!Directory.Exists(settings.LastServerFolder))
        {
            await ShowMessageAsync("Missing folder", "Choose a valid folder before starting the server.");
            return;
        }

        try
        {
            await Services.SettingsService.SaveAsync(settings);
            await Services.BackendProcessService.StartServeAsync(
                settings.LastBindAddress,
                settings.LastServerFolder,
                settings.DefaultPort);
        }
        catch (Exception ex)
        {
            Services.LogStore.AddError("serve", ex.Message);
            await ShowMessageAsync("Unable to start server", ex.Message);
        }

        UpdateUiState();
    }

    private async void OnStopServer(object sender, RoutedEventArgs e)
    {
        await Services.BackendProcessService.StopServeAsync();
        UpdateUiState();
    }

    private void OnStateChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(AppState.IsServing) or nameof(AppState.ServiceStateText) or nameof(AppState.LastOperation))
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
        CommandPreviewText.Text =
            $"lan_filesync.exe serve {settings.LastBindAddress} \"{settings.LastServerFolder}\" {settings.DefaultPort}";
    }

    private void UpdateUiState()
    {
        ServeStatusText.Text = Services.State.ServiceStateText;
        ServeProgressRing.IsActive = Services.State.IsServing;
        StartServerButton.IsEnabled = !Services.State.IsServing;
        StopServerButton.IsEnabled = Services.State.IsServing;
    }
}
