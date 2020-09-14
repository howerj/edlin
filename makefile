CFLAGS=-std=c99 -Wall -Wextra -pedantic -Os
TARGET=edlin

all: ${TARGET}

run: ${TARGET}
	./${TARGET} 1.txt

${TARGET}.o: ${TARGET}.c ${TARGET}.h

lib${TARGET}.a: ${TARGET}.o
	ar rcs $@ $<

${TARGET}: main.o lib${TARGET}.a
	${CC} ${CFLAGS} $^ -o $@
	-strip ${TARGET}

test: t ${TARGET}
	./t

clean:
	git clean -dffx

