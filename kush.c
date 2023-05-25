/*
 * kush - The knowable unix shell
 * Copyright (C) 2023  Yannic Wehner
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>

#define LOGO_ART    "Welcome to\n" \
                    "  _              _     \n" \
                    " | |            | |    \n" \
                    " | | ___   _ ___| |__  \n" \
                    " | |/ / | | / __| '_ \\ \n" \
                    " |   <| |_| \\__ \\ | | |\n" \
                    " |_|\\_\\\\__,_|___/_| |_|\n\n" \
                    "The knowable unix shell\n" \
                    "       by Yannic Wehner\n"

// The format string for the prompt
#define KUSH_PROMPT "[%s@%s:%s]> "
// Initial size for token buffer
#define KUSH_TOK_BUFF_SIZE 64
// Characters that will delimit one token from the next
#define KUSH_TOK_DELIM " \t\r\n\a"


// Boolean value used to look up if the prompt already has been printed
int printed_prompt = 0;
// Boolean value used to look up if a child process is currently running
int child_running = 0;

// Will try to look up the needed values like the username, system-name and working directory
// and print the prompt line on success. If the lookup of the current working directory fails,
// the program will exit with a failure exit code.
// For the username and system-name '<UNKNOWN>' will be used if the lookup fails.
void kush_print_prompt() {
    int errcode;
    char hostname[HOST_NAME_MAX + 1];
    struct passwd *p = NULL;
    char *username = NULL;
    char cwd[PATH_MAX + 1];
    char *unknown = "<UNKNOWN>"; // Default name used if username or system-name lookup fails

    if (!printed_prompt) { // If the prompt for the current iteration hasn't been printed yet...
        // Try to look up the current working directory.
        if (getcwd(cwd, sizeof(cwd))) { // If the working directory lookup was successful...
            // Get username
            p = getpwuid(geteuid());
            if (p == NULL) username = unknown;
            else username = p->pw_name;

            // Get hostname
            errcode = gethostname(hostname, sizeof(hostname));
            if (errcode > 0) strcpy(hostname, unknown);

            // Print prompt to stdout
            printf(KUSH_PROMPT, username, hostname, cwd);
            fflush(stdout);
        } else exit(EXIT_FAILURE);
    }
}

// Function to catch and handle signals
void sig_handler(int signum) {

    // Handle 'SIGINT' signal
    if (signum == SIGINT) {

        // Only show the exit text if no child process is running
        if (!child_running) puts("\nTo exit kush type 'exit'.");
        else puts("");

        // At this point a new prompt always should be printed, so we set printed_prompt to false.
        printed_prompt = 0;
        kush_print_prompt();
        // Prompt has been printed, so we set it back to true.
        printed_prompt = 1;
    }
}

// Reads a whole line from stdin into a dynamically sized buffer and returns a pointer to the buffer
char *kush_read_line() {
    char *line = NULL; // getline() will allocate buffer and set memory address accordingly
    size_t buff_size = 0; // getline() will set size accordingly

    if (getline(&line, &buff_size, stdin) == -1) { // Handle failure to read a line
        // Buffer should be freed when getline() fails
        free(line);

        // If eof was reached (for example when reading commands from a file) the read was finished successfully.
        if (feof(stdin)) exit(EXIT_SUCCESS);

        // Else the read failed, and we exit with a failure.
        perror("kush: Error reading line");
        exit(EXIT_FAILURE);
    }

    // If we have read a line from stdin we need to print a new prompt after, so we set printed_prompt to false
    printed_prompt = 0;
    return line;
}

// Splits the given string into a list of tokens. Splits on characters defined by KUSH_TOK_DELIM.
char **kush_tokenize(char *user_in) {
    int buff_size = KUSH_TOK_BUFF_SIZE; // Current buffer size
    int pos = 0; // Current token position in the token list
    char **tokens = malloc(buff_size * sizeof(char *)); // Buffer for the token list
    char *token = NULL; // Will hold the pointer to start of the current token

    if (!tokens) {
        fprintf(stderr, "kush: Token allocation error");
        exit(EXIT_FAILURE);
    }

    // Get the first token. strtok() will return a pointer to the start of the first token
    // and replaces the first occurrence of a defined delimiter with '/0' to end the string there.
    token = strtok(user_in, KUSH_TOK_DELIM);
    while (token != NULL) { // while there are new tokens...
        // Check if token starts with a singe-quote or double-quote
        if (strncmp(token, "\"", 1) == 0 || strncmp(token, "'", 1) == 0) { // If so...
            char quote_type = token[0]; // Save first character to determine quote type (single or double)
            // Advance the start of the string by one character as we no longer need the quote sign in the token string
            char *start = token + 1;
            while (token != NULL // While there are still new tokens...
                   && (
                           (quote_type == '"' && strcmp(&token[strlen(token) - 1], "\"") != 0)
                           || // and the tokens don't end with another quote character to terminate the token...
                           (quote_type == '\'' && strcmp(&token[strlen(token) - 1], "'") != 0)
                   )) {
                // Replace the string terminator with a space again to keep the token from ending at that position...
                token[strlen(token)] = ' ';
                // and try to get the next token.
                token = strtok(NULL, KUSH_TOK_DELIM);
            }

            unsigned long last = strlen(start) - 1; // Last character of the token string
            // If last character is the matching closing quote...
            if (quote_type == '"' && strcmp(&start[last], "\"") == 0
                || quote_type == '\'' && strcmp(&start[last], "'") == 0) {
                start[last] = '\0'; // Replace last space with a string terminator to let the token string end.
                tokens[pos] = start; // Add the pointer to the start of the whole token to the token list.
            } else {
                fprintf(stderr, "kush: Missing closing ");
                if (quote_type == '"') fprintf(stderr, "'\"'. Input invalid.\n");
                else if (quote_type == '\'') fprintf(stderr, "\"'\". Input invalid.\n");
                return NULL;
            }
        } else { // If token doesn't start with a quote...
            tokens[pos] = token; // Directly add its pointer to the token list.
        }

        pos++; // In any case we advance the position

        token = strtok(NULL, KUSH_TOK_DELIM); // Get the next token

        // Try to increase token list buffer size if the buffer is full and there still is a next token
        if (pos >= buff_size && token != NULL) {
            buff_size += KUSH_TOK_BUFF_SIZE;
            tokens = realloc(tokens, buff_size * sizeof(char *)); // NOLINT(bugprone-suspicious-realloc-usage)
            if (!tokens) {
                fprintf(stderr, "kush: Token allocation error");
                exit(EXIT_FAILURE);
            }
        }
    }

    tokens[pos] = NULL; // Terminate the token list with NULL.
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
    (void) args; // Suppress 'unused parameter' warning

    return 1;
}

int kush_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "kush: Expected argument to `cd` command\n");
    } else {
        if (chdir(args[1]) != 0) {
            perror("kush: Failed to change directory");
        }
    }

    return 0;
}

int kush_help(char **args) {
    (void) args; // Suppress 'unused parameter' warning

    puts(LOGO_ART);
    puts("Type the program name and arguments and hit enter to start a program.\n"
         "The usage of single-quotes and double-quotes (e.g. cd 'some dir') is supported.\n");
    puts("The following built-in commands are supported:");
    for (int i = 0; i < kush_num_builtins(); ++i) {
        printf("- %s\n", builtin_cmds[i]);
    }
    puts("");

    return 0;
}
// -----------------------------------------------------------------------------------------

// Tries to execute the first entry in args as a child process and blocks until execution is finished
int kush_exec(char **args) {
    pid_t pid;

    pid = fork(); // Forks a child process

    if (pid == 0) { // If we are in the child process...
        execvp(args[0], args); // Try to execute the given file and pass it all the other parameters.

        // execvp will only return on error so if we get here we print the error message and exit the child process
        perror("kush: Error executing the desired program");
        exit(EXIT_FAILURE);
    } else if (pid < 0) perror("kush: Error forking a child process");
    else { // If we are in the parent process...
        child_running = 1; // Set to true to indicate a child process is currently running.
        waitpid(pid, NULL, 0); // Wait for the child process to end.
        child_running = 0; // Set back to false as the child process has ended.
    }

    return 0;
}

// Tries to run the first entry in args as a build-in function and if it doesn't match any passes it on to kush_exec()
int kush_run(char **args) {
    if (args[0] == NULL) return 0;

    for (int i = 0; i < kush_num_builtins(); i++) {
        if (strcmp(args[0], builtin_cmds[i]) == 0) return (*builtin_func[i])(args);
    }

    return kush_exec(args);
}

// Main command loop for the shell
void kush_loop() {
    int exit; // Boolean value to check if the shell should exit
    char *user_in = NULL; // Raw user input
    char **tokens = NULL; // List of parsed tokens

    do {
        kush_print_prompt(); // Print the prompt

        user_in = kush_read_line(); // Get user input
        tokens = kush_tokenize(user_in); // Parse to token list

        if (tokens == NULL) continue; // If tokens is NULL a parsing error has occurred and we start over.

        exit = kush_run(tokens); // Try to run the given user command

        // Free buffers
        free(user_in);
        free(tokens);
    } while (!exit);
}

int main() {
    signal(SIGINT, sig_handler); // Binds the signal to our handler function
    kush_help(NULL); // Print help text on startup
    kush_loop();
    return EXIT_SUCCESS;
}
