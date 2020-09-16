#!/bin/sh
# TODO: test more commands once line numbers are fixed
set -eux;
TRC=${TRC:-}
ED=edlin
make ${ED};

### Basic Text Editing Test ###

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

### Transfer, No Line Number ###

echo "${F1}" > 0.txt
diff -w 0.txt 1.txt 
rm -v 1.txt

echo "${F1}" >  2.txt
echo "${F1}" >> 2.txt
${TRC} ./${ED} 0.txt <<EOF
t
e3.txt
EOF
diff -w 2.txt 3.txt
rm -v 0.txt 2.txt 3.txt

### Multiple commands per line  ###

echo "${F1}" > 4.txt

${TRC} ./${ED} <<EOF
i;e5.txt
${F1}
.
EOF

diff -w 4.txt 5.txt
rm -v 4.txt 5.txt

### END ###
echo PASSED
