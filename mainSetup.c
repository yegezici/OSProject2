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

void setup(char inputBuffer[], char *args[], int *background)
{
    int length, start = -1, ct = 0;
    *background = 0;

    length = read(STDIN_FILENO, inputBuffer, MAX_LINE);

    if (length == 0)
        exit(0); // End of user input (Ctrl+D)
    if (length < 0 && errno != EINTR)
    {
        perror("Error reading the command");
        exit(-1);
    }

    for (int i = 0; i < length; i++)
    {
        switch (inputBuffer[i])
        {
        case ' ':
        case '\t':
        case '%':
            if (start != -1)
            {
                args[ct++] = &inputBuffer[start];
                inputBuffer[i] = '\0';
                start = -1;
            }
            break;
        case '\n':
            if (start != -1)
            {
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
            if (start == -1)
                start = i;
        }
    }
    args[ct] = NULL;
}

void addToHistory(char inputBuffer[], char historyBuffer[MAX_HISTORY][MAX_LINE])
{
    for (int i = MAX_HISTORY - 1; i > 0; i--)
    {
        strncpy(historyBuffer[i], historyBuffer[i - 1], MAX_LINE);
    }
    strncpy(historyBuffer[0], inputBuffer, MAX_LINE);
}

void printHistory(char historyBuffer[MAX_HISTORY][MAX_LINE])
{
    for (int i = 0; i < MAX_HISTORY; i++)
    {
        if (historyBuffer[i][0] != '\0')
        {
            printf("%d. %s\n", i, historyBuffer[i]);
        }
    }
}

void moveToForeground(pid_t pid, pid_t bgProcesses[MAX_BG_PROCESSES], int *bgCount)
{
    int found = 0;
    for (int i = 0; i < *bgCount; i++)
    {
        if (bgProcesses[i] == pid)
        {
            found = 1;
            waitpid(pid, NULL, 0); // Move to foreground and wait for it to finish
            for (int j = i; j < *bgCount - 1; j++)
            {
                bgProcesses[j] = bgProcesses[j + 1];
            }
            (*bgCount)--;
            break;
        }
    }
    if (!found)
    {
        printf("Process with PID %d not found in background processes.\n", pid);
    }
}

void findCommandPath(const char *command, char *fullPath)
{
    char *pathEnv = getenv("PATH");
    if (!pathEnv)
    {
        perror("PATH environment variable not found");
        exit(1);
    }

    char *path = strtok(pathEnv, ":");
    while (path != NULL)
    {
        snprintf(fullPath, MAX_LINE, "%s/%s", path, command);
        if (access(fullPath, X_OK) == 0)
        {
            return;
        }
        path = strtok(NULL, ":");
    }

    fullPath[0] = '\0'; // Command not found
}


int main(void)
{
    char inputBuffer[MAX_LINE];
    char historyBuffer[MAX_HISTORY][MAX_LINE] = {0};
    int background;
    char *args[MAX_LINE / 2 + 1];
    pid_t bgProcesses[MAX_BG_PROCESSES];
    int bgCount = 0;

    while (1)
    {
        printf("myshell: ");
        fflush(stdout);

        setup(inputBuffer, args, &background);
        if (args[0] == NULL)
            continue; // Ignore empty input

        if (strcmp(args[0], "history") == 0)
        {
            if (args[1] == NULL)
            {
                // Print history
                printHistory(historyBuffer);
            }
            else
            {
                // Check if the argument is a valid index (negative or not)
                int index = args[1][1] - 48;

                if (index >= 0 && index < MAX_HISTORY && historyBuffer[index][0] != '\0')
                {
                    strncpy(inputBuffer, historyBuffer[index], MAX_LINE);
                    printf("Executing command from history: %s\n", inputBuffer); // Debugging inputBuffer

                    pid_t pid = fork();
                    if (pid < 0)
                    {
                        printf("\namk\n");
                        perror("Fork failed");
                        continue;
                    }

                    printf("%d\n", 2);        // Debugging: Should print 2 before forking
                    printf("pid: %d\n", pid); // Debugging: Check pid after fork

                    if (pid == 0)
                    {
                        printf("niye girmioy");
                        // Child process
                        char fullPath[MAX_LINE] = {0};
                        findCommandPath(args[0], fullPath);

                        if (fullPath[0] == '\0')
                        {
                            fprintf(stderr, "Command not found: %s\n", args[0]);
                            exit(1);
                        }

                        if (execv(fullPath, args) == -1)
                        {
                            perror("Command execution failed");
                            exit(1);
                        }
                    }
                    else
                    {
                        // Parent process
                        if (background)
                        {
                            if (bgCount < MAX_BG_PROCESSES)
                            {
                                bgProcesses[bgCount++] = pid;
                                printf("Process %d running in background\n", pid);
                            }
                            else
                            {
                                printf("Maximum background process limit reached.\n");
                            }
                        }
                        else
                        {
                            waitpid(pid, NULL, 0);
                        }
                    }
                }
                else
                {
                    printf("Invalid history index.\n");
                }
            }
            continue;
        }

        if (strcmp(args[0], "fg") == 0)
        {
            printf("fg: %s\n", args[1]);
            if (args[1] != NULL)
            {
                pid_t pid = atoi(args[1]);
                moveToForeground(pid, bgProcesses, &bgCount);
            }
            else
            {
                printf("Usage: fg <pid>\n");
            }
            continue;
        }

        addToHistory(inputBuffer, historyBuffer);

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("Fork failed");
            continue;
        }

        if (pid == 0)
        {
            // Child process
            char fullPath[MAX_LINE] = {0};
            findCommandPath(args[0], fullPath);

            if (fullPath[0] == '\0')
            {
                fprintf(stderr, "Command not found: %s\n", args[0]);
                exit(1);
            }

            if (execv(fullPath, args) == -1)
            {
                perror("Command execution failed");
                exit(1);
            }
        }
        else
        {
            // Parent process
            if (background)
            {
                if (bgCount < MAX_BG_PROCESSES)
                {
                    bgProcesses[bgCount++] = pid;
                    printf("Process %d running in background\n", pid);
                }
                else
                {
                    printf("Maximum background process limit reached.\n");
                }
            }
            else
            {
                waitpid(pid, NULL, 0);
            }
        }
    }
    return 0;
}
