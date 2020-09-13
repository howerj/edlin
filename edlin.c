/* Project: Text editor library inspired by edlin.
 * License: The Unlicense
 * Author:  Richard James Howe
 * Repo:    <https://github.com/howerj/edlin>
 *
 * TODO: Abstract out FILE I/O and memory access, unit tests, turn into
 * library, simplify and shrink code, make compatible with the original
 * edlin, fix line numbers, add 'search', 'replace', 'cut' and 'copy'...
 *
 * Use <https://www.computerhope.com/edlin.htm> to get info for 
 * compatibility. */
#include "edlin.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

typedef struct {
	const char *file_name, *line_ending, *eol;
	char **lines;
	size_t count, pos;
	FILE *msgs, *cmds;
	int fatal;
} edit_t;

static void *reallocator(void *ptr, size_t sz) {
	void *r = realloc(ptr, sz);
	if (!r) {
		free(ptr);
		return NULL;
	}
	return r;
}

static char *slurp(FILE *input, size_t *length, char *class, int include) {
	assert(input);
	assert(class);
	char *m = NULL;
	const size_t bsz = class ? 32 : 256;
	int rchar = 0;
	size_t sz = 0;
	if (length)
		*length = 0;
	for (;;) {
		if ((m = reallocator(m, sz + bsz + 1)) == NULL)
			return NULL;
		size_t j = 0;
		int ch = 0, done = 0;
		for (; ((ch = fgetc(input)) != EOF) && j < bsz; ) {
			rchar = 1;
			done = !!strchr(class, ch);
			if (include || !done)
				m[sz + j++] = ch;
			if (done)
				break;
		}
		sz += j;
		if (done || ch == EOF)
			break;
	}
	if (rchar) {
		m[sz] = '\0'; /* ensure NUL termination */
		if (length)
			*length = sz;
	} else {
		free(m);
		return NULL;
	}
	return m;
}

static int question(edit_t *e, FILE *msgs) {
	assert(e);
	if (fprintf(msgs, "?%s", e->line_ending) < 0)
		return -1;
	return 0;
}

static int destroy(edit_t *e) {
	if (!e)
		return 0;
	for (size_t i = 0; i < e->count; i++)
		free(e->lines[i]);
	free(e->lines);
	e->lines = 0;
	e->count = 0;
	return 0;
}

static int grow(edit_t *e, size_t more) {
	assert(e);
	void *n = realloc(e->lines, (e->count + more) * sizeof (e->lines[0]));
	if (!n) {
		destroy(e);
		return -1;
	}
	e->lines = n;
	assert((e->count + more) >= e->count);
	memset(&e->lines[e->count], 0, more * sizeof (e->lines[0]));
	e->count += more;
	return 0;
}

static int print(edit_t *e, size_t low, size_t high, FILE *msgs) {
	assert(e);
	assert(msgs);
	assert(e->line_ending);
	if (low > high)
		return question(e, msgs);
	for (size_t i = low; i < high; i++) {
		if (fprintf(msgs, "%4u:\t%s%s", (unsigned)i, e->lines[i], e->line_ending) < 0)
			return -1;
		if (i == e->pos)
			if (fprintf(msgs, "*%s", e->line_ending) < 0)
				return -1;
	}
	return 0;
}

static int delete(edit_t *e, size_t low, size_t high) {
	assert(e);
	if (low > high)
		return 1;
	for (size_t i = low; i < high; i++) {
		free(e->lines[i]);
		e->lines[i] = NULL;
	}
	memmove(&e->lines[low], &e->lines[high], (e->count - high) * sizeof (e->lines[0]));
	e->count -= (high - low);
	e->pos = MIN(e->pos, e->count);
	return 0;
}

static int load_file(edit_t *e, FILE *in, int interactive) {
	assert(e);
	assert(in);
	size_t length = 0;
	for (char *l = NULL;(l = slurp(in, &length, "\n", 0));) {
		if (!length && !l) { free(l); break; }
		if (!l) return -1;
		if (interactive && !strcmp(l, e->eol)) { free(l); break; }
		if (grow(e, 1) < 0) { free(l); destroy(e); return -1; }
		if (e->pos && e->count) /* a bit ugly... */
			memmove(&e->lines[e->pos], &e->lines[e->pos - 1], (e->count - e->pos) * sizeof (e->lines[0]));
		if (e->pos == 0 && e->count)
			memmove(&e->lines[e->pos + 1], &e->lines[e->pos], (e->count - e->pos - 1) * sizeof (e->lines[0]));
		assert(e->pos < e->count);
		e->lines[e->pos++] = l;
	}
	return 0;
}

static int load_name(edit_t *e, const char *name, int interactive) {
	assert(e);
	assert(name);
	FILE *in = fopen(name, "rb");
	if (!in)
		return -1;
	const int r = load_file(e, in, interactive);
	if (fclose(in) < 0)
		return -1;
	return r;
}

static int save(edit_t *e, const char *file_name, size_t low, size_t high) {
	assert(e);
	assert(file_name);
	int r = 0;
	if (low > high || high > e->count)
		return -1;
	const char *name = file_name[0] ? file_name : e->file_name;
	FILE *file = fopen(name, "wb");
	if (!file) {
		(void)fprintf(e->msgs, "%s?\n", name);
		return -1;
	}
	for (size_t i = low; i < high; i++) {
		if (fprintf(file, "%s%s", e->lines[i], e->line_ending) < 0) {
			r = -1;
			break;
		}
	}
	if (fclose(file) < 0)
		return -1;
	return r;
}

static int editor(edit_t *e) {
	assert(e);
	assert(e->cmds);
	assert(e->msgs);
	static const char *help = "\
Primitive line editor, commands:\n\n\
   1,2d       - delete lines, inclusive\n\
   1,2sString - search lines\n\
   1,2p       - print lines and update position\n\
   1,2l       - print lines\n\
   wString    - write to file, optionally supplied\n\
   eString    - write to file, optionally supplied, and quit\n\
   q          - quit\n\
   a          - append to file and enter insert mode\n\
   1t         - insert file at position\n\
   1i         - edit at line and enter insert mode\n\
   h          - print help\n\
   ?          - print help\n\
\n\
To exit insert mode enter single '.' on a line. '1' and '2' are\n\
line numbers, '1' being the lower range, '2' being the upper.\n\n";
	unsigned low = -1, high = -1;
	int cnt = -1;
	char line[256] = { 0, }, *c = NULL;
	for (;fgets(line, sizeof (line) - 1, e->cmds);) {
		if (e->fatal)
			return -1;
		for (low = 0; low < (sizeof(line) - 1); low++)
			if (line[low] == '\r' || line[low] == '\n')
				line[low] = '\0';
		/* TODO: Simplify argument parsing? */
		if (sscanf(line, "%u,%u%n", &low, &high, &cnt) == 2) {
			low = MAX(low, 0);
			high = MIN(high, (long)e->count);
			c = &line[cnt];
			switch (tolower(c[0])) {
			case 'l': e->pos = high; /* fall-through */
			case 'p': if (print(e, low, high, e->msgs) < 0) return -1; break;
			case 'd': if (delete(e, low, high) < 0) return -1; break;
			case 's': /* TODO: Search and set pos */ break;
			case 'w': (void)save(e, &line[1], low, high); break;
			default: if (fputs("?\n", e->msgs) < 0) return -1;
			}
		} else if (sscanf(line, "%u%n", &low, &cnt) == 1) {
			low = MIN(e->count, MAX(low, 0));
			c = &line[cnt];
			switch (tolower(c[0])) {
			case 'l': e->pos = low; /* fall-through */
			case 'p': if (print(e, 0, low, e->msgs) < 0)   return -1; break;
			case 'd': if (delete(e, 0, low) < 0)           return -1; break;
			case 'i': e->pos = low; if (load_file(e, e->cmds, 1) < 0) return -1; break;
			case 't':
				e->pos = low;
				if (!line[0]) {
					if (fputs("?\n", e->msgs) < 0)
						return -1;
					break;
				}
				(void)load_name(e, &line[1], 0);
				break;
			default: if (fputs("?\n", e->msgs) < 0)        return -1; break;
			}
		} else {
			c = &line[0];
			switch (tolower(c[0])) {
			case 'q': return 0;
			case '?': case 'h': if (fputs(help, e->msgs) < 0) return -1; break;
			case 'l': e->pos = e->count; /* fall-through */
			case 'p': if (print(e, 0, e->count, e->msgs) < 0) return -1; break;
			case 'e': /* fall-through */
			case 'w': (void)save(e, &line[1], 0, e->count);
				if (tolower(c[0]) == 'e')
					return 0;
				break;
			case 'a': e->pos = e->count; /* fall-through */
			case 'i': if (load_file(e, e->cmds, 1) < 0) return -1; break;
			case ' ': case '\t': case '\r': case '\n': /* fall-through */
			default: if (fputs("?\n", e->msgs) < 0) return -1;
			}
		}
	}
	return 0;
}

int edlin(const char *file, FILE *cmds, FILE *msgs) {
	assert(cmds);
	assert(msgs);
	edit_t e = {
		.file_name = file ? file : "", .line_ending = "\n", .eol = ".",
		.msgs = msgs, .cmds = cmds,
	};
	if (file)
		(void)load_name(&e, file, 0);
	if (editor(&e) < 0)
		goto fail;
	return destroy(&e);
fail:
	(void)destroy(&e);
	return -1;
}


