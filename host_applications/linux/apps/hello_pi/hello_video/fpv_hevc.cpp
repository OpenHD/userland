#include "fpv_hevc.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>

extern "C" {
#include <bcm_host.h>
#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_default_components.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext_drm.h>
#include <drm_fourcc.h>
}
#include "host_applications/linux/libs/sm/user-vcsm.h"

namespace {
constexpr uint64_t kSand128 = DRM_FORMAT_MOD_BROADCOM_SAND128;
constexpr uint64_t kModifierTypeMask = (UINT64_C(0xff) << 56) | 0xff;

struct RenderBuffer {
    AVFrame *frame;
    unsigned int vcsm_handle;
};

void release_buffer(MMAL_PORT_T *, MMAL_BUFFER_HEADER_T *buffer)
{
    auto *held = static_cast<RenderBuffer *>(buffer->user_data);
    if (held) {
        vcsm_free(held->vcsm_handle);
        av_frame_free(&held->frame);
        delete held;
    }
    buffer->user_data = nullptr;
    buffer->data = nullptr;
    mmal_buffer_header_release(buffer);
}

void release_event(MMAL_PORT_T *, MMAL_BUFFER_HEADER_T *event)
{
    mmal_buffer_header_release(event);
}

class Renderer {
public:
    ~Renderer() { close(); }

    bool show(AVFrame *frame)
    {
        if (frame->format != AV_PIX_FMT_DRM_PRIME || !frame->data[0]) return false;
        const auto &drm = *reinterpret_cast<const AVDRMFrameDescriptor *>(frame->data[0]);
        const auto &layer = drm.layers[0];
        if (drm.nb_objects != 1 || drm.nb_layers != 1 || layer.nb_planes != 2 ||
            layer.format != DRM_FORMAT_NV12) return false;
        const auto &object = drm.objects[0];
        const auto &luma = layer.planes[0];
        const auto &chroma = layer.planes[1];
        if ((object.format_modifier & kModifierTypeMask) != kSand128 ||
            luma.object_index != 0 || chroma.object_index != 0 || luma.offset != 0 ||
            chroma.offset <= 0 || chroma.offset % 128 ||
            object.size > std::numeric_limits<uint32_t>::max()) return false;

        const unsigned luma_height = chroma.offset / 128;
        const unsigned column_height = (object.format_modifier & ~(UINT64_C(0xff) << 56)) >> 8;
        if (luma_height < static_cast<unsigned>(frame->height) ||
            column_height != luma_height * 3 / 2) {
            std::fprintf(stderr, "fpv_video: unsupported SAND layout (%u luma, %u total)\n",
                         luma_height, column_height);
            return false;
        }
        if (!port || width != frame->width || height != frame->height ||
            luma_height != padded_height) {
            close();
            if (!open(frame->width, frame->height, luma_height)) return false;
        }

        // Do not hold up the decoder when the display has all its buffers queued.
        auto *buffer = mmal_queue_get(pool->queue);
        if (!buffer) return true;
        const unsigned handle = vcsm_import_dmabuf(object.fd, "OpenHD H.265");
        auto *ref = av_frame_clone(frame);
        const unsigned firmware_handle = handle ? vcsm_vc_hdl_from_hdl(handle) : 0;
        auto *held = firmware_handle && ref ? new (std::nothrow) RenderBuffer{ref, handle} : nullptr;
        if (!held) {
            if (handle) vcsm_free(handle);
            av_frame_free(&ref);
            mmal_buffer_header_release(buffer);
            return false;
        }
        buffer->data = reinterpret_cast<uint8_t *>(uintptr_t(firmware_handle));
        buffer->user_data = held;
        buffer->offset = 0;
        buffer->alloc_size = buffer->length = object.size;
        buffer->flags = MMAL_BUFFER_HEADER_FLAG_FRAME_END;
        buffer->pts = buffer->dts = MMAL_TIME_UNKNOWN;
        if (mmal_port_send_buffer(port, buffer) != MMAL_SUCCESS) {
            release_buffer(port, buffer);
            return false;
        }
        return true;
    }

private:
    bool open(int w, int h, unsigned storage_height)
    {
        if (mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &component) != MMAL_SUCCESS ||
            !component || component->input_num == 0) return false;
        if (mmal_port_enable(component->control, release_event) != MMAL_SUCCESS) return false;
        port = component->input[0];
        port->format->encoding = MMAL_ENCODING_YUVUV128;
        auto &video = port->format->es->video;
        video.width = (w + 127) & ~127;
        video.height = storage_height;
        video.crop = {0, 0, w, h};
        video.par = {1, 1};
        if (mmal_port_format_commit(port) != MMAL_SUCCESS ||
            mmal_port_parameter_set_boolean(port, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE) != MMAL_SUCCESS)
            return false;
        MMAL_DISPLAYREGION_T display{};
        display.hdr = {MMAL_PARAMETER_DISPLAYREGION, sizeof(display)};
        display.set = MMAL_DISPLAY_SET_LAYER | MMAL_DISPLAY_SET_FULLSCREEN | MMAL_DISPLAY_SET_NOASPECT;
        display.layer = -128;
        display.fullscreen = MMAL_TRUE;
        display.noaspect = MMAL_TRUE;
        if (mmal_port_parameter_set(port, &display.hdr) != MMAL_SUCCESS) return false;
        port->buffer_num = std::max(4u, port->buffer_num_min);
        port->buffer_size = port->buffer_size_min;
        pool = mmal_pool_create(port->buffer_num, 0);
        if (!pool || mmal_component_enable(component) != MMAL_SUCCESS ||
            mmal_port_enable(port, release_buffer) != MMAL_SUCCESS) return false;
        width = w;
        height = h;
        padded_height = storage_height;
        std::fprintf(stderr, "fpv_video: H.265 hardware display %dx%d on layer -128\n", w, h);
        return true;
    }

    void close()
    {
        if (port && port->is_enabled) mmal_port_disable(port);
        if (pool) mmal_pool_destroy(pool);
        if (component && component->control->is_enabled) mmal_port_disable(component->control);
        if (component) mmal_component_destroy(component);
        port = nullptr;
        component = nullptr;
        pool = nullptr;
    }

    MMAL_COMPONENT_T *component = nullptr;
    MMAL_PORT_T *port = nullptr;
    MMAL_POOL_T *pool = nullptr;
    int width = 0;
    int height = 0;
    unsigned padded_height = 0;
};

AVPixelFormat select_hardware_format(AVCodecContext *, const AVPixelFormat *formats)
{
    for (auto p = formats; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_DRM_PRIME) return *p;
    std::fprintf(stderr, "fpv_video: H.265 DRM PRIME hardware decode unavailable\n");
    return AV_PIX_FMT_NONE;
}
} // namespace

int fpv_play_hevc(const char *filename)
{
    if (vcsm_init_ex(1, -1) != 0) {
        std::fprintf(stderr, "fpv_video: cannot initialize VideoCore shared memory\n");
        return 1;
    }

    int status = 1;
    AVFormatContext *input = nullptr;
    AVCodecContext *decoder = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    const auto *codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    {
        Renderer renderer;
        auto drain = [&]() {
            int ret;
            while ((ret = avcodec_receive_frame(decoder, frame)) >= 0) {
                const bool displayed = renderer.show(frame);
                av_frame_unref(frame);
                if (!displayed) {
                    std::fprintf(stderr, "fpv_video: cannot present H.265 hardware buffer\n");
                    return AVERROR_EXTERNAL;
                }
            }
            return ret;
        };
        if (packet && frame && codec &&
            avformat_open_input(&input, filename, av_find_input_format("hevc"), nullptr) >= 0 &&
            (decoder = avcodec_alloc_context3(codec))) {
            decoder->get_format = select_hardware_format;
            decoder->thread_count = 1;
            decoder->flags |= AV_CODEC_FLAG_LOW_DELAY | AV_CODEC_FLAG_OUTPUT_CORRUPT;
            decoder->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;
            if (av_hwdevice_ctx_create(&decoder->hw_device_ctx, AV_HWDEVICE_TYPE_DRM,
                                       nullptr, nullptr, 0) >= 0 &&
                avcodec_open2(decoder, codec, nullptr) >= 0) {
                int ret;
                while ((ret = av_read_frame(input, packet)) >= 0) {
                    ret = avcodec_send_packet(decoder, packet);
                    av_packet_unref(packet);
                    if (ret < 0 && ret != AVERROR_INVALIDDATA) break;
                    ret = drain();
                    if (ret != AVERROR(EAGAIN) && ret != AVERROR_INVALIDDATA) break;
                }
                if (ret == AVERROR_EOF && avcodec_send_packet(decoder, nullptr) >= 0) {
                    ret = drain();
                    if (ret == AVERROR_EOF) status = 0;
                }
            }
        }
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&decoder);
    avformat_close_input(&input);
    vcsm_exit();
    return status;
}
