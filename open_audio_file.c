#include "libswresample/swresample.h"
#include "libavutil/mathematics.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
#include <ao/ao.h>

#include "raylib.h"

#include <stdlib.h>
#include <stdio.h>


#define OUTPUT_CHANNELS 2
#define OUTPUT_RATE 44100
#define BUFFER_SIZE 192000
#define OUTPUT_BITS 16
#define OUTPUT_FMT AV_SAMPLE_FMT_S16

static char *errtext (int err) {
    static char errbuff[256];
    av_strerror(err,errbuff,sizeof(errbuff));
    return errbuff;
}

static int open_audio_file (const char *filename, AVFormatContext **context, AVCodecContext **codec_context) {
    AVCodecContext *avctx;
    AVCodec *codec;
    int ret;
    int stream_id;
    int i;

    // Open input file
    if ((ret = avformat_open_input(context,filename,NULL,NULL)) < 0) {
        fprintf(stderr,"Error opening input file '%s': %s\n",filename,errtext(ret));
        *context = NULL;
        return ret;
    }

    // Get stream info
    if ((ret = avformat_find_stream_info(*context,NULL)) < 0) {
        fprintf(stderr,"Unable to find stream info: %s\n",errtext(ret));
        avformat_close_input(context);
        return ret;
    }

    // Find the best stream
    if ((stream_id = av_find_best_stream(*context,AVMEDIA_TYPE_AUDIO,-1,-1,&codec,0)) < 0) {
        fprintf(stderr,"Unable to find valid audio stream: %s\n",errtext(stream_id));
        avformat_close_input(context);
        return stream_id;
    }

    // Allocate a decoding context
    if (!(avctx = avcodec_alloc_context3(codec))) {
        fprintf(stderr,"Unable to allocate decoder context\n");
        avformat_close_input(context);
        return AVERROR(ENOMEM);
    }

    // Initialize stream parameters
    if ((ret = avcodec_parameters_to_context(avctx,(*context)->streams[stream_id]->codecpar)) < 0) {
        fprintf(stderr,"Unable to get stream parameters: %s\n",errtext(ret));
        avformat_close_input(context);
        avcodec_free_context(&avctx);
        return ret;
    }

    // Open the decoder
    if ((ret = avcodec_open2(avctx,codec,NULL)) < 0) {
        fprintf(stderr,"Could not open codec: %s\n",errtext(ret));
        avformat_close_input(context);
        avcodec_free_context(&avctx);
        return ret;
    }

    *codec_context = avctx;
    return 0;
}

static void init_packet (AVPacket *packet) {
    av_init_packet(packet);
    packet->data = NULL;
    packet->size = 0;
}

static int init_resampler (AVCodecContext *codec_context, SwrContext **resample_context) {
    int ret;

    // Set resampler options
    *resample_context = swr_alloc_set_opts(NULL,
                                           av_get_default_channel_layout(OUTPUT_CHANNELS),
                                           OUTPUT_FMT,
                                           codec_context->sample_rate,
                                           av_get_default_channel_layout(codec_context->channels),
                                           codec_context->sample_fmt,
                                           codec_context->sample_rate,
                                           0,NULL);
    if (!(*resample_context)) {
        fprintf(stderr,"Unable to allocate resampler context\n");
        return AVERROR(ENOMEM);
    }

    // Open the resampler
    if ((ret = swr_init(*resample_context)) < 0) {
        fprintf(stderr,"Unable to open resampler context: %s\n",errtext(ret));
        swr_free(resample_context);
        return ret;
    }

    return 0;
}

static int init_frame (AVFrame **frame) {
    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr,"Could not allocate frame\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

int main (int argc, char *argv[]) {
    AVFormatContext *context = 0;
    AVCodecContext *codec_context;
    SwrContext *resample_context = NULL;
    AVPacket packet;
    AVFrame *frame = 0;
    AVFrame *resampled = 0;
    int16_t *buffer;
    int ret, packet_ret, finished;

    ao_device *device;
    ao_sample_format format;
    int default_driver;

    if (argc != 2) {
        fprintf(stderr,"Usage: %s <filename>\n",argv[0]);
        return 1;
    }

    av_register_all();
    printf("Opening file...\n");
    if (open_audio_file(argv[1],&context,&codec_context) < 0)
        return 1;

    printf("Initializing resampler...\n");
    if (init_resampler(codec_context,&resample_context) < 0) {
        avformat_close_input(&context);
        avcodec_free_context(&codec_context);
        return 1;
    }

    // Setup libao
    printf("Starting audio device...\n");
    ao_initialize();
    default_driver = ao_default_driver_id();
    format.bits = OUTPUT_BITS;
    format.channels = OUTPUT_CHANNELS;
    format.rate = codec_context->sample_rate;
    format.byte_format = AO_FMT_NATIVE;
    format.matrix = 0;
    if ((device = ao_open_live(default_driver,&format,NULL)) == NULL) {
        fprintf(stderr,"Error opening audio device\n");
        avformat_close_input(&context);
        avcodec_free_context(&codec_context);
        swr_free(&resample_context);
        return 1;
    }

    // Mainloop
    printf("Beginning mainloop...\n");
    init_packet(&packet);
    // Read packets until done
    while (1) {
        packet_ret = av_read_frame(context,&packet);
        // Send a packet
        if ((ret = avcodec_send_packet(codec_context,&packet)) < 0)
            fprintf(stderr,"Error sending packet to decoder: %s\n",errtext(ret));

        av_packet_unref(&packet);

        while (1) {
            if (!frame)
                frame = av_frame_alloc();

            ret = avcodec_receive_frame(codec_context,frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) // Need more input
                break;
            else if (ret < 0) {
                fprintf(stderr,"Error receiving frame: %s\n",errtext(ret));
                break;
            }
            // We have a valid frame, need to resample it
            if (!resampled)
                resampled = av_frame_alloc();

            resampled->channel_layout = av_get_default_channel_layout(OUTPUT_CHANNELS);
            resampled->sample_rate = codec_context->sample_rate;
            resampled->format = OUTPUT_FMT;

            if ((ret = swr_convert_frame(resample_context,resampled,frame)) < 0) {
                fprintf(stderr,"Error resampling: %s\n",errtext(ret));
            } else {
                ao_play(device,(char*)resampled->extended_data[0],resampled->linesize[0]);
            }
            av_frame_unref(resampled);
            av_frame_unref(frame);
        }

        if (packet_ret == AVERROR_EOF)
            break;
    }

    printf("Closing file and freeing contexts...\n");
    avformat_close_input(&context);
    avcodec_free_context(&codec_context);
    swr_free(&resample_context);

    printf("Closing audio device...\n");
    ao_close(device);
    ao_shutdown();

    return 0;
}