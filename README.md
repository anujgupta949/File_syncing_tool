# LAN File Sync (Snapshot Exchange)

This project is a Windows LAN file-sync foundation that currently does:

- Recursive folder snapshot (path, size, modified time, creation time)
- Snapshot JSON export
- TCP snapshot exchange across LAN (client <-> server)
- Sync action planning (copy to local/remote, newer side, conflict)
- Whole-file transfer for new or incompatible files
- Block-based delta sync for large files when the destination already has an older copy

## Build (CMake + MSVC)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Binary path (typical):

- `build/Release/lan_filesync.exe`

## Usage

```text
lan_filesync snapshot <folder> [output_json]
lan_filesync serve <bind_address_or_*> <folder> [port]
lan_filesync sync <server_ip_or_host> <folder> [port]
```

Examples:

```powershell
# Create snapshot JSON
.\lan_filesync.exe snapshot "C:\data\client" Snapshot.json

# Start server on all interfaces (port 27015)
.\lan_filesync.exe serve * "C:\data\server" 27015

# From another machine in LAN
.\lan_filesync.exe sync 192.168.1.100 "C:\data\client" 27015
```

## Notes

- Delta sync is offset-based block sync, not a full rsync-style rolling matcher.
- It helps most when a large file is modified in place and the destination already has an older version.
- Small files, missing destination files, or cases where the delta would be larger than the full file still fall back to normal whole-file transfer.
