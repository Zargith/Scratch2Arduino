// Compiles pocketpy (fetched by cmake/S2ADependencies.cmake) with one change: its clock() goes
// through s2a_pocketpy_clock() (core/src/script_python.cpp). pocketpy can stop a script with its
// watchdog only, which compares clock() with a deadline at every step; a clock that jumps to the
// end of time when the Stop button is pressed stops any script, even "while True: pass", without
// changing pocketpy's code. <time.h> is included first so that the macro does not touch it.
#include <time.h>

clock_t s2a_pocketpy_clock(void);
#define clock() s2a_pocketpy_clock()

#include "pocketpy.c"
