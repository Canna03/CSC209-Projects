#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    
    if ( argc != 3) {
        fprintf(stderr, "Invalid Command Line Arguments.\n");
        return 1;
    }

    FILE *source_file = fopen(argv[1], "rb");
    FILE *dest_file = fopen(argv[2], "wb+");

    if (source_file == NULL || dest_file == NULL) { //Checking if files were successfully opened.
        fprintf(stderr, "Invalid File(s)\n");
        return 1;
    }
    // Copy pasting the first 44 bytes, the header of the .wav file.
    char *buff = malloc(44);
    fread(buff, 44, 1, source_file);
    fwrite(buff, 44, 1, dest_file);
    free(buff);

    short number_read;
    short left;
    short result;
    int r;
    // Creating a variable to keep track of which of the pair of shorts is being read in:
    int first = 1;

    while ((r = fread(&number_read, sizeof(short), 1, source_file))) {
        if (!(first % 2)) {
            // If first is even that means from a pair of shorts, this is the second reading.
            // *number_read is right.
            
            result = (left - number_read) / 2;

            fwrite(&result, sizeof(short), 1, dest_file);
            fwrite(&result, sizeof(short), 1, dest_file);
            
            first = 1;
        }
        else {
            // *number_read is left
            left = number_read;
            first = 0;
        }
    }


    int error_1 = fclose(source_file);
    int error_2 = fclose(dest_file);

    if (error_1 || error_2) {
        fprintf(stderr, "fclose failed\n");
        return 1;
    }
    return 0;
}