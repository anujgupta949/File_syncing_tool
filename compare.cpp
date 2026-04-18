#include "compare.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cJSON.h"

static bool EnsureChangeCapacity(struct ChangeSet *Cs) {
    if (Cs->Count < Cs->Capacity) {
        return true;
    }

    int NewCapacity = (Cs->Capacity <= 0) ? 32 : (Cs->Capacity * 2);
    struct FileChange *Temp = (struct FileChange *)realloc(Cs->Changes, (size_t)NewCapacity * sizeof(struct FileChange));
    if (Temp == NULL) {
        return false;
    }

    Cs->Changes = Temp;
    Cs->Capacity = NewCapacity;
    return true;
}

static uint64 ParseU64Field(cJSON *Obj, const char *FieldName) {
    cJSON *Field = cJSON_GetObjectItemCaseSensitive(Obj, FieldName);
    if (Field == NULL) {
        return 0;
    }

    if (cJSON_IsString(Field) && Field->valuestring != NULL) {
        return _strtoui64(Field->valuestring, NULL, 10);
    }

    if (cJSON_IsNumber(Field)) {
        return (uint64)Field->valuedouble;
    }

    return 0;
}

static int SnapshotPathCmp(const char *A, const char *B) {
    return strcmp(A, B);
}

bool ParseSnapshotJson(struct snapshot *Snap, const char *JsonString) {
    if (Snap == NULL || Snap->File == NULL || JsonString == NULL) {
        return false;
    }

    cJSON *Root = cJSON_Parse(JsonString);
    if (Root == NULL || !cJSON_IsArray(Root)) {
        cJSON_Delete(Root);
        return false;
    }

    int ArraySize = cJSON_GetArraySize(Root);
    if (ArraySize > Snap->Capacity) {
        struct FileInfo *Temp = (struct FileInfo *)realloc(Snap->File, (size_t)ArraySize * sizeof(struct FileInfo));
        if (Temp == NULL) {
            cJSON_Delete(Root);
            return false;
        }

        Snap->File = Temp;
        Snap->Capacity = ArraySize;
    }

    Snap->Count = 0;

    for (int i = 0; i < ArraySize; i++) {
        cJSON *Item = cJSON_GetArrayItem(Root, i);
        if (!cJSON_IsObject(Item)) {
            continue;
        }

        cJSON *Path = cJSON_GetObjectItemCaseSensitive(Item, "path");
        if (!cJSON_IsString(Path) || Path->valuestring == NULL) {
            continue;
        }

        struct FileInfo *Dst = &Snap->File[Snap->Count];
        strncpy_s(Dst->RelativeFilePath, Path->valuestring, MAX_PATH - 1);
        Dst->RelativeFilePath[MAX_PATH - 1] = '\0';

        Dst->FileSize = ParseU64Field(Item, "size");
        Dst->ModifiedTime = ParseU64Field(Item, "modifiedTime");
        Dst->CreationTime = ParseU64Field(Item, "creationTime");
        memset(Dst->FullHash, 0, sizeof(Dst->FullHash));

        Snap->Count++;
    }

    cJSON_Delete(Root);
    return true;
}

struct ChangeSet* CreateChangeSet() {
    struct ChangeSet *Cs = (struct ChangeSet *)malloc(sizeof(struct ChangeSet));
    if (Cs == NULL) {
        return NULL;
    }

    Cs->Count = 0;
    Cs->Capacity = 32;
    Cs->Changes = (struct FileChange *)malloc((size_t)Cs->Capacity * sizeof(struct FileChange));

    if (Cs->Changes == NULL) {
        free(Cs);
        return NULL;
    }

    return Cs;
}

void FreeChangeSet(struct ChangeSet *Cs) {
    if (Cs == NULL) {
        return;
    }

    free(Cs->Changes);
    Cs->Changes = NULL;
    Cs->Count = 0;
    Cs->Capacity = 0;
    free(Cs);
}

void AddChange(struct ChangeSet *Cs, struct FileInfo *File, enum ChangeType Type, const char *Reason) {
    if (Cs == NULL || File == NULL) {
        return;
    }

    if (!EnsureChangeCapacity(Cs)) {
        return;
    }

    struct FileChange *Dst = &Cs->Changes[Cs->Count];
    Dst->File = File;
    Dst->Type = Type;

    if (Reason != NULL) {
        strncpy_s(Dst->ConflictReason, Reason, sizeof(Dst->ConflictReason) - 1);
        Dst->ConflictReason[sizeof(Dst->ConflictReason) - 1] = '\0';
    } else {
        Dst->ConflictReason[0] = '\0';
    }

    Cs->Count++;
}

const char *ChangeTypeToString(enum ChangeType Type) {
    switch (Type) {
        case CHANGE_COPY_TO_REMOTE:
            return "COPY_TO_REMOTE";
        case CHANGE_COPY_TO_LOCAL:
            return "COPY_TO_LOCAL";
        case CHANGE_LOCAL_NEWER:
            return "LOCAL_NEWER";
        case CHANGE_REMOTE_NEWER:
            return "REMOTE_NEWER";
        case CHANGE_CONFLICT:
            return "CONFLICT";
        default:
            return "UNKNOWN";
    }
}

void CompareSnapshots(const struct snapshot *Local, const struct snapshot *Remote, struct ChangeSet *Changes) {
    if (Local == NULL || Remote == NULL || Changes == NULL) {
        return;
    }

    int I = 0;
    int J = 0;

    while (I < Local->Count || J < Remote->Count) {
        if (I >= Local->Count) {
            AddChange(Changes, &Remote->File[J], CHANGE_COPY_TO_LOCAL, "Missing locally");
            J++;
            continue;
        }

        if (J >= Remote->Count) {
            AddChange(Changes, &Local->File[I], CHANGE_COPY_TO_REMOTE, "Missing remotely");
            I++;
            continue;
        }

        int Cmp = SnapshotPathCmp(Local->File[I].RelativeFilePath, Remote->File[J].RelativeFilePath);

        if (Cmp < 0) {
            AddChange(Changes, &Local->File[I], CHANGE_COPY_TO_REMOTE, "Only exists locally");
            I++;
            continue;
        }

        if (Cmp > 0) {
            AddChange(Changes, &Remote->File[J], CHANGE_COPY_TO_LOCAL, "Only exists remotely");
            J++;
            continue;
        }

        const struct FileInfo *L = &Local->File[I];
        const struct FileInfo *R = &Remote->File[J];

        if (L->FileSize == R->FileSize && L->ModifiedTime == R->ModifiedTime) {
            I++;
            J++;
            continue;
        }

        if (L->ModifiedTime > R->ModifiedTime) {
            AddChange(Changes, &Local->File[I], CHANGE_LOCAL_NEWER, "Same path changed; local appears newer");
        } else if (L->ModifiedTime < R->ModifiedTime) {
            AddChange(Changes, &Remote->File[J], CHANGE_REMOTE_NEWER, "Same path changed; remote appears newer");
        } else {
            AddChange(Changes, &Local->File[I], CHANGE_CONFLICT, "Modified time equal, but metadata differs");
        }

        I++;
        J++;
    }
}
