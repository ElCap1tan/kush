// kush - The knowable unix shell
// Copyright (C) 2023  Yannic Wehner
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <pwd.h>
#include <string.h>

#define LOGO_ART    "Welcome to\n" \
                    " _              _     \n" \
                    "| |            | |    \n" \
                    "| | ___   _ ___| |__  \n" \
                    "| |/ / | | / __| '_ \\ \n" \
                    "|   <| |_| \\__ \\ | | |\n" \
                    "|_|\\_\\\\__,_|___/_| |_|\n" \
                    "       by Yannic Wehner\n"

#define KUSH_PROMPT "[%s@%s:%s]> "
#define KUSH_TOK_BUFF_SIZE 64
#define KUSH_TOK_DELIM " \t\r\n\a"

int printed_prompt = 0;
int prog_running = 0;

void kush_print_prompt() {
    int errcode;
    char hostname[HOST_NAME_MAX + 1];
    struct passwd *p = NULL;
    char *username = NULL;
    char cwd[PATH_MAX + 1];

    if (!printed_prompt) {
        if (getcwd(cwd, sizeof(cwd))) {
            // Get username
            p = getpwuid(geteuid());
            if (p == NULL) username = "UNKNOWN";
            else username = p->pw_name;

            // Get hostname
            errcode = gethostname(hostname, sizeof(hostname));
            if (errcode > 0) strcpy(hostname, "UNKNOWN");

            printf(KUSH_PROMPT, username, hostname, cwd);
            fflush(stdout);
        } else exit(EXIT_FAILURE);
    }
}

void sig_handler(int signum) {
    if (signum == SIGINT) {

        if (!prog_running) puts("\nTo exit kush type 'exit'.");
        else puts("");

        printed_prompt = 0;
        kush_print_prompt();
        printed_prompt = 1;
    }
}

char *kush_read_line() {
    char *line = NULL;
    size_t buff_size = 0; // getline will allocate buffer accordingly

    if (getline(&line, &buff_size, stdin) == -1) {
        free(line);
        if (feof(stdin)) exit(EXIT_SUCCESS);
        perror("kush: Error reading line");
        exit(EXIT_FAILURE);
    }

    printed_prompt = 0;
    return line;
}

char **kush_tokenize(char *user_in) {
    int buff_size = KUSH_TOK_BUFF_SIZE;
    int pos = 0;
    char **tokens = malloc(buff_size * sizeof(char *));
    char *token;

    if (!tokens) {
        fprintf(stderr, "kush: Token allocation error");
        exit(EXIT_FAILURE);
    }

    token = strtok(user_in, KUSH_TOK_DELIM);
    while (token != NULL) {
        if (strncmp(token, "\"", 1) == 0 || strncmp(token, "'", 1) == 0) {
            char quote_type = token[0];
            char *start = token + 1;
            while (token != NULL
                   && (
                           (quote_type == '"' && strcmp(&token[strlen(token) - 1], "\"") != 0)
                           ||
                           (quote_type == '\'' && strcmp(&token[strlen(token) - 1], "'") != 0)
                   )) {
                token[strlen(token)] = ' ';
                token = strtok(NULL, KUSH_TOK_DELIM);
            }

            unsigned long last = strlen(start) - 1;
            if (quote_type == '"' && strcmp(&start[last], "\"") == 0
                || quote_type == '\'' && strcmp(&start[last], "'") == 0) {
                start[last] = '\0';
                tokens[pos] = start;
            } else {
                fprintf(stderr, "kush: Missing closing ");
                if (quote_type == '"') fprintf(stderr, "'\"'. Input invalid.\n");
                else if (quote_type == '\'') fprintf(stderr, "\"'\". Input invalid.\n");
                return NULL;
            }
        } else {
            tokens[pos] = token;
        }

        pos++;

        if (pos >= buff_size) {
            buff_size += KUSH_TOK_BUFF_SIZE;
            tokens = realloc(tokens, buff_size * sizeof(char *)); // NOLINT(bugprone-suspicious-realloc-usage)
            if (!tokens) {
                fprintf(stderr, "kush: Token allocation error");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, KUSH_TOK_DELIM);
    }

    tokens[pos] = NULL;
    return tokens;
}

// Built-in function definitions
int kush_exit(char **args);

int kush_cd(char **args);

int kush_help(char **args);

// Built-in function commands list
char *builtin_cmds[] = {
        "exit",
        "cd",
        "help"
};

// List of corresponding functions
int (*builtin_func[])(char **) = {
        &kush_exit,
        &kush_cd,
        &kush_help
};

// Function that returns the number of builtin functions
int kush_num_builtins() {
    return sizeof(builtin_cmds) / sizeof(char *);
}

// Built-in function implementations
// -----------------------------------------------------------------------------------------
int kush_exit(char **args) {
    return 1;
}

int kush_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "Expected argument to `cd` command");
    } else {
        if (chdir(args[1]) != 0) {
            perror("kush: Failed to change directory");
        }
    }

    return 0;
}

int kush_help(char **args) {
    puts(LOGO_ART);
    puts("Type the program name and arguments and hit enter to start a program.\n");
    puts("The following built-in commands are supported:");
    for (int i = 0; i < kush_num_builtins(); ++i) {
        printf("- %s\n", builtin_cmds[i]);
    }
    puts("");

    return 0;
}
// -----------------------------------------------------------------------------------------

int kush_exec(char **args) {
    pid_t pid;
    int status;

    pid = fork();

    if (pid == 0) {
        execvp(args[0], args);
        // execvp will only return on error so if we get here we print the error message and exit the child process
        perror("kush: Error executing the desired program");
        exit(EXIT_FAILURE);
    } else if (pid < 0) perror("kush: Error forking a child process");
    else {
        prog_running = 1;
        waitpid(pid, &status, 0);
        prog_running = 0;
    }

    return 0;
}

int kush_run(char **args) {
    if (args[0] == NULL) return 0;

    for (int i = 0; i < kush_num_builtins(); i++) {
        if (strcmp(args[0], builtin_cmds[i]) == 0) return (*builtin_func[i])(args);
    }

    return kush_exec(args);
}

void kush_loop() {
    int exit;
    char *user_in = NULL;
    char **tokens = NULL;

    do {
        kush_print_prompt();

        user_in = kush_read_line();
        tokens = kush_tokenize(user_in);

        if (tokens == NULL) continue;

        exit = kush_run(tokens);

        free(user_in);
        free(tokens);
    } while (!exit);
}

int main() {
    signal(SIGINT, sig_handler);
    kush_help(NULL);
    kush_loop();
    return EXIT_SUCCESS;
}
