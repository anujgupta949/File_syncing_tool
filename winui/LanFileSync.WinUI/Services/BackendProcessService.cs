using System.Diagnostics;
using LanFileSync.WinUI.Models;

namespace LanFileSync.WinUI.Services;

public sealed class BackendProcessService
{
    private readonly LogStore _logStore;
    private readonly AppState _state;
    private Process? _serveProcess;

    public BackendProcessService(AppState state, LogStore logStore)
    {
        _state = state;
        _logStore = logStore;
    }

    public void RefreshBackendStatus()
    {
        var currentPath = _state.ResolvedBackendPath;
        var path = TryResolveBackendPath(_state.Settings);
        _state.ResolvedBackendPath = path ?? string.Empty;
        _state.IsBackendAvailable = path is not null;

        if (!string.Equals(currentPath, _state.ResolvedBackendPath, StringComparison.OrdinalIgnoreCase))
        {
            _logStore.AddInfo("backend", _state.IsBackendAvailable
                ? $"Using backend executable: {_state.ResolvedBackendPath}"
                : "Backend executable could not be found.");
        }
    }

    public async Task<int> RunSyncAsync(string host, string folder, string port)
    {
        if (_state.IsSyncing)
        {
            throw new InvalidOperationException("A sync is already running.");
        }

        var backendPath = EnsureBackendPath();
        _state.IsSyncing = true;
        _state.LastOperation = $"Syncing {folder} with {host}:{port}";

        try
        {
            using var process = CreateProcess(backendPath, "sync", host, folder, port);
            StartProcess(process, "sync");

            await process.WaitForExitAsync();
            _logStore.AddInfo("sync", $"Sync process finished with exit code {process.ExitCode}.");
            _state.LastOperation = process.ExitCode == 0
                ? $"Sync completed for {folder}"
                : $"Sync failed with exit code {process.ExitCode}";
            return process.ExitCode;
        }
        finally
        {
            _state.IsSyncing = false;
        }
    }

    public Task StartServeAsync(string bindAddress, string folder, string port)
    {
        if (_serveProcess is not null && !_serveProcess.HasExited)
        {
            throw new InvalidOperationException("The server is already running.");
        }

        var backendPath = EnsureBackendPath();
        var process = CreateProcess(backendPath, "serve", bindAddress, folder, port);
        process.EnableRaisingEvents = true;
        process.Exited += (_, _) =>
        {
            _logStore.AddInfo("serve", $"Server process exited with code {process.ExitCode}.");
            _state.IsServing = false;
            _state.LastOperation = process.ExitCode == 0
                ? "Server session completed."
                : $"Server stopped with exit code {process.ExitCode}.";
            _serveProcess?.Dispose();
            _serveProcess = null;
        };

        _serveProcess = process;
        StartProcess(process, "serve");

        _state.IsServing = true;
        _state.LastOperation = $"Serving {folder} on {bindAddress}:{port}";
        return Task.CompletedTask;
    }

    public Task StopServeAsync()
    {
        if (_serveProcess is null || _serveProcess.HasExited)
        {
            _state.IsServing = false;
            return Task.CompletedTask;
        }

        _logStore.AddInfo("serve", "Stopping server process.");
        _serveProcess.Kill(entireProcessTree: true);
        return Task.CompletedTask;
    }

    public string? TryResolveBackendPath(AppSettings settings)
    {
        IEnumerable<string> EnumerateCandidates()
        {
            if (!string.IsNullOrWhiteSpace(settings.BackendExecutablePath))
            {
                yield return settings.BackendExecutablePath;
            }

            var searchRoots = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
            {
                AppContext.BaseDirectory,
                Directory.GetCurrentDirectory()
            };

            foreach (var root in searchRoots.ToArray())
            {
                var current = root;
                for (var i = 0; i < 5 && !string.IsNullOrWhiteSpace(current); i++)
                {
                    searchRoots.Add(current);
                    current = Directory.GetParent(current)?.FullName ?? string.Empty;
                }
            }

            foreach (var root in searchRoots)
            {
                yield return Path.Combine(root, "lan_filesync.exe");
                yield return Path.Combine(root, "build", "Release", "lan_filesync.exe");
                yield return Path.Combine(root, "build", "lan_filesync.exe");
            }
        }

        foreach (var candidate in EnumerateCandidates())
        {
            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return null;
    }

    private static Process CreateProcess(string backendPath, params string[] arguments)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = backendPath,
            WorkingDirectory = Path.GetDirectoryName(backendPath) ?? AppContext.BaseDirectory,
            UseShellExecute = false,
            RedirectStandardError = true,
            RedirectStandardOutput = true,
            CreateNoWindow = true
        };

        foreach (var argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        return new Process
        {
            StartInfo = startInfo
        };
    }

    private string EnsureBackendPath()
    {
        RefreshBackendStatus();
        if (!_state.IsBackendAvailable || string.IsNullOrWhiteSpace(_state.ResolvedBackendPath))
        {
            throw new FileNotFoundException("Unable to find lan_filesync.exe. Open Settings and point the app at your backend executable.");
        }

        return _state.ResolvedBackendPath;
    }

    private void StartProcess(Process process, string source)
    {
        process.OutputDataReceived += (_, args) =>
        {
            if (!string.IsNullOrWhiteSpace(args.Data))
            {
                _logStore.AddInfo(source, args.Data);
            }
        };

        process.ErrorDataReceived += (_, args) =>
        {
            if (!string.IsNullOrWhiteSpace(args.Data))
            {
                _logStore.AddError(source, args.Data);
            }
        };

        if (!process.Start())
        {
            throw new InvalidOperationException("Failed to start the backend process.");
        }

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        _logStore.AddInfo(source, $"Started: {process.StartInfo.FileName} {string.Join(" ", process.StartInfo.ArgumentList)}");
    }
}
