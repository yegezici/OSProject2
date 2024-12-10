#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */
#define MAX_HISTORY 10
#define MAX_BG_PROCESSES 20

void setup(char inputBuffer[], char *args[], int *background) {
    int length, start = -1, ct = 0;
    *background = 0;

    length = read(STDIN_FILENO, inputBuffer, MAX_LINE);

    if (length == 0) exit(0); // End of user input (Ctrl+D)
    if (length < 0 && errno != EINTR) {
        perror("Error reading the command");
        exit(-1);
    }

    for (int i = 0; i < length; i++) {
        switch (inputBuffer[i]) {
            case ' ':
            case '\t':
                if (start != -1) {
                    args[ct++] = &inputBuffer[start];
                    inputBuffer[i] = '\0';
                    start = -1;
                }
                break;
            case '\n':
                if (start != -1) {
                    args[ct++] = &inputBuffer[start];
                }
                inputBuffer[i] = '\0';
                args[ct] = NULL;
                break;
            case '&':
                *background = 1;
                inputBuffer[i] = '\0';
                break;
            default:
                if (start == -1) start = i;
        }
    }
    args[ct] = NULL;
}

void addToHistory(char inputBuffer[], char historyBuffer[MAX_HISTORY][MAX_LINE]) {
    for (int i = MAX_HISTORY - 1; i > 0; i--) {
        strncpy(historyBuffer[i], historyBuffer[i - 1], MAX_LINE);
    }
    strncpy(historyBuffer[0], inputBuffer, MAX_LINE);
}

void printHistory(char historyBuffer[MAX_HISTORY][MAX_LINE]) {
    for (int i = 0; i < MAX_HISTORY; i++) {
        if (historyBuffer[i][0] != '\0') {
            printf("%d. %s\n", i, historyBuffer[i]);
        }
    }
}

void moveToForeground(pid_t pid, pid_t bgProcesses[MAX_BG_PROCESSES], int *bgCount) {
    int found = 0;
    for (int i = 0; i < *bgCount; i++) {
        if (bgProcesses[i] == pid) {
            found = 1;
            waitpid(pid, NULL, 0); // Move to foreground and wait for it to finish
            for (int j = i; j < *bgCount - 1; j++) {
                bgProcesses[j] = bgProcesses[j + 1];
            }
            (*bgCount)--;
            break;
        }
    }
    if (!found) {
        printf("Process with PID %d not found in background processes.\n", pid);
    }
}

int main(void) {
    char inputBuffer[MAX_LINE];
    char historyBuffer[MAX_HISTORY][MAX_LINE] = {0};
    int background;
    char *args[MAX_LINE / 2 + 1];
    pid_t bgProcesses[MAX_BG_PROCESSES];
    int bgCount = 0;

    while (1) {
        printf("myshell: ");
        fflush(stdout);

        setup(inputBuffer, args, &background);
        if (args[0] == NULL) continue; // Ignore empty input

        if (strcmp(args[0], "history") == 0) {
            if (args[1] == NULL) {
                printHistory(historyBuffer);
            } else {
                int index = atoi(args[1]);
                if (index >= 0 && index < MAX_HISTORY && historyBuffer[index][0] != '\0') {
                    strncpy(inputBuffer, historyBuffer[index], MAX_LINE);
                    setup(inputBuffer, args, &background);
                } else {
                    printf("Invalid history index.\n");
                }
            }
            continue;
        }

        if (strcmp(args[0], "fg") == 0) {
            if (args[1] != NULL) {
                pid_t pid = atoi(args[1]);
                moveToForeground(pid, bgProcesses, &bgCount);
            } else {
                printf("Usage: fg <pid>\n");
            }
            continue;
        }

        addToHistory(inputBuffer, historyBuffer);

        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            continue;
        }

        if (pid == 0) {
            // Child process
            if (execvp(args[0], args) == -1) {
                perror("Command execution failed");
                exit(1);
            }
        } else {
            // Parent process
            if (background) {
                if (bgCount < MAX_BG_PROCESSES) {
                    bgProcesses[bgCount++] = pid;
                    printf("Process %d running in background\n", pid);
                } else {
                    printf("Maximum background process limit reached.\n");
                }
            } else {
                waitpid(pid, NULL, 0);
            }
        }
    }
    return 0;
}
