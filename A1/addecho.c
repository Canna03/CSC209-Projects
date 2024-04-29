#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define HEADER_SIZE 22

int main(int argc, char **argv) {
    
    extern char *optarg;
    extern int optind;

    int delay = 8000;
    int volume_scale = 4;

    int opt;

    while ((opt = getopt(argc, argv, "d:v:")) != -1) {
        char *err;
        switch (opt) {
            case 'd':

                // Check for overflow and underflow
                long tmp = strtol(optarg, &err, 10);
                if (tmp > 2147483647 || tmp < -2147483648) {
                    fprintf(stderr, "Delay Parameter Must Be a 32-bit Integer\n");
                    exit(EXIT_FAILURE);
                }

                // Checks for invalid input, non-positive integers
                delay = tmp;
                if (delay != strtof(optarg, &err) || delay <= 0) {
                    fprintf(stderr, "Delay Parameter Must Contain a Positive Integer\n");
                    exit(EXIT_FAILURE);
                }
                break;

            case 'v':

                // Check for overflow and underflow
                tmp = strtol(optarg, &err, 10);
                if (tmp > 2147483647 || tmp < -2147483648) {
                    fprintf(stderr, "Volume Parameter Must Be a 32-bit Integer\n");
                    exit(EXIT_FAILURE);
                }

                // Checks for invalid input, non-positive integers
                volume_scale = tmp;
                if (volume_scale != strtof(optarg, &err) || volume_scale <= 0) {
                    fprintf(stderr, "Volume Parameter Must Contain a Positive Integer\n");
                    exit(EXIT_FAILURE);
                }
                break;

            default:
                fprintf(stderr, "Usage: %s [-d delay] [-v volume_scale] src_file dest_file\n",
                           argv[0]);
                   exit(EXIT_FAILURE);
                }
        }

    // Checks for empty file parameters
    if ((optind + 2) != argc) {
        fprintf(stderr, "Empty File Parameter(s))\n");
        exit(EXIT_FAILURE);
    }

    printf("delay: %d\n", delay);
    printf("volume_scale: %d\n", volume_scale);
    printf("source_file: %s\n", argv[optind]);
    printf("dest_file: %s\n", argv[optind + 1]);

    FILE *orig = fopen(argv[optind], "rb");
    FILE *dest_file = fopen(argv[optind + 1], "wb");

    if (orig == NULL || dest_file == NULL) { //Checking if files were successfully opened.
        fprintf(stderr, "Invalid File\n");
        return 1;
    }

    // Receive header information, and change bytes 4 and 40
    short header[HEADER_SIZE];
    unsigned int *sizeptr;
    
    fread(header, sizeof(short) * HEADER_SIZE, 1, orig);

    sizeptr = (unsigned int *)(header + 2);
    *sizeptr = *sizeptr + delay * 2;

    sizeptr = (unsigned int *)(header + 20);
    *sizeptr = *sizeptr + delay * 2;

    fwrite(header, sizeof(short) * HEADER_SIZE, 1, dest_file);
    
    // Find echo buffer size + memory allocation
    short *echo_buffer = (short*)malloc(delay * sizeof(short)); 
    if (echo_buffer == NULL) {
        fprintf(stderr, "Memory allocation failed for echo_buffer\n");
        exit(EXIT_FAILURE);
    }


    short sample;
    int r;
    int samples = 0;
    int counter = 0;
    int echo_index = 0;

    // Write the same sample until sample[delay] is reached
    // If delay > # of samples, then keep writing 0's until delay is reached
    while (counter < delay) {
        
        r = fread(&sample, sizeof(short), 1, orig);
        if (r == 0) {
            sample = 0;
            fwrite(&sample, sizeof(short), 1, dest_file);
            samples--;
        }
        else {
            fwrite(&sample, sizeof(short), 1, dest_file);
            echo_buffer[counter] = sample / volume_scale;
        }
        counter++;
        samples++;
    }

    // If # of samples < delay, then print what is remaining in echo_buffer
    if (samples < delay) {
        fwrite(echo_buffer, sizeof(short), samples, dest_file);
    }

    // If not then mix sample[i] with echo_buffer[i] and output to file
    // Then set echo_buffer[i] equal to sample[i] / volume_scale
    // until we reach the end of the file, then output what is remaining
    // in echo_buffer
    else {
        while ((r = fread(&sample, sizeof(short), 1, orig))) {
            short echo_sample = sample / volume_scale;

            short mixed_sample = sample + echo_buffer[echo_index];
            fwrite(&mixed_sample, sizeof(short), 1, dest_file);

            echo_buffer[echo_index] = echo_sample;
            echo_index = (echo_index + 1) % delay;
        }

        for (int i = echo_index; i < delay; i++) {
            fwrite(&echo_buffer[i], sizeof(short), 1, dest_file);
        }

        for (int i = 0; i < echo_index; i++) {
            fwrite(&echo_buffer[i], sizeof(short), 1, dest_file);
        }
    }

    free(echo_buffer);

    fclose(orig);   
    fclose(dest_file);

    return 0;
}