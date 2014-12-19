/*
 * Levenshtein distance computation, using GLib, for strings.
 *
 * Copyright (C) 2013-2014 by Emil Brink <emil@obsession.se>.
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

#include <glib.h>

typedef struct {
	const gchar	*half_str;
	guint16		half_len;
} LDState;

void		levenshtein_begin(LDState *state);
void		levenshtein_begin_half(LDState *state, const gchar *s1);
guint16		levenshtein_compute(LDState *state, const gchar *s1, const gchar *s2);
guint16		levenshtein_compute_half(LDState *state, const gchar *s2);
void		levenshtein_end(LDState *state);
