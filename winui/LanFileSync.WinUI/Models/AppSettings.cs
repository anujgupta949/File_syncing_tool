using LanFileSync.WinUI.ViewModels;

namespace LanFileSync.WinUI.Models;

public sealed class AppSettings : ObservableObject
{
    private string _backendExecutablePath = string.Empty;
    private string _defaultPort = "27015";
    private string _lastBindAddress = "*";
    private string _lastServerFolder = string.Empty;
    private string _lastServerHost = "127.0.0.1";
    private string _lastSyncFolder = string.Empty;

    public string BackendExecutablePath
    {
        get => _backendExecutablePath;
        set => SetProperty(ref _backendExecutablePath, value?.Trim() ?? string.Empty);
    }

    public string DefaultPort
    {
        get => _defaultPort;
        set => SetProperty(ref _defaultPort, string.IsNullOrWhiteSpace(value) ? "27015" : value.Trim());
    }

    public string LastBindAddress
    {
        get => _lastBindAddress;
        set => SetProperty(ref _lastBindAddress, string.IsNullOrWhiteSpace(value) ? "*" : value.Trim());
    }

    public string LastServerFolder
    {
        get => _lastServerFolder;
        set => SetProperty(ref _lastServerFolder, value?.Trim() ?? string.Empty);
    }

    public string LastServerHost
    {
        get => _lastServerHost;
        set => SetProperty(ref _lastServerHost, string.IsNullOrWhiteSpace(value) ? "127.0.0.1" : value.Trim());
    }

    public string LastSyncFolder
    {
        get => _lastSyncFolder;
        set => SetProperty(ref _lastSyncFolder, value?.Trim() ?? string.Empty);
    }

    public void Apply(AppSettings other)
    {
        BackendExecutablePath = other.BackendExecutablePath;
        DefaultPort = other.DefaultPort;
        LastBindAddress = other.LastBindAddress;
        LastServerFolder = other.LastServerFolder;
        LastServerHost = other.LastServerHost;
        LastSyncFolder = other.LastSyncFolder;
    }
}
