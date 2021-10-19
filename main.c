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
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "");
    InitAudioDevice();
    const char *filepath = "./resources/Crystal Castles - Celestica.mp3";
    Music music = LoadMusicStream(filepath);
    SetMasterVolume(1);
    PlayMusicStream(music);

    char *Song = (char *)malloc(128 * sizeof(char));
    Song = GetFileNameWithoutExt(filepath);

    float timePlayed = 0.0f;
    bool pause = false;

    SetTargetFPS(100000);

    int ranArr[screenWidth];

    int sample_rate = music.stream.sampleRate;
    int channel_count = 0;
    double *data;
    int size;

    if (decode_audio_file(filepath, sample_rate, &channel_count, &data, &size) != 0)
    {
        return -1;
    }

    int cursor = 0;

    Vector2 *pts = malloc(sizeof(Vector2) * size);

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

        timePlayed = GetMusicTimePlayed(music) / GetMusicTimeLength(music) * screenWidth;

        BeginDrawing();
        DrawFPS(20, 20);
        ClearBackground(RAYWHITE);

        DrawText(Song, (screenWidth - MeasureText(Song, 50)) / 2,
                 (screenHeight - 150) / 2, 50, DARKGRAY);

        DrawRectangle(0, screenHeight - 20, screenWidth, 20, LIGHTGRAY);
        DrawRectangle(0, screenHeight - 20, (int)timePlayed * 2, 20, MAROON);

        //TODO: check out https://github.com/Crelloc/Music-Visualizer-Reboot/

        for (int i = 0; i < screenWidth; i++)
        {
            pts[i] = (Vector2){i, (screenHeight - 60 - data[(int)(((timePlayed * GetMusicTimeLength(music)) / 800) * sample_rate) + i]) - data[(int)(((timePlayed * GetMusicTimeLength(music)) / 800) * sample_rate) + i] * 15};
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
    AVCodecContext* codec_ctx = stream->codec;
    AVCodec *codec = codec_ctx->codec;

    codec_ctx->thread_count = 0; // set codec to automatically determine how many threads suits best for the decoding job

    if (codec->capabilities | AV_CODEC_CAP_FRAME_THREADS)
    codec_ctx->thread_type = FF_THREAD_FRAME;
    else if (codec->capabilities | AV_CODEC_CAP_SLICE_THREADS)
    codec_ctx->thread_type = FF_THREAD_SLICE;
    else
    codec_ctx->thread_count = 1; //don't use multithreading

    if (avcodec_open2(codec_ctx, avcodec_find_decoder(codec_ctx->codec_id), NULL) < 0) {
    AVCodecContext *codec = stream->codec;
    if (avcodec_open2(codec, avcodec_find_decoder(codec->codec_id), NULL) < 0)
    {
        fprintf(stderr, "Failed to open decoder for stream #%u in file '%s'\n", stream_index, path);
        return -1;
    }

    *channel_count = codec->channels;

    // prepare resampler
    struct SwrContext* swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_count",  codec_ctx->channels, 0);
    av_opt_set_int(swr, "out_channel_count", 1, 0);
    av_opt_set_int(swr, "in_channel_layout",  codec_ctx->channel_layout, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
    av_opt_set_int(swr, "in_sample_rate", codec_ctx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate", sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt",  codec_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_DBL,  0);
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
        if (avcodec_decode_audio4(codec_ctx, frame, &gotFrame, &packet) < 0) {
            break;
        }
        if (!gotFrame)
        {
            continue;
        }
        // resample frames
        double *buffer;
        av_samples_alloc((uint8_t **)&buffer, NULL, 1, frame->nb_samples, AV_SAMPLE_FMT_DBL, 0);
        int frame_count = swr_convert(swr, (uint8_t **)&buffer, frame->nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
        // append resampled frames to data
        *data = (double *)realloc(*data, (*size + frame->nb_samples) * sizeof(double));
        memcpy(*data + *size, buffer, frame_count * sizeof(double));
        *size += frame_count;
    }

    // clean up
    av_frame_free(&frame);
    swr_free(&swr);
    avcodec_close(codec_ctx);
    avformat_free_context(format);

    // success
    return 0;
}
