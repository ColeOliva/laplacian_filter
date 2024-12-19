#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>

#define LAPLACIAN_THREADS 4 // change the number of threads as you run your concurrency experiment

/* Laplacian filter is 3 by 3 */
#define FILTER_WIDTH 3
#define FILTER_HEIGHT 3

#define RGB_COMPONENT_COLOR 255

pthread_mutex_t time_mutex;

typedef struct
{
    unsigned char r, g, b;
} PPMPixel;

struct parameter
{
    PPMPixel *image;         // original image pixel data
    PPMPixel *result;        // filtered image pixel data
    unsigned long int w;     // width of image
    unsigned long int h;     // height of image
    unsigned long int start; // starting point of work
    unsigned long int size;  // equal share of work (almost equal if odd)
};

struct file_name_args
{
    char *input_file_name;     // e.g., file1.ppm
    char output_file_name[20]; // will take the form laplaciani.ppm, e.g., laplacian1.ppm
};

/*The total_elapsed_time is the total time taken by all threads
to compute the edge detection of all input images .
*/
double total_elapsed_time = 0;

/*This is the thread function. It will compute the new values for the region of image specified in params (start to start+size) using convolution.
    For each pixel in the input image, the filter is conceptually placed on top ofthe image with its origin lying on that pixel.
    The  values  of  each  input  image  pixel  under  the  mask  are  multiplied  by the corresponding filter values.
    Truncate values smaller than zero to zero and larger than 255 to 255.
    The results are summed together to yield a single output value that is placed in the output image at the location of the pixel being processed on the input.

 */
void *compute_laplacian_threadfn(void *params)
{
    struct parameter *param = (struct parameter *)params;

    int laplacian[FILTER_WIDTH][FILTER_HEIGHT] = {
        {-1, -1, -1},
        {-1, 8, -1},
        {-1, -1, -1}};

    int red, green, blue;
    unsigned long image_width = param->w;
    unsigned long image_height = param->h;
    unsigned long start_row = param->start;
    unsigned long num_rows = param->size;
    unsigned long end_row = start_row + num_rows;

    for (unsigned long y = start_row; y < end_row; y++)
    {
        for (unsigned long x = 0; x < image_width; x++)
        {
            red = 0;
            green = 0;
            blue = 0;

            // iterate over filter dimensions
            for (int fy = 0; fy < FILTER_HEIGHT; fy++)
            {
                for (int fx = 0; fx < FILTER_WIDTH; fx++)
                {
                    // calculate coordinates for the input image
                    int x_coordinate = (x - FILTER_WIDTH / 2 + fx + image_width) % image_width;
                    int y_coordinate = (y - FILTER_HEIGHT / 2 + fy + image_height) % image_height;

                    // index for the current pixel in the image
                    unsigned long pixelIndex = y_coordinate * image_width + x_coordinate;

                    // perform convolution by applying the filter
                    red += param->image[pixelIndex].r * laplacian[fy][fx];
                    green += param->image[pixelIndex].g * laplacian[fy][fx];
                    blue += param->image[pixelIndex].b * laplacian[fy][fx];
                }
            }

            // clamp values to the range [0, 255]
            red = red < 0 ? 0 : (red > 255 ? 255 : red);
            green = green < 0 ? 0 : (green > 255 ? 255 : green);
            blue = blue < 0 ? 0 : (blue > 255 ? 255 : blue);

            // store the computed values in the result image
            unsigned long result_index = y * image_width + x;
            param->result[result_index].r = (unsigned char)red;
            param->result[result_index].g = (unsigned char)green;
            param->result[result_index].b = (unsigned char)blue;
        }
    }

    return NULL;
}

/* Apply the Laplacian filter to an image using threads.
 Each thread shall do an equal share of the work, i.e. work=height/number of threads. If the size is not even, the last thread shall take the rest of the work.
 Compute the elapsed time and store it in *elapsedTime (Read about gettimeofday).
 Return: result (filtered image)
 */
PPMPixel *apply_filters(PPMPixel *image, unsigned long w, unsigned long h, double *elapsed_time)
{
    struct timeval start, end;
    gettimeofday(&start, NULL);

    PPMPixel *result = (PPMPixel *)malloc(w * h * sizeof(PPMPixel));
    if (!result)
    {
        fprintf(stderr, "Error: Unable to allocate memory for result image\n");
        return NULL;
    }

    pthread_t threads[LAPLACIAN_THREADS];
    struct parameter params[LAPLACIAN_THREADS];
    pthread_mutex_lock(&time_mutex);

    for (int i = 0; i < LAPLACIAN_THREADS; i++)
    {
        params[i].image = image;
        params[i].result = result;
        params[i].w = w;
        params[i].h = h;
        params[i].start = i * (h / LAPLACIAN_THREADS);
        params[i].size = (i == LAPLACIAN_THREADS - 1) ? h - params[i].start : (h + LAPLACIAN_THREADS - 1) / LAPLACIAN_THREADS;
        if (pthread_create(&threads[i], NULL, compute_laplacian_threadfn, &params[i]) != 0)
        {
            fprintf(stderr, "Error: Unable to create filter thread %d\n", i);
            free(result); // ensure memory is freed before exit
            return NULL;
        }
    }

    for (int i = 0; i < LAPLACIAN_THREADS; i++)
    {
        pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, NULL);
    *elapsed_time = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    pthread_mutex_unlock(&time_mutex);

    return result;
}

/*Create a new P6 file to save the filtered image in. Write the header block
 e.g. P6
      Width Height
      Max color value
 then write the image data.
 The name of the new file shall be "filename" (the second argument).
 */
void write_image(PPMPixel *image, char *filename, unsigned long int width, unsigned long int height)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp)
    {
        fprintf(stderr, "Error: Unable to open file %s for writing\n", filename);
        exit(1);
    }
    pthread_mutex_lock(&time_mutex);
    // write the PPM header
    fprintf(fp, "P6\n%lu %lu\n%d\n", width, height, RGB_COMPONENT_COLOR);
    pthread_mutex_unlock(&time_mutex);

    // write the pixel data
    size_t pixel_count = width * height;
    if (fwrite(image, sizeof(PPMPixel), pixel_count, fp) != pixel_count)
    {
        fprintf(stderr, "Error: Failed to write pixel data to file %s\n", filename);
        fclose(fp);
        exit(1);
    }
    fclose(fp);
}

/* Open the filename image for reading, and parse it.
    Example of a ppm header:    //http://netpbm.sourceforge.net/doc/ppm.html
    P6                  -- image format
    # comment           -- comment lines begin with
    ## another comment  -- any number of comment lines
    200 300             -- image width & height
    255                 -- max color value

 Check if the image format is P6. If not, print invalid format error message.
 If there are comments in the file, skip them. You may assume that comments exist only in the header block.
 Read the image size information and store them in width and height.
 Check the rgb component, if not 255, display error message.
 Return: pointer to PPMPixel that has the pixel data of the input image (filename).The pixel data is stored in scanline order from left to right (up to bottom) in 3-byte chunks (r g b values for each pixel) encoded as binary numbers.
 */
PPMPixel *read_image(const char *filename, unsigned long int *width, unsigned long int *height)
{
    // open the file in binary mode
    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        fprintf(stderr, "Error: Unable to open file %s\n", filename);
        exit(1);
    }

    // make sure right format
    char magic[3];
    if (!fgets(magic, sizeof(magic), fp) || strncmp(magic, "P6", 2) != 0)
    {
        fprintf(stderr, "Error: Invalid format in file %s\n", filename);
        fclose(fp);
        exit(1);
    }

    // skip comments and read width, height, and max color value
    unsigned long int local_width, local_height;
    int max_color_value;

    while (1)
    {
        char line[128];
        if (!fgets(line, sizeof(line), fp))
        {
            fprintf(stderr, "Error: Unexpected end of file in header of %s\n", filename);
            fclose(fp);
            exit(1);
        }
        if (line[0] == '#')
            continue; // comment
        if (sscanf(line, "%lu %lu", &local_width, &local_height) == 2)
            break; // width and height
    }

    // read max color value
    while (1)
    {
        char line[128];
        if (!fgets(line, sizeof(line), fp))
        {
            fprintf(stderr, "Error: Unexpected end of file in header of %s\n", filename);
            fclose(fp);
            exit(1);
        }
        if (line[0] == '#')
            continue;
        if (sscanf(line, "%d", &max_color_value) == 1)
            break;
    }

    // make sure it's right
    if (max_color_value != RGB_COMPONENT_COLOR)
    {
        fprintf(stderr, "Error: Invalid max color value in file %s\n", filename);
        fclose(fp);
        exit(1);
    }

    // allocate mem
    size_t pixel_count = local_width * local_height;
    PPMPixel *image = (PPMPixel *)malloc(pixel_count * sizeof(PPMPixel));
    if (!image)
    {
        fprintf(stderr, "Error: Unable to allocate memory for image data\n");
        fclose(fp);
        exit(1);
    }

    // read pixel data
    if (fread(image, sizeof(PPMPixel), pixel_count, fp) != pixel_count)
    {
        fprintf(stderr, "Error: Unexpected end of file while reading pixel data in %s\n", filename);
        free(image);
        fclose(fp);
        exit(1);
    }

    fclose(fp);

    *width = local_width;
    *height = local_height;

    return image;
}

/* The thread function that manages an image file.
 Read an image file that is passed as an argument at runtime.
 Apply the Laplacian filter.
 Update the value of total_elapsed_time.
 Save the result image in a file called laplaciani.ppm, where i is the image file order in the passed arguments.
 Example: the result image of the file passed third during the input shall be called "laplacian3.ppm".
*/
void *manage_image_file(void *args)
{
    struct file_name_args *file_args = (struct file_name_args *)args;

    unsigned long int width, height;
    PPMPixel *image = read_image(file_args->input_file_name, &width, &height);

    double elapsed_time;
    PPMPixel *result = apply_filters(image, width, height, &elapsed_time);

    write_image(result, file_args->output_file_name, width, height);

    // Protect total_elapsed_time update with a mutex
    pthread_mutex_lock(&time_mutex);
    total_elapsed_time += elapsed_time;
    pthread_mutex_unlock(&time_mutex);

    free(image);
    free(result);
    free(file_args);

    return NULL;
}

/*The driver of the program. Check for the correct number of arguments. If wrong print the message: "Usage ./a.out filename[s]"
  It shall accept n filenames as arguments, separated by whitespace, e.g., ./a.out file1.ppm file2.ppm    file3.ppm
  It will create a thread for each input file to manage.
  It will print the total elapsed time in .4 precision seconds(e.g., 0.1234 s).
 */
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: ./a.out filename[s]\n");
        return 1;
    }
    pthread_mutex_init(&time_mutex, NULL); // initialize mutex

    pthread_t threads[argc - 1];

    // create each thread
    for (int i = 1; i < argc; i++)
    {
        pthread_mutex_lock(&time_mutex);
        struct file_name_args *args = (struct file_name_args *)malloc(sizeof(struct file_name_args));
        if (!args)
        {
            fprintf(stderr, "Error: Unable to allocate memory for file arguments.\n");
            return 1;
        }
        args->input_file_name = argv[i];
        sprintf(args->output_file_name, "laplacian%d.ppm", i);

        if (pthread_create(&threads[i - 1], NULL, manage_image_file, args) != 0)
        {
            fprintf(stderr, "Error: Unable to create thread for file %s.\n", argv[i]);
            free(args);
            return 1;
        }
        pthread_mutex_unlock(&time_mutex);
    }

    // wait for threads to finish
    for (int i = 0; i < argc - 1; i++)
    {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&time_mutex); // destroy mutex
    printf("Total elapsed time: %.4f s\n", total_elapsed_time);
    return 0;
}
