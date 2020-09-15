#define PROGRAM "EDLIN clone - a line editor"
#define LICENSE "The Unlicense (public domain)"
#define AUTHOR  "Richard James Howe"
#define EMAIL   "howe.r.j.89@gmail.com"
#define REPO    "https://github.com/howerj/edlin"
#ifndef VERSION
#define VERSION "0.0.0" /* defined by build system */
#endif
/* TODO: Abstract out FILE I/O, make compatible with the original edlin,
 * multiple commands per line separated out by a ';',
 * fix line numbers, add 'replace', memory limits/store on disk, better API, escaping, keep it small,
 * Documentation in 'readme.md' file. More testing and assertions! */
#include "edlin.h"
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NELEMS(X) (sizeof (X) / sizeof (X[0]))
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

typedef struct {
	const char *file_name, *line_ending, *eol;
	char **lines;
	size_t count, pos;
	size_t line_length_limit, line_count_limit; /* 0 = limit disabled */
	FILE *msgs, *cmds;
	int fatal, verbose;
	allocator_fn allocator;
	void *arena;
} edit_t;

/* TODO: have a maximum number of allocations controlled from here? */
static void *allocator(void *arena, void *ptr, const size_t oldsz, const size_t newsz) {
	(void)arena;
	if (newsz == 0) { free(ptr); return NULL; }
	if (newsz > oldsz) { return realloc(ptr, newsz); }
	return ptr;
}

static void *reallocator(edit_t *e, void *ptr, size_t sz) {
	assert(e && e->allocator);
	void *r = e->allocator(e->arena, ptr, 0, sz);
	if (!r && sz) {
		e->allocator(e->arena, ptr, 0, 0);
		return NULL;
	}
	return r;
}

static void release(edit_t *e, void *ptr) {
	assert(e && e->allocator);
	(void)e->allocator(e->arena, ptr, 0, 0);
}

static char *slurp(edit_t *e, FILE *input, size_t *length) {
	assert(e);
	assert(input);
	assert(length);
	size_t sz = 0, bsz = 0;
	*length = 0;
	char *m = NULL;
	for (int ch = 0;(ch = fgetc(input)) != EOF;) {
		if (e->line_length_limit && bsz > e->line_length_limit) {
			release(e, m);
			return NULL;
		}
		if ((sz + 1) > bsz) {
			bsz += 80;
			char *n = reallocator(e, m, bsz + 1);
			if (!n) {
				release(e, m);
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

static int msg(edit_t *e, const char *fmt, ...) {
	assert(e);
	assert(e->msgs);
	va_list ap;
	va_start(ap, fmt);
	const int r1 = vfprintf(e->msgs, fmt, ap);
	va_end(ap);
	const int r2 = fputs(e->line_ending, e->msgs);
	e->fatal = r1 < 0 || r2 < 0;
	return -(e->fatal);
}

static int question(edit_t *e) {
	assert(e);
	return msg(e, "?");
}

static char *duplicate(const char *s) {
	assert(s);
	size_t sl = strlen(s) + 1;
	char *r = malloc(sl);
	if (!r)
		return NULL;
	return memcpy(r, s, sl);
}

static int destroy(edit_t *e) {
	if (!e)
		return 0;
	e->fatal = 1;
	for (size_t i = 0; i < e->count; i++)
		release(e, e->lines[i]);
	release(e, e->lines);
	e->lines = 0;
	e->count = 0;
	return 0;
}

static int grow(edit_t *e, size_t more) {
	assert(e);
	if (e->count + more < e->count)
		return -1;
	if (e->line_count_limit && (e->count + more) > e->line_count_limit)
		return -1;
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
	for (size_t i = low; i < high; i++)
		if (msg(e, "%4u%c %s", (unsigned)i, i == e->pos ? '*' : ':', e->lines[i]) < 0)
			return -1;
	return 0;
}

static int delete(edit_t *e, size_t low, size_t high) {
	assert(e);
	if (low > high || high > e->count)
		return question(e);
	for (size_t i = low; i < high; i++) {
		release(e, e->lines[i]);
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
		if (e->verbose && interactive) { if (fputs(":", e->msgs) < 0) e->fatal = 1; }
		l = slurp(e, in, &length);
		if (!length && !l) { release(e, l); break; }
		if (!l) return -1;
		if (interactive && !strcmp(l, e->eol)) { release(e, l); break; }
		if (grow(e, 1) < 0) { release(e, l); destroy(e); return -1; }
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
			msg(e, "t '%s'?", name);
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
		(void)msg(e, "w '%s'?", name);
		return -1;
	}
	for (size_t i = low; i < high; i++) {
		assert(e->lines[i]);
		if ((r = fprintf(file, "%s%s", e->lines[i], e->line_ending)) < 0)
			break;
	}
	if (fclose(file) < 0)
		return -1;
	if (e->verbose)
		if (msg(e, "w '%s'%c", name, r < 0 ? '?' : ' ') < 0)
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
				if (msg(e, "%4lu: %s", (unsigned long)i, e->lines[i]) < 0)
					return -1;
			e->pos = i;
			return 0;
		}
	e->pos = e->count;
	return 0;
}

#if 0
static int replace(edit_t *e, size_t from, size_t to, const char *match, const char *repl) {
	assert(e);
	if (!match || !repl || low > high || high > e->count)
		return question(e);
	for (size_t i = 0; i < high; i++) {
		char *m = strstr(e->lines[i], match);
		if (m) {
			// TODO: Cut match, if repl > match { grow }, redo search from end of new replacement
		}
	}
	return 0;
}
#endif

static int move(edit_t *e, size_t from, size_t to, size_t count) {
	assert(e);
	if (to > e->count || from >= e->count || ((from + count) < from) || ((from + count) > e->count))
		return question(e);
	if (count == 0 || from == to)
		return 0;
	for (size_t i = 0; i < count; i++) {
		assert(to   + i < e->count);
		assert(from + i < e->count);
		char *t = e->lines[to + i];
		e->lines[to + i] = e->lines[from + i];
		e->lines[from + i] = t;
	}
	return 0;
}

static int copy(edit_t *e, size_t from, size_t to, size_t lines, size_t count) {
	assert(e);
	if (to > e->count || from >= e->count || ((from + lines) > e->count))
		return question(e);
	if (count == 0)
		return 0;
	const size_t end = e->count;
	char **s = &e->lines[end];
	if (grow(e, lines * count) < 0)
		return -1;
	assert(*s == NULL);
	for (size_t i = 0; i < count; i++) {
		for (size_t j = 0; j < lines; j++) {
			char *n = duplicate(e->lines[from + j]);
			if (!n) {
				(void)destroy(e);
				return -1;
			}
			s[(lines * i) + j] = n;
		}
	}
	if (move(e, end, to, lines * count) < 0) {
		e->fatal = 1;
		return -1;
	}
	e->pos = to + (lines * count);
	return 0;
}

static int help(edit_t *e) {
	assert(e);
static const char *help_string ="\
Program: " PROGRAM "\nVersion: " VERSION "\nLicense: " LICENSE "\nAuthor:  " AUTHOR "\n\
Email:   " EMAIL "\nRepo:    " REPO " \n\n\
[#][,#]e<>  write file and quit | q           quit\n\
[#][,#]w<>  write file          | [#][,#]l    list lines (no cursor update)\n\
[#][,#]d    delete lines        | [#]i        insert at cursor or line\n\
[#][,#]p    print lines         | a           insert at end of file\n\
h           print help          | ?           print info\n\
[#][,#]s$   search for string   | [#]t<>      transfer file into line\n\
[#]v        set verbosity level | #,[#],#m    move lines\n\
#,#,#,#c    copy lines          | [#][,#]r$   replace\n\
\n$ = string, <> = file, [] = optional, # = number. A single '.' on a new\n\
line exits insert mode.\n";
	return msg(e, help_string);
}

static int edit_command(edit_t *e, char *line) {
	assert(e);
	assert(e->cmds);
	assert(e->msgs);
	if (e->fatal)
		return -1;
	for (size_t i = 0; line[i]; i++)
		if (line[i] == '\r' || line[i] == '\n')
			line[i] = '\0';
	int argc = 0, cnt = 0, tot = 0;
	unsigned long argv[4] = { 0, };
	/* TODO: Handle '.' (current line) and '$' (last line) characters */
	for (argc = 0; argc < ((int)NELEMS(argv)) && 1 == sscanf(&line[tot], "%lu%n", &argv[argc], &cnt); argc++) {
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
	case 'q': if (argc != 0) { question(e); break; } return 1;
	case 'm': move(e, low, high, argc == 3 ? argv[2] : 1); break;
	case 'c': copy(e, low, high, argc < 3 ? 1 : argv[2], argc < 4 ? 1 : argv[3]); break;
	//case 'r': replace(e, low, high, str1,str2); break;
	case 'p': if (argc == 0) { high = e->count; } e->pos = high; /* fall-through */
	case 'l': if (argc == 0) { low = 0; high = e->count; } (void)print(e, low, high); break;
	case 'd': (void)delete(e, low, high); break;
	case 'h': if (argc != 0) { question(e); break; } help(e); break;
	case 'e': /* fall-through */
	case 'w': if (argc == 0) { low = 0; high = e->count; } save(e, str, low, high); if (ch == 'e') return 1; break;
	case 'a': if (argc != 0) { question(e); break; } low = e->count; /* fall-through */
	case 'i': e->pos = low; if (load_file(e, e->cmds, 1) < 0) return -1; break;
	case 't': if (argc > 1) { question(e); break; } e->pos = low; str = str[0] ? str : e->file_name; if (load_name(e, str, 0) < 0) { msg(e, "%s?", str); } break;
	case 's': if (argc == 0) { high = e->count; } search(e, &line[1], low, high); break;
	case '?': if (argc != 0) { question(e); break; }; msg(e, "file='%s' pos=%lu count=%lu", e->file_name, (unsigned long)e->pos, (unsigned long)e->count); break;
	case 'v': if (argc > 1) { question(e); break; } e->verbose = low; break;
	case '\0': if (argc == 1) { e->pos = low; break; /* TODO: Single line edit */ } /* fall-through */
	default: question(e);
	}
	return 0;
}

static int editor(edit_t *e) {
	assert(e);
	e->pos = 0;
	for (char line[256] = { 0, };fgets(line, sizeof (line) - 1, e->cmds);) {
		const int r = edit_command(e, line);
		if (r)
			return r;
		if (e->fatal)
			return -1;
	}
	return 0;
}

int edlin(const char *file, FILE *cmds, FILE *msgs) {
	assert(cmds);
	assert(msgs);
	edit_t e = {
		.file_name = file ? file : "", .line_ending = "\n", .eol = ".",
		.msgs = msgs, .cmds = cmds,
		.allocator = allocator, .arena = NULL,
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


