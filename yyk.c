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
pid_t bgProcesses[MAX_BG_PROCESSES];
int bgCount = 0;

void handleSigTSTP(int sig);
void handleSigCHLD(int sig);
void setup(char inputBuffer[], char *args[], int *background);
void findCommandPath(const char *command, char *fullPath);
void executeFromHistory(char *historyLine, char *args[]);
void addToHistory(char *args[], char historyBuffer[MAX_HISTORY][MAX_LINE], int background);
void printHistory(char historyBuffer[MAX_HISTORY][MAX_LINE]);
void moveToForeground(pid_t pid, pid_t bgProcesses[MAX_BG_PROCESSES], int *bgCount);
void executePipedCommands(char *args[], char *inputBuffer);
void terminateProgram(int bgCount);
int redirect(char *args[], int background);
void terminateAllBackgroundProcesses();

int main(void)
{
    signal(SIGTSTP, handleSigTSTP);
    signal(SIGCHLD, handleSigCHLD);
    char inputBuffer[MAX_LINE];
    char historyBuffer[MAX_HISTORY][MAX_LINE] = {0};
    int background;
    char *args[MAX_LINE / 2 + 1];

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
            addToHistory(args, historyBuffer, background);
            executePipedCommands(args, inputBuffer);
            continue;
        }
        if (redirect(args, background))
        {
            addToHistory(args, historyBuffer, background);
            continue;
        }
        if (strcmp(args[0], "exit") == 0)
        {
            terminateProgram(bgCount);
            continue;
        }
        if (strcmp(args[0], "terminate_bg") == 0)
        {
            terminateAllBackgroundProcesses();
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
            else
            {
                if (args[1][0] != '-' || args[1][1] != 'i')
                {
                    printf("\"-i\" must be entered before the index.\n");
                    continue;
                }
                // Try to parse the index from args[1]
                int historyIndex = args[2][0] - '0';

                // Validate index range
                if (historyIndex >= 0 && historyIndex < MAX_HISTORY)
                {
                    if (historyBuffer[historyIndex][0] != '\0') // Check if the history entry is valid
                    {
                        char historyLine[MAX_LINE];
                        strncpy(historyLine, historyBuffer[historyIndex], MAX_LINE); // Copy the history command
                        historyLine[MAX_LINE - 1] = '\0';                            // Null-terminate

                        // Parse and execute the history command
                        executeFromHistory(historyLine, args);
                    }
                    else
                    {
                        printf("Error: No such history entry.\n");
                    }
                }
                else
                {
                    printf("Error: Invalid history index.\n");
                }
            }
            continue;
        }

        // Handle fg command
        if (strcmp(args[0], "fg") == 0)
        {
            if (args[1] != NULL)
            {
                if (args[1][0] != '%')
                {
                    fprintf(stderr, "USE fg WITH CORRECT SYNTAX.\n");
                    continue;
                }

                char fgIndex[MAX_LINE];
                int i;
                for (i = 0; args[1][i] != '\0'; i++)
                {
                    fgIndex[i - 1] = args[1][i];
                }
                fgIndex[i - 1] = '\0';
                pid_t pid = atoi(fgIndex);
                moveToForeground(pid, bgProcesses, &bgCount);
            }
            else
            {
                printf("Usage: fg <pid>\n");
            }
            continue;
        }

        // Add to history

        addToHistory(args, historyBuffer, background);

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
                fg_pid = pid;
                waitpid(pid, NULL, 0);
                fg_pid = -1;
            }
        }
    }

    return 0;
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
        default:
            if (start == -1)
                start = i;
        }
    }
    args[ct] = NULL;
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
void executeFromHistory(char *historyLine, char *args[])
{

    int background = 0;
    int ct = 0, start = -1;

    // Parse the historyLine into arguments
    int i;
    for (i = 0; historyLine[i] != '\0'; i++)
    {
        switch (historyLine[i])
        {
        case ' ':
        case '\t':
            if (start != -1)
            {
                args[ct++] = &historyLine[start];
                historyLine[i] = '\0'; // Null-terminate the argument
                start = -1;
            }
            break;
        case '\n':
            if (start != -1)
            {
                args[ct++] = &historyLine[start];
            }
            historyLine[i] = '\0'; // Null-terminate the argument
            args[ct] = NULL;       // Mark the end of the argument list
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

    // If no valid command, return
    if (args[0] == NULL)
    {
        printf("Error: Invalid command in history.\n");
        return;
    }
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
        executePipedCommands(args, historyLine);
        return;
    }
    // If index has a I/O operation
    if (redirect(args, background))
    {
        return;
    }

    // Fork and execute the command from history
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("Fork failed");
        return;
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
            // Background execution
            bgProcesses[bgCount++] = pid;
            printf("Process %d running in background\n", pid);
        }
        else
        {
            fg_pid = pid;
            waitpid(pid, NULL, 0); // Wait for the command to complete
            fg_pid = -1;
        }
    }
}

void addToHistory(char *args[], char historyBuffer[MAX_HISTORY][MAX_LINE], int background)
{
    char inputBuffer[MAX_LINE] = {0};
    int index = 0;

    // Reconstruct the full command from args[]
    for (int i = 0; args[i] != NULL; i++)
    {
        // Add the argument to inputBuffer
        strcat(inputBuffer, args[i]);

        // Add a space between arguments
        if (args[i] != NULL)
        {
            strcat(inputBuffer, " ");
        }
    }
    if (background)
    {
        strcat(inputBuffer, "& ");
    }
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
            fg_pid = pid;                  // Set the foreground process ID
            waitpid(pid, NULL, WUNTRACED); // Wait for it to finish or be stopped
            fg_pid = -1;                   // Reset the foreground process ID
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
void terminateProgram(int bgCount)
{
    if (bgCount != 0)
    {
        printf("There are still background processes running!\n");
        return;
    }
    else
    {
        if (fg_pid != -1)
        {
            printf("Terminating foreground process %d...\n", fg_pid);
            kill(fg_pid, SIGKILL);
        }
        printf("Exiting shell...\n");
        exit(0);
    }
}

void terminateAllBackgroundProcesses()
{
    for (int i = 0; i < bgCount; i++)
    {
        printf("Terminating background process %d...\n", bgProcesses[i]);
        kill(bgProcesses[i], SIGKILL);
    }
    bgCount = 0;
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
void handleSigCHLD(int sig)
{
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        // Remove the terminated process from bgProcesses
        for (int i = 0; i < bgCount; i++)
        {
            if (bgProcesses[i] == pid)
            {
                for (int j = i; j < bgCount - 1; j++)
                {
                    bgProcesses[j] = bgProcesses[j + 1];
                }
                bgCount--;
                break;
            }
        }
    }
}
