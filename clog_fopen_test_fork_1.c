// Program created by Luis Miguel Iglesias Sánchez in order to test a reading and parsing
// function that strips unnecessary characters from a file and shows an embellished version
// of the current's version changelog. TODO: It must be able to open the file too with the
// standard text file app.


// TODO: Add a 5 char memory vector on which each category of line is decided
//      to avoid mistakenly calling a category a PR/Issue Code.

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

int main() {

    FILE *clog;
    int buffer;
    int i,displacement,versions_passed;
    char firstchar;

    if ((clog = fopen("changelog.md", "r")) != NULL) {
        versions_passed = 0;
        while ((firstchar = buffer = getc(clog)) != EOF) {
            fseek(clog, -1, 1);

            // READS 3 CHARACTERS INTO THE FUTURE!
            // EVERY LOOP READS, AT LEAST! ONE CHARACTER
            if (fgetc(clog) == '#') {
                if (fgetc(clog) == '#') {
                    if (fgetc(clog) != '#') {
                        fseek(clog, -1, 1);
                        versions_passed++;
                        printf("(Version)");

                        // DEBUG: printf("\n\n %d VERSION PASSED \n\n", versions_passed);

                        if (versions_passed >= 2){
                            break;
                        }
                    }
                    else {
                        fseek(clog, -1, 1);
                        printf("(Category)");
                    }
                }
                else {
                    fseek(clog, -1, 1);
                    printf("(PR/Issue Code)");
                }
            }
            else if (firstchar == '*'){
                printf("(feature)");
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
