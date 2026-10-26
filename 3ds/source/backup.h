#ifndef BACKUP_H
#define BACKUP_H

#include "common.h"
#include "archive.h"

// Copy a save to sdmc:/3ds/3dssync/backups/<TITLEID>/<YYYYMMDD-HHMMSS>/
// and delete older copies so only the newest `keep` remain.
// Does nothing (and succeeds) if keep <= 0 or there are no files.
// On success the backup folder is copied to out_dir (may be NULL).
// Returns false if the copy could not be written completely.
bool backup_save(const char *title_id_hex, const ArchiveFile *files, int file_count,
                 int keep, char *out_dir, int out_size);

#endif // BACKUP_H
