using LanFileSync.WinUI.Models;
using LanFileSync.WinUI.ViewModels;

namespace LanFileSync.WinUI.Services;

public sealed class AppState : ObservableObject
{
    private string _currentPageTitle = "Dashboard";
    private bool _isBackendAvailable;
    private bool _isServing;
    private bool _isSyncing;
    private string _lastOperation = "Ready to connect the WinUI app to the existing LAN backend.";
    private string _resolvedBackendPath = string.Empty;

    public AppSettings Settings { get; } = new();

    public string CurrentPageTitle
    {
        get => _currentPageTitle;
        set => SetProperty(ref _currentPageTitle, value);
    }

    public bool IsBackendAvailable
    {
        get => _isBackendAvailable;
        set
        {
            if (SetProperty(ref _isBackendAvailable, value))
            {
                OnPropertyChanged(nameof(BackendAvailabilityText));
                OnPropertyChanged(nameof(BackendSummaryText));
            }
        }
    }

    public bool IsServing
    {
        get => _isServing;
        set
        {
            if (SetProperty(ref _isServing, value))
            {
                OnPropertyChanged(nameof(ServiceStateText));
            }
        }
    }

    public bool IsSyncing
    {
        get => _isSyncing;
        set
        {
            if (SetProperty(ref _isSyncing, value))
            {
                OnPropertyChanged(nameof(SyncStateText));
            }
        }
    }

    public string LastOperation
    {
        get => _lastOperation;
        set => SetProperty(ref _lastOperation, value);
    }

    public string ResolvedBackendPath
    {
        get => _resolvedBackendPath;
        set
        {
            if (SetProperty(ref _resolvedBackendPath, value))
            {
                OnPropertyChanged(nameof(BackendSummaryText));
            }
        }
    }

    public string BackendAvailabilityText =>
        IsBackendAvailable ? "Backend detected" : "Backend missing";

    public string BackendSummaryText =>
        IsBackendAvailable
            ? ResolvedBackendPath
            : "Point Settings at lan_filesync.exe, or build the native backend first.";

    public string ServiceStateText =>
        IsServing ? "Server is running and waiting for a client." : "Server is idle.";

    public string SyncStateText =>
        IsSyncing ? "Sync is currently running." : "No sync is running.";
}
