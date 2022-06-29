// Program created by Luis Miguel Iglesias Sánchez in order to test a reading and parsing
// function that strips unnecessary characters from a file and shows an embellished version
// of the current's version changelog. It must be able to open the file too with the
// standard text file app.

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

int main() {

    FILE *clog;
    int buffer;
    int i,displacement,versions_passed;

    if ((clog = fopen("changelog.md", "r")) != NULL) {
        versions_passed = 0;
        while ((buffer = getc(clog)) != EOF) {
            fseek(clog, -1, 1);

            if (fgetc(clog) == '#') {
                if (fgetc(clog) == '#') {
                    if (fgetc(clog) != '#') {
                        fseek(clog, -1, 1);
                        versions_passed++;

                        // DEBUG: printf("\n\n %d VERSION PASSED \n\n", versions_passed);

                        if (versions_passed >= 2){
                            break;
                        }
                    }
                    else {
                        fseek(clog, -1, 1);
                    }
                }
                else {
                    fseek(clog, -1, 1);
                }
            }
            putc(buffer, stdout);
        }
        fclose(clog);
    }
    else
        puts("Could not open \"changelog.md\".");

    putchar('\n');

    system("pause");
    return 0;
}
