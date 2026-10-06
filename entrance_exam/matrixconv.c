#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct matrix_t {
    /* data */
    uint32_t rows;
    uint32_t cols;
    char** values;
} matrix_t;

void set_matrix_size(matrix_t* m, uint32_t h, uint32_t w) {
    m->rows = h;
    m->cols = w;
}

int matrix_malloc(matrix_t* m) {
    int status = 0;
    // uint32_t rows = m->rows;
    // uint32_t cols = m->cols;
    // if (rows != 0 && cols != 0) {
    //     m->values = malloc(rows * sizeof(char*) + rows * cols * sizeof(char));
    //     if (!(m->values)) {
    //         status = -2;
    //     } else {
    //         for (uint32_t i = 0; i < rows; i++) {
                
    //         }
    //     }
    // } else {
    //     status = -1;
    // }
    return status;
}

int main(int argc, char* argv[]) {
    int opt = -1;

    // File names
    char* input_file_name = NULL;
    char* ouput_file_name = NULL;

    // File descriptors
    FILE* ifptr = NULL;
    FILE* ofptr = NULL;

    uint32_t input_height = 0;
    uint32_t input_width = 0;

    matrix_t ma;
    matrix_t mb;
    matrix_t mc;
    matrix_t md;

    // Parse arguments
    while ((opt = getopt(argc, argv, "i:o:")) != -1) {
        switch (opt) {
        case 'i':
            input_file_name = optarg;
        #ifdef PRINT_DEBUG_INFO
            printf("Get input file name: '%s'\n", input_file_name);
        #endif
            break;
        case 'o':
            ouput_file_name = optarg;
        #ifdef PRINT_DEBUG_INFO
            printf("Get output file name: '%s'\n", ouput_file_name);
        #endif
            break;
        }
    }
    if (!input_file_name || !ouput_file_name) {
    #ifdef PRINT_DEBUG_INFO
        printf("At least one file name was not provided.\n");
    #endif
        return 10;
    }

    // Process input binary file
    ifptr = fopen(input_file_name, "rb");
    if (!ifptr) {
    #ifdef PRINT_DEBUG_INFO
        printf("Cannot open file with name '%s'\n", input_file_name);
    #endif
        return 1;
    }

    size_t succesfull_reading_count = 0;
    succesfull_reading_count += fread(&input_height, sizeof(uint32_t), 1, ifptr);
    if (succesfull_reading_count != 1) {
    #ifdef PRINT_DEBUG_INFO
        printf("Cannot read Height from '%s'\n", input_file_name);
    #endif
        return 2;
    }
    succesfull_reading_count += fread(&input_width, sizeof(uint32_t), 1, ifptr);
    if (succesfull_reading_count != 2) {
    #ifdef PRINT_DEBUG_INFO
        printf("Cannot read Width from '%s'\n", input_file_name);
    #endif
        return 2;
    }
    #ifdef PRINT_DEBUG_INFO
        printf("H = %0d, W = %0d\n", input_height, input_width);
    #endif

    set_matrix_size(&ma, input_height, input_width);
    set_matrix_size(&mb, input_height, input_width);
    set_matrix_size(&mc, input_height, input_width);

    int status_a = matrix_malloc(&ma);
    if (status_a) {
        return 1;
    }
    int status_b = matrix_malloc(&mb);
    if (status_b) {
        return 1;
    }
    int status_c = matrix_malloc(&mc);
    if (status_c) {
        return 1;
    }

    // fwrite()

}
