/*
 * Levenshtein distance computation, using GLib, for strings.
 *
 * Implemented directly from the Wikipedia pseudo-code at
 * <http://en.wikipedia.org/wiki/Levenshtein_distance#Computing_Levenshtein_distance>,
 * using a simple GHashTable for memoization.
 *
 * Copyright (C) 2013 by Emil Brink <emil@obsession.se>.
 *
 * This file is part of gitbrowser.
 *
 * gitbrowser is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gitbrowser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gitbrowser.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>

#include "levenshtein.h"

/* -------------------------------------------------------------------------------------------------------------- */

typedef struct {
	guint16	i;
	guint16	len1;
	guint16	j;
	guint16	len2;
} LKey;

struct LKeyMemo {
	LKey			key;
	guint16		dist;
	struct LKeyMemo	*next;
};

typedef struct LKeyMemo	LKeyMemo;

/* -------------------------------------------------------------------------------------------------------------- */

static guint lkey_hash(gconstpointer key)
{
	const LKey	*lkey = key;
	const gint	tmp = (lkey->i << 7) ^ (lkey->len1 >> 1) ^ (lkey->j << 17) ^ (lkey->len2);

	return g_int_hash(&tmp);
}

static gboolean lkey_equal(gconstpointer a, gconstpointer b)
{
	const LKey	*keya = a, *keyb = b;

	return (keya->i == keyb->i) && (keya->len1 == keyb->len1) && (keya->j == keyb->j) && (keya->len2 == keyb->len2);
}

/* -------------------------------------------------------------------------------------------------------------- */

void levenshtein_begin(LDState *state)
{
	state->cache = g_hash_table_new(lkey_hash, lkey_equal);
	state->data = NULL;
}

void levenshtein_begin_half(LDState *state, const gchar *s1)
{
	levenshtein_begin(state);
	state->half_str = s1;
	state->half_len = strlen(s1);
}

static guint16 compute(LDState *state, const gchar *s1, guint16 i, guint16 len1, const gchar *s2, guint16 j, guint16 len2)
{
	LKey		key;
	LKeyMemo	*mkey;
	guint16	cost, d1, d2, d3;

	key.i	 = i;
	key.len1 = len1;
	key.j	 = j;
	key.len2 = len2;

	if((mkey = g_hash_table_lookup(state->cache, &key)) != NULL)
		return mkey->dist;

	if(len1 == 0)
		return len2;
	if(len2 == 0)
		return len1;
		
	cost = (s1[i] != s2[j]);
	d1 = compute(state, s1, i + 1, len1 - 1, s2, j, len2) + 1;
	d2 = compute(state, s1, i,     len1,     s2, j + 1, len2 - 1) + 1;
	d3 = compute(state, s1, i + 1, len1 - 1, s2, j + 1, len2 - 1) + cost;
	
	mkey = g_slice_alloc(sizeof *mkey);
	mkey->key = key;
	mkey->dist = MIN(d1, MIN(d2, d3));
	mkey->next = state->data;
	state->data = mkey;
	
	g_hash_table_insert(state->cache, &mkey->key, mkey);

	return mkey->dist;
}

guint16 levenshtein_compute(LDState *state, const gchar *s1, const gchar *s2)
{
	if(s1 == NULL || s2 == NULL)
		return 0;
	return compute(state, s1, 0, strlen(s1), s2, 0, strlen(s2));
}

guint16 levenshtein_compute_half(LDState *state, const gchar *s2)
{
	if(s2 == NULL)
		return 0;
	return compute(state, state->half_str, 0, state->half_len, s2, 0, strlen(s2));
}

void levenshtein_end(LDState *state)
{
	LKeyMemo	*iter, *next;

	g_hash_table_destroy(state->cache);
	for(iter = state->data; iter != NULL; iter = next)
	{
		next = iter->next;
		g_slice_free1(sizeof *iter, iter);
	}
}
