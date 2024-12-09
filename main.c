#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_LINE 80

// The setup function: Parses the user input into commands and arguments
void setup(char *inputBuffer, char *args[], int *background, char **input_file, char **output_file) {
    int i = 0, start = -1, ct = 0;
    *background = 0;
    *input_file = NULL;
    *output_file = NULL;

    while (inputBuffer[i] != '\0') {
        switch (inputBuffer[i]) {
            case ' ':
            case '\t':
                if (start != -1) {
                    args[ct++] = &inputBuffer[start];
                }
                inputBuffer[i] = '\0';
                start = -1;
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
            case '>':
                *output_file = &inputBuffer[i + 1];
                inputBuffer[i] = '\0';
                break;
            case '<':
                *input_file = &inputBuffer[i + 1];
                inputBuffer[i] = '\0';
                break;
            default:
                if (start == -1) {
                    start = i;
                }
        }
        i++;
    }
    args[ct] = NULL;  // Null-terminate the argument list
}

// Function to execute commands
void execute_command(char *args[], char *input_file, char *output_file) {
    int fd_in, fd_out;

    if (input_file != NULL) {
        fd_in = open(input_file, O_RDONLY);
        if (fd_in == -1) {
            perror("Error opening input file");
            exit(1);
        }
        dup2(fd_in, STDIN_FILENO);  // Redirect input to file
        close(fd_in);
    }

    if (output_file != NULL) {
        fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out == -1) {
            perror("Error opening output file");
            exit(1);
        }
        dup2(fd_out, STDOUT_FILENO);  // Redirect output to file
        close(fd_out);
    }

    execvp(args[0], args);
    perror("Exec failed");
    exit(1);
}

int main() {
    char inputBuffer[MAX_LINE];  // Buffer for user input
    char *args[MAX_LINE / 2 + 1];  // Argument list for exec
    char *input_file, *output_file;
    int background;
    pid_t pid;

    while (1) {
        background = 0;
        input_file = NULL;
        output_file = NULL;

        printf("myshell: ");
        fflush(stdout);

        // Read input from the user
        if (fgets(inputBuffer, MAX_LINE, stdin) == NULL) {
            break;  // End of input (Ctrl+D)
        }

        // Parse the input using the setup function
        setup(inputBuffer, args, &background, &input_file, &output_file);

        if (args[0] == NULL) {
            continue;  // Empty input
        }

        pid = fork();
        if (pid == 0) {  // Child process
            execute_command(args, input_file, output_file);
        } else {  // Parent process
            if (background == 0) {
                waitpid(pid, NULL, 0);  // Wait for foreground processes
            } else {
                printf("Started background process with PID: %d\n", pid);
            }
        }
    }

    return 0;
}
