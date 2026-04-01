/*
 * cygnus-media
 * A minimalist media player for Cygnus WM.
 * Supports free formats
 *
 * by Jonathan Torres
 * 
 * This program is free software: you can redistribute it and/or modify it under the terms of the 
 * GNU General Public License as published by the Free Software Foundation, either version 3 of 
 * the License, or (at your option) any later version.
 * 
 */


#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_ttf.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <math.h>

#define COL_BG 0x1a1a1a
#define COL_FG 0xffffff
#define COL_BTN 0x222222
#define COL_HL 0x3366ff

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define CONTROL_HEIGHT 50
#define BTN_WIDTH 70
#define BTN_HEIGHT 30

typedef struct MyPacketList {
    AVPacket pkt;
    struct MyPacketList *next;
} MyPacketList;

typedef struct PacketQueue {
    MyPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;

int quit = 0;
int pause_state = 0;
int volume = 100;
int audio_finished = 0;

void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {
    MyPacketList *pkt1 = av_malloc(sizeof(MyPacketList));
    if (!pkt1) return -1;
    av_packet_move_ref(&pkt1->pkt, pkt);
    pkt1->next = NULL;

    SDL_LockMutex(q->mutex);
    if (!q->last_pkt) q->first_pkt = pkt1;
    else q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
    return 0;
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
    MyPacketList *pkt1;
    int ret;
    SDL_LockMutex(q->mutex);
    for(;;) {
        if (quit) { ret = -1; break; }
        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt) q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            av_packet_move_ref(pkt, &pkt1->pkt);
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

void packet_queue_flush(PacketQueue *q) {
    SDL_LockMutex(q->mutex);
    MyPacketList *pkt, *pkt1;
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_free(pkt);
    }
    q->first_pkt = q->last_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    SDL_UnlockMutex(q->mutex);
}

void packet_queue_abort(PacketQueue *q) {
    SDL_LockMutex(q->mutex);
    quit = 1;
    SDL_CondSignal(q->cond);
    SDL_UnlockMutex(q->mutex);
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf) {
    static AVPacket pkt;
    static AVFrame *frame = NULL;
    static SwrContext *swr_ctx = NULL;
    int data_size = 0;

    if (!frame) frame = av_frame_alloc();

    for(;;) {
        while (avcodec_receive_frame(aCodecCtx, frame) == 0) {
            if (!swr_ctx) {
                swr_ctx = swr_alloc();
                av_opt_set_chlayout(swr_ctx, "in_chlayout", &aCodecCtx->ch_layout, 0);
                av_opt_set_int(swr_ctx, "in_sample_rate", aCodecCtx->sample_rate, 0);
                av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", aCodecCtx->sample_fmt, 0);

                AVChannelLayout out_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
                av_opt_set_chlayout(swr_ctx, "out_chlayout", &out_layout, 0);
                av_opt_set_int(swr_ctx, "out_sample_rate", aCodecCtx->sample_rate, 0);
                av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                swr_init(swr_ctx);
            }

            uint8_t *out_data[1] = { audio_buf };
            int out_samples = swr_convert(swr_ctx, out_data, MAX_AUDIO_FRAME_SIZE / 4,
                                          (const uint8_t **)frame->data, frame->nb_samples);

            data_size = out_samples * 2 * 2; // 2 channels * 2 bytes (S16)
            return data_size;
        }

        if (pkt.data) av_packet_unref(&pkt);
        if (quit) return -1;
        if (audio_finished) return -1;
        if (packet_queue_get(&audioq, &pkt, 1) < 0) return -1;

        if (avcodec_send_packet(aCodecCtx, &pkt) < 0) return -1;
    }
}

void audio_callback(void *userdata, Uint8 *stream, int len) {
    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
    int len1, audio_size;
    static uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while(len > 0) {
        if(audio_buf_index >= audio_buf_size) {
            audio_size = audio_decode_frame(aCodecCtx, audio_buf);
            if(audio_size < 0) {
                audio_buf_size = 1024;
                memset(audio_buf, 0, audio_buf_size);
            } else {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if(len1 > len) len1 = len;

        int16_t *src16 = (int16_t *)(audio_buf + audio_buf_index);
        int16_t *stream16 = (int16_t *)stream;
        int samples = len1 / 2; // S16 is 2 bytes per sample
        for (int i=0; i<samples; i++) {
            int val = src16[i];
            val = (val * volume) / 100;
            if (val > 32767) val = 32767;
            if (val < -32768) val = -32768;
            stream16[i] = (int16_t)val;
        }

        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}

void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    SDL_Surface *surface = TTF_RenderText_Solid(font, text, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

//button pos
SDL_Rect btn_rwd_rect, btn_play_rect, btn_fwd_rect, btn_vold_rect, btn_volu_rect;

void update_button_rects(int w, int h) {
    int btn_y = h - CONTROL_HEIGHT + 10;
    int spacing = 10;
    int right_btn_width = BTN_WIDTH * 2 + spacing;
    int min_width = 10 + BTN_WIDTH + spacing + (BTN_WIDTH + 20) + spacing + BTN_WIDTH + spacing + 10 + right_btn_width + 10;
    int effective_width = w > min_width ? w : min_width;
    int x = 10;

    btn_rwd_rect = (SDL_Rect){x, btn_y, BTN_WIDTH, BTN_HEIGHT};
    x += BTN_WIDTH + spacing;

    btn_play_rect = (SDL_Rect){x, btn_y, BTN_WIDTH + 20, BTN_HEIGHT};
    x += BTN_WIDTH + 20 + spacing;

    btn_fwd_rect = (SDL_Rect){x, btn_y, BTN_WIDTH, BTN_HEIGHT};

    x = effective_width - right_btn_width - 10;
    btn_vold_rect = (SDL_Rect){x, btn_y, BTN_WIDTH, BTN_HEIGHT};
    x += BTN_WIDTH + spacing;
    btn_volu_rect = (SDL_Rect){x, btn_y, BTN_WIDTH, BTN_HEIGHT};
}

void draw_ui(SDL_Renderer *renderer, TTF_Font *font, int w, int h) {
    SDL_Color white = {255, 255, 255, 255};

	//ctrl bar bg
    SDL_Rect rect = {0, h - CONTROL_HEIGHT, w, CONTROL_HEIGHT};
    SDL_SetRenderDrawColor(renderer, 0x22, 0x22, 0x22, 255);
    SDL_RenderFillRect(renderer, &rect);

    update_button_rects(w, h);

    //rwd
    SDL_SetRenderDrawColor(renderer, 0x33, 0x33, 0x33, 255);
    SDL_RenderFillRect(renderer, &btn_rwd_rect);
    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 255);
    SDL_RenderDrawRect(renderer, &btn_rwd_rect);
    int text_w = 0, text_h = 0;
    TTF_SizeText(font, "rwd", &text_w, &text_h);
    draw_text(renderer, font, "rwd", btn_rwd_rect.x + (BTN_WIDTH - text_w)/2, btn_rwd_rect.y + (BTN_HEIGHT - text_h)/2, white);

    //play/pause
    if (pause_state) SDL_SetRenderDrawColor(renderer, 0x55, 0x55, 0x55, 255);
    else SDL_SetRenderDrawColor(renderer, 0x33, 0x66, 0xff, 255);
    SDL_RenderFillRect(renderer, &btn_play_rect);
    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 255);
    SDL_RenderDrawRect(renderer, &btn_play_rect);
    const char *play_label = pause_state ? "play" : "pause";
    TTF_SizeText(font, play_label, &text_w, &text_h);
    draw_text(renderer, font, play_label, btn_play_rect.x + (btn_play_rect.w - text_w)/2, btn_play_rect.y + (BTN_HEIGHT - text_h)/2, white);

    //fwd
    SDL_SetRenderDrawColor(renderer, 0x33, 0x33, 0x33, 255);
    SDL_RenderFillRect(renderer, &btn_fwd_rect);
    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 255);
    SDL_RenderDrawRect(renderer, &btn_fwd_rect);
    TTF_SizeText(font, "fwd", &text_w, &text_h);
    draw_text(renderer, font, "fwd", btn_fwd_rect.x + (BTN_WIDTH - text_w)/2, btn_fwd_rect.y + (BTN_HEIGHT - text_h)/2, white);

    //vol-
    SDL_SetRenderDrawColor(renderer, 0x33, 0x33, 0x33, 255);
    SDL_RenderFillRect(renderer, &btn_vold_rect);
    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 255);
    SDL_RenderDrawRect(renderer, &btn_vold_rect);
    TTF_SizeText(font, "Vol-", &text_w, &text_h);
    draw_text(renderer, font, "Vol-", btn_vold_rect.x + (BTN_WIDTH - text_w)/2, btn_vold_rect.y + (BTN_HEIGHT - text_h)/2, white);

    //vol+
    SDL_SetRenderDrawColor(renderer, 0x33, 0x33, 0x33, 255);
    SDL_RenderFillRect(renderer, &btn_volu_rect);
    SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 255);
    SDL_RenderDrawRect(renderer, &btn_volu_rect);
    TTF_SizeText(font, "Vol+", &text_w, &text_h);
    draw_text(renderer, font, "Vol+", btn_volu_rect.x + (BTN_WIDTH - text_w)/2, btn_volu_rect.y + (BTN_HEIGHT - text_h)/2, white);
}

int point_in_rect(int x, int y, SDL_Rect *r) {
    return (x >= r->x && x < r->x + r->w &&
            y >= r->y && y < r->y + r->h);
}
//using cygnus-open to choose files
char* pick_file() {
    static char filename[1024];
    filename[0] = '\0';

    FILE *fp = popen("cygnus-open -media", "r");
    if (fp) {
        if (fgets(filename, sizeof(filename), fp)) {
            filename[strcspn(filename, "\n")] = 0;
        }
        pclose(fp);
        if (strlen(filename) > 0) {
            return filename;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    char *filename = NULL;
    char filename_buf[1024];

    if (argc < 2) {
        filename = pick_file();
        if (!filename || strlen(filename) == 0) {
            fprintf(stderr, "No file selected. Usage: %s <media_file>\n", argv[0]);
            return 1;
        }
        strncpy(filename_buf, filename, sizeof(filename_buf) - 1);
        filename_buf[sizeof(filename_buf) - 1] = '\0';
        filename = filename_buf;
    } else {
        filename = argv[1];
    }

    AVFormatContext *pFormatCtx = NULL;
    int videoStream = -1, audioStream = -1;
    AVCodecContext *pCodecCtx = NULL;
    AVCodecContext *aCodecCtx = NULL;
    const AVCodec *pCodec = NULL;
    const AVCodec *aCodec = NULL;
    AVFrame *pFrame = NULL;
    AVPacket *packet = av_packet_alloc();

    if(avformat_open_input(&pFormatCtx, filename, NULL, NULL)!=0) {
        fprintf(stderr, "Could not open file: %s\n", filename);
        return -1;
    }
    if(avformat_find_stream_info(pFormatCtx, NULL)<0) {
        fprintf(stderr, "Could not find stream info\n");
        return -1;
    }

    for(int i=0; i<(int)pFormatCtx->nb_streams; i++) {
        if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && videoStream < 0)
            videoStream=i;
        if(pFormatCtx->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO && audioStream < 0)
            audioStream=i;
    }

    if(videoStream==-1 && audioStream==-1) {
        fprintf(stderr, "No audio or video streams found\n");
        return -1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
        return -1;
    }

    if (TTF_Init() == -1) {
        fprintf(stderr, "Could not initialize SDL_ttf: %s\n", TTF_GetError());
        SDL_Quit();
        return -1;
    }

    if (audioStream >= 0) {
        aCodec = avcodec_find_decoder(pFormatCtx->streams[audioStream]->codecpar->codec_id);
        aCodecCtx = avcodec_alloc_context3(aCodec);
        avcodec_parameters_to_context(aCodecCtx, pFormatCtx->streams[audioStream]->codecpar);
        avcodec_open2(aCodecCtx, aCodec, NULL);

        SDL_AudioSpec wanted_spec, spec;
        wanted_spec.freq = aCodecCtx->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = 2; //force stereo
        wanted_spec.silence = 0;
        wanted_spec.samples = 4096; //bigger buffer to prevent choppy audio
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = aCodecCtx;

        if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
            fprintf(stderr, "Could not open audio: %s\n", SDL_GetError());
        }
        packet_queue_init(&audioq);
        SDL_PauseAudio(0);
    }

    if (videoStream >= 0) {
        pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
        pCodecCtx = avcodec_alloc_context3(pCodec);
        avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
        avcodec_open2(pCodecCtx, pCodec, NULL);
        pFrame = av_frame_alloc();
    }

    SDL_Window *window = SDL_CreateWindow("Cygnus Media Player",
                          SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                          500, 100, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);//width, height (always forget which is which)
    if (!window) {
        fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, 0);
    }
    SDL_Texture *bmp = NULL;

    if (videoStream >= 0) {
        bmp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
    }
//try to load font
    TTF_Font *font = NULL;
    const char *font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/gnu-free/FreeSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/Windows/Fonts/arial.ttf",
        NULL
    };

    for (int i = 0; font_paths[i]; i++) {
        font = TTF_OpenFont(font_paths[i], 12);
        if (font) break;
    }

    if (!font) {
        fprintf(stderr, "Warning: Could not load font, buttons will not have labels\n");
    }

    SDL_Event event;
    [[maybe_unused]] int has_video_ended = 0;

    while(!quit) {
        while(SDL_PollEvent(&event)) {
            if(event.type == SDL_QUIT) quit = 1;
            else if (event.type == SDL_MOUSEBUTTONDOWN) {
                int mx = event.button.x;
                int my = event.button.y;
                if (point_in_rect(mx, my, &btn_rwd_rect)) {
                    if (pFormatCtx && audioStream >= 0) {
                        int64_t seek_target = av_rescale_q(-10, (AVRational){1, 1}, pFormatCtx->streams[audioStream]->time_base);
                        av_seek_frame(pFormatCtx, audioStream, seek_target, AVSEEK_FLAG_BACKWARD);
                        packet_queue_flush(&audioq);
                    }
                } else if (point_in_rect(mx, my, &btn_play_rect)) {
                    pause_state = !pause_state;
                    if (audioStream>=0) SDL_PauseAudio(pause_state);
                } else if (point_in_rect(mx, my, &btn_fwd_rect)) {
                    if (pFormatCtx && audioStream >= 0) {
                        int64_t seek_target = av_rescale_q(10, (AVRational){1, 1}, pFormatCtx->streams[audioStream]->time_base);
                        av_seek_frame(pFormatCtx, audioStream, seek_target, 0);
                        packet_queue_flush(&audioq);
                    }
                } else if (point_in_rect(mx, my, &btn_vold_rect)) {
                    volume = (volume > 10) ? volume - 10 : 0;
                } else if (point_in_rect(mx, my, &btn_volu_rect)) {
                    volume = (volume < 90) ? volume + 10 : 100;
                }
            }
            else if (event.type == SDL_KEYDOWN) {
                switch(event.key.keysym.sym) {
                    case SDLK_SPACE:
                        pause_state = !pause_state;
                        if (audioStream>=0) SDL_PauseAudio(pause_state);
                        break;
                    case SDLK_UP:
                        volume = (volume < 90) ? volume + 10 : 100;
                        break;
                    case SDLK_DOWN:
                        volume = (volume > 10) ? volume - 10 : 0;
                        break;
                    case SDLK_LEFT:
                        //rw 10 secs
                        if (pFormatCtx && audioStream >= 0) {
                            int64_t seek_target = av_rescale_q(-10, (AVRational){1, 1}, pFormatCtx->streams[audioStream]->time_base);
                            av_seek_frame(pFormatCtx, audioStream, seek_target, AVSEEK_FLAG_BACKWARD);
                            packet_queue_flush(&audioq);
                        }
                        break;
                    case SDLK_RIGHT:
                        //fw 10 secs
                        if (pFormatCtx && audioStream >= 0) {
                            int64_t seek_target = av_rescale_q(10, (AVRational){1, 1}, pFormatCtx->streams[audioStream]->time_base);
                            av_seek_frame(pFormatCtx, audioStream, seek_target, 0);
                            packet_queue_flush(&audioq);
                        }
                        break;
                    case SDLK_q:
                        if (event.key.keysym.mod & KMOD_ALT) quit = 1;
                        break;
                    default: break;
                }
            }
        }

        int w, h;
        SDL_GetWindowSize(window, &w, &h);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        int audio_queue_target = audioStream >= 0 ? 500000 : 0;
        int max_packets_per_frame = 50;

        for (int pkt_count = 0; pkt_count < max_packets_per_frame && !pause_state && !audio_finished; pkt_count++) {
            if (audioStream >= 0 && audioq.size > audio_queue_target) {
                break; 
            }

            if (av_read_frame(pFormatCtx, packet) >= 0) {
                if (videoStream >= 0 && packet->stream_index == videoStream) {
                    avcodec_send_packet(pCodecCtx, packet);
                    while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
                        if (bmp) {
                            SDL_UpdateYUVTexture(bmp, NULL,
                                                 pFrame->data[0], pFrame->linesize[0],
                                                 pFrame->data[1], pFrame->linesize[1],
                                                 pFrame->data[2], pFrame->linesize[2]);
                            SDL_Rect dst = {0, 0, w, h - CONTROL_HEIGHT};
                            SDL_RenderCopy(renderer, bmp, NULL, &dst);
                        }
                    }
                    av_packet_unref(packet);
                } else if (audioStream >= 0 && packet->stream_index == audioStream) {
                    packet_queue_put(&audioq, packet);
                } else {
                    av_packet_unref(packet);
                }
            } else {
                audio_finished = 1;
                break;
            }
        }

        draw_ui(renderer, font, w, h);

        SDL_RenderPresent(renderer);

        SDL_Delay(5);
    }

    if (font) TTF_CloseFont(font);
    if (pFrame) av_frame_free(&pFrame);
    if (packet) av_packet_free(&packet);
    if(videoStream>=0) avcodec_free_context(&pCodecCtx);
    if(audioStream>=0) avcodec_free_context(&aCodecCtx);
    if(bmp) SDL_DestroyTexture(bmp);
    if(renderer) SDL_DestroyRenderer(renderer);
    if(window) SDL_DestroyWindow(window);
    avformat_close_input(&pFormatCtx);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
