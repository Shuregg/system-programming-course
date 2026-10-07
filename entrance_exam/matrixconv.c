#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef PRINT_DEBUG_INFO
#define DEBUG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...) ((void)0)
#endif

// Number of matrices A, B, C.
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

// Matrices A, B and C of size h * w.
// Each matrix has its own buffer.
// Value of matrix ch at (row, col) is data[ch][row * w + col].
typedef struct {
    uint32_t h;
    uint32_t w;
    uint8_t *data[CHANNELS];
} image_t;

// Kernel D of size h x w with signed values.
typedef struct {
    uint16_t h;
    uint16_t w;
    int8_t *data;
} kernel_t;

// Get file names from -i and -o options.
// Both "-i file" and "-ifile" forms are accepted, in any order.
// If -i or -o is given several times, the last file name is used.
static int parse_args(int argc, char *argv[],
                      const char **input_name, const char **output_name) {
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

// Allocate buffers for all matrices of img.
// img->h and img->w must be set.
static int image_alloc(image_t *img) {
    size_t size = (size_t)img->h * img->w;

    // malloc(0) may return NULL, so we always ask for at least 1 byte.
    if (size == 0) {
        size = 1;
    }

    for (int ch = 0; ch < CHANNELS; ch++) {
        img->data[ch] = malloc(size);
        if (img->data[ch] == NULL) {
            DEBUG_PRINT("Cannot allocate %zu bytes for a matrix\n", size);
            return RC_NO_MEMORY;
        }
    }
    return RC_OK;
}

static void image_free(image_t *img) {
    for (int ch = 0; ch < CHANNELS; ch++) {
        free(img->data[ch]);
        img->data[ch] = NULL;
    }
}

// Read exactly size bytes. Returns 1 on success and 0 if the file is too short.
static int read_exact(FILE *file, void *dst, size_t size) {
    return fread(dst, 1, size, file) == size;
}

// Read values of A, B, C. In the file they are mixed: A1 B1 C1 A2 B2 C2 ...
// We read one row at a time and split it into three buffers.
static int read_matrices(FILE *file, image_t *img) {
    // Nothing to read. Also h >= 1 below, so the row size fits in size_t.
    if (img->h == 0 || img->w == 0) {
        // Empty matrices are valid: there are 0 bytes to read, so this is a success.
        return RC_OK;
    }

    const size_t line_size = (size_t)img->w * CHANNELS;
    uint8_t *line = malloc(line_size);
    if (line == NULL) {
        DEBUG_PRINT("Cannot allocate %zu bytes for a row\n", line_size);
        return RC_NO_MEMORY;
    }

    int rc = RC_OK;
    for (size_t row = 0; row < img->h; row++) {
        if (!read_exact(file, line, line_size)) {
            DEBUG_PRINT("Cannot read row %zu of A, B, C\n", row);
            rc = RC_BAD_INPUT;
            break;
        }
        for (size_t col = 0; col < img->w; col++) {
            for (int ch = 0; ch < CHANNELS; ch++) {
                img->data[ch][row * img->w + col] = line[col * CHANNELS + ch];
            }
        }
    }

    free(line);
    return rc;
}

// Read matrices A, B, C and kernel D.
static int read_input(FILE *file, image_t *img, kernel_t *ker) {
    *img = (image_t){0};
    *ker = (kernel_t){0};

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

    int rc = image_alloc(img);
    if (rc != RC_OK) {
        return rc;
    }
    rc = read_matrices(file, img);
    if (rc != RC_OK) {
        return rc;
    }

    if (!read_exact(file, &ker->h, sizeof(ker->h)) ||
        !read_exact(file, &ker->w, sizeof(ker->w))) {
        DEBUG_PRINT("Cannot read DH and DW\n");
        return RC_BAD_INPUT;
    }
    DEBUG_PRINT("DH = %u, DW = %u\n", ker->h, ker->w);

    // The kernel must have a center, so both sizes must be odd (task condition)
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

// Bring the sum into 0..255 by the rules of the task.
static uint8_t normalize(int64_t sum) {
    if (sum > 255) {
        return (uint8_t)(sum % 251);
    }
    if (sum < 0) {
        return (uint8_t)(-sum % 241);
    }
    return (uint8_t)sum;
}

// Apply kernel 'ker' to one matrix 'src' of size height * width
// and write the result to 'dst'. Values outside the matrix are 0.
static void convolve(const uint8_t *src, uint8_t *dst,
                     uint32_t height, uint32_t width, const kernel_t *ker) {
    const int64_t h = height;
    const int64_t w = width;
    const int64_t kh = ker->h;
    const int64_t kw = ker->w;
    // Position of the kernel center.
    const int64_t cy = kh / 2;
    const int64_t cx = kw / 2;

    for (int64_t row = 0; row < h; row++) {
        // Kernel rows i in [i_from, i_to) fall inside the matrix.
        // Rows outside give 0 (task condition), so we skip them instead of checking each value.
        // i_from = max(0, cy - row) ; i_to = min(kh, h - row + cy)
        const int64_t i_from = (cy > row) ? cy - row : 0;
        const int64_t i_to = (h - row + cy < kh) ? h - row + cy : kh;

        for (int64_t col = 0; col < w; col++) {
            // The same for kernel columns j in [j_from, j_to).
            // j_from = max(0, cx - col) ; j_to = min(kw, w - col + cx)
            const int64_t j_from = (cx > col) ? cx - col : 0;
            const int64_t j_to = (w - col + cx < kw) ? w - col + cx : kw;
            const int64_t len = j_to - j_from;
            int64_t sum = 0;

            for (int64_t i = i_from; i < i_to; i++) {
                // First used kernel value in row i and the matrix value under it.
                const int8_t *k = &ker->data[i * kw + j_from];
                const uint8_t *s = &src[(row + i - cy) * w + col + j_from - cx];

                for (int64_t j = 0; j < len; j++) {
                    sum += k[j] * s[j];
                }
            }

            dst[row * w + col] = normalize(sum);
        }
    }
}

// Write values of A', B', C' mixed in the same order as in the input file.
// We build one row at a time and write it with one fwrite().
static int write_matrices(FILE *file, const image_t *img) {
    // Nothing to write. Also h >= 1 below, so the row size fits in size_t.
    if (img->h == 0 || img->w == 0) {
        return RC_OK;
    }

    const size_t line_size = (size_t)img->w * CHANNELS;
    uint8_t *line = malloc(line_size);
    if (line == NULL) {
        DEBUG_PRINT("Cannot allocate %zu bytes for a row\n", line_size);
        return RC_NO_MEMORY;
    }

    int rc = RC_OK;
    for (size_t row = 0; row < img->h; row++) {
        for (size_t col = 0; col < img->w; col++) {
            for (int ch = 0; ch < CHANNELS; ch++) {
                line[col * CHANNELS + ch] = img->data[ch][row * img->w + col];
            }
        }
        if (fwrite(line, 1, line_size, file) != line_size) {
            rc = RC_WRITE;
            break;
        }
    }

    free(line);
    return rc;
}

// Write H, W and matrices A', B', C' to the output file.
static int write_output(const char *name, const image_t *img) {
    FILE *file = fopen(name, "wb");
    if (file == NULL) {
        DEBUG_PRINT("Cannot open output file '%s'\n", name);
        return RC_WRITE;
    }

    int rc = RC_OK;
    if (fwrite(&img->h, sizeof(img->h), 1, file) != 1 ||
        fwrite(&img->w, sizeof(img->w), 1, file) != 1) {
        rc = RC_WRITE;
    } else {
        rc = write_matrices(file, img);
    }

    // fclose() writes the rest of the buffer to disk, so it can fail too.
    if (fclose(file) != 0 && rc == RC_OK) {
        rc = RC_WRITE;
    }
    if (rc != RC_OK) {
        DEBUG_PRINT("Cannot write output file '%s'\n", name);
    }
    return rc;
}

int main(int argc, char *argv[]) {
    const char *input_name;
    const char *output_name;
    image_t src;
    kernel_t ker;
    image_t dst = {0};

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

    if (rc == RC_OK) {
        dst.h = src.h;
        dst.w = src.w;
        rc = image_alloc(&dst);
    }

    if (rc == RC_OK) {
        for (int ch = 0; ch < CHANNELS; ch++) {
            convolve(src.data[ch], dst.data[ch], src.h, src.w, &ker);
        }
        rc = write_output(output_name, &dst);
    }

    // Always free allocated memory even if return code is not 0.
    image_free(&src);
    free(ker.data);
    image_free(&dst);
    return rc;
}
