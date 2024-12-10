#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 80 /* Maximum command line length */
#define HISTORY_SIZE 10 /* Command history size */

/* Parses the input into arguments */
void setup(char inputBuffer[], char *args[], int *background) {
    int length, start = -1, ct = 0;

    *background = 0; /* Reset background flag */

    /* Read input from the user */
    length = read(STDIN_FILENO, inputBuffer, MAX_LINE);

    if (length == 0) exit(0); /* Exit on Ctrl+D */

    if ((length < 0) && (errno != EINTR)) {
        perror("Error reading the command");
        exit(-1);
    }

    /* Parse input into arguments */
    for (int i = 0; i < length; i++) {
        switch (inputBuffer[i]) {
            case ' ':
            case '\t':
                if (start != -1) {
                    args[ct++] = &inputBuffer[start];
                    inputBuffer[i] = '\0'; /* Null-terminate the string */
                    start = -1;
                }
                break;

            case '\n':
                if (start != -1) {
                    args[ct++] = &inputBuffer[start];
                }
                inputBuffer[i] = '\0';
                args[ct] = NULL; /* Null-terminate the args array */
                break;

            default:
                if (start == -1) start = i;
                if (inputBuffer[i] == '&') {
                    *background = 1; /* Set background flag */
                    inputBuffer[i] = '\0';
                }
                break;
        }
    }

    args[ct] = NULL;

    if (ct == 0) args[0] = NULL; /* Handle empty input */
}

/* Adds a command to the history */
void addHistory(char inputBuffer[], char historyBuffer[HISTORY_SIZE][MAX_LINE]) {
    if (strlen(inputBuffer) == 0) return; /* Ignore empty commands */

    /* Shift history entries */
    for (int i = HISTORY_SIZE - 1; i > 0; i--) {
        strncpy(historyBuffer[i], historyBuffer[i - 1], MAX_LINE);
    }

    /* Add new command to history */
    strncpy(historyBuffer[0], inputBuffer, MAX_LINE);
}

/* Prints the command history */
void printHistory(char historyBuffer[HISTORY_SIZE][MAX_LINE]) {
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (strlen(historyBuffer[i]) > 0) {
            printf("%d. %s\n", i, historyBuffer[i]);
        }
    }
}

int main(void) {
    char inputBuffer[MAX_LINE]; /* Buffer for the command */
    char historyBuffer[HISTORY_SIZE][MAX_LINE] = {0}; /* Command history */
    char *args[MAX_LINE / 2 + 1]; /* Argument array */
    int background; /* Background process flag */

    while (1) {
        background = 0;

        printf("myshell: ");
        fflush(stdout);

        /* Parse input */
        setup(inputBuffer, args, &background);

        if (args[0] == NULL) continue; /* Ignore empty commands */

        /* Add command to history */
        addHistory(inputBuffer, historyBuffer);

        /* Handle "history" command */
        if (strcmp(args[0], "history") == 0) {
            printHistory(historyBuffer);
            continue;
        }

        /* Handle "history [index]" command */
        if (strcmp(args[0], "!" ) == 0 && args[1] != NULL) {
            int index = atoi(args[1]);

            if (index >= 0 && index < HISTORY_SIZE && strlen(historyBuffer[index]) > 0) {
                /* Re-run the command at the specified index */
                strncpy(inputBuffer, historyBuffer[index], MAX_LINE);
                setup(inputBuffer, args, &background);
            } else {
                printf("Invalid history index!\n");
                continue;
            }
        }

        /* Fork and execute the command */
        pid_t pid = fork();

        if (pid < 0) {
            perror("Fork failed");
        } else if (pid == 0) {
            /* Child process */
            execvp(args[0], args);
            perror("Command not found"); /* If execvp fails */
            exit(1);
        } else {
            /* Parent process */
            if (!background) {
                waitpid(pid, NULL, 0); /* Wait for the child to complete */
            }
        }
    }

    return 0;
}
