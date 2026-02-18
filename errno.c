/*=======================================================================
 * Convert between errno value and text
 *=======================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAX_ERRNO 200                   /* Just a guess */

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s num|text\n", argv[0]);
        return 2;
    }
    const char *arg = argv[1];
    char *end;
    unsigned value = strtoul(arg, &end, 10);
    if (end == arg || *end != '\0') {
        /* Not an integer, so text to search for */
        for (value = 1; value <= MAX_ERRNO; value ++) {
            char *text = strerror(value);
            if (strncmp(text, "Unknown error ", 14) == 0) {
                continue;
            }
            if (strstr(text, arg) != NULL) {
                printf("%u %s\n", value, text);
            }
        }
    } else {
        char *text = strerror(value);
        if (strncmp(text, "Unknown error ", 14) == 0) {
            fprintf(stderr, "%s\n", text);
            return 1;
        }
        printf("%s\n", text);
    }
    return 0;
}
