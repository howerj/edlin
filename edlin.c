#define PROGRAM "EDLIN clone - a line editor"
#define LICENSE "The Unlicense (public domain)"
#define AUTHOR  "Richard James Howe"
#define EMAIL   "howe.r.j.89@gmail.com"
#define REPO    "https://github.com/howerj/edlin"
#ifndef VERSION
#define VERSION "0.0.0" /* defined by build system */
#endif
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
#define DEFAULT_LINE_LENGTH (80ul)

typedef FILE *edlin_file_t;

typedef struct {
	char **lines;
	const char *file_name, *line_ending, *eol;
	size_t count, pos;                          /* line count, line cursor */
	size_t line_length_limit, line_count_limit; /* optional limits, 0 = limit disabled */
	edlin_file_t msgs, cmds;
	int fatal, verbose;
	/* --- porting code to a new platform --- */
	allocator_fn allocator; /* memory allocator used in this editor */
	void *arena;            /* memory arena (may be NULL) for allocator */
	int   (*fgetc)(edlin_file_t in);
	int   (*vfprintf)(edlin_file_t out, const char *fmt, va_list ap);
	edlin_file_t (*fopen)(const char *file, const char *mode);
	int   (*fclose)(edlin_file_t f);
} edlin_t;

static void *allocator(void *arena, void *ptr, const size_t oldsz, const size_t newsz) {
	(void)arena;
	if (newsz == 0) { free(ptr); return NULL; }
	if (newsz > oldsz) { return realloc(ptr, newsz); }
	return ptr;
}

static void *reallocator(edlin_t *e, void *ptr, size_t oldsz, size_t newsz) {
	assert(e && e->allocator);
	void *r = e->allocator(e->arena, ptr, oldsz, newsz);
	if (!r && newsz) {
		e->allocator(e->arena, ptr, oldsz, 0);
		return NULL;
	}
	return r;
}

static void release(edlin_t *e, void *ptr) {
	assert(e && e->allocator);
	(void)e->allocator(e->arena, ptr, 0, 0);
}

static char *slurp(edlin_t *e, edlin_file_t input, size_t *length) {
	assert(e);
	assert(e->fgetc);
	assert(input);
	assert(length);
	size_t sz = 0, bsz = 0;
	*length = 0;
	char *m = NULL;
	for (int ch = 0;(ch = e->fgetc(input)) != EOF;) {
		if (e->line_length_limit && bsz > e->line_length_limit) {
			release(e, m);
			return NULL;
		}
		if ((sz + 1ul) > bsz) {
			bsz += DEFAULT_LINE_LENGTH;
			char *n = reallocator(e, m, bsz - DEFAULT_LINE_LENGTH, bsz + 1);
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

static int edprint(edlin_t *e, edlin_file_t out, const char *fmt, ...) {
	assert(e);
	assert(e->msgs);
	assert(e->vfprintf);
	assert(out);
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	const int r = e->vfprintf(out, fmt, ap);
	va_end(ap);
	return r;
}

static int msg(edlin_t *e, const char *fmt, ...) {
	assert(e);
	assert(e->msgs);
	assert(e->vfprintf);
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	const int r1 = e->vfprintf(e->msgs, fmt, ap);
	va_end(ap);
	const int r2 = edprint(e, e->msgs, "%s", e->line_ending);
	e->fatal = r1 < 0 || r2 < 0;
	return -(e->fatal);
}

static int question(edlin_t *e) {
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

static int destroy(edlin_t *e) {
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

static int grow(edlin_t *e, size_t more) {
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

static int hexCharToNibble(int c) {
	c = tolower(c);
	if ('a' <= c && c <= 'f')
		return 0xa + c - 'a';
	return c - '0';
}

/* converts up to two characters and returns number of characters converted */
static int hexStr2ToInt(const char *str, int *const val) {
	assert(str);
	assert(val);
	*val = 0;
	if (!isxdigit(*str))
		return 0;
	*val = hexCharToNibble(*str++);
	if (!isxdigit(*str))
		return 1;
	*val = (*val << 4) + hexCharToNibble(*str);
	return 2;
}

static int unescape(char *r, size_t length) {
	assert(r);
	if (!length)
		return -1;
	size_t k = 0;
	for (size_t j = 0, ch = 0; (ch = r[j]) && k < length; j++, k++) {
		if (ch == '\\') {
			j++;
			switch (r[j]) {
			case '\0': return -1;
			case '\n': k--;         break; /* multi-line hack (Unix line-endings only) */
			case '\\': r[k] = '\\'; break;
			case  'a': r[k] = '\a'; break;
			case  'b': r[k] = '\b'; break;
			case  'e': r[k] = 27;   break;
			case  'f': r[k] = '\f'; break;
			case  'n': r[k] = '\n'; break;
			case  'r': r[k] = '\r'; break;
			case  't': r[k] = '\t'; break;
			case  'v': r[k] = '\v'; break;
			case  'x': {
				int val = 0;
				const int pos = hexStr2ToInt(&r[j + 1], &val);
				if (pos < 1)
					return -2;
				j += pos;
				r[k] = val;
				break;
			}
			default:
				r[k] = r[j]; break;
			}
		} else {
			r[k] = ch;
		}
	}
	r[k] = '\0';
	return k;
}

static int print(edlin_t *e, size_t low, size_t high) {
	assert(e);
	assert(e->line_ending);
	if (low > high || high > e->count)
		return question(e);
	for (size_t i = low; i < high; i++) /* NB. Might want to optionally limit line length that is printed */
		if (msg(e, "%4lu%c %s", (unsigned long)(i + 1u), i == e->pos ? '*' : ':', e->lines[i]) < 0)
			return -1;
	return 0;
}

static int delete(edlin_t *e, size_t low, size_t high) {
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

static int load_file(edlin_t *e, edlin_file_t in, int interactive, size_t max_read) {
	assert(e);
	assert(in);
	size_t length = 0, read_in = 0;
	for (char *l = NULL;;read_in++) {
		if (max_read && read_in >= max_read)
			return 0;
		if (e->verbose && interactive) { if (edprint(e, e->msgs, ":") < 0) e->fatal = 1; }
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

static int load_name(edlin_t *e, char *name, int interactive, int escape) {
	assert(e);
	assert(e->fopen);
	assert(e->fclose);
	assert(name);
	if (escape)
		unescape(name, strlen(name) + 1ul);
	edlin_file_t in = e->fopen(name, "rb");
	if (!in) {
		if (e->verbose)
			msg(e, "t '%s'?", name);
		return -1;
	}
	const int r = load_file(e, in, interactive, 0);
	if (e->fclose(in) < 0)
		return -1;
	return r;
}

static int save(edlin_t *e, const char *file_name, size_t low, size_t high) {
	assert(e);
	assert(e->fopen);
	assert(e->fclose);
	assert(file_name);
	int r = 0;
	if (low > high || high > e->count)
		return question(e);
	const char *name = file_name[0] ? file_name : e->file_name;
	edlin_file_t file = e->fopen(name, "wb");
	if (!file) {
		(void)msg(e, "w '%s'?", name);
		return -1;
	}
	for (size_t i = low; i < high; i++) {
		assert(e->lines[i]);
		if ((r = edprint(e, file, "%s%s", e->lines[i], e->line_ending)) < 0)
			break;
	}
	if (e->fclose(file) < 0)
		return -1;
	if (e->verbose)
		if (msg(e, "w '%s'%c", name, r < 0 ? '?' : ' ') < 0)
			r = -1;
	return r;
}

static int search(edlin_t *e, const char *string, size_t low, size_t high) {
	assert(e);
	assert(string);
	if (low > high || high > e->count)
		return question(e);
	for (size_t i = low; i < high; i++)
		if (strstr(e->lines[i], string)) {
			if (e->verbose)
				if (msg(e, "%4lu: %s", (unsigned long)(i + 1ul), e->lines[i]) < 0)
					return -1;
			e->pos = i;
			return 0;
		}
	e->pos = e->count;
	return 0;
}

static int replace(edlin_t *e, size_t low, size_t high, const char *match, const char *repl) {
	assert(e);
	if (!match || !repl || low > high || high > e->count)
		return question(e);
	const size_t mlen = strlen(match), rlen = strlen(repl);
	if (mlen == 0)
		return question(e);
	for (size_t i = low; i < high; i++) { /* Not terribly efficient, but it should work */
		size_t llen = strlen(e->lines[i]);
		char *l = e->lines[i], *ol = reallocator(e, NULL, 0, llen + 1ul);
		if (!ol) { e->fatal = 1; return -1; }
		size_t k = 0, ms = 0;
		for (size_t j = 0; j < llen;) { /* global replacement */
			char *m = strstr(&l[j], match);
			if (m) {
				if (ms == 0 && e->verbose)
					msg(e, "%4ld: %s", l);
				ms++;
				if (rlen > mlen) {
					ol = reallocator(e, ol, llen + ((ms - 1ul) * rlen), llen + (ms * rlen) + 1ul);
					if (!ol) { e->fatal = 1; return -1; }
				}
				memcpy(&ol[k], &l[j], m - l);
				k = m - l;
				memcpy(&ol[k], repl, rlen);
				j = ((size_t)(m - l)) + mlen;
				k += rlen;
			} else {
				ol[k++] = l[j++];
			}
		}
		ol[k] = '\0';
		release(e, l);
		e->lines[i] = ol;
	}
	return 0;
}

static int move(edlin_t *e, size_t from, size_t to, size_t count) {
	assert(e);
	if (to > e->count || from >= e->count || ((from + count) < from) || ((from + count) > e->count))
		return question(e);
	if (count == 0 || from == to)
		return 0;
	for (size_t i = 0; i < count; i++) {
		assert(to   + i < e->count);
		assert(from + i < e->count);
		char *t = e->lines[to + i];
		e->lines[to   + i] = e->lines[from + i];
		e->lines[from + i] = t;
	}
	return 0;
}

static int copy(edlin_t *e, size_t from, size_t to, size_t lines, size_t count) {
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

static int number(edlin_t *e, const char *line, unsigned long *out, int *cnt) {
	assert(e);
	assert(line);
	assert(out);
	assert(cnt);
	*out = 0;
	*cnt = 0;
	if (line[0] == '.') { *out = e->pos;   *cnt = 1; return 1; }
	if (line[0] == '$') { *out = e->count; *cnt = 1; return 1; }
	//return sscanf(line, "%lu%n", out, cnt); /* TODO: Remove dependency on sscanf */
	/* TODO: This code needs to work with '.' and '$' */
	unsigned long n1 = 0, n2 = 0;
	char ch = 0;
	const int r = sscanf(line, "%lu%n%[-+]%n%lu%n", &n1, cnt, &ch, cnt, &n2, cnt);
	if (r != 1 && r != 3)
		return -1;
	if (r == 3) {
		if (ch != '+' && ch != '-')
			return -1;
		n1 += ch == '+' ? n2 : -n2;
	}
	if (r > 0)
		*out = n1;
	return r > 0 ? 1 : -1;
}

static int help(edlin_t *e) {
	assert(e);
static const char *help_string ="\
Program: " PROGRAM "\nVersion: " VERSION "\nLicense: " LICENSE "\nAuthor:  " AUTHOR "\n\
Email:   " EMAIL "\nRepo:    " REPO " \n\n\
[#][,#]e<>  write file and quit | q           quit\n\
[#][,#]w<>  write file          | [#][,#]l    list lines (no cursor update)\n\
[#][,#]d    delete lines        | [#]i        insert at cursor or line\n\
[#][,#]p    print lines         | a           insert at end of file\n\
? OR h      print help          | @           print editor info\n\
[#][,#]s$   search for string   | [#]t<>      transfer file into line\n\
[#]v        set verbosity level | #,[#],#m    move lines\n\
[#][,#][,#][,#]c    copy lines  | [#][,#]r$   replace\n\
#           edit single line    |\n\
\n# = number ('.' for current line and '$' for end of file). $ = string,\n\
<> = file, [] = optional, A single '.' on a new line exits insert mode.\n";
	return msg(e, help_string);
}

static int edit_command(edlin_t *e, char *line) {
	assert(e);
	assert(e->cmds);
	assert(e->msgs);
	if (e->fatal)
		return -1;
	for (size_t i = 0; line[i]; i++)
		if (line[i] == '\r' || line[i] == '\n')
			line[i] = '\0';
	int argc = 0, cnt = 0, tot = 0;
	unsigned long argv[4] = { 0, }; /* parse out number arguments */
	for (argc = 0; argc < ((int)NELEMS(argv)) && 1 == number(e, &line[tot], &argv[argc], &cnt); argc++) {
		tot += cnt;
		if (line[tot] != ',') {
			argc++;
			break;
		}
		tot++;
	}
	const int ch = line[tot]; /* our command */
	char *str1 = &line[tot + (ch != '\0')], *str2 = NULL;
	unsigned long low = MAX(argv[0], 0), high = MIN(argv[1] + 1, e->count);
	if (argc == 1) { /* set defaults and min/max */
		low = MIN(e->count, MAX(low, 0));
		high = MIN(e->count, low + 1ul);
	} else if (argc == 0) {
		low = MAX(e->pos, MAX(low, 0));
		high = MIN(e->count, low + 1ul);
	}
	for (size_t i = 0; str1[i]; i++) { /* parse out string arguments */
		if (str1[i] == ',') {
			str2 = &str1[i + 1];
			str1[i] = '\0';
		}
		if (str1[i] == '\\' && str1[i + 1] == ',')
			i++;
	}
	unescape(str1, strlen(str1) + 1ul);
	if (str2)
		unescape(str2, strlen(str2) + 1ul);
	/* TODO: Line number correction is still buggy */
	low  -= !!low;
	high -= !!high;

	if (argc >= 4 && ch != 'c') { question(e); return 0; }
	if (argc >= 3 && ch != 'c' && ch != 'm') { question(e); return 0; }

	switch (ch) {
	case 'q': if (argc != 0) { question(e); break; } return 1;
	case 'm': move(e, low, high, argc == 3 ? argv[2] : 1); break;
	case 'c': copy(e, low, high, argc < 3 ? 1 : argv[2], argc < 4 ? 1 : argv[3]); break;
	case 'r': replace(e, low, high, str1, str2); break;
	case 'p': if (argc == 0) { high = e->count; } e->pos = high; /* fall-through */
	case 'l': if (argc == 0) { low = 0; high = e->count; } (void)print(e, low, high); break;
	case 'd': (void)delete(e, low, high); break;
	case '?': /* fall-through */
	case 'h': if (argc != 0) { question(e); break; } help(e); break;
	case 'e': /* fall-through */
	case 'w': if (argc == 0) { low = 0; high = e->count; } save(e, str1, low, high); if (ch == 'e') return 1; break;
	case 'a': if (argc != 0) { question(e); break; } low = e->count; /* fall-through */
	case 'i': e->pos = low; if (load_file(e, e->cmds, 1, 0) < 0) return -1; break;
	case 't': if (argc > 1) { question(e); break; } e->pos = low; str1 = str1[0] ? str1 : (char*)e->file_name; if (load_name(e, str1, 0, str1 != e->file_name) < 0) { msg(e, "%s?", str1); } break;
	case 's': if (argc == 0) { high = e->count; } search(e, &line[1], low, high); break;
	case '@': if (argc != 0) { question(e); break; }; msg(e, "file='%s' pos=%lu count=%lu", e->file_name, (unsigned long)e->pos, (unsigned long)e->count); break;
	case 'v': if (argc > 1) { question(e); break; } e->verbose = low; break;
	case '\0': if (argc == 1) { e->pos = low; if (load_file(e, e->cmds, 1, 1) < 0) return -1; (void)delete(e, e->pos, MIN(e->count, e->pos + 1ul)); break; } /* fall-through */
	default: (void)question(e);
	}
	return 0;
}

static char *get_string(edlin_t *e, char *line, size_t length, edlin_file_t in) {
	assert(e);
	assert(e->fgetc);
	assert(line);
	assert(in);
	if (length == 0ul)
		return NULL;
	memset(line, '\0', length);
	if (length == 1ul)
		return NULL;
	size_t i = 0ul;
	for (i = 0ul; i < (length - 1ul); i++) {
		const int ch = e->fgetc(in);
		if (ch == EOF || ch == '\n')
			break;
		line[i] = ch;
	}
	line[i] = '\0';
	return i ? line : NULL;
}

static int editor(edlin_t *e) {
	assert(e);
	e->pos = 0;
	for (char line[256] = { 0, }; get_string(e, line, sizeof(line), e->cmds);) {
		for (int i = 0, j = 0; line[i]; i++) {
			if (line[i] == ';') {
				line[i] = '\0';
				const int r = edit_command(e, &line[j]);
				if (r)        return r;
				if (e->fatal) return -1;
				j = i + 1;
			}
			if (line[i + 1] == '\0') {
				const int r = edit_command(e, &line[j]);
				if (r)        return r;
				if (e->fatal) return -1;
				break;
			}
			if (line[i] == '\\' && line[i + 1] == ';')
				i++;
		}
	}
	return 0;
}

int edlin(const char *file, edlin_file_t cmds, edlin_file_t msgs) {
	assert(cmds);
	assert(msgs);
	edlin_t e = {
		.file_name = file ? file : "", .line_ending = "\n", .eol = ".",
		.msgs = msgs, .cmds = cmds,
		.allocator = allocator, .arena = NULL,
		.vfprintf = vfprintf, .fgetc = fgetc, .fopen = fopen, .fclose = fclose,
	};
	if (file)
		(void)load_name(&e, (char*)file, 0, 0/*'file' string not modified if 0*/);
	if (editor(&e) < 0)
		goto fail;
	return destroy(&e);
fail:
	(void)destroy(&e);
	return -1;
}


