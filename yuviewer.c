#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#define MAX_HEADER_SIZE 1000

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

int main(int argc, char **argv)
{
    char *filename;
    FILE *fp;
    struct yuv_header header;
    int r, g, b;
    uint8_t y, u, v;

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

    int y_frame_length = header.w * header.h;
    int u_frame_length = header.w * header.h / 4;
    int v_frame_length = u_frame_length;

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

    while (read_frame(fp, y_frame, u_frame, v_frame, y_frame_length, u_frame_length, v_frame_length) == 1) {
        for (int i = 0; i < header.h; i++) {
            for (int j = 0; j < header.w; j++) {
                y = y_frame[header.w * i + j];
                u = u_frame[(header.w >> 1) * (i >> 1) + (j >> 1)];
                v = v_frame[(header.w >> 1) * (i >> 1) + (j >> 1)];

                video_buffer[header.w * i + j] = yuv_to_rgb(y, u, v);
            }
        }

        /* display in the window */
        SDL_UpdateTexture(texture, NULL, video_buffer, header.w * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, &frame_rect);
        SDL_RenderPresent(renderer);
        
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) return 0;
        }
        SDL_Delay(1000.0 / fps);
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