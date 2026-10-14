/*
 * Host test for ps5/src/xbox_hdd.c: lists the saves on xbox_hdd_fixture.py's
 * disk image for the fixture to check.
 *   test_xbox_hdd <image.qcow2>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "util.h"
#include "xbox_hdd.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void app_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static TreeFile make_file(const char *path, uint32_t size, uint8_t seed)
{
    TreeFile f;
    memset(&f, 0, sizeof(f));
    snprintf(f.path, sizeof(f.path), "%s", path);
    f.size = size;
    f.data = malloc(size + 1);
    for (uint32_t i = 0; i < size; ++i)
        f.data[i] = (uint8_t)(i * 31 + seed);
    return f;
}

/* tree read back from the image == want */
static bool same_title(const char *image, const char *id, SaveTree *want)
{
    char err[96];
    XboxHdd *hdd = xhdd_open(image, err, sizeof(err));
    SaveTree got;
    if (!hdd || !xhdd_read_title(hdd, id, 64u << 20, &got, err, sizeof(err)))
    {
        fprintf(stderr, "read back %s: %s\n", id, err);
        xhdd_close(hdd);
        return false;
    }
    xhdd_close(hdd);
    tree_sort(want);
    bool ok = got.count == want->count;
    for (int i = 0; ok && i < got.count; ++i)
        ok = strcmp(got.files[i].path, want->files[i].path) == 0 &&
             got.files[i].size == want->files[i].size &&
             memcmp(got.files[i].data, want->files[i].data, got.files[i].size) == 0;
    if (!ok)
        fprintf(stderr, "%s read back differently\n", id);
    tree_free(&got);
    return ok;
}

/* A new title (UDATA grows a slot), then the old one replaced. */
static int write_tests(const char *image)
{
    char err[96];
    TreeFile a[] = {make_file("SaveMeta.xbx", 40, 1), make_file("B00000000001\\save.dat", 70000, 2),
                    make_file("B00000000001\\sub\\deep.bin", 9, 3), make_file("zero.bin", 0, 4)};
    SaveTree new_title = {a, 4};
    TreeFile b[] = {make_file("only.sav", 20000, 5)};
    SaveTree replacement = {b, 1};
    XboxHdd *hdd = xhdd_open_rw(image, err, sizeof(err));
    if (!hdd || !xhdd_write_title(hdd, "4D530099", &new_title, err, sizeof(err)) ||
        !xhdd_write_title(hdd, "4D53006E", &replacement, err, sizeof(err)))
    {
        fprintf(stderr, "write: %s\n", err);
        return 1;
    }
    xhdd_close(hdd);
    hdd = xhdd_open(image, err, sizeof(err));
    char ids[8][9];
    int n = hdd ? xhdd_titles(hdd, ids, 8) : 0;
    xhdd_close(hdd);
    /* the new one takes the deleted entry's slot, ahead of the old one */
    if (n != 2 || strcmp(ids[0], "4D530099") != 0 || strcmp(ids[1], "4D53006E") != 0)
    {
        fprintf(stderr, "titles after writing: %d\n", n);
        return 1;
    }
    if (!same_title(image, "4D530099", &new_title) || !same_title(image, "4D53006E", &replacement))
        return 1;
    /* again: the second write frees the first's clusters for reuse */
    hdd = xhdd_open_rw(image, err, sizeof(err));
    if (!hdd || !xhdd_write_title(hdd, "4D530099", &new_title, err, sizeof(err)))
    {
        fprintf(stderr, "rewrite: %s\n", err);
        return 1;
    }
    xhdd_close(hdd);
    if (!same_title(image, "4D530099", &new_title))
        return 1;
    for (int i = 0; i < 4; ++i)
        free(a[i].data);
    free(b[0].data);
    fprintf(stderr, "xbox_hdd writes ok\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    char err[96];
    XboxHdd *hdd = xhdd_open(argv[1], err, sizeof(err));
    if (!hdd)
    {
        fprintf(stderr, "open: %s\n", err);
        return 1;
    }
    char ids[8][9];
    int n = xhdd_titles(hdd, ids, 8);
    printf("titles");
    for (int i = 0; i < n; ++i)
        printf(" %s", ids[i]);
    printf("\n");
    SaveTree tree;
    if (n != 1 || !xhdd_read_title(hdd, ids[0], 1 << 20, &tree, err, sizeof(err)))
    {
        fprintf(stderr, "read: %s\n", n ? err : "no title");
        return 1;
    }
    for (int i = 0; i < tree.count; ++i)
    {
        char hex[65];
        util_sha256_hex(tree.files[i].data, tree.files[i].size, hex);
        printf("%s %u %s\n", tree.files[i].path, tree.files[i].size, hex);
    }
    char hash[65];
    tree_hash(&tree, hash, NULL);
    printf("hash %s\n", hash);
    tree_free(&tree);
    /* too big for the cap: refused, not truncated */
    if (xhdd_read_title(hdd, ids[0], 100, &tree, err, sizeof(err)))
    {
        fprintf(stderr, "a save over the cap was read\n");
        return 1;
    }
    xhdd_close(hdd);
    return write_tests(argv[1]);
}
