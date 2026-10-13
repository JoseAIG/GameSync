/*
 * Host test: the C PS2 card code against cards built by the server's Python
 * (server/app/services/ps2mc.py). Run through ps5/tests/run_host_tests.sh.
 *
 *   test_ps2mc <server_card.ps2> <expected_p2fd_sha_list.txt> <out_card.ps2>
 *
 * The list holds "<dir> <sha256 of python build_p2fd>" lines. The test parses
 * the server card, checks every folder's P2FD hash, swaps in a new folder,
 * and writes the result for the Python side to verify.
 */
#include "ps2mc.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c, msg)                                   \
    do                                                  \
    {                                                   \
        if (!(c))                                       \
        {                                               \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
            failures++;                                 \
        }                                               \
    } while (0)

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s card expected out\n", argv[0]);
        return 2;
    }
    char error[128] = "";
    Ps2CardFile file;
    CHECK(ps2mc_load_file(argv[1], &file, error, sizeof(error)), error);
    CHECK(file.ecc, "server card should be ECC form");

    FILE *f = fopen(argv[2], "r");
    CHECK(f != NULL, "expected list");
    char dir[64], want[80];
    int seen = 0;
    while (f && fscanf(f, "%63s %79s", dir, want) == 2)
    {
        int i = ps2mc_find_dir(&file.card, dir);
        CHECK(i >= 0, dir);
        if (i < 0)
            continue;
        size_t size;
        uint8_t *p2fd = ps2mc_build_p2fd(&file.card.dirs[i], &size);
        char got[65];
        util_sha256_hex(p2fd, size, got);
        CHECK(strcmp(got, want) == 0, "p2fd hash differs from python");
        Ps2Dir back;
        CHECK(ps2mc_parse_p2fd(p2fd, size, &back), "p2fd roundtrip");
        CHECK(back.file_count == file.card.dirs[i].file_count, "p2fd file count");
        ps2mc_free_dir(&back);
        free(p2fd);
        seen++;
    }
    if (f)
        fclose(f);
    CHECK(seen == file.card.count, "folder count");

    char serial[16];
    CHECK(ps2mc_serial_from_dirname("BASLUS-20312", serial, sizeof(serial)) &&
              strcmp(serial, "SLUS20312") == 0,
          "serial BASLUS-20312");
    CHECK(ps2mc_serial_from_dirname("BESLES-123.45GAME", serial, sizeof(serial)) &&
              strcmp(serial, "SLES12345") == 0,
          "serial with dot");
    CHECK(!ps2mc_serial_from_dirname("BADATA-SYSTEM", serial, sizeof(serial)), "system dir");

    /* replace the first folder, add a new one */
    Ps2Dir add;
    memset(&add, 0, sizeof(add));
    snprintf(add.name, sizeof(add.name), "BASLUS-99999");
    add.files = calloc(2, sizeof(Ps2File));
    add.file_count = 2;
    snprintf(add.files[0].name, 33, "icon.sys");
    add.files[0].size = 964;
    add.files[0].data = calloc(1, 964);
    memcpy(add.files[0].data, "PS2D", 4);
    snprintf(add.files[1].name, 33, "BASLUS-99999");
    add.files[1].size = 3000;
    add.files[1].data = malloc(3000);
    for (int i = 0; i < 3000; ++i)
        add.files[1].data[i] = (uint8_t)(i * 7);
    CHECK(ps2mc_put_dir(&file.card, &add), "put new");

    Ps2Dir repl;
    memset(&repl, 0, sizeof(repl));
    snprintf(repl.name, sizeof(repl.name), "%s", file.card.dirs[0].name);
    repl.files = calloc(1, sizeof(Ps2File));
    repl.file_count = 1;
    snprintf(repl.files[0].name, 33, "replaced.bin");
    repl.files[0].size = 2049;
    repl.files[0].data = malloc(2049);
    memset(repl.files[0].data, 0xAB, 2049);
    int before = file.card.count;
    CHECK(ps2mc_put_dir(&file.card, &repl), "put replace");
    CHECK(file.card.count == before, "replace keeps count");

    CHECK(ps2mc_save_file(argv[3], &file, 1700000000u, error, sizeof(error)), error);
    ps2mc_free(&file.card);

    Ps2CardFile again;
    CHECK(ps2mc_load_file(argv[3], &again, error, sizeof(error)), error);
    CHECK(again.card.count == before, "reloaded folder count");
    int r = ps2mc_find_dir(&again.card, "BASLUS-99999");
    CHECK(r >= 0 && again.card.dirs[r].files[1].size == 3000 &&
              again.card.dirs[r].files[1].data[2999] == (uint8_t)(2999 * 7),
          "new folder data");
    ps2mc_free(&again.card);

    printf("%s (%d failures)\n", failures ? "FAILED" : "ps2mc ok", failures);
    return failures ? 1 : 0;
}
