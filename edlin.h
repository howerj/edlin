/* Project: Text editor library inspired by edlin.
 * License: The Unlicense
 * Author:  Richard James Howe
 * Repo:    <https://github.com/howerj/edlin> */

#ifndef EDLIN_H
#define EDLIN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

int edlin(const char *file, FILE *cmds, FILE *msgs);

#ifdef __cplusplus
}
#endif
#endif
