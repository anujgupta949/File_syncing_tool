#ifndef FILE_SEARCH_H
#define FILE_SEARCH_H

#include "snapshot.h"

bool InitSnapshot(struct snapshot *Snap, int InitialCapacity);
void FreeSnapshot(struct snapshot *Snap);
bool BuildSnapshotFromFolder(struct snapshot *Snap, const char *RootFolder);
void SortSnapshot(struct snapshot *Snap);
char *SnapshotToJson(const struct snapshot *Snap);
bool WriteSnapshotJsonFile(const struct snapshot *Snap, const char *OutputPath);

#endif
