CFLAGS=-std=c99 -Wall -Wextra -pedantic -O2
TARGET=edlin

all: ${TARGET}

run: ${TARGET}
	./${TARGET} 1.txt

clean:
	git clean -dffx

