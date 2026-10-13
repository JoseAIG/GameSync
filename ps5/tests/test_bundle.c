/*
 * Host test: the C 3DSS bundle code against the server's Python
 * (server/app/services/bundle.py). Run through ps5/tests/run_host_tests.sh.
 *
 *   test_bundle make  <folder> <out.3dss>         prints the folder's hash
 *   test_bundle parse <in.3dss> <hash> <folder>   writes the files, checks the hash
 */
#include "bundle.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc >= 4 && strcmp(argv[1], "make") == 0)
    {
        SaveTree tree;
        if (!tree_read(argv[2], "nocopy", &tree))
            return 1;
        if (tree.count != 3)
        {
            fprintf(stderr, "expected 3 files without nocopy, got %d\n", tree.count);
            return 1;
        }
        /* strcmp order: "a-b" < "banner.bin" < "sub/a.dat" */
        if (strcmp(tree.files[0].path, "a-b") || strcmp(tree.files[2].path, "sub/a.dat"))
        {
            fprintf(stderr, "bad order\n");
            return 1;
        }
        size_t size;
        uint8_t *b = bundle_build("WII_RMCE", &tree, 1700000000u, &size);
        char hash[65];
        tree_hash(&tree, hash, NULL);
        tree_free(&tree);
        if (!b || !util_write_file_atomic(argv[3], b, size))
            return 1;
        free(b);
        printf("%s", hash);
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "parse") == 0)
    {
        size_t size;
        uint8_t *data = util_read_file(argv[2], &size);
        SaveTree tree;
        if (!data || !bundle_parse(data, size, &tree))
        {
            fprintf(stderr, "parse failed\n");
            return 1;
        }
        free(data);
        char hash[65];
        tree_hash(&tree, hash, NULL);
        if (strcmp(hash, argv[3]) != 0)
        {
            fprintf(stderr, "hash %s != server %s\n", hash, argv[3]);
            return 1;
        }
        if (!tree_write_replace(argv[4], &tree))
            return 1;
        /* again: the first copy becomes .bak */
        if (!tree_write_replace(argv[4], &tree))
            return 1;
        char check[600], bak[600];
        snprintf(check, sizeof(check), "%s/dir/inner.bin", argv[4]);
        snprintf(bak, sizeof(bak), "%s.bak/dir/inner.bin", argv[4]);
        if (!util_file_exists(check) || !util_file_exists(bak))
        {
            fprintf(stderr, "files not written\n");
            return 1;
        }
        tree_free(&tree);
        printf("bundle ok\n");
        return 0;
    }
    return 2;
}
