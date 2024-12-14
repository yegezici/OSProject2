#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */
#define MAX_HISTORY 10
#define MAX_BG_PROCESSES 20

void setup(char inputBuffer[], char *args[], int *background)
{
    int length, start = -1, ct = 0;
    *background = 0;

    length = read(STDIN_FILENO, inputBuffer, MAX_LINE);
    printf("inputBuffer: %s\n", inputBuffer);
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

    for(int i = 0; args[i] != NULL; i++)
    {
        printf("args[%d]: %s\n", i, args[i]);
    }
    args[ct] = NULL;
}

void addToHistory(char *args[], char historyBuffer[MAX_HISTORY][MAX_LINE])
{
    char inputBuffer[MAX_LINE] = {0};
    int index = 0;

    // Reconstruct the full command from args[]
    for (int i = 0; args[i] != NULL; i++)
    {
        // Add the argument to inputBuffer
        strcat(inputBuffer, args[i]);

        // Add a space between arguments (except for the last one)
        if (args[i + 1] != NULL)
        {
            strcat(inputBuffer, " ");
        }
    }

    // Print the command for debugging purposes
    printf("Adding to history: %s\n", inputBuffer);

    // Shift history to make space for the new command
    for (int i = MAX_HISTORY - 1; i > 0; i--)
    {
        strncpy(historyBuffer[i], historyBuffer[i - 1], MAX_LINE);
    }

    // Store the reconstructed command in historyBuffer
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
    printf("Command: %s\n", command);
    printf("fullPath: %s\n", fullPath);
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

void executePipedCommands(char *args[], char *inputBuffer)
{
    int pipefd[2];
    pid_t pid1, pid2;

    // Parse input into two commands
    char *cmd1[MAX_LINE / 2 + 1];
    char *cmd2[MAX_LINE / 2 + 1];
    int cmd1_len = 0, cmd2_len = 0;
    int pipeIndex = -1;

    for (int i = 0; args[i] != NULL; i++)
    {
        if (strcmp(args[i], "|") == 0)
        {
            pipeIndex = i;
            break;
        }
    }

    if (pipeIndex == -1)
    {
        fprintf(stderr, "Error: No pipe found in command.\n");
        return;
    }

    for (int i = 0; i < pipeIndex; i++)
    {
        cmd1[cmd1_len++] = args[i];
    }
    cmd1[cmd1_len] = NULL;

    for (int i = pipeIndex + 1; args[i] != NULL; i++)
    {
        cmd2[cmd2_len++] = args[i];
    }
    cmd2[cmd2_len] = NULL;

    if (pipe(pipefd) == -1)
    {
        perror("Pipe creation failed");
        return;
    }

    pid1 = fork();
    if (pid1 == 0)
    {
        // First child: Executes cmd1, writes output to pipe
        close(pipefd[0]);               // Close unused read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe write end
        close(pipefd[1]);

        char fullPath[MAX_LINE] = {0};
        findCommandPath(cmd1[0], fullPath);
        if (fullPath[0] == '\0')
        {
            fprintf(stderr, "Command not found: %s\n", cmd1[0]);
            exit(1);
        }

        if (execv(fullPath, cmd1) == -1)
        {
            perror("Command execution failed");
            exit(1);
        }
    }

    pid2 = fork();
    if (pid2 == 0)
    {
        // Second child: Reads from pipe, executes cmd2
        close(pipefd[1]);              // Close unused write end
        dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to pipe read end
        close(pipefd[0]);

        char fullPath[MAX_LINE] = {0};
        findCommandPath(cmd2[0], fullPath);
        if (fullPath[0] == '\0')
        {
            fprintf(stderr, "Command not found: %s\n", cmd2[0]);
            exit(1);
        }

        if (execv(fullPath, cmd2) == -1)
        {
            perror("Command execution failed");
            exit(1);
        }
    }

    // Parent process: Close both ends of the pipe and wait for children
    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
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

        // Pipe handling
        int hasPipe = 0;
        for (int i = 0; args[i] != NULL; i++)
        {
            if (strcmp(args[i], "|") == 0)
            {
                hasPipe = 1;
                break;
            }
        }

        if (hasPipe)
        {
            executePipedCommands(args, inputBuffer);
            continue;
        }

        // Handle history command
        if (strcmp(args[0], "history") == 0)
        {
            if (args[1] == NULL)
            {
                // Print history
                printHistory(historyBuffer);
            }
            else if (args[1][0] == '-')
            {
                int index = atoi(&args[1][1]);

                // Validate index
                if (index >= 0 && index < MAX_HISTORY && historyBuffer[index][0] != '\0')
                {
                    char historyLine[MAX_LINE];

                    // Copy the command from history to inputBuffer
                    strncpy(historyLine, historyBuffer[index], MAX_LINE);
                    historyLine[MAX_LINE - 1] = '\0'; // Null-terminate

                    printf("Executing from history: %s\n", historyLine);

                    // Manually parse the command into args[] (similar to setup() function)
                    int background = 0;
                    int ct = 0, start = -1;

                    // Parse inputBuffer manually, just like the setup function
                    for (int i = 0; historyLine[i] != '\0'; i++)
                    {
                        switch (historyLine[i])
                        {
                        case '-':
                        case ' ':
                        case '\t':
                        case '%':
                            if (start != -1)
                            {
                                historyLine[i] = '\0'; // End the argument
                                args[ct++] = &historyLine[start];
                                start = -1;
                            }
                            break;
                        case '\n':
                            if (start != -1)
                            {
                                historyLine[i] = '\0'; // End the argument
                                args[ct++] = &historyLine[start];
                            }
                            args[ct] = NULL;
                            break;
                        case '&':
                            background = 1;
                            historyLine[i] = '\0'; // Remove '&' from inputBuffer
                            break;
                        default:
                            if (start == -1)
                                start = i;
                        }
                    }

                    // Ensure the last argument is null-terminated
                    if (args[ct] != NULL)
                        args[ct] = NULL;

                    // If no valid command, continue
                    if (args[1] == NULL)
                    {
                        printf("3131Error: Invalid command in history.\n");
                        continue;
                    }

                    // Fork and execute the command from history
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
                        printf("Command:args    %s\n", args[0]);
                        findCommandPath(args[0], fullPath);

                        if (fullPath[0] == '\0')
                        {
                            fprintf(stderr, "Co13123mmand not found: %s\n", args[0]);
                            exit(1);
                        }
                        printf("Executing from history: %s\n", fullPath);

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
                                printf("Maximum background processes reached.\n");
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

        // Handle fg command
        if (strcmp(args[0], "fg") == 0)
        {
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

        // Add to history
        addToHistory(args, historyBuffer);

        // Execute command
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
                    printf("Maximum background processes reached.\n");
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
