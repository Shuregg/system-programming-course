#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef PRINT_DEBUG_INFO
#define DEBUG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...) ((void)0)
#endif

// Number of matrices (A, B, C) stored together in one buffer.
#define CHANNELS 3

// Exit codes of the program.
enum {
    RC_OK = 0,
    RC_OPEN_INPUT = 1,
    RC_BAD_INPUT = 2,
    RC_NO_MEMORY = 3,
    RC_WRITE = 4,
    RC_ARGS = 10
};

// Matrices A, B and C of size h x w.
// Values are mixed in the same order as in the file: A1 B1 C1 A2 B2 C2 ...
// So value of channel ch at (row, col) is data[(row * w + col) * CHANNELS + ch].
typedef struct {
    uint32_t h;
    uint32_t w;
    uint8_t *data;
} image_t;

// Kernel D of size h x w with signed values.
typedef struct {
    uint16_t h;
    uint16_t w;
    int8_t *data;
} kernel_t;

// Get file names from -i and -o options.
// Both "-i file" and "-ifile" forms are accepted, in any order.
static int parse_args(int argc, char *argv[],
                      const char **input_name, const char **output_name)
{
    *input_name = NULL;
    *output_name = NULL;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char **target;

        if (arg[0] != '-') {
            return RC_ARGS;
        }
        switch (arg[1]) {
        case 'i':
            target = input_name;
            break;
        case 'o':
            target = output_name;
            break;
        default:
            // Unknown option.
            return RC_ARGS;
        }

        if (arg[2] != '\0') {
            *target = &arg[2];
        } else if (i + 1 < argc) {
            i++;
            *target = argv[i];
        } else {
            // Option without a value.
            return RC_ARGS;
        }
    }

    if (*input_name == NULL || *output_name == NULL) {
        DEBUG_PRINT("Input or output file name is missing\n");
        return RC_ARGS;
    }

    DEBUG_PRINT("Input file: '%s', output file: '%s'\n", *input_name, *output_name);
    return RC_OK;
}

// Read exactly size bytes. Returns 1 on success and 0 if the file is too short.
static int read_exact(FILE *file, void *dst, size_t size) {
    return fread(dst, 1, size, file) == size;
}

// Read matrices A, B, C and kernel D.
// On error the caller must still free img->data and ker->data.
static int read_input(FILE *file, image_t *img, kernel_t *ker) {
    img->data = NULL;
    ker->data = NULL;

    if (!read_exact(file, &img->h, sizeof(img->h)) ||
        !read_exact(file, &img->w, sizeof(img->w))) {
        DEBUG_PRINT("Cannot read H and W\n");
        return RC_BAD_INPUT;
    }
    DEBUG_PRINT("H = %" PRIu32 ", W = %" PRIu32 "\n", img->h, img->w);

    // Check that h * w * CHANNELS fits in size_t before we multiply.
    if (img->w != 0 && img->h > SIZE_MAX / CHANNELS / img->w) {
        DEBUG_PRINT("Matrix is too big\n");
        return RC_BAD_INPUT;
    }
    size_t img_size = (size_t)img->h * img->w * CHANNELS;

    // malloc(0) may return NULL, so we always ask for at least 1 byte.
    img->data = malloc(img_size > 0 ? img_size : 1);
    if (img->data == NULL) {
        DEBUG_PRINT("Cannot allocate %zu bytes for A, B, C\n", img_size);
        return RC_NO_MEMORY;
    }
    if (!read_exact(file, img->data, img_size)) {
        DEBUG_PRINT("Cannot read values of A, B, C\n");
        return RC_BAD_INPUT;
    }

    if (!read_exact(file, &ker->h, sizeof(ker->h)) ||
        !read_exact(file, &ker->w, sizeof(ker->w))) {
        DEBUG_PRINT("Cannot read DH and DW\n");
        return RC_BAD_INPUT;
    }
    DEBUG_PRINT("DH = %u, DW = %u\n", ker->h, ker->w);

    // The kernel must have a center, so both sizes must be odd.
    if (ker->h % 2 == 0 || ker->w % 2 == 0) {
        DEBUG_PRINT("DH and DW must be odd\n");
        return RC_BAD_INPUT;
    }

    // Cast before multiply: uint16_t * uint16_t is done in int and may overflow.
    size_t ker_size = (size_t)ker->h * ker->w;

    ker->data = malloc(ker_size);
    if (ker->data == NULL) {
        DEBUG_PRINT("Cannot allocate %zu bytes for D\n", ker_size);
        return RC_NO_MEMORY;
    }
    if (!read_exact(file, ker->data, ker_size)) {
        DEBUG_PRINT("Cannot read values of D\n");
        return RC_BAD_INPUT;
    }

    return RC_OK;
}

int main(int argc, char *argv[]) {
    const char *input_name;
    const char *output_name;
    image_t src;
    kernel_t ker;

    int rc = parse_args(argc, argv, &input_name, &output_name);
    if (rc != RC_OK) {
        return rc;
    }

    FILE *input = fopen(input_name, "rb");
    if (input == NULL) {
        DEBUG_PRINT("Cannot open input file '%s'\n", input_name);
        return RC_OPEN_INPUT;
    }
    rc = read_input(input, &src, &ker);
    fclose(input);

    // TODO: convolve, write output.

    free(src.data);
    free(ker.data);
    return rc;
}
