namespace LanFileSync.WinUI.Models;

public sealed class LogEntry
{
    public DateTimeOffset Timestamp { get; init; }
    public string Source { get; init; } = string.Empty;
    public string Message { get; init; } = string.Empty;
    public string Level { get; init; } = string.Empty;

    public string DisplayTime => Timestamp.ToLocalTime().ToString("HH:mm:ss");
}
