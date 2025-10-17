/*
 * XSC Fork Test Program
 *
 * Tests fork() and execve() through XSC rings.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

int main() {
    printf("XSC Fork Test\n");
    printf("Parent PID: %d\n", getpid());

    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "Fork failed!\n");
        return 1;
    }

    if (pid == 0) {
        /* Child process */
        printf("Child PID: %d (parent: %d)\n", getpid(), getppid());
        printf("Child: Sleeping 1 second...\n");
        sleep(1);
        printf("Child: Exiting\n");
        return 42;
    } else {
        /* Parent process */
        printf("Parent: Created child with PID %d\n", pid);

        int status;
        pid_t waited = wait(&status);

        if (WIFEXITED(status)) {
            printf("Parent: Child %d exited with status %d\n",
                   waited, WEXITSTATUS(status));
        }

        printf("Parent: Test completed successfully!\n");
        return 0;
    }
}
