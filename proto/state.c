/*
 * state.c -- "shared" state-machine
 *
 * NOTE: This file is meant to be #include'd, not separately compiled.
 *       With -DTEST_MAIN, it builds a standalone unit-test program.
 */
//#include "state.h"

static int
next_state(int state)
{
    switch (state) {
    case 0:  return 1;
    case 1:  return 2;
    case 2:  return 1;
    case 3:  return 4;
    case 4:  return 5;
    case 5:  return 6;
    case 6:  return 1;
    default: return 0;
    }
}

#if 0
static int
prev_state(int state)
{
    switch (state) {
    case 0:  return 0;
    case 1:  return 2;
    case 2:  return 1;
    case 3:  return 2;
    case 4:  return 3;
    case 5:  return 4;
    case 6:  return 5;
    default: return 0;
    }
}
#endif


#ifdef TEST_MAIN
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>

void
test_state()
{
    int i, u;

    u = next_state(1);
    i = next_state(u);
    assert(i != u);
    assert(i == 1);
}

int
main()
{
    test_state();
    exit(EXIT_SUCCESS);
}
#endif /* TEST_MAIN */
