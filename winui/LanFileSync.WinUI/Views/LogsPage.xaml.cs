using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanFileSync.WinUI.Views;

public sealed partial class LogsPage : Page
{
    public LogsPage()
    {
        InitializeComponent();
        LogsList.ItemsSource = App.CurrentServices.LogStore.Entries;
    }

    private void OnClearLogs(object sender, RoutedEventArgs e)
    {
        App.CurrentServices.LogStore.Clear();
    }

    private void OnRefreshBackend(object sender, RoutedEventArgs e)
    {
        App.CurrentServices.BackendProcessService.RefreshBackendStatus();
    }
}
