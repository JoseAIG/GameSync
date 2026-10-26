#include "config_file.h"

void config_set_defaults(AppConfig *config) {
    memset(config, 0, sizeof(AppConfig));
    config->utc_offset_minutes = 0;
    config->local_backups = 3;
    config->tls_verify = true;
    config->update_check = true;
    config->reduce_motion = false;
}

// Trim leading/trailing whitespace in-place
static void trim(char *str) {
    char *start = str;
    while (*start == ' ' || *start == '\t') start++;
    if (start != str) memmove(str, start, strlen(start) + 1);

    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
                       str[len - 1] == '\r' || str[len - 1] == '\n'))
        str[--len] = '\0';
}

// Skip UTF-8 BOM if present at start of file
static void skip_bom(char *str) {
    if ((unsigned char)str[0] == 0xEF &&
        (unsigned char)str[1] == 0xBB &&
        (unsigned char)str[2] == 0xBF) {
        memmove(str, str + 3, strlen(str + 3) + 1);
    }
}

static void copy_value(char *dst, size_t size, const char *val) {
    strncpy(dst, val, size - 1);
    dst[size - 1] = '\0';
}

void config_parse_stream(AppConfig *config, FILE *f) {
    char line[512];
    bool first_line = true;
    while (fgets(line, sizeof(line), f)) {
        if (first_line) {
            skip_bom(line);
            first_line = false;
        }

        // Skip comments and blank lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);

        if (strcmp(key, "server_url") == 0) {
            copy_value(config->server_url, sizeof(config->server_url), val);
        } else if (strcmp(key, "server_url_alt") == 0) {
            copy_value(config->server_url_alt, sizeof(config->server_url_alt), val);
        } else if (strcmp(key, "api_key") == 0) {
            copy_value(config->api_key, sizeof(config->api_key), val);
        } else if (strcmp(key, "nds_dir") == 0) {
            copy_value(config->nds_dir, sizeof(config->nds_dir), val);
        } else if (strcmp(key, "utc_offset_minutes") == 0) {
            // The 3DS clock is local time without a zone; this is the offset
            // of that clock from UTC, used to send real Unix timestamps.
            int offset = atoi(val);
            if (offset < -14 * 60) offset = -14 * 60;
            if (offset > 14 * 60) offset = 14 * 60;
            config->utc_offset_minutes = offset;
        } else if (strcmp(key, "local_backups") == 0) {
            // Copies of a save kept on the SD card before it is overwritten
            int keep = atoi(val);
            if (keep < 0) keep = 0;
            if (keep > 50) keep = 50;
            config->local_backups = keep;
        } else if (strcmp(key, "tls_verify") == 0) {
            // 0 = accept any HTTPS certificate (old behaviour)
            config->tls_verify = atoi(val) != 0;
        } else if (strcmp(key, "update_check") == 0) {
            // 0 = never contact the server for app updates
            config->update_check = atoi(val) != 0;
        } else if (strcmp(key, "reduce_motion") == 0) {
            // 1 = no sliding or springing: fades and snaps instead
            config->reduce_motion = atoi(val) != 0;
        }
    }
}

bool config_write_stream(const AppConfig *config, FILE *f) {
    fprintf(f, "# 3DS Save Sync Configuration\n");
    fprintf(f, "server_url=%s\n", config->server_url);
    if (config->server_url_alt[0])
        fprintf(f, "server_url_alt=%s\n", config->server_url_alt);
    fprintf(f, "api_key=%s\n", config->api_key);
    if (config->nds_dir[0])
        fprintf(f, "nds_dir=%s\n", config->nds_dir);
    fprintf(f, "utc_offset_minutes=%d\n", config->utc_offset_minutes);
    fprintf(f, "local_backups=%d\n", config->local_backups);
    fprintf(f, "tls_verify=%d\n", config->tls_verify ? 1 : 0);
    fprintf(f, "update_check=%d\n", config->update_check ? 1 : 0);
    fprintf(f, "reduce_motion=%d\n", config->reduce_motion ? 1 : 0);
    return !ferror(f);
}
