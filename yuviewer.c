#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#define MAX_HEADER_SIZE 1000

/* chroma subsampling modes */
#define C420 0
#define C422 1
#define C444 2

struct yuv_header {
    int w;
    int h;
    int f_num;
    int f_den;
    char interlacing;
    int a_num;
    int a_den;
    int c;
};

void read_header(FILE *fp, struct yuv_header *header) {
    char header_buffer[MAX_HEADER_SIZE];
    int i = 0;
    int j;
    char c;
    while ((i < MAX_HEADER_SIZE) && ((c = fgetc(fp)) != 0x0a)) {
        header_buffer[i++] = c;
    }
    header_buffer[i] = '\0';

    /* default C value*/
    header->c = C420;

    char *token = strtok(header_buffer, " ");
    while (token != NULL) {
        switch (token[0]) {
            case 'W': header->w = atoi(token + 1); break;
            case 'H': header->h = atoi(token + 1); break;
            case 'I': header->interlacing = token[1]; break;
            case 'A':
                j = 0;
                while (token[j] != '\0') {
                    if (token[j] == ':') { token[j] = '\0'; break; }
                    j++;
                }
                header->a_num = atoi(token + 1);
                header->a_den = atoi(token + j + 1);
            break;
            case 'F':
                j = 0;
                while (token[j] != '\0') {
                    if (token[j] == ':') { token[j] = '\0'; break; }
                    j++;
                }
                header->f_num = atoi(token + 1);
                header->f_den = atoi(token + j + 1);
            case 'C':
                if (strcmp(token + 1, "420") == 0) {
                    header->c = C420;
                } else if (strcmp(token + 1, "422") == 0) {
                    header->c = C422;
                } else if (strcmp(token + 1, "444") == 0) {
                    header->c = C444;
                }
        }
        token = strtok(NULL, " ");
    }
}

int read_frame(FILE *fp, uint8_t *y_frame, uint8_t *u_frame, uint8_t *v_frame, int y_len, int u_len, int v_len)
{
    char buffer[6];
    int ret;

    fread(buffer, 6, 1, fp);
    fread(y_frame, y_len, 1, fp);
    fread(u_frame, u_len, 1, fp);
    ret = fread(v_frame, v_len, 1, fp);

    return ret;
}

uint32_t yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v)
{
    int r = (int)(y + 1.13984 * (v - 128));
    int g = (int)(y - 0.39465 * (u - 128) - 0.58060 * (v - 128));
    int b = (int)(y + 2.03211 * (u - 128));

    /* truncate the value */
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;

    return (0xff << 24) | (r << 16) | (g << 8) | b;
}

void get_lengths(int *y_len, int *u_len, int *v_len, int w, int h, int mode)
{
    if (mode == C420) {
        *y_len = w * h;
        *u_len = w * h / 4;
        *v_len = *u_len;
    } else if (mode == C422) {
        *y_len = w * h;
        *u_len = w * h / 2;
        *v_len = *u_len;
    } else if (mode == C444) {
        *y_len = w * h;
        *u_len = *y_len;
        *v_len = *y_len;
    }
}

void read_pixel(uint8_t *y_frame, uint8_t *u_frame, uint8_t *v_frame, uint8_t *y, uint8_t *u, uint8_t *v, int j, int i, int w, int mode)
{
    if (mode == C420) {
        *y = y_frame[w * i + j];
        *u = u_frame[(w >> 1) * (i >> 1) + (j >> 1)];
        *v = v_frame[(w >> 1) * (i >> 1) + (j >> 1)];
    } else if (mode == C422) {
        *y = y_frame[w * i + j];
        *u = u_frame[(w >> 1) * i + (j >> 1)];
        *v = v_frame[(w >> 1) * i + (j >> 1)];
    } else if (mode == C444) {
        *y = y_frame[w * i + j];
        *u = u_frame[w * i + j];
        *v = v_frame[w * i + j];
    }
}

int main(int argc, char **argv)
{
    char *filename;
    FILE *fp;
    struct yuv_header header;
    int r, g, b;
    uint8_t y, u, v;
    int y_frame_length, u_frame_length, v_frame_length;
    uint64_t frame_start, frame_end, elapsed;
    float to_wait;
    int run;

    SDL_Event event;
    SDL_Renderer *renderer;
    SDL_Window *window;

    uint8_t *y_frame, *u_frame, *v_frame;

    if (argc < 2) {
        fprintf(stderr, "Usage: ./yuv [filename]\n");
        return 1;
    }

    filename = argv[1];

    fp = fopen(filename, "rb");
    read_header(fp, &header);

    float fps = (float)header.f_num / (float)header.f_den;
    float aspect_ratio = (float)header.a_num / (float)header.a_den;

    printf("Width: %d\n", header.w);
    printf("Height: %d\n", header.h);
    printf("FPS: %f\n", fps);
    printf("Aspect ratio: %f\n", aspect_ratio);
    printf("Interlacing: %c\n", header.interlacing);
    printf("C: %d\n", header.c);

    get_lengths(&y_frame_length, &u_frame_length, &v_frame_length, header.w, header.h, header.c);

    y_frame = malloc(y_frame_length * sizeof(char));
    u_frame = malloc(u_frame_length * sizeof(char));
    v_frame = malloc(v_frame_length * sizeof(char));

    SDL_Init(SDL_INIT_VIDEO);
    SDL_CreateWindowAndRenderer((500 * aspect_ratio), 500, 0, &window, &renderer);
    SDL_Rect frame_rect = {.x = 0, .y = 0, .w = (500 * aspect_ratio), .h = 500};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, header.w, header.h);
    uint32_t *video_buffer = malloc(header.w * header.h * sizeof(uint32_t));

    run = 1;
    while (run && read_frame(fp, y_frame, u_frame, v_frame, y_frame_length, u_frame_length, v_frame_length) == 1) {
        frame_start = SDL_GetPerformanceCounter();
        /* fill the video buffer */
        for (int i = 0; i < header.h; i++) {
            for (int j = 0; j < header.w; j++) {
                read_pixel(y_frame, u_frame, v_frame, &y, &u, &v, j, i, header.w, header.c);
                video_buffer[header.w * i + j] = yuv_to_rgb(y, u, v);
            }
        }

        /* display in the window */
        SDL_UpdateTexture(texture, NULL, video_buffer, header.w * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, &frame_rect);
        SDL_RenderPresent(renderer);
        
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) run = 0;
        }

        frame_end = SDL_GetPerformanceCounter();
        elapsed = frame_end - frame_start;
        to_wait = (1000.0 / fps) - 1000.0 * ((float)elapsed / SDL_GetPerformanceFrequency());
        if (to_wait < 0.0) to_wait = 0.0;
        SDL_Delay(to_wait);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    /* clean up */
    fclose(fp);
    free(y_frame);
    free(u_frame);
    free(v_frame);
    free(video_buffer);
    return 0;
}