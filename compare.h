#ifndef COMPARE_H
#define COMPARE_H

#include "snapshot.h"

bool ParseSnapshotJson(struct snapshot *Snap, const char *JsonString);
void CompareSnapshots(const struct snapshot *Local, const struct snapshot *Remote, struct ChangeSet *Changes);
struct ChangeSet* CreateChangeSet();
void FreeChangeSet(struct ChangeSet *cs);
void AddChange(struct ChangeSet *cs, struct FileInfo *file, enum ChangeType type, const char *reason);
const char *ChangeTypeToString(enum ChangeType Type);

#endif
