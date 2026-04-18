using System.Text.Json;
using LanFileSync.WinUI.Models;

namespace LanFileSync.WinUI.Services;

public sealed class SettingsService
{
    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        WriteIndented = true
    };

    public async Task<AppSettings> LoadAsync()
    {
        var path = GetSettingsPath();
        if (!File.Exists(path))
        {
            return new AppSettings();
        }

        await using var stream = File.OpenRead(path);
        var settings = await JsonSerializer.DeserializeAsync<AppSettings>(stream, _jsonOptions);
        return settings ?? new AppSettings();
    }

    public async Task SaveAsync(AppSettings settings)
    {
        var path = GetSettingsPath();
        var directory = Path.GetDirectoryName(path);

        if (!string.IsNullOrWhiteSpace(directory))
        {
            Directory.CreateDirectory(directory);
        }

        await using var stream = File.Create(path);
        await JsonSerializer.SerializeAsync(stream, settings, _jsonOptions);
    }

    public string GetSettingsPath()
    {
        var appData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return Path.Combine(appData, "LanFileSync.WinUI", "settings.json");
    }
}
