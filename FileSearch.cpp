#include "FileSearch.h"

#include <Shlwapi.h>
#include <corecrt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <windows.h>

#include "cJSON.h"

#pragma comment(lib, "shlwapi.lib")

static bool EnsureCapacity(struct snapshot *Snap) {
    if (Snap->Count < Snap->Capacity) {
        return true;
    }

    int NewCapacity = (Snap->Capacity <= 0) ? 64 : (Snap->Capacity * 2);
    struct FileInfo *Temp = (struct FileInfo *)realloc(Snap->File, NewCapacity * sizeof(struct FileInfo));
    if (Temp == NULL) {
        return false;
    }

    Snap->File = Temp;
    Snap->Capacity = NewCapacity;
    return true;
}

static void AddFileToSnapshot(struct snapshot *Snap, const char *RootFolder, const char *FullFilePath, const WIN32_FIND_DATAA *FileTree) {
    if (!EnsureCapacity(Snap)) {
        return;
    }

    char RelativePath[MAX_PATH];
    bool RelativeOk = PathRelativePathToA(RelativePath, RootFolder, FILE_ATTRIBUTE_DIRECTORY, FullFilePath, FILE_ATTRIBUTE_NORMAL);

    LARGE_INTEGER FileSize;
    FileSize.LowPart = FileTree->nFileSizeLow;
    FileSize.HighPart = FileTree->nFileSizeHigh;

    ULARGE_INTEGER mTime;
    mTime.LowPart = FileTree->ftLastWriteTime.dwLowDateTime;
    mTime.HighPart = FileTree->ftLastWriteTime.dwHighDateTime;

    ULARGE_INTEGER cTime;
    cTime.LowPart = FileTree->ftCreationTime.dwLowDateTime;
    cTime.HighPart = FileTree->ftCreationTime.dwHighDateTime;

    struct FileInfo *Dst = &Snap->File[Snap->Count];

    if (RelativeOk) {
        strncpy_s(Dst->RelativeFilePath, RelativePath + 2, MAX_PATH - 1);
    } else {
        strncpy_s(Dst->RelativeFilePath, FullFilePath, MAX_PATH - 1);
    }

    Dst->RelativeFilePath[MAX_PATH - 1] = '\0';
    Dst->FileSize = (uint64)FileSize.QuadPart;
    Dst->ModifiedTime = (uint64)mTime.QuadPart;
    Dst->CreationTime = (uint64)cTime.QuadPart;
    memset(Dst->FullHash, 0, sizeof(Dst->FullHash));

    Snap->Count++;
}

static void ScanFolderRecursive(struct snapshot *Snap, const char *CurrentFolder, const char *RootFolder) {
    char SearchPath[MAX_PATH];
    snprintf(SearchPath, MAX_PATH, "%s\\*", CurrentFolder);

    WIN32_FIND_DATAA FileTree;
    HANDLE FileHandle = FindFirstFileA(SearchPath, &FileTree);

    if (FileHandle == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (strcmp(FileTree.cFileName, ".") == 0 || strcmp(FileTree.cFileName, "..") == 0) {
            continue;
        }

        char FullFilePath[MAX_PATH];
        snprintf(FullFilePath, MAX_PATH, "%s\\%s", CurrentFolder, FileTree.cFileName);

        if (FileTree.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ScanFolderRecursive(Snap, FullFilePath, RootFolder);
        } else {
            AddFileToSnapshot(Snap, RootFolder, FullFilePath, &FileTree);
        }
    } while (FindNextFileA(FileHandle, &FileTree) != 0);

    FindClose(FileHandle);
}

static int SnapshotFileCmp(const void *A, const void *B) {
    const struct FileInfo *Left = (const struct FileInfo *)A;
    const struct FileInfo *Right = (const struct FileInfo *)B;
    return strcmp(Left->RelativeFilePath, Right->RelativeFilePath);
}

bool InitSnapshot(struct snapshot *Snap, int InitialCapacity) {
    if (Snap == NULL) {
        return false;
    }

    if (InitialCapacity <= 0) {
        InitialCapacity = 64;
    }

    Snap->File = (struct FileInfo *)malloc((size_t)InitialCapacity * sizeof(struct FileInfo));
    if (Snap->File == NULL) {
        Snap->Count = 0;
        Snap->Capacity = 0;
        Snap->SnapTime = 0;
        return false;
    }

    Snap->Count = 0;
    Snap->Capacity = InitialCapacity;
    Snap->SnapTime = (uint64)GetTickCount64();
    return true;
}

void FreeSnapshot(struct snapshot *Snap) {
    if (Snap == NULL) {
        return;
    }

    if (Snap->File != NULL) {
        free(Snap->File);
    }

    Snap->File = NULL;
    Snap->Count = 0;
    Snap->Capacity = 0;
    Snap->SnapTime = 0;
}

bool BuildSnapshotFromFolder(struct snapshot *Snap, const char *RootFolder) {
    if (Snap == NULL || RootFolder == NULL) {
        return false;
    }

    DWORD Attr = GetFileAttributesA(RootFolder);
    if (Attr == INVALID_FILE_ATTRIBUTES || !(Attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    Snap->Count = 0;
    ScanFolderRecursive(Snap, RootFolder, RootFolder);
    SortSnapshot(Snap);
    return true;
}

void SortSnapshot(struct snapshot *Snap) {
    if (Snap == NULL || Snap->File == NULL || Snap->Count <= 1) {
        return;
    }

    qsort(Snap->File, (size_t)Snap->Count, sizeof(struct FileInfo), SnapshotFileCmp);
}

char *SnapshotToJson(const struct snapshot *Snap) {
    if (Snap == NULL) {
        return NULL;
    }

    cJSON *Array = cJSON_CreateArray();
    if (Array == NULL) {
        return NULL;
    }

    char NumberBuffer[32];

    for (int i = 0; i < Snap->Count; i++) {
        cJSON *Obj = cJSON_CreateObject();
        if (Obj == NULL) {
            cJSON_Delete(Array);
            return NULL;
        }

        cJSON_AddStringToObject(Obj, "path", Snap->File[i].RelativeFilePath);

        _ui64toa_s(Snap->File[i].FileSize, NumberBuffer, sizeof(NumberBuffer), 10);
        cJSON_AddStringToObject(Obj, "size", NumberBuffer);

        _ui64toa_s(Snap->File[i].ModifiedTime, NumberBuffer, sizeof(NumberBuffer), 10);
        cJSON_AddStringToObject(Obj, "modifiedTime", NumberBuffer);

        _ui64toa_s(Snap->File[i].CreationTime, NumberBuffer, sizeof(NumberBuffer), 10);
        cJSON_AddStringToObject(Obj, "creationTime", NumberBuffer);

        cJSON_AddItemToArray(Array, Obj);
    }

    char *JsonString = cJSON_PrintUnformatted(Array);
    cJSON_Delete(Array);
    return JsonString;
}

bool WriteSnapshotJsonFile(const struct snapshot *Snap, const char *OutputPath) {
    if (Snap == NULL || OutputPath == NULL) {
        return false;
    }

    char *Json = SnapshotToJson(Snap);
    if (Json == NULL) {
        return false;
    }

    FILE *Fp = NULL;
    errno_t Error = fopen_s(&Fp, OutputPath, "wb");
    if (Error != 0 || Fp == NULL) {
        free(Json);
        return false;
    }

    fputs(Json, Fp);
    fclose(Fp);
    free(Json);
    return true;
}
