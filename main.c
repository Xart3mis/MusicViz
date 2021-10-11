#include "libswresample/swresample.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"

#include "raylib.h"

#include <stdlib.h>
#include <stdio.h>

int decode_audio_file(const char *path, const int sample_rate, int *channel_count, double **data, int *size);

int main(void)
{
    InitAudioDevice();

    const char *filepath = "./resources/fiend - morissey.mp3";

    Music music = LoadMusicStream(filepath);
    PlayMusicStream(music);

    music.looping = false;

    double *data = (double*)malloc(sizeof(double)*music.stream.sampleSize);
    int size = (int)(music.stream.sampleSize/1.5);
    int sample_rate = music.stream.sampleRate;
    int channel_count = music.stream.channels;

    if (decode_audio_file(filepath, sample_rate, &channel_count, &data, &size) != 0)
    {
        return -1;
    }

    int cursor = 0;

    char *Song = (char *)malloc(sizeof(char) * 128);
    Song = GetFileNameWithoutExt(filepath);

    const int screenHeight = 450;
    const int screenWidth = 800;
    SetConfigFlags(FLAG_WINDOW_ALWAYS_RUN | FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, Song);

    Vector2 *pts = malloc(sizeof(Vector2) * screenWidth);

    float timePlayed = 0.0f;
    bool pause = false;

    SetTargetFPS(3000);

    while (!WindowShouldClose())
    {
        UpdateMusicStream(music);

        if (IsKeyPressed(KEY_R))
        {
            StopMusicStream(music);
            PlayMusicStream(music);
        }

        if (IsKeyPressed(KEY_SPACE))
        {
            pause = !pause;

            if (pause)
                PauseMusicStream(music);
            else
                ResumeMusicStream(music);
        }

        timePlayed = (GetMusicTimePlayed(music) / GetMusicTimeLength(music)) * screenWidth;

        BeginDrawing();
        DrawFPS(20, 20);
        ClearBackground(RAYWHITE);

        DrawText(Song, (screenWidth - MeasureText(Song, screenWidth/16)) / 2, (screenHeight - 150) / 2, screenWidth/16, DARKGRAY);

        DrawRectangle(0, screenHeight - (screenHeight/24), screenWidth, (screenHeight/24), LIGHTGRAY);
        DrawRectangle(0, screenHeight - (screenHeight/24), (int)timePlayed, (screenHeight/24), MAROON);


        for (int i = 0; i < screenWidth; i++)
        {
            cursor = (int)(((timePlayed * GetMusicTimeLength(music)) / screenWidth) * sample_rate);
            if(cursor > size) {
                break;
            }

            pts[i] = (Vector2){i, (screenHeight - (screenHeight/7) - data[cursor]) - data[cursor] * (screenHeight/30)};
            cursor += i;
        }

        DrawLineStrip(pts, screenWidth, BLACK);
        EndDrawing();
    }

    UnloadMusicStream(music);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}

int decode_audio_file(const char *path, const int sample_rate, int *channel_count, double **data, int *size)
{
    // initialize all muxers, demuxers and protocols for libavformat
    // (does nothing if called twice during the course of one program execution)
    av_register_all();

    // get format from audio file
    AVFormatContext *format = avformat_alloc_context();
    if (avformat_open_input(&format, path, NULL, NULL) != 0)
    {
        fprintf(stderr, "Could not open file '%s'\n", path);
        return -1;
    }
    if (avformat_find_stream_info(format, NULL) < 0)
    {
        fprintf(stderr, "Could not retrieve stream info from file '%s'\n", path);
        return -1;
    }

    // Find the index of the first audio stream
    int stream_index = -1;
    for (int i = 0; i < format->nb_streams; i++)
    {
        if (format->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            stream_index = i;
            break;
        }
    }

    if (stream_index == -1)
    {
        fprintf(stderr, "Could not retrieve audio stream from file '%s'\n", path);
        return -1;
    }

    AVStream *stream = format->streams[stream_index];

    // find & open codec
    AVCodecContext *codec = stream->codec;
    if (avcodec_open2(codec, avcodec_find_decoder(codec->codec_id), NULL) < 0)
    {
        fprintf(stderr, "Failed to open decoder for stream #%u in file '%s'\n", stream_index, path);
        return -1;
    }

    *channel_count = codec->channels;

    // prepare resampler
    struct SwrContext *swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_count", codec->channels, 0);
    av_opt_set_int(swr, "out_channel_count", 1, 0);
    av_opt_set_int(swr, "in_channel_layout", codec->channel_layout, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
    av_opt_set_int(swr, "in_sample_rate", codec->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", codec->sample_fmt, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_DBLP, 0);
    swr_init(swr);
    if (!swr_is_initialized(swr))
    {
        fprintf(stderr, "Resampler has not been properly initialized\n");
        return -1;
    }

    // prepare to read data
    AVPacket packet;
    av_init_packet(&packet);
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        fprintf(stderr, "Error allocating the frame\n");
        return -1;
    }

    // iterate through frames
    *data = NULL;
    *size = 0;
    while (av_read_frame(format, &packet) >= 0)
    {
        // decode one frame
        int gotFrame;
        if (avcodec_decode_audio4(codec, frame, &gotFrame, &packet) < 0)
        {
            break;
        }
        if (!gotFrame)
        {
            continue;
        }
        // resample frames
        double *buffer;
        av_samples_alloc((uint8_t **)&buffer, NULL, 1, frame->nb_samples, AV_SAMPLE_FMT_DBLP, 0);
        int frame_count = swr_convert(swr, (uint8_t **)&buffer, frame->nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
        // append resampled frames to data
        *data = (double *)realloc(*data, (*size + frame->nb_samples) * sizeof(double));
        memcpy(*data + *size, buffer, frame_count * sizeof(double));
        *size += frame_count;
    }

    // clean up
    av_frame_free(&frame);
    swr_free(&swr);
    avcodec_close(codec);
    avformat_free_context(format);

    // success
    return 0;
}