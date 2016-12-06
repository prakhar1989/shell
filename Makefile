EXECUTABLE=w4118_sh

all: shell

shell: shell.c
	gcc -g -o $(EXECUTABLE) shell.c -Wall -W

clean:
	rm $(EXECUTABLE)

test: tester.py
	python tester.py

.PHONY: valgrind
valgrind: shell
	valgrind --leak-check=full --show-leak-kinds=all ./$(EXECUTABLE)

.PHONY: lint
lint: shell.c
	checkpatch --no-tree --terse --file shell.c
	checkpatch --no-tree --terse --file shell.h

.PHONY: lint-verbose
lint-verbose: shell.c
	checkpatch --no-tree --file shell.c
	checkpatch --no-tree --file shell.h
