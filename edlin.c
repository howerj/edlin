/* Project: Text editor library inspired by edlin.
 * License: The Unlicense
 * Author:  Richard James Howe
 * Repo:    <https://github.com/howerj/edlin>
 *
 * TODO: Abstract out FILE I/O and memory access, unit tests, turn into
 * library, simplify and shrink code, make compatible with the original
 * edlin, fix line numbers, add 'search', 'replace', 'copy', 'move',
 * memory limits/store on disk, better API, escaping, keep it small...
 *
 * Use <https://www.computerhope.com/edlin.htm> to get info for 
 * compatibility. */
#include "edlin.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifndef VERSION
#define VERSION (0x000900ul)
#endif

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

typedef struct {
	const char *file_name, *line_ending, *eol;
	char **lines;
	size_t count, pos;
	FILE *msgs, *cmds;
	int fatal, verbose;
} edit_t;

static void *reallocator(void *ptr, size_t sz) {
	void *r = realloc(ptr, sz);
	if (!r) {
		free(ptr);
		return NULL;
	}
	return r;
}

static char *slurp(FILE *input, size_t *length) {
	assert(input);
	assert(length);
	size_t sz = 0, bsz = 0;
	*length = 0;
	char *m = NULL;
	for (int ch = 0;(ch = fgetc(input)) != EOF;) {
		if ((sz + 1) > bsz) {
			bsz += 80;
			char *n = reallocator(m, bsz + 1);
			if (!n) {
				free(m);
				return NULL;
			}
			m = n;
		}
		if (ch == '\n') {
			m[sz++] = 0;
			break;
		}
		m[sz++] = ch;
	}
	*length = sz;
	if (m)
		m[sz] = 0;
	return m;
}

static int question(edit_t *e) {
	assert(e);
	if (fprintf(e->msgs, "?%s", e->line_ending) < 0)
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

static int print(edit_t *e, size_t low, size_t high) {
	assert(e);
	assert(e->line_ending);
	if (low > high || high > e->count)
		return question(e);
	for (size_t i = low; i < high; i++) {
		if (fprintf(e->msgs, "%4u:\t%s%s", (unsigned)i, e->lines[i], e->line_ending) < 0)
			return -1;
		if (i == e->pos)
			if (fprintf(e->msgs, "*%s", e->line_ending) < 0)
				return -1;
	}
	return 0;
}

static int delete(edit_t *e, size_t low, size_t high) {
	assert(e);
	if (low > high || high > e->count)
		return question(e);
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
	for (char *l = NULL;;) {
		if (e->verbose && interactive) { if (fprintf(e->msgs, ":") < 0) return -1; }
		l = slurp(in, &length);
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
	if (!in) {
		if (e->verbose)
			fprintf(e->msgs, "failed to load file %s", name);
		return -1;
	}
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
		return question(e);
	const char *name = file_name[0] ? file_name : e->file_name;
	FILE *file = fopen(name, "wb");
	if (!file) {
		(void)fprintf(e->msgs, "%s?\n", name);
		return -1;
	}
	for (size_t i = low; i < high; i++)
		if ((r = fprintf(file, "%s%s", e->lines[i], e->line_ending)) < 0)
			break;
	if (fclose(file) < 0)
		return -1;
	if (e->verbose)
		if (fprintf(e->msgs, "saved '%s': %d\n", name, r) < 0)
			r = -1;
	return r;
}

static int search(edit_t *e, const char *string, size_t low, size_t high) {
	assert(e);
	assert(string);
	if (low > high || high > e->count)
		return question(e);
	for (size_t i = low; i < high; i++)
		if (strstr(e->lines[i], string)) {
			if (e->verbose)
				if (print(e, i, i) < 0)
					return -1;
			e->pos = i;
			/* TODO: Print if verbose */
			return 0;
		}
	e->pos = e->count;
	return 0;
}

static int editor(edit_t *e) {
	assert(e);
	assert(e->cmds);
	assert(e->msgs);
	e->pos = 0;

static const char *help ="\
edlin clone, MIT license, Richard James Howe, <https//github.com/howerj/edlin>\n\n\
[#][,#]e<>  write file and quit | q           quit\n\
[#][,#]w<>  write file          | [#][,#]l    list lines (no cursor update)\n\
[#][,#]d    delete lines        | [#]i        insert at cursor or line\n\
[#][,#]p    print lines         | a           insert at end of file\n\
h           print help          | ?           print info\n\
[#][,#]s$   search for string   | [#]t<>      transfer file into line\n\n\
$ = string, <> = file, [] = optional, # = number. A single '.' on a new\n\
line exits insert mode.\n\n";
	for (char line[256] = { 0, };fgets(line, sizeof (line) - 1, e->cmds);) {
		if (e->fatal)
			return -1;
		for (size_t i = 0; i < (sizeof(line) - 1); i++)
			if (line[i] == '\r' || line[i] == '\n')
				line[i] = '\0';
		int argc = 0, cnt = 0, tot = 0;
		unsigned long argv[4] = { 0, };
		for (argc = 0; argc < 4 && 1 == sscanf(&line[tot], "%lu%n", &argv[argc], &cnt); argc++) {
			tot += cnt;
			if (line[tot] != ',') {
				argc++;
				break;
			}
			tot++;
		}
		const int ch = line[tot];
		const char *str = &line[tot + 1];
		unsigned long low = MAX(argv[0], 0), high = MIN(argv[1] + 1, e->count);
		if (argc == 1) {
			low = MIN(e->count, MAX(low, 0));
			high = MIN(e->count, low + 1ul);
		} else if (argc == 0) {
			low = MAX(e->pos, MAX(low, 0));
			high = MIN(e->count, low + 1ul);
		}

		switch (ch) {
		case 'q': if (argc != 0) { question(e); break; } return 0;
		case 'p': e->pos = high; /* fall-through */
		case 'l': if (print(e, low, high) < 0) e->fatal = 1; break;
		case 'd': if (delete(e, low, high) < 0) e->fatal = 1; break;
		case 'h': if (argc != 0) { question(e); break; } if (fputs(help, e->msgs) < 0) e->fatal = 1; break;
		case 'e': /* fall-through */
		case 'w': if (argc == 0) { low = 0; high = e->count; } save(e, str, low, high); if (ch == 'e') return 0; break;
		case 'a': if (argc != 0) { question(e); break; } low = e->count; /* fall-through */
		case 'i': e->pos = low; if (load_file(e, e->cmds, 1) < 0) return -1; break;
		case 't': if (argc > 1) { question(e); break; } e->pos = low; load_name(e, str, 0); break;
		case 's': if (argc == 0) { high = e->count; } search(e, &line[1], low, high); break;
		case '?': if (argc != 0) { question(e); break; }; if (fprintf(e->msgs, "file='%s' pos=%lu count=%lu\n", e->file_name, (unsigned long)e->pos, (unsigned long)e->count) < 0) e->fatal = 1; break;
		case '\0': if (argc == 1) { e->pos = low; break; } /* fall-through */
		default: question(e);
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


