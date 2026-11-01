/*
 * A scripted brain for tests only: it answers with the actions it was
 * given, in order, then "not ready" for ever. It is registered by the
 * tests, never by the program, which shows that a brain can be added
 * without touching the simulation.
 */
#ifndef AGON_BRAIN_SCRIPT_H
#define AGON_BRAIN_SCRIPT_H

#include "brain.h"

#define SCRIPT_MAX 32

extern const Brain BRAIN_SCRIPT; /* name "script" */

/* Replaces the brain's script with count actions (at most SCRIPT_MAX).
 * b must be an open "script" brain. */
void brain_script_load(Brain *b, const Action *actions, int count);
/* How often decide, on_birth and on_death were called. */
int brain_script_decisions(const Brain *b);
int brain_script_births(const Brain *b);
int brain_script_deaths(const Brain *b);

#endif
