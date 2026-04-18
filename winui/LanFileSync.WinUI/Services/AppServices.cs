using Microsoft.UI.Dispatching;
using LanFileSync.WinUI;

namespace LanFileSync.WinUI.Services;

public sealed class AppServices
{
    private bool _initialized;

    public AppServices()
    {
        State = new AppState();
        LogStore = new LogStore();
        SettingsService = new SettingsService();
        FolderPickerService = new FolderPickerService();
        BackendProcessService = new BackendProcessService(State, LogStore);
    }

    public BackendProcessService BackendProcessService { get; }
    public FolderPickerService FolderPickerService { get; }
    public LogStore LogStore { get; }
    public MainWindow? MainWindow { get; private set; }
    public AppState State { get; }
    public SettingsService SettingsService { get; }

    public void AttachWindow(MainWindow window)
    {
        MainWindow = window;
        LogStore.AttachDispatcher(DispatcherQueue.GetForCurrentThread());
    }

    public async Task InitializeAsync()
    {
        if (_initialized)
        {
            return;
        }

        var loadedSettings = await SettingsService.LoadAsync();
        State.Settings.Apply(loadedSettings);
        BackendProcessService.RefreshBackendStatus();
        LogStore.AddInfo("app", "WinUI shell initialized.");
        _initialized = true;
    }
}
