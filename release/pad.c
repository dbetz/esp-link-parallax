#include <stdio.h>
int main(int argc, char *argv[])
{
    int sectorSize = argc > 1 ? atoi(argv[1]) : 1024;
    int count = 0, ch;
    while ((ch = getchar()) != EOF) {
        putchar(ch);
        ++count;
    }
    while ((count % sectorSize) != 0) {
        putchar('\0');
        ++count;
    }
    return 0;
}
