VERSION=0.9.3
CFLAGS=-std=c99 -Wall -Wextra -pedantic -Os -g -DVERSION="\"${VERSION}\""
TARGET=edlin
DESTDIR=install

ifeq ($(OS),Windows_NT)
EXE=.exe
DLL=dll
else # Assume Unixen
EXE=
DLL=so
endif

.PHONY: all run test dist install clean

all: ${TARGET}

run: ${TARGET}
	./${TARGET} 1.txt

${TARGET}.o: ${TARGET}.c ${TARGET}.h

lib${TARGET}.a: ${TARGET}.o
	ar rcs $@ $<

${TARGET}: main.o lib${TARGET}.a	
	${CC} ${CFLAGS} $^ -o $@
	-strip ${TARGET}${EXE}

test: t ${TARGET}${EXE}
	./t

${TARGET}.1: readme.md
	-pandoc -s -f markdown -t man $< -o $@

install: ${TARGET}${EXE} lib${TARGET}.a ${TARGET}.1 .git
	install -p -D ${TARGET}${EXE} ${DESTDIR}/bin/${TARGET}${EXE}
	install -p -m 644 -D lib${TARGET}.a ${DESTDIR}/lib/lib${TARGET}.a
	install -p -m 644 -D ${TARGET}.h ${DESTDIR}/include/${TARGET}.h
	-install -p -m 644 -D ${TARGET}.1 ${DESTDIR}/man/${TARGET}.1
	mkdir -p ${DESTDIR}/src
	cp -a .git ${DESTDIR}/src
	cd ${DESTDIR}/src && git reset --hard HEAD

dist: install
	tar zcf ${TARGET}-${VERSION}.tgz ${DESTDIR}

install:

clean:
	git clean -dffx

