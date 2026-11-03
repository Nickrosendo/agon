#include "test.h"

#include <string.h> /* strstr, memcmp */

#include "settings.h"

/* Under build/, which git ignores and make clean removes. Tests run from
 * the repository's top folder. */
#define TEST_FILE "build/test_settings.conf"

/* Writes text to TEST_FILE. */
static void write_file(const char *text)
{
    FILE *f = fopen(TEST_FILE, "w");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

/* Loads text as a settings file into *s; the message goes to err. */
static bool load_text(const char *text, Settings *s, char *err, size_t errlen)
{
    write_file(text);
    return settings_load(s, TEST_FILE, err, errlen);
}

/* True when text fails to load, with both pieces in the message, and the
 * settings are left exactly as they were. */
static bool fails_with(const char *text, const char *piece1,
                       const char *piece2)
{
    Settings s = settings_default();
    char err[256] = "";
    bool loaded = load_text(text, &s, err, sizeof(err));
    Settings fresh = settings_default();
    if (!loaded && strstr(err, piece1) != NULL && strstr(err, piece2) != NULL &&
        memcmp(&s, &fresh, sizeof(s)) == 0) {
        return true;
    }
    fprintf(stderr, "expected a failure for [%s], message: [%s]\n", text, err);
    return false;
}

static void test_two_values_change_and_no_others(void)
{
    Settings s = settings_default();
    char err[256] = "";
    CHECK(load_text("mutation_rate = 0.25\npool_capacity = 100\n", &s, err,
                    sizeof(err)));
    CHECK(s.mutation_rate == 0.25f);
    CHECK(s.pool_capacity == 100);
    /* Put the two back: everything must now match the defaults. Settings
     * has no padding (test_table_covers_every_field), so memcmp sees every
     * byte that matters and no stray ones. */
    Settings fresh = settings_default();
    s.mutation_rate = fresh.mutation_rate;
    s.pool_capacity = fresh.pool_capacity;
    CHECK(memcmp(&s, &fresh, sizeof(s)) == 0);
}

static void test_comments_blanks_spaces_sections(void)
{
    Settings s = settings_default();
    char err[256] = "";
    CHECK(load_text("# a comment\n"
                    "\n"
                    "   \t  \n"
                    "[brain]\n"
                    "  [ another section ]  \n"
                    "   max_age   =   77   # why not\n"
                    "\tupkeep_base=0.5\r\n"
                    "   # an indented comment\n"
                    "brain = random\n"
                    "stats_every = 5", /* no newline at the end */
                    &s, err, sizeof(err)));
    CHECK(s.max_age == 77);
    CHECK(s.upkeep_base == 0.5f);
    CHECK(strcmp(s.brain_name, "random") == 0);
    CHECK(s.stats_every == 5);
}

static void test_later_line_wins(void)
{
    Settings s = settings_default();
    char err[256] = "";
    CHECK(load_text("max_age = 10\nmax_age = 20\n", &s, err, sizeof(err)));
    CHECK(s.max_age == 20);
}

/* Settings that used to exist are ordinary mistakes now: a file still naming
 * them says which line, so nobody thinks they still do something. */
static void test_removed_settings_are_errors(void)
{
    CHECK(fails_with("pack_defence = 1\n", "line 1", "pack_defence"));
    CHECK(fails_with("# old\nsplit_threshold = 3\n", "line 2",
                     "split_threshold"));
    CHECK(fails_with("split_share = 0.5\n", "line 1", "split_share"));
    CHECK(fails_with("pack_kinship = 0.5\n", "line 1", "pack_kinship"));
}

static void test_mistakes_name_the_line(void)
{
    CHECK(fails_with("max_age = 5\n\nbanana = 3\n", "line 3", "banana"));
    CHECK(fails_with("pool_capacity = banana\n", "line 1", "pool_capacity"));
    CHECK(fails_with("# ok\npool_capacity = -3\n", "line 2", "pool_capacity"));
    CHECK(fails_with("max_age 5\n", "line 1", "="));
    CHECK(fails_with("= 5\n", "line 1", "="));
    CHECK(fails_with("max_age =\n", "line 1", "max_age"));
    CHECK(fails_with("max_age = 5 6\n", "line 1", "max_age"));
    CHECK(fails_with("max_age = 5.5\n", "line 1", "max_age"));
    CHECK(fails_with("upkeep_base = 1.0x\n", "line 1", "upkeep_base"));
    CHECK(fails_with("upkeep_base = nan\n", "line 1", "upkeep_base"));
    CHECK(fails_with("upkeep_base = inf\n", "line 1", "upkeep_base"));
    CHECK(fails_with("mate_share = 2\n", "line 1", "mate_share"));
    CHECK(fails_with("[brain\n", "line 1", "]"));
    /* One character more than brain_name holds, its '\0' included. */
    char name[BRAIN_NAME_MAX + 16] = "brain = ";
    size_t start = strlen(name);
    memset(name + start, 'x', BRAIN_NAME_MAX);
    name[start + BRAIN_NAME_MAX] = '\0';
    CHECK(fails_with(name, "line 1", "brain"));
}

static void test_too_long_line_fails(void)
{
    /* Longer than the read buffer: the tail must not be read as a line of
     * its own, and the first value must not take effect. */
    char text[SETTINGS_LINE_MAX + 64] = "max_age = 5 #";
    size_t start = strlen(text);
    memset(text + start, 'x', SETTINGS_LINE_MAX);
    text[start + SETTINGS_LINE_MAX] = '\0';
    CHECK(fails_with(text, "line 1", "long"));
}

static void test_missing_file_names_the_path(void)
{
    Settings s = settings_default();
    char err[256] = "";
    CHECK(!settings_load(&s, "build/no_such_file.conf", err, sizeof(err)));
    CHECK(strstr(err, "build/no_such_file.conf") != NULL);
}

/*
 * The guard against forgetting a new setting. Every field of Settings is
 * an int, a float or the name; if the table's sizes add up to the size of
 * the struct, no field is missing (and the struct has no padding).
 */
static void test_table_covers_every_field(void)
{
    size_t total = 0;
    for (int i = 0; i < settings_count(); i++) {
        const SettingInfo *info = settings_info(i);
        total += info->type == SETTING_TEXT ? BRAIN_NAME_MAX : 4;
        for (int j = 0; j < i; j++) {
            CHECK(strcmp(info->name, settings_info(j)->name) != 0);
            CHECK(info->offset != settings_info(j)->offset);
        }
        CHECK(info->help != NULL && info->help[0] != '\0');
    }
    CHECK(total == sizeof(Settings));
}

static void test_defaults_pass_their_own_ranges(void)
{
    Settings s = settings_default();
    char err[256] = "";
    bool ok = settings_check(&s, err, sizeof(err));
    if (!ok) {
        fprintf(stderr, "%s\n", err);
    }
    CHECK(ok);
}

static void test_check_rejects_out_of_range(void)
{
    char err[256] = "";
    Settings s = settings_default();
    s.pool_capacity = 0;
    CHECK(!settings_check(&s, err, sizeof(err)));
    CHECK(strstr(err, "pool_capacity") != NULL);
    s = settings_default();
    s.noise_scale = 0.0f; /* would divide by zero in the terrain */
    CHECK(!settings_check(&s, err, sizeof(err)));
    s = settings_default();
    /* A name with no '\0' inside its array: not a string at all. */
    memset(s.brain_name, 'x', BRAIN_NAME_MAX);
    CHECK(!settings_check(&s, err, sizeof(err)));
}

/* The list settings_print writes is itself a valid settings file that
 * gives back the defaults. */
static void test_print_loads_back(void)
{
    Settings defaults = settings_default();
    FILE *f = fopen(TEST_FILE, "w");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    settings_print(f, &defaults);
    fclose(f);
    Settings s = settings_default();
    s.pool_capacity = 1; /* everything must come from the file */
    s.upkeep_base = 9.0f;
    s.brain_name[0] = 'z';
    char err[256] = "";
    CHECK(settings_load(&s, TEST_FILE, err, sizeof(err)));
    CHECK(memcmp(&s, &defaults, sizeof(s)) == 0);
}

/* The example shipped with the program must not fall behind the code. */
static void test_example_file_is_current(void)
{
    Settings s = settings_default();
    s.pool_capacity = 1;
    char err[256] = "";
    bool ok = settings_load(&s, "agon.conf.example", err, sizeof(err));
    if (!ok) {
        fprintf(stderr, "%s\n", err);
    }
    CHECK(ok);
    Settings defaults = settings_default();
    CHECK(memcmp(&s, &defaults, sizeof(s)) == 0);

    /* Every setting must be listed, not merely default by omission. */
    FILE *f = fopen("agon.conf.example", "r");
    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    static char all[65536];
    size_t got = fread(all, 1, sizeof(all) - 1, f);
    all[got] = '\0';
    fclose(f);
    for (int i = 0; i < settings_count(); i++) {
        char wanted[64];
        snprintf(wanted, sizeof(wanted), "\n%s = ", settings_info(i)->name);
        if (strstr(all, wanted) == NULL) {
            fprintf(stderr, "agon.conf.example lacks %s\n",
                    settings_info(i)->name);
            CHECK(false);
        }
    }
}

int main(void)
{
    test_two_values_change_and_no_others();
    test_comments_blanks_spaces_sections();
    test_later_line_wins();
    test_mistakes_name_the_line();
    test_removed_settings_are_errors();
    test_too_long_line_fails();
    test_missing_file_names_the_path();
    test_table_covers_every_field();
    test_defaults_pass_their_own_ranges();
    test_check_rejects_out_of_range();
    test_print_loads_back();
    test_example_file_is_current();
    remove(TEST_FILE);
    TEST_SUMMARY();
}
