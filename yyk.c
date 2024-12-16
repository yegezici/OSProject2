#include <stdio.h>     // Standard I/O library functions
#include <unistd.h>    // UNIX standard function definitions
#include <errno.h>     // Error number definitions
#include <stdlib.h>    // Standard library definitions
#include <string.h>    // String operation functions
#include <sys/types.h> // Data types
#include <sys/wait.h>  // Declarations for waiting
#include <fcntl.h>     // File control options
#include <sys/stat.h>  // Data returned by the stat() function
#include <signal.h>    // Signal handling functions

#define MAX_LINE 128        /* Maximum characters per command line */
#define MAX_ARGS 32         /*Maximum different characters per command line*/
#define MAX_HISTORY 10      /* Maximum number of commands in history */
#define MAX_BG_PROCESSES 20 /* Maximum background processes allowed */

/* Global variables */
pid_t fg_pid = -1;                   // Foreground process ID
pid_t bgProcesses[MAX_BG_PROCESSES]; // Array of background process IDs
int bgCount = 0;                     // Count of background processes

/* Function prototypes */
void handleSigTSTP(int sig);                                                                         // Handler for SIGTSTP (Ctrl+Z)
void handleSigCHLD(int sig);                                                                         // Handler for SIGCHLD (child termination)
void setup(char inputBuffer[], char *args[], int *background);                                       // Parses input
void findCommandPath(const char *command, char *fullPath);                                           // Finds command path
void executeFromHistory(char *historyLine, char *args[], char historyBuffer[MAX_HISTORY][MAX_LINE]); // Executes history command
void addToHistory(char *args[], char historyBuffer[MAX_HISTORY][MAX_LINE], int background);          // Adds command to history
void printHistory(char historyBuffer[MAX_HISTORY][MAX_LINE]);                                        // Prints command history
void moveToForeground(pid_t pid, pid_t bgProcesses[MAX_BG_PROCESSES], int *bgCount);                 // Brings BG process to FG
void executePipedCommands(char *args[], char *inputBuffer);                                          // Executes piped commands
void terminateProgram(int bgCount);                                                                  // Exits the shell
int redirect(char *args[], int background);                                                          // Handles I/O redirection

int main(void)
{
    /* Set up signal handlers */
    signal(SIGTSTP, handleSigTSTP); // Handle Ctrl+Z
    signal(SIGCHLD, handleSigCHLD); // Handle child termination

    char inputBuffer[MAX_LINE];                      // Buffer to hold input
    char historyBuffer[MAX_HISTORY][MAX_LINE] = {0}; // Command history
    int background;                                  // Background execution flag
    char *args[MAX_LINE / 2 + 1];                    // Command arguments

    while (1)
    {
        /* Display prompt */
        printf("myshell: ");
        fflush(stdout);

        /* Parse input */
        setup(inputBuffer, args, &background);

        if (args[0] == NULL)
            continue; // Ignore empty input

        /* Check for pipes */
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
            /* Handle piped commands */
            addToHistory(args, historyBuffer, background);
            executePipedCommands(args, inputBuffer);
            continue;
        }

        /* Check for I/O redirection */
        if (redirect(args, background))
        {
            addToHistory(args, historyBuffer, background);
            continue;
        }

        /* Built-in commands */
        if (strcmp(args[0], "exit") == 0)
        {
            terminateProgram(bgCount); // Exit shell
            continue;
        }

        if (strcmp(args[0], "history") == 0)
        {
            /* Handle history command */
            if (args[1] == NULL)
            {
                printHistory(historyBuffer); // Print history
            }
            else
            {
                if (args[1][0] != '-' || args[1][1] != 'i')
                {
                    fprintf(stderr, "\"-i\" must be entered before the index.\n");
                    continue;
                }
                int historyIndex = atoi(args[2]); // Get history index
                if (historyIndex >= 0 && historyIndex < MAX_HISTORY)
                {
                    if (historyBuffer[historyIndex][0] != '\0')
                    {
                        char historyLine[MAX_LINE];
                        strncpy(historyLine, historyBuffer[historyIndex], MAX_LINE);
                        historyLine[MAX_LINE - 1] = '\0';
                        executeFromHistory(historyLine, args, historyBuffer); // Execute history command
                    }
                    else
                    {
                        fprintf(stderr, "Error: No such history entry.\n");
                    }
                }
                else
                {
                    fprintf(stderr, "Error: Invalid history index.\n");
                }
            }
            continue;
        }

        if (strcmp(args[0], "fg") == 0)
        {
            /* Bring background process to foreground */
            if (args[1] != NULL)
            {
                if (args[1][0] != '%') /* Check syntax usage */
                {
                    fprintf(stderr, "Use fg with correct syntax.\n");
                    continue;
                }
                char fgIndex[MAX_LINE];
                int i;
                for (i = 0; args[1][i] != '\0'; i++)
                {
                    fgIndex[i - 1] = args[1][i]; // Each character from args[1] is copied to fgIndex with an offset of -1
                }
                fgIndex[i - 1] = '\0';
                pid_t pid = atoi(fgIndex);                    // Convert string fgIndex to integer
                moveToForeground(pid, bgProcesses, &bgCount); // Move process to foreground
            }
            else
            {
                printf("Usage: fg <pid>\n");
            }
            continue;
        }

        /* Add command to history */
        addToHistory(args, historyBuffer, background);

        /* Fork a child process */
        pid_t pid = fork();
        if (pid < 0)
        {
            fprintf(stderr, "Fork failed");
            continue;
        }

        if (pid == 0) // Only child process runs this block
        {
            /* Child process */
            char fullPath[MAX_LINE] = {0};
            findCommandPath(args[0], fullPath); // Find command path

            if (fullPath[0] == '\0')
            {
                fprintf(stderr, "Command not found: %s\n", args[0]);
                exit(1);
            }

            if (execv(fullPath, args) == -1)
            {
                fprintf(stderr, "Command execution failed");
                exit(1);
            }
        }
        else
        {
            /* Parent process */
            if (background)
            {
                /* Run in background */
                if (bgCount < MAX_BG_PROCESSES)
                {
                    bgProcesses[bgCount++] = pid; // Store background process ID
                    printf("Process %d running in background\n", pid);
                }
                else
                {
                    fprintf(stderr, "Maximum background processes reached.\n");
                }
            }
            else
            {
                /* Run in foreground */
                fg_pid = pid;          // Set foreground process ID
                waitpid(pid, NULL, 0); // Wait for child
                fg_pid = -1;           // Reset foreground process ID
            }
        }
    }

    return 0;
}

/* Parses the input line and populates args[] with the parsed components */
void setup(char inputBuffer[], char *args[], int *background)
{
    int length, start = -1, ct = 0;
    *background = 0; // Initialize background flag to 0, foreground execution

    /* Read input */
    length = read(STDIN_FILENO, inputBuffer, MAX_LINE);
    if (length == 0)
        exit(0); // End of input (Ctrl+D)
    if (length < 0 && errno != EINTR)
    {
        fprintf(stderr, "Error reading the command");
        exit(-1);
    }

    /* Parse inputBuffer */
    for (int i = 0; i < length; i++)
    {
        switch (inputBuffer[i])
        {
        case ' ':
        case '\t':
            if (start != -1)
            {
                args[ct++] = &inputBuffer[start]; // Add argument
                inputBuffer[i] = '\0';            // Null-terminate
                start = -1;
            }
            break;
        case '\n':
            if (start != -1)
            {
                args[ct++] = &inputBuffer[start];
            }
            inputBuffer[i] = '\0';
            args[ct] = NULL; // Mark end of arguments
            break;
        case '&':
            *background = 1; // Background execution
            inputBuffer[i] = '\0';
            break;
        default:
            if (start == -1)
                start = i; // Start of new argument
        }
    }

    args[ct] = NULL; // Ensure args ends with NULL
}

/* Finds the full path of the command */
void findCommandPath(const char *command, char *fullPath)
{
    char *pathEnv = getenv("PATH"); // Retrieve PATH environment variable
    if (!pathEnv)
    {
        fprintf(stderr, "PATH environment variable not found");
        exit(1);
    }

    char *path = strtok(pathEnv, ":"); // Split PATH by ':'
    while (path != NULL)
    {
        snprintf(fullPath, MAX_LINE, "%s/%s", path, command); // Constructs a potential full path for the command by combining the current directory path and the command name.
        if (access(fullPath, X_OK) == 0)                      // X_OK flag checks if the file is executable
        {
            return; // Command found
        }
        path = strtok(NULL, ":"); // Moves to next directory path
    }

    fullPath[0] = '\0'; // Command not found
}

/* Executes a command from history */
void executeFromHistory(char *historyLine, char *args[], char historyBuffer[MAX_HISTORY][MAX_LINE])
{
    int background = 0;
    int ct = 0, start = -1;

    /* Parse historyLine */
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
                historyLine[i] = '\0'; // Null-terminate
                start = -1;
            }
            break;
        case '&':
            background = 1;
            historyLine[i] = '\0'; 
            break;
        default:
            if (start == -1)
                start = i;
        }
    }

    /* Ensure args ends with NULL */
    if (args[ct] != NULL)
        args[ct] = NULL;

    if (args[0] == NULL)
    {
        fprintf(stderr, "Error: Invalid command in history.\n");
        return;
    }

    addToHistory(args, historyBuffer, background); // Add to history

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
    if (redirect(args, background))
    {
        return;
    }

    /* Fork and execute */
    pid_t pid = fork();
    if (pid < 0)
    {
        fprintf(stderr, "Fork failed");
        return;
    }

    if (pid == 0)
    {
        /* Child process */
        char fullPath[MAX_LINE] = {0};
        findCommandPath(args[0], fullPath);

        if (fullPath[0] == '\0')
        {
            fprintf(stderr, "Command not found: %s\n", args[0]);
            exit(1);
        }

        if (execv(fullPath, args) == -1)
        {
            fprintf(stderr, "Command execution failed");
            exit(1);
        }
    }
    else
    {
        /* Parent process */
        if (background)
        {
            bgProcesses[bgCount++] = pid;
            printf("Process %d running in background\n", pid);
        }
        else
        {
            fg_pid = pid;
            waitpid(pid, NULL, 0); // Wait for child
            fg_pid = -1;
        }
    }
}

/* Adds a command to the history buffer */
void addToHistory(char *args[], char historyBuffer[MAX_HISTORY][MAX_LINE], int background)
{
    char inputBuffer[MAX_LINE] = {0};

    /* Reconstruct the command */
    for (int i = 0; args[i] != NULL; i++)
    {
        strcat(inputBuffer, args[i]);
        if (args[i + 1] != NULL)
        {
            strcat(inputBuffer, " ");
        }
    }
    if (background)
    {
        strcat(inputBuffer, " &");
    }

    /* Shift history */
    for (int i = MAX_HISTORY - 1; i > 0; i--)
    {
        strncpy(historyBuffer[i], historyBuffer[i - 1], MAX_LINE);
    }

    /* Add new command */
    strncpy(historyBuffer[0], inputBuffer, MAX_LINE);
}

/* Prints the command history */
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

/* Brings a background process to the foreground */
void moveToForeground(pid_t pid, pid_t bgProcesses[MAX_BG_PROCESSES], int *bgCount)
{
    int found = 0;
    for (int i = 0; i < *bgCount; i++)
    {
        if (bgProcesses[i] == pid)
        {
            found = 1;
            fg_pid = pid;                  // Set as foreground process
            waitpid(pid, NULL, WUNTRACED); // Wait for process
            fg_pid = -1;
            /* Remove from background processes */
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

/* Executes piped commands */
void executePipedCommands(char *args[], char *inputBuffer)
{
    int pipefd[2];
    pid_t pid1, pid2;

    /* Split commands at pipe */
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
    // Each element of args up to pipeIndex is copied into the cmd1 array.
    for (int i = 0; i < pipeIndex; i++)
    {
        cmd1[cmd1_len++] = args[i];
    }
    cmd1[cmd1_len] = NULL;
    // Each element of args after pipeIndex is copied into the cmd2 array.
    for (int i = pipeIndex + 1; args[i] != NULL; i++)
    {
        cmd2[cmd2_len++] = args[i];
    }
    cmd2[cmd2_len] = NULL;

    if (pipe(pipefd) == -1)
    {
        fprintf(stderr, "Pipe creation failed");
        return;
    }

    pid1 = fork();
    if (pid1 == 0)
    {
        /* First child process */
        close(pipefd[0]);               // Close read end of the pipe (not used)
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to child process to the pipe
        close(pipefd[1]);               // Close write end of the pipe

        char fullPath[MAX_LINE] = {0};
        findCommandPath(cmd1[0], fullPath);
        if (fullPath[0] == '\0')
        {
            fprintf(stderr, "Command not found: %s\n", cmd1[0]);
            exit(1);
        }

        if (execv(fullPath, cmd1) == -1)
        {
            fprintf(stderr, "Command execution failed");
            exit(1);
        }
    }

    pid2 = fork();
    if (pid2 == 0)
    {
        /* Second child */
        close(pipefd[1]);              // Close write end of the pipe (not used)
        dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to child process from the pipe
        close(pipefd[0]);              // Close read end of the pipe

        /* Handle redirection in cmd2 */
        for (int i = 0; cmd2[i] != NULL; i++)
        {
            if (strcmp(cmd2[i], ">") == 0)
            {
                int fd = open(cmd2[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644); // Open file, create if not exists, truncate
                if (fd < 0)
                {
                    fprintf(stderr, "Error opening file");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO); // Redirect stdout to file
                close(fd);               // Close file descriptor
                cmd2[i] = NULL;
                break;
            }
            else if (strcmp(cmd2[i], ">>") == 0)
            {
                int fd = open(cmd2[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644); // Open file, create if not exists, append
                if (fd < 0)
                {
                    fprintf(stderr, "Error opening file");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO); // Redirect stdout to file
                close(fd);               // Close file descriptor
                cmd2[i] = NULL;
                break;
            }
            else if (strcmp(cmd2[i], "2>") == 0)
            {
                int fd = open(cmd2[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644); // Open file, create if not exists, truncate
                if (fd < 0)
                {
                    fprintf(stderr, "Error opening file");
                    exit(1);
                }
                dup2(fd, STDERR_FILENO); // Redirect stderr to file
                close(fd);               // Close file descriptor
                cmd2[i] = NULL;
                break;
            }
        }

        char fullPath[MAX_LINE] = {0};
        findCommandPath(cmd2[0], fullPath);
        if (fullPath[0] == '\0')
        {
            fprintf(stderr, "Command not found: %s\n", cmd2[0]);
            exit(1);
        }

        if (execv(fullPath, cmd2) == -1)
        {
            fprintf(stderr, "Command execution failed");
            exit(1);
        }
    }

    /* Parent process */
    close(pipefd[0]); // Close read end of the pipe
    close(pipefd[1]); // Close write end of the pipe
    waitpid(pid1, NULL, 0); // Wait for child 1
    waitpid(pid2, NULL, 0); // Wait for child 2
}

/* Handles SIGTSTP (Ctrl+Z) */
void handleSigTSTP(int sig)
{
    if (fg_pid != -1)
    {
        printf("\nCaught SIGTSTP (Ctrl+Z). Terminating foreground process %d and its descendants.\n", fg_pid);
        kill(fg_pid, SIGTERM); // Terminate FG process
    }
    else
    {
        fprintf(stderr, "\nNo foreground process to terminate.\n");
        return;
    }
}

/* Exits the shell */
void terminateProgram(int bgCount)
{
    if (bgCount != 0)
    {
        printf("There are still background processes running!\n");
        return;
    }
    else
    {
        printf("Exiting shell...\n");
        exit(0);
    }
}

/* Handles I/O redirection */
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
        return 0; // No redirection
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
        /* Child process */
        if (strcmp(">", args[i]) == 0) // 
        {
            /* Output redirection */
            int fd = open(args[i + 1], O_WRONLY | O_TRUNC | O_CREAT, 0644); // Open file, create if not exists, truncate
            if (fd < 0)
            {
                fprintf(stderr, "Error opening file");
                exit(1);
            }
            args[i] = NULL;
            dup2(fd, STDOUT_FILENO); // Redirect stdout to file
            close(fd);
        }
        else if (strcmp(">>", args[i]) == 0)
        {
            /* Append output redirection */
            int fd = open(args[i + 1], O_WRONLY | O_APPEND | O_CREAT, 0644); // Open file, create if not exists, append
            if (fd < 0)
            {
                fprintf(stderr, "Error opening file");
                exit(1);
            }
            args[i] = NULL;
            dup2(fd, STDOUT_FILENO); // Redirect stdout to file
            close(fd);
        }
        else if (strcmp("2>", args[i]) == 0)
        {
            /* Error redirection */
            int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644); // Open file, create if not exists, truncate
            if (fd < 0)
            {
                fprintf(stderr, "Error opening file");
                exit(1);
            }
            args[i] = NULL;
            dup2(fd, STDERR_FILENO); // Redirect stderr to file
            close(fd);
        }
        else if (strcmp(args[i], "<") == 0)
        {
            /* Input redirection */
            if (args[i + 2] != NULL && strcmp(args[i + 2], ">") == 0)
            {
                /* Handle input and output redirection */
                if (args[i + 3] == NULL)
                {
                    fprintf(stderr, "Missing output file after '>'.\n");
                    return 0;
                }
                int fd_in = open(args[i + 1], O_RDONLY);
                if (fd_in < 0)
                {
                    fprintf(stderr, "Error opening input file");
                    exit(1);
                }
                int fd_out = open(args[i + 3], O_WRONLY | O_CREAT | O_TRUNC, 0644); // Open output file
                if (fd_out < 0)
                {
                    fprintf(stderr, "Error opening output file");
                    exit(1);
                }
                dup2(fd_in, STDIN_FILENO); // Redirect stdin to input file
                dup2(fd_out, STDOUT_FILENO); // Redirect stdout to output file
                close(fd_in);
                close(fd_out);
                args[i] = NULL;
            }
            else
            {
                /* Only input redirection */
                if (args[i + 1] == NULL)
                {
                    fprintf(stderr, "Missing input file after '<'.\n");
                    return 0;
                }
                int fd_in = open(args[i + 1], O_RDONLY); // Open input file
                if (fd_in < 0)
                {
                    fprintf(stderr, "Error opening input file");
                    exit(1);
                }
                dup2(fd_in, STDIN_FILENO); // Redirect stdin to input file
                close(fd_in);
                args[i] = NULL;
            }
        }
        char fullPath[MAX_LINE] = {0};
        findCommandPath(args[0], fullPath);

        if (execv(fullPath, args) == -1)
        {
            fprintf(stderr, "Command execution failed");
            exit(1);
        }
    }
    else
    {
        /* Parent process */
        if (!background)
        {
            waitpid(pid, NULL, 0);
        }
    }
    return 1;
}

/* Handles SIGCHLD (child termination) */
void handleSigCHLD(int sig)
{
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) 
    // WNOHANG: This flag tells waitpid or wait to return immediately if no child process has exited, rather than blocking the calling process.
    {
        /* Remove terminated process from background processes */
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
