using Microsoft.UI.Xaml;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace LanFileSync.WinUI.Services;

public sealed class FolderPickerService
{
    public async Task<string?> PickExecutableAsync(Window window)
    {
        var picker = new FileOpenPicker
        {
            SuggestedStartLocation = PickerLocationId.Desktop
        };
        picker.FileTypeFilter.Add(".exe");

        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(window));

        var file = await picker.PickSingleFileAsync();
        return file?.Path;
    }

    public async Task<string?> PickFolderAsync(Window window)
    {
        var picker = new FolderPicker
        {
            SuggestedStartLocation = PickerLocationId.DocumentsLibrary
        };
        picker.FileTypeFilter.Add("*");

        InitializeWithWindow.Initialize(picker, WindowNative.GetWindowHandle(window));

        var folder = await picker.PickSingleFolderAsync();
        return folder?.Path;
    }
}
