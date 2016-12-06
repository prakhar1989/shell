#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include "shell.h"

/* Shared global variables */
static char **history;             /* array of strings for storing history */
static int history_len;            /* current number of items in history */
static char *input;		   /* input entered by the user */

/* useful for keeping track of parent's prev call for cleanup */
static struct command *parent_cmd;
static struct commands *parent_cmds;
static char *temp_line;


/* returns whether a command is a history command */
int is_history_command(char *input)
{
	const char *key = "history";

	if (strlen(input) < strlen(key))
		return 0;
	int i;

	for (i = 0; i < (int) strlen(key); i++) {
		if (input[i] != key[i])
			return 0;
	}
	return 1;
}


int is_blank(char *input)
{
	int n = (int) strlen(input);
	int i;

	for (i = 0; i < n; i++) {
		if (!isspace(input[i]))
			return 0;
	}
	return 1;
}


/* Parses a single command into a command struct.
 * Allocates memory for keeping the struct and the caller is responsible
 * for freeing up the memory
 */
struct command *parse_command(char *input)
{
	int tokenCount = 0;
	char *token;

	/* allocate memory for the cmd structure */
	struct command *cmd = calloc(sizeof(struct command) +
				     ARG_MAX_COUNT * sizeof(char *), 1);

	if (cmd == NULL) {
		fprintf(stderr, "error: memory alloc error\n");
		exit(EXIT_FAILURE);
	}

	/* get token by splitting on whitespace */
	token = strtok(input, " ");

	while (token != NULL && tokenCount < ARG_MAX_COUNT) {
		cmd->argv[tokenCount++] = token;
		token = strtok(NULL, " ");
	}
	cmd->name = cmd->argv[0];
	cmd->argc = tokenCount;
	return cmd;
}


/* Parses a command with pipes into a commands* structure.
 * Allocates memory for keeping the struct and the caller is responsible
 * for freeing up the memory
 */
struct commands *parse_commands_with_pipes(char *input)
{
	int commandCount = 0;
	int i = 0;
	char *token;
	char *saveptr;
	char *c = input;
	struct commands *cmds;

	while (*c != '\0') {
		if (*c == '|')
			commandCount++;
		c++;
	}

	commandCount++;

	cmds = calloc(sizeof(struct commands) +
		      commandCount * sizeof(struct command *), 1);

	if (cmds == NULL) {
		fprintf(stderr, "error: memory alloc error\n");
		exit(EXIT_FAILURE);
	}

	token = strtok_r(input, "|", &saveptr);
	while (token != NULL && i < commandCount) {
		cmds->cmds[i++] = parse_command(token);
		token = strtok_r(NULL, "|", &saveptr);
	}

	cmds->cmd_count = commandCount;
	return cmds;
}


/* Returns whether a command is a built-in. As of now
 * one of [exit, cd, history]
 */
int check_built_in(struct command *cmd)
{
	return strcmp(cmd->name, "exit") == 0 ||
		strcmp(cmd->name, "cd") == 0 ||
		strcmp(cmd->name, "history") == 0;
}


/* Responsible for handling the history shell builtin */
int handle_history(struct commands *cmds, struct command *cmd)
{
	/* just `history` executed? print history */
	if (cmd->argc == 1) {
		int i;

		for (i = 0; i < history_len ; i++) {
			// write to a file descriptor - output_fd
			printf("%d %s\n", i, history[i]);
		}
		return 1;
	}
	if (cmd->argc > 1) {
		/* clear history */
		if (strcmp(cmd->argv[1], "-c") == 0) {
			clear_history();
			return 0;
		}

		/* exec command from history */
		char *end;
		long loffset;
		int offset;

		loffset = strtol(cmd->argv[1], &end, 10);
		if (end == cmd->argv[1]) {
			fprintf(stderr, "error: cannot convert to number\n");
			return 1;
		}

		offset = (int) loffset;
		if (offset > history_len) {
			fprintf(stderr, "error: offset > number of items\n");
			return 1;
		}

		/* parse execute command */
		char *line = strdup(history[offset]);

		if (line == NULL)
			return 1;

		struct commands *new_commands = parse_commands_with_pipes(line);

		/* set pointers so that these can be freed when
		 * child processes die during execution
		 */
		parent_cmd = cmd;
		temp_line = line;
		parent_cmds = cmds;

		exec_commands(new_commands);
		cleanup_commands(new_commands);
		free(line);

		/* reset */
		parent_cmd = NULL;
		temp_line = NULL;
		parent_cmds = NULL;

		return 0;
	}
	return 0;
}


/* Handles the shell built-in comamnds. Takes the input/output file descriptors
 * to execute the built-in against. Since none of the built-ins read input from
 * STDIN, only the value of output fd is to be used that too only for the
 * history command since that is the only command that writes to STDOUT.
 *
 * Returns -1 to indicate that program should exit.
 */
int handle_built_in(struct commands *cmds, struct command *cmd)
{
	int ret;

	if (strcmp(cmd->name, "exit") == 0)
		return -1;

	if (strcmp(cmd->name, "cd") == 0) {
		ret = chdir(cmd->argv[1]);
		if (ret != 0) {
			fprintf(stderr, "error: unable to change dir\n");
			return 1;
		}
		return 0;
	}
	if (strcmp(cmd->name, "history") == 0)
		return handle_history(cmds, cmd);
	return 0;
}


/* Clears the history */
int clear_history(void)
{
	int i;

	for (i = 0; i < history_len; i++)
		free(history[i]);
	history_len = 0;
	return 0;
}


/* Adds the user's input to the history. the implementation
 * is rather simplistic in that it uses memmove whenever
 * the number of items reaches max number of items.
 * For a few 100 items, this works well and easy to reason about
 */
int add_to_history(char *input)
{
	/* initialize on first call */
	if (history == NULL) {
		history = calloc(sizeof(char *) * HISTORY_MAXITEMS, 1);
		if (history == NULL) {
			fprintf(stderr, "error: memory alloc error\n");
			return 0;
		}
	}

	/* make a copy of the input */
	char *line;

	line = strdup(input);
	if (line == NULL)
		return 0;

	/* when max items have been reached, move the old
	 * contents to a previous position, and decrement len
	 */
	if (history_len == HISTORY_MAXITEMS) {
		free(history[0]);
		int space_to_move = sizeof(char *) * (HISTORY_MAXITEMS - 1);

		memmove(history, history+1, space_to_move);
		if (history == NULL) {
			fprintf(stderr, "error: memory alloc error\n");
			return 0;
		}

		history_len--;
	}

	history[history_len++] = line;
	return 1;
}


/* Executes a command by forking of a child and calling exec.
 * Causes the calling progress to halt until the child is done executing.
 */
int exec_command(struct commands *cmds, struct command *cmd, int (*pipes)[2])
{
	if (check_built_in(cmd) == 1)
		return handle_built_in(cmds, cmd);

	pid_t child_pid = fork();

	if (child_pid == -1) {
		fprintf(stderr, "error: fork error\n");
		return 0;
	}

	/* in the child */
	if (child_pid == 0) {

		int input_fd = cmd->fds[0];
		int output_fd = cmd->fds[1];

		// change input/output file descriptors if they aren't standard
		if (input_fd != -1 && input_fd != STDIN_FILENO)
			dup2(input_fd, STDIN_FILENO);

		if (output_fd != -1 && output_fd != STDOUT_FILENO)
			dup2(output_fd, STDOUT_FILENO);

		if (pipes != NULL) {
			int pipe_count = cmds->cmd_count - 1;

			close_pipes(pipes, pipe_count);
		}

		/* execute the command */
		execv(cmd->name, cmd->argv);

		/* execv returns only if an error occurs */
		fprintf(stderr, "error: %s\n", strerror(errno));

		/* cleanup in the child to avoid memory leaks */
		clear_history();
		free(history);
		free(pipes);
		free(input);
		cleanup_commands(cmds);

		if (parent_cmd != NULL) {
			free(parent_cmd);
			free(temp_line);
			free(parent_cmds);
		}


		/* exit from child so that the parent can handle the scenario*/
		_exit(EXIT_FAILURE);
	}
	/* parent continues here */
	return child_pid;
}

/* closes all the pipes */
void close_pipes(int (*pipes)[2], int pipe_count)
{
	int i;

	for (i = 0; i < pipe_count; i++) {
		close(pipes[i][0]);
		close(pipes[i][1]);
	}

}

/* Executes a set of commands that are piped together.
 * If it's a single command, it simply calls `exec_command`.
 */
int exec_commands(struct commands *cmds)
{
	int exec_ret;

	/* single command? run it */
	if (cmds->cmd_count == 1) {
		cmds->cmds[0]->fds[STDIN_FILENO] = STDIN_FILENO;
		cmds->cmds[0]->fds[STDOUT_FILENO] = STDOUT_FILENO;
		exec_ret = exec_command(cmds, cmds->cmds[0], NULL);
		wait(NULL);
	} else {
		/* execute a pipeline */
		int pipe_count = cmds->cmd_count - 1;

		/* if any command in the pipeline is a built-in, raise error */
		int i;

		for (i = 0; i < cmds->cmd_count; i++) {
			if (check_built_in(cmds->cmds[i])) {
				fprintf(stderr, "error: no builtins in pipe\n");
				return 0;
			}

		}

		/* allocate an array of pipes. Each member is array[2] */
		int (*pipes)[2] = calloc(pipe_count * sizeof(int[2]), 1);

		if (pipes == NULL) {
			fprintf(stderr, "error: memory alloc error\n");
			return 0;
		}


		/* create pipes and set file descriptors on commands */
		cmds->cmds[0]->fds[STDIN_FILENO] = STDIN_FILENO;
		for (i = 1; i < cmds->cmd_count; i++) {
			pipe(pipes[i-1]);
			cmds->cmds[i-1]->fds[STDOUT_FILENO] = pipes[i-1][1];
			cmds->cmds[i]->fds[STDIN_FILENO] = pipes[i-1][0];
		}
		cmds->cmds[pipe_count]->fds[STDOUT_FILENO] = STDOUT_FILENO;

		/* execute the commands */
		for (i = 0; i < cmds->cmd_count; i++)
			exec_ret = exec_command(cmds, cmds->cmds[i], pipes);

		close_pipes(pipes, pipe_count);

		/* wait for children to finish */
		for (i = 0; i < cmds->cmd_count; ++i)
			wait(NULL);

		free(pipes);
	}

	return exec_ret;
}


/* Frees up memory for the commands */
void cleanup_commands(struct commands *cmds)
{
	int i;

	for (i = 0; i < cmds->cmd_count; i++)
		free(cmds->cmds[i]);

	free(cmds);
}


/* cleans up history before exits */
void cleanup_and_exit(int status)
{
	clear_history();
	free(history);
	exit(status);
}


/* Returns a pointer to a input entered by user.
 * the caller is responsible for freeing up the memory
 */
char *read_input(void)
{
	int buffer_size = 2048;
	char *input = malloc(buffer_size * sizeof(char));
	int i = 0;
	char c;

	if (input == NULL) {
		fprintf(stderr, "error: malloc failed\n");
		cleanup_and_exit(EXIT_FAILURE);
	}

	while ((c = getchar()) != '\n') {
		/* did user enter ctrl+d ?*/
		if (c == EOF) {
			free(input);
			return NULL;
		}

		/* allocate more memory for input */
		if (i >= buffer_size) {
			buffer_size = 2 * buffer_size;
			input = realloc(input, buffer_size);
		}

		input[i++] = c;
	}

	input[i] = '\0';
	return input;
}


int main(void)
{
	int exec_ret;

	while (1) {
		fputs("$", stdout);

		input = read_input();

		if (input == NULL) {
			/* user entered ctrl+D, exit gracefully */
			cleanup_and_exit(EXIT_SUCCESS);
		}

		if (strlen(input) > 0 && !is_blank(input) && input[0] != '|') {
			char *linecopy = strdup(input);

			struct commands *commands =
				parse_commands_with_pipes(input);

			/* add pipeline cmds & other commands to history */
			if (commands->cmd_count > 1
			    || !is_history_command(input))
				add_to_history(linecopy);

			free(linecopy);
			exec_ret = exec_commands(commands);
			cleanup_commands(commands);
		}

		free(input);

		/* get ready to exit */
		if (exec_ret == -1)
			break;
	}

	/* perform cleanup to ensure no leaks */
	cleanup_and_exit(EXIT_SUCCESS);
	return 0;
}
