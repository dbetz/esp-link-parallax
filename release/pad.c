#include <stdio.h>
int main(int argc, char *argv[])
{
    int sectorSize = argc > 1 ? atoi(argv[1]) : 1024;
    int count = 0, ch;
    FILE *ifp, *ofp;

    if (argc != 4) {
		printf("usage: pad sector-size in-file out-file\n");
		return 1;
	}

    sectorSize = atoi(argv[1]);

    if (!(ifp = fopen(argv[2], "rb"))) {
		printf("error: can't open %s\n", argv[2]);
		return 1;
	}

    if (!(ofp = fopen(argv[3], "wb"))) {
		printf("error: can't create %s\n", argv[3]);
		return 1;
	}

    while ((ch = getc(ifp)) != EOF) {
        putc(ch, ofp);
        ++count;
    }

    while ((count % sectorSize) != 0) {
        putc('\0', ofp);
        ++count;
    }

    fclose(ifp);
	fclose(ofp);

    return 0;
}
