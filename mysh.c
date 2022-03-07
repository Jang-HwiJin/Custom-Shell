#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// This is the maximum number of arguments your shell should handle for one command
#define MAX_ARGS 128

/*
preprocess (1) removes the white space from the
right end, (2) adds ';' if there is no ';' or '&'
at the end, and (3) adds a null terminator
*/
void preprocess(char* str) {
  int length = strlen(str);
  int pos = length - 1;
  while (pos >= 0) {
    if (!isspace(str[pos])) {
      // if there's punctuation, just add null terminator
      if ((str[pos] == ';') || (str[pos] == '&')) {
        str[pos + 1] = '\0';
      } else {
        // otherwise, add ';' and null terminator
        str[pos + 1] = ';';
        str[pos + 2] = '\0';
      }
      break;
    }
    pos -= 1;
  }
}

/*
exec_command handles the fork and execute processes
for one command
*/
void exec_command(char* line, bool is_foreground) {
  // parse the line into tokens
  char* tokens[MAX_ARGS];
  int size = 0;
  for (char* token = strtok_r(line, " ", &line); token != NULL && size < MAX_ARGS;
       token = strtok_r(line, " ", &line)) {
    tokens[size] = token;
    size += 1;
  }
  tokens[size] = NULL;

  // handle exit
  if (strcmp(tokens[0], "exit") == 0) {
    exit(0);
  }

  // handle cd
  if (strcmp(tokens[0], "cd") == 0) {
    if (size > 1) {
      if (chdir(tokens[1]) != 0) {
        // when cd failed
        printf("shell: cd: %s: No such file or directory\n", tokens[1]);
      }
    }
    return;
  }

  // create a child process
  pid_t child_id = fork();
  if (child_id == -1) {
    perror("fork failed");
    exit(EXIT_FAILURE);
  }

  // Execute child process
  if (child_id == 0) {
    if (execvp(tokens[0], tokens) == -1) {
      perror("execvp failed, invalid command.\n");
      exit(1);
    }
  } else {
    // In the parent: wait for the child process to exit
    int status;
    // Only wait if this is a foreground process
    if (is_foreground) {
      waitpid(child_id, &status, 0);
      // Handle child's exit status
      if (WIFEXITED(status)) {
        printf("[%s exited with status %d]\n", tokens[0], WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        printf("[%s died with status %d]\n", tokens[0], WTERMSIG(status));
      } else {
        printf("Something unexpected happened.\n");
      }
    }
  }
}

int main(int argc, char** argv) {
  // If there was a command line option passed in, use that file instead of stdin
  if (argc == 2) {
    // Try to open the file
    int new_input = open(argv[1], O_RDONLY);
    if (new_input == -1) {
      fprintf(stderr, "Failed to open input file %s\n", argv[1]);
      exit(1);
    }

    // Now swap this file in and use it as stdin
    if (dup2(new_input, STDIN_FILENO) == -1) {
      fprintf(stderr, "Failed to set new file as input\n");
      exit(2);
    }
  }

  char* line = NULL;     // Pointer that will hold the line we read in
  size_t line_size = 0;  // The number of bytes available in line

  // Loop forever
  while (true) {
    // Print the shell prompt
    printf("$ ");

    // Get a line of stdin, storing the string pointer in line
    if (getline(&line, &line_size, stdin) == -1) {
      if (errno == EINVAL) {
        perror("Unable to read command line");
        exit(2);
      } else {
        // Must have been end of file (ctrl+D)
        printf("\nShutting down...\n");

        // Exit the infinite loop
        break;
      }
    }

    // trim right whitespace, add null terminator,
    // and punctuation (if needed)
    preprocess(line);

    // split line by commands and execute them one by one
    for (char* next = strpbrk(line, ";&"); next != NULL; next = strpbrk(line, ";&")) {
      bool is_foreground = (*next == ';');
      *next = '\0';
      exec_command(line, is_foreground);
      line = next + 1;
    }

    // process exit status for background commands
    int status;
    for (pid_t pid = waitpid(0, &status, WNOHANG); pid > 0; pid = waitpid(0, &status, WNOHANG)) {
      if (WIFEXITED(status)) {
        printf("[background process %d exited with status %d]\n", pid, WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        printf("[background process %d exited with status %d]\n", pid, WTERMSIG(status));
      } else {
        printf("Something unexpected happened.\n");
      }
    }

    // reset line pointer
    line = NULL;
  }

  // If we read in at least one line, free this space
  if (line != NULL) {
    free(line);
  }

  return 0;
}
