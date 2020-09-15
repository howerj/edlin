/* Project: Text editor library inspired by edlin.
 * License: The Unlicense
 * Author:  Richard James Howe
 * Email:   howe.r.j.89@gmail.com
 * Repo:    https://github.com/howerj/edlin */
#ifndef EDLIN_H
#define EDLIN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#ifndef ALLOCATOR_FN
#define ALLOCATOR_FN
typedef void *(*allocator_fn)(void *arena, void *ptr, size_t oldsz, size_t newsz);
#endif

int edlin(const char *file, FILE *cmds, FILE *msgs);

#ifdef __cplusplus
}
#endif
#endif
