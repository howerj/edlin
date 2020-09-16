% edlin(1) | Line Editor / EDLIN clone

# NAME

EDLIN - A line editor

# SYNOPSYS

edlin files...

edlin < script.txt

# DESCRIPTION

*  Project:   Edlin Clone, a line editor
*  Author:    Richard James Howe
*  Copyright: 2020 Richard James Howe
*  License:   The Unlicense
*  Email:     howe.r.j.89@gmail.com
*  Website:   <https://github.com/howerj/edlin>

A Line Editor inspired by [edlin](https://en.wikipedia.org/wiki/Edlin), which
is like a simplified version of [ed](https://en.wikipedia.org/wiki/Ed_%28text_editor%29).

The idea of this text editor is to use it on systems that behave a little like
MS-DOS did, so a microcontroller with a command line and a file system. It is
very niche and I do not expect it to be useful to anyone bar myself. The only
dependencies should be on the standard C library, it should be fairly easy to
port.

# EXAMPLE

An example creating a new file called 'hello.c' containing the standard C
program:

	$ ./edlin # or 'edlin.exe' for Windows users
	i
	#include <stdio.h>

	int main(void) {
		printf("Hello, World\n");
		return 0;
	}
	.
	whello.c
	q

Have fun.

# COMMANDS

The editor starts up in command mode, commands 'i' and 'a' switch the
interpreter into insert mode where text can be entered, a single '.' on a new
line can exit insert mode back into command mode.

The commands are, where '#' is a number, <> is a file name, $ is a string, \[\]
is an optional argument. If a file name is not provided the file opened up for
editing is used. Ranges behave differently depending on the command and which
optional numbers are provided. A file pointer exists which is used and updated
depending on the command and optional numbers provided.

For numbers, the character '.' can be used for the current line number and '$' 
can be used to mean the end of the file. For example '.,$d' would delete 
everything from the current line up until the end of the file.

* q

Quit the editor without saving.

* \[#\]\[,#\]w<>

Write a range to a file.

* \[#\]\[,#\]e<>

Save a range to a file then exit.

* \[#\]\[,#\]l

List a range, without updating the file pointer. A '\*' marks the current
position of the file pointer.

* \[#\]\[,#\]p

List a range, updating the file pointer to the end of the range. A '\*' 
marks the current position of the file pointer.

* \[#\]\[,#\]d

Delete a range of lines. If no range is provided the current line the file
pointer points to is deleted.

* \[#\]i

Enter insert mode at a line if provided, at the file position if one is not.
This command updates the file position to end of the last inserted line. To
exit insert mode type a single '.' character on an empty line.

* a

Enter insert at the end of the file. This command updates the file position 
to end of the last inserted line. To exit insert mode type a single '.' 
character on an empty line.

* h

Print out the help message. This is all.

* ?

Print information about the currently opened file.

* \[#\]\[,#\]s$

Search a range for a string.

* \[#\]t<>

Transfer a file into the currently opened file at the line specified, if no
line is specified transfer to the file pointer.

* \[#\]v

Set the editor verbosity to the number provided, higher numbers mean more
messages and noise.

* #,\[#\],#m

Move a range of lines from one position to another.

* #,#,#,#c

Copy a range of lines.

* \[#\]\[,#\]r$

Replace a range of lines.

* #

Edit a single line, updating the file position to the line after the one edited once
editing is done. This does use '.' as an escape character, so if you need to insert
a single '.' on a line then this is one way to do it.

# KNOWN LIMITATIONS

* The file that is being edited is loaded into RAM so files cannot be bigger
than your memory.
* 'unsigned long' is used to keep track of line numbers, and 'int' for line
line lengths. The minimum values for these are 2^32 - 1 and 65535 
respectively, so that limits the maximum line count and maximum line length
to those values.

# BUGS

Yes.

# COPYRIGHT

The project is released under the 'Unlicense', which puts the project into the
public domain. Do what thou wilt!

