#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <fcntl.h>

#define MAX_LENGTH 1024
#define MAX_ARGS 64
#define MAX_COMMANDS 10 // Define a maximum number of commands
#define MAX_HISTORY 5

int main() {
    char *history[MAX_HISTORY];
    char input[MAX_LENGTH];
    char temp[MAX_LENGTH];
    char *args[MAX_COMMANDS]; // Array to store individual commands
    int start = 0;
    int end = 0;
    int history_count = 0;
    char *token;
    pid_t pid;
    int input_des, output_des;
    struct rusage usage;

    while (1) {
        printf("shell> "); // Show prompt

        if (!fgets(input, MAX_LENGTH, stdin)) {
            continue;
        }

        input[strcspn(input, "\n")] = 0; // Remove newline character from the input

        if (strcmp(input, "quit") == 0) {
            break; // Quit command
        }
        else if (strcmp(input, "history") == 0) {//takes the stored history and prints it out
        printf("Command history:\n");
        for (int i = 0; i < history_count; i++) {
        int index = (start + i) % MAX_HISTORY;
        printf("[ %d ] %s\n", i + 1, history[index]);
        }//gets the new input number either back to go back to input a new command or pull an old from the list
        if (!fgets(input, MAX_LENGTH, stdin)) {
            continue;
        }
        input[strcspn(input, "\n")] = 0;//removes newline character from new input

        if (strcmp(input, "back") == 0) {
        continue;
        }

        if (input[0] == '!') {//appends input to number to pull new command from history
        int command_number = atoi(input + 1);
        if (command_number < 1 || command_number > history_count) {
            printf("Invalid command number.\n");
            continue;
        }
        int history_index = (start + command_number - 1) % MAX_HISTORY;
        strcpy(input, history[history_index]); // Copy the command from history to input for execution
    }

        }

        // Tokenize string into commands based on pipes for idividual commands to be executed
        strcpy(temp, input);//stores origninal command for later use in history if successfully executed
        int argCount = 0;
        token = strtok(input, "|");
        while (token != NULL && argCount < MAX_COMMANDS) {
            args[argCount++] = token;
            
            token = strtok(NULL, "|");
        }

        int num_commands = argCount; // stores number of commands to execute

        // Create pipes
        int pipes[2 * (num_commands - 1)];
        for (int i = 0; i < num_commands - 1; i++) {
            if (pipe(pipes + i * 2) < 0) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        for (int i = 0; i < num_commands; i++) {
            //forks for a child proccess for every command
            pid = fork();

            if (pid < 0) {
                perror("Fork failed");
                exit(1);
            }

            if (pid == 0) { // Child process
            //creates pipes for all arguments 
                if (i < num_commands - 1) {
                    dup2(pipes[i * 2 + 1], STDOUT_FILENO);
                }
                if (i > 0) {
                    dup2(pipes[(i - 1) * 2], STDIN_FILENO);
                }

                // Close all pipe file descriptors in child
                for (int j = 0; j < 2 * (num_commands - 1); j++) {
                    close(pipes[j]);
                }

                // Tokenize the piped commands into their individual arguments
                char *cmd_args[MAX_ARGS];
                int cmd_arg_count = 0;
                token = strtok(args[i], " \n");

                while (token != NULL) {
                    if (strcmp(token, "<") == 0) {//gets file redirection
                        token = strtok(NULL, " \n"); 
                        if (token) {
                            input_des = open(token, O_RDONLY);//open file for read only and return the input destination
                            if (input_des < 0) {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            dup2(input_des, STDIN_FILENO);//redirecting stadnard iput to the file then closing
                            close(input_des);
                        }
                        token = strtok(NULL, " \n"); // Continue tokenizing
                        continue;
                    }

                    if (strcmp(token, ">") == 0) {
                        token = strtok(NULL, " \n"); // Get the file name for output redirection
                        if (token) {

                            if (access(token, F_OK) != -1) {//checks if the file already exists if not the command is invalid
                            //and will throw an error so it is not kept in memory
                            fprintf(stderr, "File %s already exists\n", token);
                            exit(EXIT_FAILURE);
                            }
                            output_des = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0644);//opens file for write only, create if doesnt exist
                            //and if the file exists and is opened it will erase the content and the owner has read write permission
                            if (output_des < 0) {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            dup2(output_des, STDOUT_FILENO);
                            close(output_des);
                        }
                        token = strtok(NULL, " \n"); // Continue tokenizing
                        continue;
                    }

                    cmd_args[cmd_arg_count++] = token;
                    token = strtok(NULL, " \n");
                }
                cmd_args[cmd_arg_count] = NULL;

                execvp(cmd_args[0], cmd_args);
                perror("execvp failed"); // execvp returns only on error
                exit(1);
            }
        }

        // Close all pipe file descriptors in parent
        for (int i = 0; i < 2 * (num_commands - 1); i++) {
            close(pipes[i]);
        }

        // Wait for all child processes to finish
        for (int i = 0; i < num_commands; i++) {
            wait(NULL);
        }

        int success = 1;
    for (int i = 0; i < num_commands; i++) {
        int status;
        waitpid(-1, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            success = 0;
            break;
        }
    }

    // If all commands in the pipe chain were successful, add to history
    if (success) {
        // Add command to history and manage the circular buffer
        if (history_count == MAX_HISTORY) {
        free(history[end]); // Free memory if overwriting older command
        
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            history[i] = history[i + 1];
        }
        
        // The last slot will be overwritten by the new command
        history_count--; // Decrease count temporarily
        }

        history[history_count] = strdup(temp);

    // Update the count of history, ensuring it does not exceed MAX_HISTORY
    if (history_count < MAX_HISTORY) {
        history_count++;
    }
        
    }
        // Get resource usage of terminated child processes
        if (getrusage(RUSAGE_CHILDREN, &usage) == -1) {
            perror("getrusage failed");
        } else {
            printf("time used: %ld.%06ld seconds\n",
                   usage.ru_utime.tv_sec, usage.ru_utime.tv_usec);
            printf("context switches: %ld\n", usage.ru_nivcsw);
        }
    }

    for (int i = 0; i < MAX_HISTORY; i++) {
    free(history[i]);
}

    return 0;
}

