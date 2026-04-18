#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#define uint64 uint64_t
#define uint8 uint8_t

struct FileInfo {
    char RelativeFilePath[MAX_PATH];
    uint64 FileSize;
    uint64 ModifiedTime;
    uint64 CreationTime;
    uint8 FullHash[32];
};

struct snapshot {
    struct FileInfo *File;
    int Count;
    int Capacity;
    uint64 SnapTime;
};

enum ChangeType {
    CHANGE_COPY_TO_REMOTE,
    CHANGE_COPY_TO_LOCAL,
    CHANGE_LOCAL_NEWER,
    CHANGE_REMOTE_NEWER,
    CHANGE_CONFLICT
};

struct FileChange {
    struct FileInfo *File;
    enum ChangeType Type;
    char ConflictReason[256];
};

struct ChangeSet {
    struct FileChange *Changes;
    int Count;
    int Capacity;
};

#endif
