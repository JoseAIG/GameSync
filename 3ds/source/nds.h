#ifndef NDS_H
#define NDS_H

#include "common.h"
#include "archive.h"
#include "folder_scan.h"

// What counts as a DS ROM (".nds") and where its save lives ("<stem>.sav"
// beside it, else in "saves/"): nds_scan() and the folder picker's preview
// share these rules.
extern const FolderRules nds_folder_rules;
#define NDS_ITEM_LABEL ".nds ROMs"

// Scan an SD card directory for NDS ROMs with .sav files.
// Fills titles starting at offset, up to max_titles.
// Returns number of NDS titles found.
int nds_scan(const char *nds_dir, TitleInfo *titles, int offset, int max_titles);

// Read NDS save file into ArchiveFile format (single file "save.dat").
// Returns number of files (1) on success, ARCHIVE_ERR_OPEN if the .sav does
// not exist, ARCHIVE_ERR_READ if it exists but can't be read.
// Caller must call archive_free_files() when done.
int nds_read_save(const char *sav_path, ArchiveFile *files, int max_files);

// Write save data back to NDS .sav file.
// Expects file data from a parsed bundle (typically one file "save.dat").
// Returns true on success.
bool nds_write_save(const char *sav_path, const ArchiveFile *files, int file_count);

// Read save from a physical NDS cartridge via SPI.
// Detects save type automatically. Returns 1 on success, ARCHIVE_ERR_OPEN if
// no save chip is detected, ARCHIVE_ERR_READ if reading it fails.
// Caller must call archive_free_files() when done.
int nds_cart_read_save(ArchiveFile *files, int max_files);

// Write save to a physical NDS cartridge via SPI.
// Detects save type automatically. Returns true on success.
bool nds_cart_write_save(const ArchiveFile *files, int file_count);

#endif // NDS_H
