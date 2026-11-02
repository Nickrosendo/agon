/*
 * Prints a world's fingerprint: seed, width, height and ticks on the command
 * line. Uses only the simulation, so it builds against old commits too, to
 * show that a phase of work did not change what the simulation does.
 */
#include <inttypes.h> /* PRIx64 */
#include <stdio.h>
#include <stdlib.h>   /* strtol, strtoull */

#include "settings.h"
#include "sim.h"

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: checksum_tool SEED WIDTH HEIGHT TICKS\n");
        return 2;
    }
    uint64_t seed = strtoull(argv[1], NULL, 0);
    int w = (int)strtol(argv[2], NULL, 10);
    int h = (int)strtol(argv[3], NULL, 10);
    long ticks = strtol(argv[4], NULL, 10);
    Settings set = settings_default();
    Sim s;
    if (!sim_init(&s, &set, seed, w, h)) {
        fprintf(stderr, "checksum_tool: could not build the world\n");
        return 1;
    }
    for (long i = 0; i < ticks; i++) {
        sim_step(&s);
    }
    printf("0x%016" PRIx64 "\n", sim_checksum(&s));
    sim_free(&s);
    return 0;
}
