#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */
#define MAX_HISTORY 10
#define MAX_BG_PROCESSES 20

pid_t fg_pid = -1;
void handleSigTSTP(int sig)
{
    if (fg_pid != -1)
    {
        printf("\nCaught SIGTSTP (Ctrl+Z). Terminating foreground process %d and its descendants.\n", fg_pid);
        kill(fg_pid, SIGTERM); // Send SIGTERM to terminate the foreground process
    }
    else
    {
        printf("\nNo foreground process to terminate.\n");
        return;
    }
}
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
            case '|':
                if (start != -1)
                {
                    args[ct++] = &inputBuffer[start];
                    inputBuffer[i] = '\0';
                    start = -1;
                }
                args[ct++] = "|";
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

void terminateProgram(int bgCount)
{

    if (bgCount != 0)
    {
        printf("There are still background process that are still running!");
    }
    else
        exit(1);
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

    // Make a copy of PATH
    char *pathEnvCopy = strdup(pathEnv);
    if (!pathEnvCopy)
    {
        perror("Failed to duplicate PATH");
        exit(1);
    }

    char *path = strtok(pathEnvCopy, ":");
    while (path != NULL)
    {
        snprintf(fullPath, MAX_LINE, "%s/%s", path, command);
        if (access(fullPath, X_OK) == 0)
        {
            free(pathEnvCopy);
            return;
        }
        path = strtok(NULL, ":");
    }

    fullPath[0] = '\0'; // Command not found
    free(pathEnvCopy);
}
int redirect(char *args[], int background)
{
    int i = 0;
    while (args[i] != NULL)
    {
        if (strcmp("<", args[i]) == 0 || strcmp(">>", args[i]) == 0 ||
            strcmp("2>", args[i]) == 0 || strcmp(">", args[i]) == 0)
        {
            break;
        }
        i++;
    }

    if (args[i] == NULL)
    {
        return 0;
    }

    if (args[i + 1] == NULL)
    {
        fprintf(stderr, "Missing argument.\n");
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        fprintf(stderr, "Fork failed!\n");
        return -1;
    }

    if (pid == 0)
    {
        if (strcmp(">", args[i]) == 0)
        {
            int fd = open(args[i + 1], O_WRONLY | O_TRUNC | O_CREAT, 0644);
            if (fd < 0)
            {
                perror("Error opening file");
                exit(1);
            }
            args[i] = NULL;
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        else if (strcmp(">>", args[i]) == 0)
        {
            int fd = open(args[i + 1], O_WRONLY | O_APPEND | O_CREAT, 0644);
            if (fd < 0)
            {
                perror("Error opening file");
                exit(1);
            }
            args[i] = NULL;
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        else if (strcmp("2>", args[i]) == 0)
        {
            int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0)
            {
                perror("Error opening file");
                exit(1);
            }
            args[i] = NULL;
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
        else if (strcmp(args[i], "<") == 0)
        {
            // Check if there's an output redirection after input redirection
            if (args[i + 2] != NULL && strcmp(args[i + 2], ">") == 0)
            {
                if (args[i + 3] == NULL)
                {
                    fprintf(stderr, "Missing output file after '>'.\n");
                    return 0;
                }

                // Open the input file
                int fd_in = open(args[i + 1], O_RDONLY);
                if (fd_in < 0)
                {
                    perror("Error opening input file");
                    exit(1);
                }

                // Open the output file
                int fd_out = open(args[i + 3], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd_out < 0)
                {
                    perror("Error opening output file");
                    exit(1);
                }

                // Redirect input and output
                dup2(fd_in, STDIN_FILENO);
                dup2(fd_out, STDOUT_FILENO);

                // Close the file descriptors
                close(fd_in);
                close(fd_out);

                // Nullify the redirection symbols and filenames in args
                args[i] = NULL;
            }
            else
            {
                // Handle only input redirection
                if (args[i + 1] == NULL)
                {
                    fprintf(stderr, "Missing input file after '<'.\n");
                    return 0;
                }

                // Open the input file
                int fd_in = open(args[i + 1], O_RDONLY);
                if (fd_in < 0)
                {
                    perror("Error opening input file");
                    exit(1);
                }

                // Redirect input
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);

                // Nullify the redirection symbols and filenames in args
                args[i] = NULL;
            }
        }
        char fullPath[MAX_LINE] = {0};
        findCommandPath(args[0], fullPath);

        if (execv(fullPath, args) == -1)
        {
            perror("Command execution failed");
            exit(1);
        }
    }
    else
    {
        if (!background)
        {
            waitpid(pid, NULL, 0);
        }
    }
    return 1;
}
void execCommand(char *args[]);
void executePipe(char *args[])
{
    int fd[2];
    pid_t pid1, pid2;
    char *cmd1[MAX_LINE / 2 + 1];
    char *cmd2[MAX_LINE / 2 + 1];
    int i = 0, j = 0;
    char fullPath1[MAX_LINE] = {0};
    char fullPath2[MAX_LINE] = {0};

    // Split into two commands
    while (args[i] && strcmp(args[i], "|") != 0) {
        cmd1[i] = args[i];
        i++;
    }
    cmd1[i] = NULL;
    
    i++; // Skip pipe symbol
    while (args[i]) {
        cmd2[j] = args[i];
        i++;
        j++;
    }
    cmd2[j] = NULL;

    // Find full paths before forking
    findCommandPath(cmd1[0], fullPath1);
    findCommandPath(cmd2[0], fullPath2);

    if (fullPath1[0] == '\0' || fullPath2[0] == '\0') {
        fprintf(stderr, "Command not found\n");
        return;
    }

    if (pipe(fd) == -1) {
        perror("pipe");
        return;
    }

    if ((pid1 = fork()) == 0) {
        // First child
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        close(fd[1]);

        execv(fullPath1, cmd1);
        perror("exec1");
        exit(1);
    }

    if ((pid2 = fork()) == 0) {
        // Second child  
        close(fd[1]);
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);

        execv(fullPath2, cmd2);
        perror("exec2");
        exit(1);
    }

    // Parent
    close(fd[0]);
    close(fd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

void execCommand(char *args[])
{
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

int main(void)
{
    signal(SIGTSTP, handleSigTSTP);
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

        // Check for pipe in args
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
            executePipe(args);
            continue;
        }

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
                else
                {
                    printf("Invalid history index.\n");
                }
            }
            continue;
        }

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
        if (redirect(args, background))
        {
            continue;
        }
        if (strcmp(args[0], "exit") == 0)
        {
            terminateProgram(bgCount);
            continue;
        }
        if (strchr(inputBuffer, '|'))
        {
            executePipe(args);
        }
        else
        {
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
                    fg_pid = pid;
                    waitpid(pid, NULL, 0);
                    fg_pid = -1;
                }
            }
        }
    }
    return 0;
}
