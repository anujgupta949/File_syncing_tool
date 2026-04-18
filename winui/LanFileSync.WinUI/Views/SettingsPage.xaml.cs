using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanFileSync.WinUI.Views;

public sealed partial class SettingsPage : Page
{
    public SettingsPage()
    {
        InitializeComponent();
        SettingsRoot.DataContext = App.CurrentServices.State.Settings;
        Loaded += OnLoaded;
    }

    private async void OnAutoDetect(object sender, RoutedEventArgs e)
    {
        var services = App.CurrentServices;
        var detectedPath = services.BackendProcessService.TryResolveBackendPath(services.State.Settings);
        if (!string.IsNullOrWhiteSpace(detectedPath))
        {
            services.State.Settings.BackendExecutablePath = detectedPath;
            services.BackendProcessService.RefreshBackendStatus();
            UpdateResolvedPath();
            return;
        }

        await ShowMessageAsync("Backend not found", "I couldn't find lan_filesync.exe automatically. Build it first or browse to it manually.");
    }

    private async void OnBrowseExecutable(object sender, RoutedEventArgs e)
    {
        var services = App.CurrentServices;
        if (services.MainWindow is null)
        {
            return;
        }

        var path = await services.FolderPickerService.PickExecutableAsync(services.MainWindow);
        if (!string.IsNullOrWhiteSpace(path))
        {
            services.State.Settings.BackendExecutablePath = path;
            services.BackendProcessService.RefreshBackendStatus();
            UpdateResolvedPath();
        }
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        SettingsPathText.Text = App.CurrentServices.SettingsService.GetSettingsPath();
        UpdateResolvedPath();
    }

    private async void OnSaveSettings(object sender, RoutedEventArgs e)
    {
        var services = App.CurrentServices;
        await services.SettingsService.SaveAsync(services.State.Settings);
        services.BackendProcessService.RefreshBackendStatus();
        UpdateResolvedPath();
        await ShowMessageAsync("Saved", "Settings were saved successfully.");
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

    private void UpdateResolvedPath()
    {
        var state = App.CurrentServices.State;
        ResolvedPathText.Text = state.IsBackendAvailable
            ? state.ResolvedBackendPath
            : "No backend executable is currently resolved.";
    }
}
