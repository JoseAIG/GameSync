/*
 * Host test: unzip.c against a ZIP_STORED archive written by Python's
 * zipfile, as the server writes ?extract=cue sets. Run through
 * ps5/tests/run_host_tests.sh.
 *
 *   test_unzip <archive.zip> <out folder>
 */
#include "unzip.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    char error[128] = "";
    if (!unzip_stored(argv[1], argv[2], error, sizeof(error)))
    {
        fprintf(stderr, "unzip failed: %s\n", error);
        return 1;
    }
    char path[600];
    snprintf(path, sizeof(path), "%s/Game (USA).cue", argv[2]);
    size_t size;
    char *cue = (char *)util_read_file(path, &size);
    snprintf(path, sizeof(path), "%s/Game (USA) (Track 1).bin", argv[2]);
    int64_t bin = util_file_size(path);
    if (!cue || !strstr(cue, "Track 1") || bin != 2352 * 300)
    {
        fprintf(stderr, "extracted files wrong\n");
        return 1;
    }
    free(cue);
    /* a traversal attempt must be refused */
    snprintf(path, sizeof(path), "%s/../escape.txt", argv[2]);
    if (util_file_exists(path))
    {
        fprintf(stderr, "wrote outside the folder\n");
        return 1;
    }
    printf("unzip ok\n");
    return 0;
}
