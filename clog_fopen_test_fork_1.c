// Program created by Luis Miguel Iglesias Sánchez in order to test a reading and parsing
// function that strips unnecessary characters from a file and shows an embellished version
// of the current's version changelog. It must be able to open the file too with the
// standard text file app.

//#define _CRT_SECURE_NO_WARNINGS

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

int main() {

    FILE *clog;
    int buffer;
    // int foresight[20] = {0};
    // int major_version = 17;
    // int minor_version = 5;
    int i,displacement,versions_passed;

    // int previous_minor_version = minor_version-1;

    // Take into account the possibility of the first minor version (e.g. 1.17.0)
    //if (minor_version == 0) {
    //    previous_minor_version = 0;
    //}

    //char str2[20] = {0}; itoa(major_version, str2, 10);
    //char str4[20] = {0}; itoa(previous_minor_version, str4, 10);
    //char str3[1] = ".";
    //char str1[] = "Version 1.";

    //char version_string[100];

    //strcpy(version_string,str1);
    //strcat(version_string,str2);
    //strcat(version_string,str3);
    //// itoa overflows and concatenates str3 with str4
    ////strcat(version_string,str4);

    //printf("%s\n", version_string);
    //system("pause");

    if ((clog = fopen("changelog.md", "r")) != NULL) {
        versions_passed = 0;
        while ((buffer = getc(clog)) != EOF) {
            fseek(clog, -1, 1);

            // This strongly depends on changelog  formatting
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
            // system("pause");
        }
        fclose(clog);
    }
    else
        puts("Could not open \"changelog.md\".");

    putchar('\n');

    system("pause");
    return 0;
}
