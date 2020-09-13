#include "edlin.h"

int main(int argc, char **argv) {
	if (argc <= 1)
		return !!edlin(NULL, stdin, stdout);
	for (int i = 1; i < argc; i++)
		if (edlin(argv[i], stdin, stdout) < 0)
			return 1;
	return 0;
}

