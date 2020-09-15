#!/bin/sh
# TODO: test more commands once line numbers are fixed
set -eux;
TRC=${TRC:-}
ED=edlin
make ${ED};

F1=$(cat <<EOF
#include <stdio.h>

int main(void) {
	puts("Hello, World!");
	return 0;
}
EOF
);

${TRC} ./${ED} <<EOF
a
${F1}
.
e1.txt
EOF

echo "${F1}" > 0.txt
diff -w 0.txt 1.txt 
rm -v 0.txt 1.txt


