using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace LanFileSync.WinUI.Views;

public sealed partial class DashboardPage : Page
{
    public DashboardPage()
    {
        InitializeComponent();
        DataContext = App.CurrentServices.State;
    }

    private void OnOpenServe(object sender, RoutedEventArgs e)
    {
        App.CurrentServices.MainWindow?.NavigateTo("serve");
    }

    private void OnOpenSettings(object sender, RoutedEventArgs e)
    {
        App.CurrentServices.MainWindow?.NavigateTo("settings");
    }

    private void OnOpenSync(object sender, RoutedEventArgs e)
    {
        App.CurrentServices.MainWindow?.NavigateTo("sync");
    }
}
