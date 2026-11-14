#include "test.h"

#include <stdlib.h> /* free */
#include <string.h> /* strlen, strstr, strcpy */

#include "actions.h"
#include "genome.h"
#include "settings.h"
#include "sim.h"
#include "weather.h"
#include "worldcode.h"

static int first_alive(const Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            return i;
        }
    }
    return -1;
}

/* A world with two settings changed, a trait given and a drought, 400 ticks
 * old: what a Sandbox player might copy. */
static void played_world(Sim *s)
{
    Settings set = settings_default();
    set.trait_mutation_scale = 3.0f;
    set.jump_genes = 1 << GENE_ARMOUR;
    CHECK(sim_init(s, &set, 41, 64, 32));
    for (int i = 0; i < 100; i++) {
        sim_step(s);
    }
    int slot = first_alive(s);
    CHECK(slot >= 0);
    CHECK(actions_give_trait(s, s->pool.items[slot].id, GENE_ARMOUR, 1.0f));
    for (int i = 0; i < 100; i++) {
        sim_step(s);
    }
    CHECK(actions_weather(s, WEATHER_DROUGHT));
    for (int i = 0; i < 200; i++) {
        sim_step(s);
    }
}

/* Opens text and replays it to its tick, as the window does in slices. */
static bool replay(const char *text, Sim *out)
{
    WorldCode code;
    char err[128];
    if (!worldcode_read(text, &code, err, sizeof(err))) {
        fprintf(stderr, "refused: %s\n", err);
        return false;
    }
    bool ok = worldcode_start(out, &code, err, sizeof(err));
    uint64_t until = code.until; /* worldcode_free empties code */
    worldcode_free(&code);
    while (ok && out->tick < until) {
        sim_step(out);
    }
    return ok;
}

/* A code grows the same world again: equal at the code's tick, and still equal
 * after both run on. */
static void test_round_trip(void)
{
    Sim s;
    played_world(&s);
    char *text = NULL;
    CHECK(worldcode_write(&s, &text));
    CHECK(strncmp(text, WORLDCODE_PREFIX, strlen(WORLDCODE_PREFIX)) == 0);

    WorldCode code;
    char err[128];
    CHECK(worldcode_read(text, &code, err, sizeof(err)));
    CHECK(code.seed == 41 && code.w == 64 && code.h == 32 && code.until == 400);
    CHECK(code.set.trait_mutation_scale == 3.0f);
    CHECK(code.set.jump_genes == 1 << GENE_ARMOUR);
    CHECK(code.log.count == 2);
    worldcode_free(&code);

    Sim again;
    CHECK(replay(text, &again));
    CHECK(again.tick == s.tick);
    CHECK(sim_checksum(&again) == sim_checksum(&s));
    CHECK(sim_story_checksum(&again) == sim_story_checksum(&s));
    for (int i = 0; i < 300; i++) {
        sim_step(&s);
        sim_step(&again);
    }
    CHECK(sim_checksum(&again) == sim_checksum(&s));
    free(text);
    sim_free(&again);
    sim_free(&s);
}

/* An action made but not yet applied (the player pressed a key, the next tick
 * has not run) is in the code and happens in the replay at the same tick. */
static void test_pending_action(void)
{
    Sim s;
    played_world(&s);
    CHECK(actions_weather(&s, WEATHER_HARD_WINTER));
    char *text = NULL;
    CHECK(worldcode_write(&s, &text));
    Sim again;
    CHECK(replay(text, &again));
    CHECK(again.tick == s.tick);
    for (int i = 0; i < 50; i++) {
        sim_step(&s);
        sim_step(&again);
    }
    CHECK(again.weather.kind == WEATHER_HARD_WINTER);
    CHECK(sim_checksum(&again) == sim_checksum(&s));
    free(text);
    sim_free(&again);
    sim_free(&s);
}

/* A plain world, never touched: only seed, size and tick in it, and short. */
static void test_plain_world_is_short(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 7, 48, 24));
    char *text = NULL;
    CHECK(worldcode_write(&s, &text));
    /* 4 length + 2 rules + 8 seed + 4 size + 8 tick + 2 settings + 4 actions
     * + 4 check = 36 bytes, 48 characters. */
    CHECK(strlen(text) == strlen(WORLDCODE_PREFIX) + 48);
    Sim again;
    CHECK(replay(text, &again));
    CHECK(sim_checksum(&again) == sim_checksum(&s));
    free(text);
    sim_free(&again);
    sim_free(&s);
}

/* Reads text, expecting a refusal whose line holds want, and nothing kept. */
static void refused(const char *text, const char *want)
{
    WorldCode code;
    char err[128] = "";
    CHECK(!worldcode_read(text, &code, err, sizeof(err)));
    CHECK(strstr(err, want) != NULL);
    CHECK(strchr(err, '\n') == NULL); /* one line */
    CHECK(code.log.items == NULL && code.log.count == 0);
    if (strstr(err, want) == NULL) {
        fprintf(stderr, "  got \"%s\", wanted \"%s\"\n", err, want);
    }
}

/* Typos, cut codes, other rules and other texts are each refused with one
 * plain line; the sanitizers check that nothing leaks on the way. */
static void test_bad_codes_refused(void)
{
    Sim s;
    played_world(&s);
    char *text = NULL;
    CHECK(worldcode_write(&s, &text));
    size_t n = strlen(text);
    char *copy = malloc(n + 1);
    CHECK(copy != NULL);

    /* One letter changed, at the start, in the middle and in the check. */
    size_t places[3] = {strlen(WORLDCODE_PREFIX) + 1, n / 2, n - 2};
    for (int k = 0; k < 3; k++) {
        strcpy(copy, text);
        copy[places[k]] = copy[places[k]] == 'A' ? 'B' : 'A';
        refused(copy, "typo");
    }
    strcpy(copy, text);
    copy[n / 2] = '!'; /* not one of the 64 characters */
    refused(copy, "typo");

    /* Cut short: by half, by one character, to the prefix alone. */
    size_t cuts[3] = {n / 2, n - 1, strlen(WORLDCODE_PREFIX)};
    for (int k = 0; k < 3; k++) {
        strcpy(copy, text);
        copy[cuts[k]] = '\0';
        refused(copy, "cut short");
    }

    refused("hello", "not an agon world code");
    refused("", "not an agon world code");

    char *other = NULL;
    CHECK(worldcode_write_as(&s, AGON_RULES + 1, &other));
    char want[64];
    snprintf(want, sizeof(want), "made with rules %d; this agon has rules %d",
             AGON_RULES + 1, AGON_RULES);
    refused(other, want);

    free(other);
    free(copy);
    free(text);
    sim_free(&s);
}

/* A code wrapped over lines and indented, as an email or a chat may leave it,
 * still opens. */
static void test_blanks_skipped(void)
{
    Sim s;
    played_world(&s);
    char *text = NULL;
    CHECK(worldcode_write(&s, &text));
    size_t n = strlen(text);
    char *wrapped = malloc(2 * n + 8);
    CHECK(wrapped != NULL);
    size_t at = 0;
    wrapped[at++] = ' ';
    for (size_t i = 0; i < n; i++) {
        wrapped[at++] = text[i];
        if (i > strlen(WORLDCODE_PREFIX) && i % 20 == 0) {
            wrapped[at++] = i % 40 == 0 ? '\n' : ' ';
        }
    }
    wrapped[at++] = '\r';
    wrapped[at++] = '\n';
    wrapped[at] = '\0';
    Sim again;
    CHECK(replay(wrapped, &again));
    CHECK(sim_checksum(&again) == sim_checksum(&s));
    free(wrapped);
    free(text);
    sim_free(&again);
    sim_free(&s);
}

/* A link to the browser page opens the same world: the code is read from the
 * address's code= field, up to the next field or the '#', and a ':' written
 * as %3A still counts. Text around a link that holds no code is refused. */
static void test_link_opens(void)
{
    Sim s;
    played_world(&s);
    char *text = NULL;
    CHECK(worldcode_write(&s, &text));
    const char *body = text + strlen(WORLDCODE_PREFIX);
    size_t room = strlen(text) + 128;
    char *link = malloc(room);
    CHECK(link != NULL);

    /* Three ways a link may come: with fields after the code and a place on
     * the page, with the code last, and with the ':' in percent form. */
    const char *forms[3][2] = {
        {"see https://example.org/agon/?seed=3&code=agon1:", "&x=1#top now"},
        {"https://example.org/agon/?code=agon1:", ""},
        {"https://example.org/agon/?code=agon1%3a", "\n"},
    };
    for (int k = 0; k < 3; k++) {
        snprintf(link, room, "%s%s%s", forms[k][0], body, forms[k][1]);
        Sim again;
        CHECK(replay(link, &again));
        CHECK(sim_checksum(&again) == sim_checksum(&s));
        sim_free(&again);
    }
    refused("https://example.org/agon/?seed=3", "not an agon world code");
    refused("https://example.org/agon/?code=hello", "not an agon world code");
    /* A link cut inside the code is a cut code, not a typo. */
    snprintf(link, room, "https://example.org/agon/?code=%s", text);
    link[strlen(link) - 10] = '\0';
    refused(link, "cut short");

    free(link);
    free(text);
    sim_free(&s);
}

int main(void)
{
    test_round_trip();
    test_pending_action();
    test_plain_world_is_short();
    test_bad_codes_refused();
    test_blanks_skipped();
    test_link_opens();
    TEST_SUMMARY();
}
