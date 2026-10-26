#ifndef CONFIG_FILE_H
#define CONFIG_FILE_H

// The config.txt format: key=value lines. Plain C on stdio streams so the
// load -> save -> load round trip is host-tested (tests/test_config_file.c);
// config.c opens the real file.

#include "common.h"
#include <stdio.h>

// Zero the struct and set every default
void config_set_defaults(AppConfig *config);

// Read key=value lines into `config` (call config_set_defaults first).
// Skips a UTF-8 BOM, comments (#), blank lines and unknown keys; numbers are
// clamped to their allowed range.
void config_parse_stream(AppConfig *config, FILE *f);

// Write every persisted key. Runtime-only fields (use_alt_url) and the
// console id (its own file) are not written. Returns false on a write error.
bool config_write_stream(const AppConfig *config, FILE *f);

#endif // CONFIG_FILE_H
