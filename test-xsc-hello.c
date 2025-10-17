/*
 * XSC Hello World Test Program
 *
 * Simple test to verify XSC syscalls are working.
 * This should be compiled with the XSC toolchain.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main() {
    const char *msg = "Hello from XSC v7!\n";

    /* This write() will go through XSC rings instead of trap */
    ssize_t written = write(STDOUT_FILENO, msg, strlen(msg));

    if (written < 0) {
        const char *err = "Error: write() failed\n";
        write(STDERR_FILENO, err, strlen(err));
        return 1;
    }

    printf("Successfully wrote %zd bytes via XSC\n", written);

    return 0;
}
