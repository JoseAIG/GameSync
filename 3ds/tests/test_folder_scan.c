// Host test for folder_scan.c: path helpers, the NDS rules, listing,
// incremental counting (against real temporary folders) and the LRU cache
#include "folder_scan.h"
#include "test_util.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const nds_exts[] = { ".nds", NULL };
static const FolderRules nds = { nds_exts, ".sav", "saves" };

static char root[FOLDER_PATH_MAX];

static void touch(const char *rel) {
    char p[FOLDER_PATH_MAX * 2];
    snprintf(p, sizeof(p), "%s/%s", root, rel);
    FILE *f = fopen(p, "w");
    if (f) fclose(f);
}

static void mkd(const char *rel) {
    char p[FOLDER_PATH_MAX * 2];
    snprintf(p, sizeof(p), "%s/%s", root, rel);
    mkdir(p, 0777);
}

static void at(const char *rel, char *out, size_t size) {
    folder_path_join(root, rel, out, size);
}

static void test_paths(void) {
    char out[FOLDER_PATH_MAX];
    folder_path_join("sdmc:/", "roms", out, sizeof(out));
    CHECK_STR(out, "sdmc:/roms");
    folder_path_join("sdmc:/roms", "nds", out, sizeof(out));
    CHECK_STR(out, "sdmc:/roms/nds");
    folder_path_join("sdmc:/roms/", "/nds", out, sizeof(out));
    CHECK_STR(out, "sdmc:/roms/nds");
    folder_path_join("sdmc:", "x.sav", out, sizeof(out));
    CHECK_STR(out, "sdmc:/x.sav");

    folder_path_normalize("/roms/nds/", out, sizeof(out));
    CHECK_STR(out, "sdmc:/roms/nds");
    folder_path_normalize("  roms//nds  ", out, sizeof(out));
    CHECK_STR(out, "sdmc:/roms/nds");
    folder_path_normalize("sdmc:", out, sizeof(out));
    CHECK_STR(out, "sdmc:/");
    folder_path_normalize("sdmc://", out, sizeof(out));
    CHECK_STR(out, "sdmc:/");
    folder_path_normalize("sdmc:roms", out, sizeof(out));
    CHECK_STR(out, "sdmc:/roms");
    folder_path_normalize("", out, sizeof(out));
    CHECK_STR(out, "sdmc:/");

    CHECK(folder_path_is_root("sdmc:/"));
    CHECK(!folder_path_is_root("sdmc:/roms"));
    CHECK(folder_path_parent("sdmc:/roms/nds", out, sizeof(out)));
    CHECK_STR(out, "sdmc:/roms");
    CHECK(folder_path_parent("sdmc:/roms", out, sizeof(out)));
    CHECK_STR(out, "sdmc:/");
    CHECK(!folder_path_parent("sdmc:/", out, sizeof(out)));
    CHECK_STR(folder_path_name("sdmc:/roms/nds"), "nds");
    CHECK_STR(folder_path_name("sdmc:/"), "sdmc:/");
}

static void test_rules(void) {
    char out[64];
    CHECK(folder_name_has_ext("Mario Kart DS.nds", nds.exts));
    CHECK(folder_name_has_ext("ZELDA.NDS", nds.exts));
    CHECK(!folder_name_has_ext("game.nds.txt", nds.exts));
    CHECK(!folder_name_has_ext("nds", nds.exts));
    CHECK(folder_save_name(&nds, "Mario Kart DS.nds", out, sizeof(out)));
    CHECK_STR(out, "Mario Kart DS.sav");
    CHECK(folder_save_name(&nds, "v1.2 hack.nds", out, sizeof(out)));
    CHECK_STR(out, "v1.2 hack.sav");
    const FolderRules none = { nds_exts, NULL, NULL };
    CHECK(!folder_save_name(&none, "a.nds", out, sizeof(out)));
}

static void make_tree(void) {
    snprintf(root, sizeof(root), "/tmp/gs_folder_XXXXXX");
    if (!mkdtemp(root)) {
        perror("mkdtemp");
        exit(1);
    }
    // nds: 3 ROMs; one save beside it, one (other case) in saves/, one without
    mkd("nds");
    touch("nds/A.nds");
    touch("nds/B.NDS");
    touch("nds/C.nds");
    touch("nds/a.sav");
    touch("nds/notes.txt");
    touch("nds/.hidden.nds");
    mkd("nds/saves");
    touch("nds/saves/b.SAV");
    touch("nds/saves/zz.sav");    // no ROM: not counted
    mkd("nds/Games");
    mkd("nds/.git");
    mkd("nds/folder.nds");        // a folder, not a ROM
    // A save beside the ROM wins over one in saves/
    mkd("both");
    touch("both/x.nds");
    touch("both/x.sav");
    mkd("both/saves");
    touch("both/saves/x.sav");
    mkd("empty");
}

static void test_list(void) {
    char p[FOLDER_PATH_MAX];
    char names[8][FOLDER_NAME_MAX];
    FolderList list = { names, 8, 0, false };
    at("nds", p, sizeof(p));
    CHECK(folder_list_read(p, &list, NULL, NULL));
    // Folders only, no dot names, sorted case-insensitively
    CHECK(list.count == 3);
    if (list.count == 3) {
        CHECK_STR(list.names[0], "folder.nds");
        CHECK_STR(list.names[1], "Games");
        CHECK_STR(list.names[2], "saves");
    }
    CHECK(!list.truncated);
    CHECK(folder_list_find(&list, "games") == 1);
    CHECK(folder_list_find(&list, "nope") == -1);

    // The cap: the rest is flagged, not kept
    FolderList small = { names, 2, 0, false };
    CHECK(folder_list_read(p, &small, NULL, NULL));
    CHECK(small.count == 2 && small.truncated);

    at("missing", p, sizeof(p));
    CHECK(!folder_list_read(p, &list, NULL, NULL));
    CHECK(list.count == 0);
}

static uint64_t fake_ms;
static uint64_t fake_clock(void) {
    return fake_ms++;   // every entry costs a "millisecond"
}

static FolderCounter counter;

static void count(const char *rel, FolderCount *out) {
    char p[FOLDER_PATH_MAX];
    at(rel, p, sizeof(p));
    folder_count_start(&counter, &nds, p);
    CHECK(folder_count_busy(&counter));
    // A tiny budget per call: it must resume where it left off
    int calls = 0;
    while (!folder_count_step(&counter, fake_clock, fake_ms + 2) && calls < 10000) calls++;
    CHECK(folder_count_done(&counter));
    CHECK(calls > 0 || counter.result.items == 0);
    *out = counter.result;
}

static void test_count(void) {
    FolderCount c;
    count("nds", &c);
    CHECK(c.opened && !c.capped);
    CHECK(c.items == 3);
    CHECK(c.saves == 2);
    CHECK(c.saves_in_sub == 1);
    CHECK(c.folders == 3);

    count("both", &c);
    CHECK(c.items == 1 && c.saves == 1 && c.saves_in_sub == 0);

    count("empty", &c);
    CHECK(c.opened && c.items == 0 && c.folders == 0);

    count("missing", &c);
    CHECK(!c.opened && c.items == 0);

    // Run to completion with no clock
    char p[FOLDER_PATH_MAX];
    at("nds", p, sizeof(p));
    folder_count_start(&counter, &nds, p);
    CHECK(folder_count_step(&counter, NULL, 0));
    CHECK(counter.result.items == 3);

    // Stopping mid-way closes the cursor and leaves it idle
    folder_count_start(&counter, &nds, p);
    folder_count_step(&counter, fake_clock, fake_ms);
    folder_count_stop(&counter);
    CHECK(!folder_count_busy(&counter) && !folder_count_done(&counter));
    folder_count_stop(&counter);

    // Over the cap: counts of the first FOLDER_SCAN_CAP entries, flagged
    mkd("big");
    for (int i = 0; i < FOLDER_SCAN_CAP + 20; i++) {
        char rel[64];
        snprintf(rel, sizeof(rel), "big/g%04d.nds", i);
        touch(rel);
    }
    count("big", &c);
    CHECK(c.capped);
    CHECK(c.items <= FOLDER_SCAN_CAP && c.items >= FOLDER_SCAN_CAP - 2);
}

static void test_text(void) {
    char out[96];
    FolderCount c = { 12, 10, 4, 0, true, false };
    folder_count_summary(&c, &nds, ".nds ROMs", out, sizeof(out));
    CHECK_STR(out, "12 .nds ROMs \xC2\xB7 10 saves (4 in saves/)");
    FolderCount one = { 1, 0, 0, 0, true, false };
    folder_count_summary(&one, &nds, ".nds ROMs", out, sizeof(out));
    CHECK_STR(out, "1 .nds ROM \xC2\xB7 no saves");
    FolderCount same = { 2, 1, 0, 0, true, false };
    folder_count_summary(&same, &nds, ".nds ROMs", out, sizeof(out));
    CHECK_STR(out, "2 .nds ROMs \xC2\xB7 1 save");
    FolderCount none = { 0, 0, 0, 5, true, false };
    folder_count_summary(&none, &nds, ".nds ROMs", out, sizeof(out));
    CHECK_STR(out, "No .nds ROMs in this folder");
    folder_count_brief(&none, ".nds ROMs", out, sizeof(out));
    CHECK_STR(out, "no .nds ROMs \xC2\xB7 5 folders");
    folder_count_brief(&c, ".nds ROMs", out, sizeof(out));
    CHECK_STR(out, "12 .nds ROMs");
    FolderCount bad = { 0 };
    folder_count_brief(&bad, ".nds ROMs", out, sizeof(out));
    CHECK_STR(out, "can't be read");
}

static void test_cache(void) {
    static FolderCache cache;
    FolderCount c = { 1, 0, 0, 0, true, false }, out;
    char p[32];
    for (int i = 0; i < FOLDER_CACHE_SIZE; i++) {
        snprintf(p, sizeof(p), "sdmc:/%d", i);
        c.items = i;
        folder_cache_put(&cache, p, &c);
    }
    // Touch 0 so 1 is the least recently used, then add one more
    CHECK(folder_cache_get(&cache, "sdmc:/0", &out) && out.items == 0);
    c.items = 99;
    folder_cache_put(&cache, "sdmc:/new", &c);
    CHECK(!folder_cache_get(&cache, "sdmc:/1", NULL));
    CHECK(folder_cache_get(&cache, "sdmc:/0", NULL));
    CHECK(folder_cache_get(&cache, "sdmc:/new", &out) && out.items == 99);
    // Putting a path again updates it in place
    c.items = 7;
    folder_cache_put(&cache, "sdmc:/new", &c);
    CHECK(folder_cache_get(&cache, "sdmc:/new", &out) && out.items == 7);
    CHECK(folder_cache_get(&cache, "sdmc:/15", NULL));
}

int main(void) {
    test_paths();
    test_rules();
    make_tree();
    test_list();
    test_count();
    test_text();
    test_cache();
    char cmd[FOLDER_PATH_MAX + 16];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    if (system(cmd) != 0) fprintf(stderr, "couldn't remove %s\n", root);
    TEST_DONE();
}
