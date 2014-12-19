/*
 * Levenshtein distance module test driver program.
 *
 * Compile with:
 * $ gcc $(pkg-config --cflags glib-2.0) -o leventest leventest.c levenshtein.c $(pkg-config --libs glib-2.0)
 *
 * Run like:
 * $ ./leventest "ture" "true"
*/

#include <stdio.h>
#include <stdlib.h>

#include "levenshtein.h"

int main(int argc, char *argv[])
{
	if(argc >= 3)
	{
		const gchar	*s1 = argv[1], *s2 = argv[2];
		LDState		state;

		levenshtein_begin(&state);
		printf("distance between '%s' and '%s': %d\n", s1, s2, levenshtein_compute(&state, s1, s2));
		levenshtein_end(&state);

		levenshtein_begin_half(&state, s1);
		printf("distance between '%s' and '%s' in half-mode: %d\n", s1, s2, levenshtein_compute_half(&state, s2));
		levenshtein_end(&state);
	}
	return EXIT_SUCCESS;
}
