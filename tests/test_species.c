#include "test.h"

#include <math.h>   /* fabsf */
#include <string.h> /* strcmp */

#include "sprite.h"
#include "species.h"

/* Raw genes in, rounded traits out, as the drawing does it. */
static BodyTraits rounded(float size, float speed, float sight, float plant,
                          float meat, float armour, float weapon, float legs,
                          float swim)
{
    BodyTraits raw = {size, speed, sight, plant, meat, armour, weapon, legs, swim};
    return species_round(&raw);
}

/* All genes 0.2 except what a test changes afterwards. */
static BodyTraits base(void)
{
    return rounded(0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f);
}

static int role_of(BodyTraits raw)
{
    BodyTraits r = species_round(&raw);
    return species_role(&r);
}

static void test_roles_first_match_wins(void)
{
    BodyTraits t = {0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f};
    CHECK(role_of(t) == ROLE_GRAZER);

    t = base(); t.meat = 0.8f; t.plant = 0.6f; t.swim = 1.0f;
    CHECK(role_of(t) == ROLE_HUNTER);       /* meat beats leaf and swim */
    t = base(); t.meat = 0.6f; t.plant = 0.7f;
    CHECK(role_of(t) != ROLE_HUNTER);       /* both round to 2/3: a tie */
    t = base(); t.armour = 1.0f; t.swim = 1.0f;
    CHECK(role_of(t) == ROLE_SWIMMER);      /* swim before armour */
    t = base(); t.armour = 1.0f;
    CHECK(role_of(t) == ROLE_ARMOURED);
    t = base(); t.speed = 0.5f;
    CHECK(role_of(t) == ROLE_RUNNER);
    t = base(); t.legs = 0.5f;
    CHECK(role_of(t) == ROLE_RUNNER);
    t = base(); t.speed = 0.49f; t.legs = 0.49f;
    CHECK(role_of(t) != ROLE_RUNNER);
    t = base(); t.sight = 0.75f;
    CHECK(role_of(t) == ROLE_WATCHER);
    t = base(); t.sight = 0.74f;
    CHECK(role_of(t) != ROLE_WATCHER);
}

static void test_every_species_listed_once_in_its_role(void)
{
    int in_role[ROLE_COUNT] = {0};
    for (int s = 0; s < SPECIES_COUNT; s++) {
        CHECK(SPECIES[s].name != NULL);
        CHECK(SPECIES[s].role >= 0 && SPECIES[s].role < ROLE_COUNT);
        in_role[SPECIES[s].role]++;
        for (int o = s + 1; o < SPECIES_COUNT; o++) {
            CHECK(strcmp(SPECIES[s].name, SPECIES[o].name) != 0);
            CHECK(!(SPECIES[s].look.r == SPECIES[o].look.r &&
                    SPECIES[s].look.g == SPECIES[o].look.g &&
                    SPECIES[s].look.b == SPECIES[o].look.b));
        }
    }
    for (int r = 0; r < ROLE_COUNT; r++) {
        CHECK(in_role[r] >= 2);
    }
    CHECK(SPECIES[SPECIES_OTTER].role == ROLE_SWIMMER);
    CHECK(SPECIES[SPECIES_HIPPO].role == ROLE_SWIMMER);
    CHECK(SPECIES[SPECIES_LEMUR].role == ROLE_WATCHER);
    CHECK(SPECIES[SPECIES_GOAT].role == ROLE_GRAZER);
}

/* How many of 3000 lineages land on each species of a role. */
static void spread(int role, float real_size, int counts[SPECIES_COUNT])
{
    for (int s = 0; s < SPECIES_COUNT; s++) {
        counts[s] = 0;
    }
    for (uint32_t l = 0; l < 3000; l++) {
        counts[species_pick(l, role, real_size)]++;
    }
}

static void test_species_hash_stable_and_spread(void)
{
    for (uint32_t l = 0; l < 20; l++) {
        int first = species_pick(l, ROLE_HUNTER, 2.0f);
        for (int i = 0; i < 1000; i++) {
            if (species_pick(l, ROLE_HUNTER, 2.0f) != first) {
                CHECK(0);
                break;
            }
        }
    }
    int counts[SPECIES_COUNT];
    /* Three-species lists at a size where all are allowed. */
    int three[3][4] = {
        {ROLE_HUNTER, SPECIES_WOLF, SPECIES_LION, SPECIES_TIGER},
        {ROLE_RUNNER, SPECIES_HARE, SPECIES_GAZELLE, SPECIES_FOX},
        {ROLE_GRAZER, SPECIES_DEER, SPECIES_ZEBRA, SPECIES_GOAT},
    };
    for (int i = 0; i < 3; i++) {
        spread(three[i][0], 2.0f, counts);
        for (int k = 1; k <= 3; k++) {
            CHECK(counts[three[i][k]] >= 750 && counts[three[i][k]] <= 1260);
        }
    }
    spread(ROLE_SWIMMER, 7.0f, counts); /* otter, beaver, hippo */
    CHECK(counts[SPECIES_OTTER] >= 750 && counts[SPECIES_OTTER] <= 1260);
    CHECK(counts[SPECIES_BEAVER] >= 750 && counts[SPECIES_BEAVER] <= 1260);
    CHECK(counts[SPECIES_HIPPO] >= 750 && counts[SPECIES_HIPPO] <= 1260);
    /* Two-species lists. */
    spread(ROLE_WATCHER, 2.0f, counts);
    CHECK(counts[SPECIES_FENNEC] >= 1200 && counts[SPECIES_FENNEC] <= 1800);
    CHECK(counts[SPECIES_LEMUR] >= 1200 && counts[SPECIES_LEMUR] <= 1800);
    spread(ROLE_ARMOURED, 4.0f, counts);
    CHECK(counts[SPECIES_ELEPHANT] >= 1200 && counts[SPECIES_ELEPHANT] <= 1800);
    CHECK(counts[SPECIES_RHINO] >= 1200 && counts[SPECIES_RHINO] <= 1800);
    /* The role matters: the same lineage need not agree across roles. */
    CHECK(species_hash(5, ROLE_HUNTER) != species_hash(5, ROLE_GRAZER));
}

static void test_size_rules(void)
{
    for (uint32_t l = 0; l < 500; l++) {
        CHECK(species_pick(l, ROLE_ARMOURED, 1.0f) == SPECIES_ARMADILLO);
        CHECK(species_pick(l, ROLE_ARMOURED, SPRITE_TWO_TILES - 0.01f) == SPECIES_ARMADILLO);
        CHECK(species_pick(l, ROLE_ARMOURED, SPRITE_TWO_TILES) != SPECIES_ARMADILLO);
        CHECK(species_pick(l, ROLE_ARMOURED, 20.0f) != SPECIES_ARMADILLO);
        CHECK(species_pick(l, ROLE_SWIMMER, SPRITE_THREE_TILES - 0.01f) != SPECIES_HIPPO);
        CHECK(species_pick(l, ROLE_SWIMMER, 1.0f) != SPECIES_HIPPO);
        CHECK(SPECIES[species_pick(l, ROLE_SWIMMER, 1.0f)].role == ROLE_SWIMMER);
    }
    int hippos = 0;
    for (uint32_t l = 0; l < 500; l++) {
        hippos += species_pick(l, ROLE_SWIMMER, SPRITE_THREE_TILES) == SPECIES_HIPPO;
    }
    CHECK(hippos > 0);
}

static void test_small_gene_change_same_body(void)
{
    BodyTraits a = {0.40f, 0.3f, 0.3f, 0.5f, 0.1f, 0.2f, 0.2f, 0.2f, 0.2f};
    BodyTraits b = a;
    b.size = 0.41f;
    BodyTraits ra = species_round(&a), rb = species_round(&b);
    CHECK(ra.size == rb.size);
    CHECK(species_role(&ra) == species_role(&rb));
    CHECK(species_pick(9, species_role(&ra), 2.0f) ==
          species_pick(9, species_role(&rb), 2.0f));
}

static void test_k_levels(void)
{
    CHECK(SPECIES_K_LEVELS == 8);
    for (int i = 1; i < SPECIES_K_LEVELS; i++) {
        CHECK(SPECIES_K[i] > SPECIES_K[i - 1]);
    }
    CHECK(fabsf(SPECIES_K[species_k_level(1.0f)] - 0.95f) < 1e-6f);
    CHECK(species_k_level(0.49f) == 0);
    CHECK(species_k_level(0.5f) == 1);
    CHECK(species_k_level(1.0f) == 2);
    CHECK(species_k_level(1.99f) == 2);
    CHECK(species_k_level(2.0f) == 3);
    CHECK(species_k_level(SPRITE_TWO_TILES - 0.01f) == 3);
    CHECK(species_k_level(SPRITE_TWO_TILES) == 4);
    CHECK(species_k_level(4.5f) == 5);
    CHECK(species_k_level(SPRITE_THREE_TILES - 0.01f) == 5);
    CHECK(species_k_level(SPRITE_THREE_TILES) == 6);
    CHECK(species_k_level(9.0f) == 7);
    CHECK(species_k_level(1000.0f) == 7);
    CHECK(species_k_level(0.0f) == 0);
}

static void test_anatomy_follows_genes(void)
{
    Anatomy lean, meaty, shortlegs, longlegs, elephant, hare;
    BodyTraits t = base();
    t.meat = 0.0f;
    species_anatomy(&lean, SPECIES_WOLF, &t, 1.0f);
    t.meat = 1.0f;
    species_anatomy(&meaty, SPECIES_WOLF, &t, 1.0f);
    CHECK(meaty.chest_r > lean.chest_r);
    CHECK(meaty.skull > lean.skull);
    t = base();
    t.legs = 0.0f;
    species_anatomy(&shortlegs, SPECIES_WOLF, &t, 1.0f);
    t.legs = 1.0f;
    species_anatomy(&longlegs, SPECIES_WOLF, &t, 1.0f);
    CHECK(longlegs.leg_len > shortlegs.leg_len);
    CHECK(longlegs.stride > shortlegs.stride);
    t = base();
    species_anatomy(&elephant, SPECIES_ELEPHANT, &t, 1.0f);
    species_anatomy(&hare, SPECIES_HARE, &t, 1.0f);
    CHECK(elephant.body_len > hare.body_len);
    /* Values by hand for the wolf with all genes 0 (a rounded 0): the formulas
     * with every gene term gone. */
    BodyTraits zero = {0};
    Anatomy w;
    species_anatomy(&w, SPECIES_WOLF, &zero, 1.0f);
    CHECK(fabsf(w.body_len - 4.6f * 0.8f) < 1e-5f);
    CHECK(fabsf(w.leg_len - 2.4f) < 1e-5f);
    CHECK(fabsf(w.neck_angle - 0.55f) < 1e-6f);
    CHECK(!w.eye_big);
    Anatomy lemur;
    species_anatomy(&lemur, SPECIES_LEMUR, &zero, 1.0f);
    CHECK(lemur.eye_big);
    CHECK(w.species == SPECIES_WOLF && w.k == 1.0f);
}

int main(void)
{
    test_roles_first_match_wins();
    test_every_species_listed_once_in_its_role();
    test_species_hash_stable_and_spread();
    test_size_rules();
    test_small_gene_change_same_body();
    test_k_levels();
    test_anatomy_follows_genes();
    TEST_SUMMARY();
}
