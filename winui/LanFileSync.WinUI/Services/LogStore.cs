using System.Collections.ObjectModel;
using Microsoft.UI.Dispatching;
using LanFileSync.WinUI.Models;

namespace LanFileSync.WinUI.Services;

public sealed class LogStore
{
    private DispatcherQueue? _dispatcher;

    public ObservableCollection<LogEntry> Entries { get; } = new();

    public void AttachDispatcher(DispatcherQueue dispatcher)
    {
        _dispatcher = dispatcher;
    }

    public void AddInfo(string source, string message) => Add("INFO", source, message);

    public void AddError(string source, string message) => Add("ERROR", source, message);

    public void Add(string level, string source, string message)
    {
        if (string.IsNullOrWhiteSpace(message))
        {
            return;
        }

        void AddCore()
        {
            Entries.Add(new LogEntry
            {
                Timestamp = DateTimeOffset.Now,
                Source = source,
                Message = message,
                Level = level
            });

            if (Entries.Count > 500)
            {
                Entries.RemoveAt(0);
            }
        }

        if (_dispatcher is not null && !_dispatcher.HasThreadAccess)
        {
            _dispatcher.TryEnqueue(AddCore);
            return;
        }

        AddCore();
    }

    public void Clear()
    {
        void ClearCore() => Entries.Clear();

        if (_dispatcher is not null && !_dispatcher.HasThreadAccess)
        {
            _dispatcher.TryEnqueue(ClearCore);
            return;
        }

        ClearCore();
    }
}
