using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using LanFileSync.WinUI.Services;
using LanFileSync.WinUI.Views;

namespace LanFileSync.WinUI;

public sealed partial class MainWindow : Window
{
    private readonly AppServices _services;

    public MainWindow() : this(App.CurrentServices)
    {
    }

    public MainWindow(AppServices services)
    {
        _services = services;
        InitializeComponent();
        RootLayout.DataContext = _services.State;

        AppNavigation.Loaded += OnLoaded;
        Closed += OnClosed;
    }

    public void NavigateTo(string tag)
    {
        var normalizedTag = tag.ToLowerInvariant();
        var item = AppNavigation.MenuItems
            .OfType<NavigationViewItem>()
            .FirstOrDefault(candidate => string.Equals(candidate.Tag?.ToString(), normalizedTag, StringComparison.OrdinalIgnoreCase));

        if (item is not null)
        {
            AppNavigation.SelectedItem = item;
        }

        ContentFrame.Navigate(GetPageType(normalizedTag));
        _services.State.CurrentPageTitle = GetPageTitle(normalizedTag);
    }

    private static Type GetPageType(string tag) =>
        tag switch
        {
            "serve" => typeof(ServePage),
            "sync" => typeof(SyncPage),
            "logs" => typeof(LogsPage),
            "settings" => typeof(SettingsPage),
            _ => typeof(DashboardPage)
        };

    private static string GetPageTitle(string tag) =>
        tag switch
        {
            "serve" => "Serve a Folder",
            "sync" => "Sync with a Server",
            "logs" => "Live Logs",
            "settings" => "Settings",
            _ => "Dashboard"
        };

    private void OnClosed(object sender, WindowEventArgs args)
    {
        _services.State.PropertyChanged -= OnStatePropertyChanged;
        _ = _services.BackendProcessService.StopServeAsync();
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        AppNavigation.Loaded -= OnLoaded;
        _services.State.PropertyChanged += OnStatePropertyChanged;
        await _services.InitializeAsync();
        NavigateTo("dashboard");
        Title = $"LAN File Sync - {_services.State.CurrentPageTitle}";
    }

    private void OnNavigationSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        var tag = args.SelectedItemContainer?.Tag?.ToString() ?? "dashboard";
        NavigateTo(tag);
    }

    private void OnStatePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(AppState.CurrentPageTitle))
        {
            Title = $"LAN File Sync - {_services.State.CurrentPageTitle}";
        }
    }
}
