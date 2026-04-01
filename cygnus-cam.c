/*
 * cygnus-cam
 * Minimal webcam application using V4L2 and SDL2
 * 
 * by Jonathan Torres
 *
 * This app uses stb_image_write.h for direct JPEG/PNG output.
 * stb_image_write.h was written by Sean Barret, and is used under the MIT license.
 * License text included within the header file.
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the 
 * GNU General Public License as published by the Free Software Foundation, either version 3 of 
 * the License, or (at your option) any later version.
 *
 */

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/videodev2.h>
#include <SDL2/SDL.h>

#define DEVICE "/dev/video0"
#define WIDTH 640
#define HEIGHT 480
#define BUTTON_WIDTH 120
#define BUTTON_HEIGHT 50
#define PREVIEW_HEIGHT 480
#define WINDOW_HEIGHT 560

struct buffer {
    void *start;
    size_t length;
};

static int fd = -1;
static struct buffer *buffers = NULL;
static unsigned int n_buffers = 0;
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;

static int xioctl(int fh, int request, void *arg)
{
    int r;
    do {
        r = ioctl(fh, request, arg);
    } while (r == -1 && EINTR == errno);
    return r;
}

static int init_camera(void)
{
    struct v4l2_format fmt = {0};
    struct v4l2_requestbuffers req = {0};
    struct v4l2_buffer buf = {0};
    enum v4l2_buf_type type;

    fd = open(DEVICE, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        perror("Cannot open device");
        return -1;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return -1;
    }

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    buffers = calloc(req.count, sizeof(*buffers));
    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = n_buffers;

        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL, buf.length,
                PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

        if (MAP_FAILED == buffers[n_buffers].start) {
            perror("mmap");
            return -1;
        }
    }

    for (unsigned int i = 0; i < n_buffers; ++i) {
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        xioctl(fd, VIDIOC_QBUF, &buf);
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }

    return 0;
}

static void yuyv_to_rgb24(const unsigned char *yuyv, unsigned char *rgb, int width, int height)
{
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j += 2) {
            int index = i * width + j;
            int y0 = yuyv[index * 2];
            int u = yuyv[index * 2 + 1];
            int y1 = yuyv[index * 2 + 2];
            int v = yuyv[index * 2 + 3];

            int c = y0 - 16;
            int d = u - 128;
            int e = v - 128;

            int r = (298 * c + 409 * e + 128) >> 8;
            int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            int b = (298 * c + 516 * d + 128) >> 8;

            rgb[index * 3] = r < 0 ? 0 : (r > 255 ? 255 : r);
            rgb[index * 3 + 1] = g < 0 ? 0 : (g > 255 ? 255 : g);
            rgb[index * 3 + 2] = b < 0 ? 0 : (b > 255 ? 255 : b);

            c = y1 - 16;
            r = (298 * c + 409 * e + 128) >> 8;
            g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            b = (298 * c + 516 * d + 128) >> 8;

            rgb[(index + 1) * 3] = r < 0 ? 0 : (r > 255 ? 255 : r);
            rgb[(index + 1) * 3 + 1] = g < 0 ? 0 : (g > 255 ? 255 : g);
            rgb[(index + 1) * 3 + 2] = b < 0 ? 0 : (b > 255 ? 255 : b);
        }
    }
}

static int capture_frame(unsigned char **rgb_data)
{
    struct v4l2_buffer buf = {0};
    fd_set fds;
    struct timeval tv;
    int r;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    r = select(fd + 1, &fds, NULL, NULL, &tv);
    if (r < 0) return -1;
    if (r == 0) return -1;

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno != EAGAIN) perror("VIDIOC_DQBUF");
        return -1;
    }

    *rgb_data = malloc(WIDTH * HEIGHT * 3);
    yuyv_to_rgb24(buffers[buf.index].start, *rgb_data, WIDTH, HEIGHT);

    if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
    }

    return 0;
}

static void draw_button(void)
{
    SDL_Rect button = {
        (WIDTH - BUTTON_WIDTH) / 2,
        PREVIEW_HEIGHT + (WINDOW_HEIGHT - PREVIEW_HEIGHT - BUTTON_HEIGHT) / 2,
        BUTTON_WIDTH,
        BUTTON_HEIGHT
    };

    SDL_SetRenderDrawColor(renderer, 200, 50, 50, 255);
    SDL_RenderFillRect(renderer, &button);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderDrawRect(renderer, &button);
}


static void cleanup_camera(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMOFF, &type);

    for (unsigned int i = 0; i < n_buffers; ++i)
        munmap(buffers[i].start, buffers[i].length);

    free(buffers);
    close(fd);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (init_camera() < 0) {
        fprintf(stderr, "Failed to initialize camera\n");
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("Webcam",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
            SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        return 1;
    }

    int running = 1;
    SDL_Event evt;
    unsigned char *rgb_data = NULL;

    while (running) {
        while (SDL_PollEvent(&evt)) {
            if (evt.type == SDL_QUIT) {
                running = 0;
            } else if (evt.type == SDL_MOUSEBUTTONDOWN) {
                int mx = evt.button.x;
                int my = evt.button.y;
                int bx = (WIDTH - BUTTON_WIDTH) / 2;
                int by = PREVIEW_HEIGHT + (WINDOW_HEIGHT - PREVIEW_HEIGHT - BUTTON_HEIGHT) / 2;

                if (mx >= bx && mx <= bx + BUTTON_WIDTH &&
                    my >= by && my <= by + BUTTON_HEIGHT) {

                    if (!rgb_data) continue;

                    char path[512];
                    snprintf(path, sizeof(path), "%s/Pictures", getenv("HOME"));
                    mkdir(path, 0755);

                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);
                    char filename[1024];
                    snprintf(filename, sizeof(filename), "%s/webcam_%04d%02d%02d_%02d%02d%02d.jpg",
                            path, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                            tm->tm_hour, tm->tm_min, tm->tm_sec);

                    if (stbi_write_jpg(filename, WIDTH, HEIGHT, 3, rgb_data, 90)) {
                        printf("Saved: %s\n", filename);
                    } else {
                        fprintf(stderr, "Failed to save image\n");
                    }
                }
            }
        }

        if (capture_frame(&rgb_data) == 0) {
            SDL_UpdateTexture(texture, NULL, rgb_data, WIDTH * 3);
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        SDL_Rect dst = {0, 0, WIDTH, HEIGHT};
        SDL_RenderCopy(renderer, texture, NULL, &dst);

        draw_button();

        SDL_RenderPresent(renderer);
        SDL_Delay(33);
    }

    free(rgb_data);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    cleanup_camera();

    return 0;
}
