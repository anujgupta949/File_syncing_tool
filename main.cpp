#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "FileSearch.h"
#include "server.h"

static void PrintUsage() {
    printf("Usage:\n");
    printf("  lan_filesync snapshot <folder> [output_json]\n");
    printf("  lan_filesync serve <bind_address_or_*> <folder> [port]\n");
    printf("  lan_filesync sync <server_ip_or_host> <folder> [port]\n");
}

static bool BuildSnapshotOrFail(const char *Folder, struct snapshot *Snap) {
    if (!InitSnapshot(Snap, 256)) {
        printf("Failed to initialize snapshot\n");
        return false;
    }

    if (!BuildSnapshotFromFolder(Snap, Folder)) {
        printf("Failed to scan folder: %s\n", Folder);
        FreeSnapshot(Snap);
        return false;
    }

    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const char *Mode = argv[1];

    if (strcmp(Mode, "snapshot") == 0) {
        if (argc < 3) {
            PrintUsage();
            return 1;
        }

        const char *Folder = argv[2];
        const char *Output = (argc >= 4) ? argv[3] : "Snapshot.json";

        struct snapshot Snap;
        if (!BuildSnapshotOrFail(Folder, &Snap)) {
            return 1;
        }

        bool Ok = WriteSnapshotJsonFile(&Snap, Output);
        if (Ok) {
            printf("Snapshot saved to %s with %d files\n", Output, Snap.Count);
        } else {
            printf("Failed to write snapshot file\n");
        }
        FreeSnapshot(&Snap);
        return Ok ? 0 : 1;
    }

    if (strcmp(Mode, "serve") == 0) {
        if (argc < 4) {
            PrintUsage();
            return 1;
        }

        const char *BindAddress = argv[2];
        const char *Folder = argv[3];
        const char *Port = (argc >= 5) ? argv[4] : "27015";
        const char *BindParam = (strcmp(BindAddress, "*") == 0) ? NULL : BindAddress;
        bool Ok = RunSyncServerSession(BindParam, Port, Folder);
        if (!Ok) {
            printf("Server session failed\n");
            return 1;
        }
        printf("Server session completed\n");
        return 0;
    }

    if (strcmp(Mode, "sync") == 0) {
        if (argc < 4) {
            PrintUsage();
            return 1;
        }

        const char *ServerHost = argv[2];
        const char *Folder = argv[3];
        const char *Port = (argc >= 5) ? argv[4] : "27015";
        bool Ok = RunSyncClientSession(ServerHost, Port, Folder);
        if (!Ok) {
            printf("Sync failed\n");
            return 1;
        }
        printf("Sync completed\n");
        return 0;
    }

    PrintUsage();
    return 1;
}
