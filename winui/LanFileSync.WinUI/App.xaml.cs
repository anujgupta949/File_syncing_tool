using Microsoft.UI.Xaml;
using LanFileSync.WinUI.Services;

namespace LanFileSync.WinUI;

public partial class App : Application
{
    private MainWindow? _window;

    public App()
    {
        InitializeComponent();
        Services = new AppServices();
    }

    public AppServices Services { get; }

    public static AppServices CurrentServices =>
        ((App)Current).Services;

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _window = new MainWindow(Services);
        Services.AttachWindow(_window);
        _window.Activate();
    }
}
