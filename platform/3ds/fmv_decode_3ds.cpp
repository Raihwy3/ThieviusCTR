// Real-time on-hardware FMV decoder (Bink .bik + PS2 MPEG-2 .m2v) via stripped FFmpeg + Y2R.
#include "fmv_decode_3ds.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/defs.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include <vector>

extern void PauseMusicSequencer();
extern void ContinueMusicSequencer(bool continueAmbientSounds = true);
extern bool FMusicSequencerPaused();
extern void SetFmvAudioMode(bool enabled);
extern void PreloadVag(const char* filename);
extern void ContinueVag();
extern void StopVag();
extern void CanePumpVagStream3ds();
extern void CaneVagStats3ds(bool* loaded, bool* streaming, unsigned* rate, float* mixGain,
                            float* ch2Vol, unsigned* readSamples, unsigned* totalSamples);
extern bool CaneFmvPcmStart3ds(unsigned sampleRate, unsigned channels);
extern unsigned CaneFmvPcmFreeFrames3ds();
extern void CaneFmvPcmPush3ds(const int16_t* samples, unsigned frames);
extern void CaneFmvPcmPump3ds(bool flushTail);
extern void CaneFmvPcmStop3ds();
extern void CaneFmvPcmStats3ds(bool* active, unsigned* rate, unsigned* channels,
                               uint64_t* pushedFrames, uint64_t* doneFrames);
extern bool CaneSwapBA3ds();

namespace {

constexpr unsigned kNumFrameSlots = 4;
constexpr unsigned kWorkerStackBytes = 64 * 1024;
constexpr size_t kMaxPendingPcmFrames = 65536;
constexpr unsigned kAvioBufBytes = 262144;

bool fileExists(const char* path) {
    if (!path || !*path) return false;
    struct stat st{};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool isWidescreenPs3(int index) {
    switch (index) {
    case 0:
    case 4: case 5: case 6: case 7: case 8: case 9:
    case 10: case 11: case 12: case 13: case 14:
    case 16: case 17:
        return true;
    default:
        return false;
    }
}

bool isWidescreenPs2(int index) {
    switch (index) {
    case 0:
    case 4: case 5: case 6: case 7: case 8: case 9:
    case 10: case 11: case 12: case 13: case 14:
        return true;
    default:
        return false;
    }
}

int fmvIoRead(void* opaque, uint8_t* buf, int bufSize) {
    FILE* fp = static_cast<FILE*>(opaque);
    const size_t got = std::fread(buf, 1, size_t(bufSize), fp);
    if (got > 0) return int(got);
    return std::feof(fp) ? AVERROR_EOF : AVERROR(EIO);
}

int64_t fmvIoSeek(void* opaque, int64_t offset, int whence) {
    FILE* fp = static_cast<FILE*>(opaque);
    if (whence == AVSEEK_SIZE) {
        const off_t cur = ftello(fp);
        if (cur < 0 || fseeko(fp, 0, SEEK_END) != 0) return AVERROR(EIO);
        const off_t sz = ftello(fp);
        fseeko(fp, cur, SEEK_SET);
        return sz >= 0 ? int64_t(sz) : int64_t(AVERROR(EIO));
    }
    int origin = SEEK_SET;
    if (whence == SEEK_CUR) origin = SEEK_CUR;
    else if (whence == SEEK_END) origin = SEEK_END;
    else if (whence != SEEK_SET) return AVERROR(EINVAL);
    if (fseeko(fp, off_t(offset), origin) != 0) return AVERROR(errno ? errno : EIO);
    return int64_t(ftello(fp));
}

struct FrameSlot {
    AVFrame* frame = nullptr;
    uint64_t ptsMs = 0;
    bool ready = false;
};

struct LinearPool {
    LightLock lock{};
    std::vector<uint8_t*> freeBlocks;
    size_t blockBytes = 0;
    unsigned poolW = 0;
    unsigned poolH = 0;
    int poolFmt = AV_PIX_FMT_NONE;

    void init() {
        LightLock_Init(&lock);
    }

    void reset() {
        LightLock_Lock(&lock);
        std::vector<uint8_t*> stale;
        stale.swap(freeBlocks);
        blockBytes = 0;
        poolW = 0;
        poolH = 0;
        poolFmt = AV_PIX_FMT_NONE;
        LightLock_Unlock(&lock);
        for (uint8_t* ptr : stale) {
            linearFree(ptr);
        }
    }
};

struct LinearPoolCookie {
    LinearPool* pool = nullptr;
    size_t size = 0;
};

struct DecodeState {
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* videoCtx = nullptr;
    AVCodecContext* audioCtx = nullptr;
    int videoStreamIdx = -1;
    int audioStreamIdx = -1;
    bool isBik = false;

    unsigned convW = 640;
    unsigned convH = 360;
    unsigned cropY = 0;
    bool interlacedField = false;
    size_t yuvBytes = 0;
    uint8_t* fallbackLinearYuv = nullptr;

    LinearPool linearPool{};

    double fps = 30.0;
    double tbMs = 0.0;
    bool hasFirstTs = false;
    int64_t firstTs = 0;
    uint64_t frameOrdinal = 0;
    uint64_t lastPtsMs = 0;

    FrameSlot slots[kNumFrameSlots]{};
    unsigned writeIdx = 0;
    unsigned readIdx = 0;

    LightLock lock{};
    LightEvent slotReadyEvent{};
    LightEvent slotFreeEvent{};

    std::vector<int16_t> pcmQueue;
    size_t pcmReadOff = 0;
    unsigned audioChannels = 0;
    uint64_t pcmDecodedFrames = 0;
    bool audioEnabled = false;
    bool eofFlushed = false;

    volatile bool stopRequested = false;
    volatile bool workerEof = false;
    volatile bool workerError = false;
    volatile bool skipNonRef = false;
    uint32_t decodedFrames = 0;

    bool usedZeroCopy = false;
    bool usedFallbackCopy = false;
    uint32_t y2rFailures = 0;
    uint32_t y2rTimeouts = 0;
    u64 y2rTicks = 0;
    u64 decTicks = 0;
    u64 drawTicks = 0;

    AVPacket* pkt = nullptr;
    AVFrame* vFrame = nullptr;
    AVFrame* aFrame = nullptr;
    std::vector<int16_t> audioTmp;
};

void releaseLinearPooledBuffer(void* opaque, uint8_t* data) {
    auto* cookie = static_cast<LinearPoolCookie*>(opaque);
    if (!cookie) {
        if (data) linearFree(data);
        return;
    }
    LinearPool* lp = cookie->pool;
    const size_t sz = cookie->size;
    delete cookie;
    if (!data) return;
    if (lp) {
        LightLock_Lock(&lp->lock);
        if (sz == lp->blockBytes && lp->freeBlocks.size() < 12u) {
            lp->freeBlocks.push_back(data);
            LightLock_Unlock(&lp->lock);
            return;
        }
        LightLock_Unlock(&lp->lock);
    }
    linearFree(data);
}

int fmvGetBuffer2(AVCodecContext* avctx, AVFrame* frame, int flags) {
    if (!avctx || !frame || avctx->codec_type != AVMEDIA_TYPE_VIDEO ||
        !avctx->codec || !(avctx->codec->capabilities & AV_CODEC_CAP_DR1) ||
        (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P)) {
        return avcodec_default_get_buffer2(avctx, frame, flags);
    }
    auto* st = static_cast<DecodeState*>(avctx->opaque);
    if (!st) {
        return avcodec_default_get_buffer2(avctx, frame, flags);
    }

    int alignedW = frame->width;
    int alignedH = frame->height;
    int strideAlign[AV_NUM_DATA_POINTERS]{};
    avcodec_align_dimensions2(avctx, &alignedW, &alignedH, strideAlign);
    int linesize[4]{};
    const auto pixFmt = AVPixelFormat(frame->format);
    unsigned allocW = std::max(16u, (unsigned(alignedW) + 15u) & ~15u);
    for (int iter = 0; iter < 8; ++iter) {
        if (av_image_fill_linesizes(linesize, pixFmt, int(allocW)) < 0) {
            return avcodec_default_get_buffer2(avctx, frame, flags);
        }
        bool ok = (linesize[0] >= alignedW) && ((linesize[1] % 8) == 0) && ((linesize[2] % 8) == 0);
        for (int i = 0; i < 4; ++i) {
            if (strideAlign[i] > 0 && (linesize[i] % strideAlign[i]) != 0) ok = false;
        }
        if (ok) break;
        allocW += 16u;
    }
    const unsigned allocH = std::max(16u, (unsigned(alignedH) + 15u) & ~15u);
    const int imgBytes = av_image_get_buffer_size(pixFmt, int(allocW), int(allocH), 1);
    if (imgBytes <= 0) {
        return avcodec_default_get_buffer2(avctx, frame, flags);
    }
    const size_t totalBytes = size_t(imgBytes) + AV_INPUT_BUFFER_PADDING_SIZE + 64u;

    uint8_t* raw = nullptr;
    std::vector<uint8_t*> stale;
    LightLock_Lock(&st->linearPool.lock);
    if (st->linearPool.poolW != allocW ||
        st->linearPool.poolH != allocH ||
        st->linearPool.poolFmt != frame->format ||
        st->linearPool.blockBytes != totalBytes) {
        stale.swap(st->linearPool.freeBlocks);
        st->linearPool.poolW = allocW;
        st->linearPool.poolH = allocH;
        st->linearPool.poolFmt = frame->format;
        st->linearPool.blockBytes = totalBytes;
    }
    if (!st->linearPool.freeBlocks.empty()) {
        raw = st->linearPool.freeBlocks.back();
        st->linearPool.freeBlocks.pop_back();
    }
    LightLock_Unlock(&st->linearPool.lock);

    for (uint8_t* ptr : stale) {
        linearFree(ptr);
    }
    if (!raw) {
        raw = static_cast<uint8_t*>(linearAlloc(totalBytes));
        if (!raw) {
            return avcodec_default_get_buffer2(avctx, frame, flags);
        }
        std::memset(raw, 0, totalBytes);
    }

    auto* cookie = new (std::nothrow) LinearPoolCookie{ &st->linearPool, totalBytes };
    if (!cookie) {
        linearFree(raw);
        return avcodec_default_get_buffer2(avctx, frame, flags);
    }
    AVBufferRef* buf = av_buffer_create(raw, totalBytes, releaseLinearPooledBuffer, cookie, 0);
    if (!buf) {
        delete cookie;
        linearFree(raw);
        return avcodec_default_get_buffer2(avctx, frame, flags);
    }

    std::memset(frame->data, 0, sizeof(frame->data));
    std::memset(frame->linesize, 0, sizeof(frame->linesize));
    std::memset(frame->buf, 0, sizeof(frame->buf));
    const int filled = av_image_fill_arrays(frame->data, frame->linesize, raw,
                                            pixFmt, int(allocW), int(allocH), 1);
    if (filled <= 0 || size_t(filled) > totalBytes) {
        av_buffer_unref(&buf);
        return avcodec_default_get_buffer2(avctx, frame, flags);
    }
    frame->buf[0] = buf;
    frame->extended_data = frame->data;
    return 0;
}

bool isLinearMemory(const void* ptr) {
    if (!ptr) return false;
    const uintptr_t addr = uintptr_t(ptr);
    const bool inRange =
        (addr >= uintptr_t(OS_OLD_FCRAM_VADDR) && addr < uintptr_t(OS_OLD_FCRAM_VADDR) + OS_OLD_FCRAM_SIZE) ||
        (addr >= uintptr_t(OS_FCRAM_VADDR) && addr < uintptr_t(OS_FCRAM_VADDR) + OS_FCRAM_SIZE);
    return inRange && osConvertVirtToPhys(ptr) != 0;
}

void packFrameToLinearBuffer(const DecodeState& st, const AVFrame* src, uint8_t* dst) {
    const u32 yBytes = st.convW * st.convH;
    const u32 uvW = st.convW / 2u;
    const u32 uvH = st.convH / 2u;
    const u32 uvBytes = uvW * uvH;
    uint8_t* dstY = dst;
    uint8_t* dstU = dstY + yBytes;
    uint8_t* dstV = dstU + uvBytes;

    const unsigned srcStep = st.interlacedField ? 2u : 1u;
    const int srcW = std::max(0, src->width);
    const int srcH = std::max(0, src->height);
    const unsigned copyW = std::min<unsigned>(st.convW, unsigned(srcW));

    for (unsigned row = 0; row < st.convH; ++row) {
        const unsigned sy = std::min<unsigned>(st.cropY + row * srcStep, srcH > 0 ? unsigned(srcH - 1) : 0u);
        uint8_t* outRow = dstY + size_t(row) * st.convW;
        if (src->data[0] && copyW > 0) {
            std::memcpy(outRow, src->data[0] + size_t(sy) * size_t(src->linesize[0]), copyW);
        }
        if (copyW < st.convW) {
            std::memset(outRow + copyW, 0, st.convW - copyW);
        }
    }

    const unsigned cropUvY = st.cropY / 2u;
    const unsigned copyUvW = std::min<unsigned>(uvW, unsigned(srcW / 2));
    const unsigned maxUvY = (srcH >= 2) ? unsigned(srcH / 2 - 1) : 0u;
    for (unsigned row = 0; row < uvH; ++row) {
        const unsigned sy = std::min<unsigned>(cropUvY + row * srcStep, maxUvY);
        uint8_t* outU = dstU + size_t(row) * uvW;
        uint8_t* outV = dstV + size_t(row) * uvW;
        if (src->data[1] && copyUvW > 0) {
            std::memcpy(outU, src->data[1] + size_t(sy) * size_t(src->linesize[1]), copyUvW);
        }
        if (src->data[2] && copyUvW > 0) {
            std::memcpy(outV, src->data[2] + size_t(sy) * size_t(src->linesize[2]), copyUvW);
        }
        if (copyUvW < uvW) {
            std::memset(outU + copyUvW, 128, uvW - copyUvW);
            std::memset(outV + copyUvW, 128, uvW - copyUvW);
        }
    }
}

void appendAudioFrame(DecodeState& st, const AVFrame* aFrame, bool threadedWait) {
    if (!st.audioEnabled || !st.audioChannels || aFrame->nb_samples <= 0) return;
    const unsigned samples = unsigned(aFrame->nb_samples);
    const unsigned srcCh = unsigned(std::max(1, aFrame->ch_layout.nb_channels));
    const unsigned outCh = st.audioChannels;
    st.audioTmp.resize(size_t(samples) * outCh);

    const auto toS16 = [](float x) -> int16_t {
        if (!(x == x)) return 0;
        const int32_t v = int32_t(x * 32767.0f);
        if (v > 32767) return 32767;
        if (v < -32768) return -32768;
        return int16_t(v);
    };

    const int fmt = aFrame->format;
    if (fmt == AV_SAMPLE_FMT_FLTP) {
        if (outCh == 2u && srcCh >= 2u) {
            const float* srcL = reinterpret_cast<const float*>(aFrame->extended_data[0]);
            const float* srcR = reinterpret_cast<const float*>(aFrame->extended_data[1]);
            int16_t* dst = st.audioTmp.data();
            for (unsigned i = 0; i < samples; ++i) {
                dst[0] = toS16(srcL[i]);
                dst[1] = toS16(srcR[i]);
                dst += 2;
            }
        } else {
            for (unsigned c = 0; c < outCh; ++c) {
                const float* src = reinterpret_cast<const float*>(
                    aFrame->extended_data[std::min(c, srcCh - 1)]);
                for (unsigned i = 0; i < samples; ++i) {
                    st.audioTmp[size_t(i) * outCh + c] = toS16(src[i]);
                }
            }
        }
    } else if (fmt == AV_SAMPLE_FMT_FLT) {
        const float* src = reinterpret_cast<const float*>(aFrame->data[0]);
        for (unsigned i = 0; i < samples; ++i) {
            for (unsigned c = 0; c < outCh; ++c) {
                st.audioTmp[size_t(i) * outCh + c] = toS16(src[size_t(i) * srcCh + std::min(c, srcCh - 1)]);
            }
        }
    } else if (fmt == AV_SAMPLE_FMT_S16P) {
        if (outCh == 2u && srcCh >= 2u) {
            const int16_t* srcL = reinterpret_cast<const int16_t*>(aFrame->extended_data[0]);
            const int16_t* srcR = reinterpret_cast<const int16_t*>(aFrame->extended_data[1]);
            int16_t* dst = st.audioTmp.data();
            for (unsigned i = 0; i < samples; ++i) {
                dst[0] = srcL[i];
                dst[1] = srcR[i];
                dst += 2;
            }
        } else {
            for (unsigned c = 0; c < outCh; ++c) {
                const int16_t* src = reinterpret_cast<const int16_t*>(
                    aFrame->extended_data[std::min(c, srcCh - 1)]);
                for (unsigned i = 0; i < samples; ++i) {
                    st.audioTmp[size_t(i) * outCh + c] = src[i];
                }
            }
        }
    } else if (fmt == AV_SAMPLE_FMT_S16) {
        const int16_t* src = reinterpret_cast<const int16_t*>(aFrame->data[0]);
        if (srcCh == outCh) {
            std::memcpy(st.audioTmp.data(), src, size_t(samples) * outCh * sizeof(int16_t));
        } else {
            for (unsigned i = 0; i < samples; ++i) {
                for (unsigned c = 0; c < outCh; ++c) {
                    st.audioTmp[size_t(i) * outCh + c] = src[size_t(i) * srcCh + std::min(c, srcCh - 1)];
                }
            }
        }
    } else {
        return;
    }

    if (!threadedWait) {
        size_t off = 0;
        while (off < samples && !__atomic_load_n(&st.stopRequested, __ATOMIC_ACQUIRE)) {
            CaneFmvPcmPump3ds(false);
            const unsigned freeF = CaneFmvPcmFreeFrames3ds();
            if (freeF > 0) {
                const unsigned chunk = std::min<unsigned>(samples - unsigned(off), freeF);
                CaneFmvPcmPush3ds(st.audioTmp.data() + off * outCh, chunk);
                st.pcmDecodedFrames += chunk;
                off += chunk;
            } else {
                svcSleepThread(2000000LL);
            }
        }
        return;
    }

    for (;;) {
        if (__atomic_load_n(&st.stopRequested, __ATOMIC_ACQUIRE)) return;
        LightLock_Lock(&st.lock);
        if (st.pcmReadOff > 0 && st.pcmReadOff == st.pcmQueue.size()) {
            st.pcmQueue.clear();
            st.pcmReadOff = 0;
        }
        const size_t queuedFrames = (st.pcmQueue.size() - st.pcmReadOff) / outCh;
        if (queuedFrames < kMaxPendingPcmFrames) {
            st.pcmQueue.insert(st.pcmQueue.end(), st.audioTmp.begin(), st.audioTmp.end());
            st.pcmDecodedFrames += samples;
            LightLock_Unlock(&st.lock);
            return;
        }
        LightLock_Unlock(&st.lock);
        svcSleepThread(4000000LL);
    }
}

uint64_t computePtsMs(DecodeState& st, const AVFrame* frame) {
    int64_t ts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
        ? frame->best_effort_timestamp
        : frame->pkt_dts;
    uint64_t ptsMs = 0;
    if (ts != AV_NOPTS_VALUE && st.tbMs > 0.0) {
        if (!st.hasFirstTs) {
            st.firstTs = ts;
            st.hasFirstTs = true;
        }
        const double rel = double(ts - st.firstTs) * st.tbMs;
        ptsMs = rel > 0.0 ? uint64_t(rel) : 0ull;
    } else {
        ptsMs = uint64_t((double(st.frameOrdinal) * 1000.0) / st.fps);
    }
    ++st.frameOrdinal;
    if (ptsMs < st.lastPtsMs) ptsMs = st.lastPtsMs;
    st.lastPtsMs = ptsMs;
    return ptsMs;
}

bool decodeNextVideoSlot(DecodeState& st, bool threadedWait) {
    u64 segStart = svcGetSystemTick();
    for (;;) {
        if (__atomic_load_n(&st.stopRequested, __ATOMIC_ACQUIRE)) return false;

        if (!st.isBik && st.videoCtx) {
            st.videoCtx->skip_frame = __atomic_load_n(&st.skipNonRef, __ATOMIC_ACQUIRE)
                ? AVDISCARD_NONREF
                : AVDISCARD_DEFAULT;
        }

        const int rx = avcodec_receive_frame(st.videoCtx, st.vFrame);
        if (rx == 0) {
            st.decTicks += svcGetSystemTick() - segStart;
            const uint64_t ptsMs = computePtsMs(st, st.vFrame);
            while (threadedWait) {
                if (__atomic_load_n(&st.stopRequested, __ATOMIC_ACQUIRE)) {
                    av_frame_unref(st.vFrame);
                    return false;
                }
                LightLock_Lock(&st.lock);
                const bool slotFree = !st.slots[st.writeIdx].ready;
                LightLock_Unlock(&st.lock);
                if (slotFree) break;
                LightEvent_WaitTimeout(&st.slotFreeEvent, 20000000LL);
            }

            FrameSlot& slot = st.slots[st.writeIdx];
            av_frame_unref(slot.frame);
            av_frame_move_ref(slot.frame, st.vFrame);

            LightLock_Lock(&st.lock);
            slot.ptsMs = ptsMs;
            slot.ready = true;
            st.writeIdx = (st.writeIdx + 1u) % kNumFrameSlots;
            __atomic_fetch_add(&st.decodedFrames, 1u, __ATOMIC_RELAXED);
            LightLock_Unlock(&st.lock);
            if (threadedWait) LightEvent_Signal(&st.slotReadyEvent);
            return true;
        }
        if (rx == AVERROR_EOF) {
            st.decTicks += svcGetSystemTick() - segStart;
            LightLock_Lock(&st.lock);
            __atomic_store_n(&st.workerEof, true, __ATOMIC_RELEASE);
            LightLock_Unlock(&st.lock);
            if (threadedWait) LightEvent_Signal(&st.slotReadyEvent);
            return false;
        }
        if (rx < 0 && rx != AVERROR(EAGAIN)) {
            st.decTicks += svcGetSystemTick() - segStart;
            LightLock_Lock(&st.lock);
            __atomic_store_n(&st.workerError, true, __ATOMIC_RELEASE);
            __atomic_store_n(&st.workerEof, true, __ATOMIC_RELEASE);
            LightLock_Unlock(&st.lock);
            if (threadedWait) LightEvent_Signal(&st.slotReadyEvent);
            return false;
        }

        const int rd = av_read_frame(st.fmtCtx, st.pkt);
        if (rd == AVERROR_EOF) {
            if (!st.eofFlushed) {
                st.eofFlushed = true;
                avcodec_send_packet(st.videoCtx, nullptr);
                if (st.audioEnabled && st.audioCtx) {
                    avcodec_send_packet(st.audioCtx, nullptr);
                    while (avcodec_receive_frame(st.audioCtx, st.aFrame) == 0) {
                        appendAudioFrame(st, st.aFrame, threadedWait);
                        av_frame_unref(st.aFrame);
                        if (__atomic_load_n(&st.stopRequested, __ATOMIC_ACQUIRE)) break;
                    }
                }
                continue;
            }
            st.decTicks += svcGetSystemTick() - segStart;
            LightLock_Lock(&st.lock);
            __atomic_store_n(&st.workerEof, true, __ATOMIC_RELEASE);
            LightLock_Unlock(&st.lock);
            if (threadedWait) LightEvent_Signal(&st.slotReadyEvent);
            return false;
        }
        if (rd < 0) {
            st.decTicks += svcGetSystemTick() - segStart;
            LightLock_Lock(&st.lock);
            __atomic_store_n(&st.workerError, true, __ATOMIC_RELEASE);
            __atomic_store_n(&st.workerEof, true, __ATOMIC_RELEASE);
            LightLock_Unlock(&st.lock);
            if (threadedWait) LightEvent_Signal(&st.slotReadyEvent);
            return false;
        }

        if (st.pkt->stream_index == st.videoStreamIdx) {
            const int sr = avcodec_send_packet(st.videoCtx, st.pkt);
            av_packet_unref(st.pkt);
            if (sr < 0 && sr != AVERROR(EAGAIN) && sr != AVERROR_INVALIDDATA) {
                st.decTicks += svcGetSystemTick() - segStart;
                LightLock_Lock(&st.lock);
                __atomic_store_n(&st.workerError, true, __ATOMIC_RELEASE);
                __atomic_store_n(&st.workerEof, true, __ATOMIC_RELEASE);
                LightLock_Unlock(&st.lock);
                if (threadedWait) LightEvent_Signal(&st.slotReadyEvent);
                return false;
            }
        } else if (st.audioEnabled && st.audioCtx && st.pkt->stream_index == st.audioStreamIdx) {
            if (avcodec_send_packet(st.audioCtx, st.pkt) >= 0) {
                while (avcodec_receive_frame(st.audioCtx, st.aFrame) == 0) {
                    appendAudioFrame(st, st.aFrame, threadedWait);
                    av_frame_unref(st.aFrame);
                    if (__atomic_load_n(&st.stopRequested, __ATOMIC_ACQUIRE)) break;
                }
            }
            av_packet_unref(st.pkt);
        } else {
            av_packet_unref(st.pkt);
        }
    }
}

void fmvWorkerEntry(void* arg) {
    auto* st = static_cast<DecodeState*>(arg);
    while (!__atomic_load_n(&st->stopRequested, __ATOMIC_ACQUIRE)) {
        if (!decodeNextVideoSlot(*st, true)) break;
    }
}

void drainDecodedPcmToNdsp(DecodeState& st, bool flushTail) {
    if (!st.audioChannels) return;
    LightLock_Lock(&st.lock);
    const size_t availSamples = st.pcmQueue.size() - st.pcmReadOff;
    const unsigned availFrames = unsigned(availSamples / st.audioChannels);
    const unsigned freeFrames = CaneFmvPcmFreeFrames3ds();
    const unsigned takeFrames = std::min(availFrames, freeFrames);
    if (takeFrames > 0) {
        CaneFmvPcmPush3ds(st.pcmQueue.data() + st.pcmReadOff, takeFrames);
        st.pcmReadOff += size_t(takeFrames) * st.audioChannels;
        if (st.pcmReadOff == st.pcmQueue.size()) {
            st.pcmQueue.clear();
            st.pcmReadOff = 0;
        } else if (st.pcmReadOff > 4096u * st.audioChannels) {
            st.pcmQueue.erase(st.pcmQueue.begin(), st.pcmQueue.begin() + ptrdiff_t(st.pcmReadOff));
            st.pcmReadOff = 0;
        }
    }
    LightLock_Unlock(&st.lock);
    CaneFmvPcmPump3ds(flushTail);
}

bool waitY2rDone(Handle y2rEvent) {
    const Result waitRes = y2rEvent ? svcWaitSynchronization(y2rEvent, 200000000LL) : 0;
    bool doneRec = false;
    Result recRes = Y2RU_IsDoneReceiving(&doneRec);
    if (R_SUCCEEDED(waitRes) && R_SUCCEEDED(recRes) && !doneRec) {
        const u64 deadline = osGetTime() + 100ull;
        while (R_SUCCEEDED(recRes) && !doneRec && osGetTime() < deadline) {
            svcSleepThread(100000LL);
            recRes = Y2RU_IsDoneReceiving(&doneRec);
        }
    }
    return R_SUCCEEDED(recRes) && doneRec;
}

enum class Y2rResult { Ok, Timeout, Fatal };

Y2rResult convertFrameToTexY2r(DecodeState& st, const AVFrame* frame, unsigned texW, C3D_Tex* tex, Handle y2rEvent) {
    const unsigned srcStep = st.interlacedField ? 2u : 1u;
    const u32 yBytes = u32(st.convW) * u32(st.convH);
    const u32 uvW = u32(st.convW / 2u);
    const u32 uvH = u32(st.convH / 2u);
    const u32 uvBytes = uvW * uvH;

    const bool pixOk = (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUVJ420P);
    const unsigned maxSrcY = st.cropY + (st.convH - 1u) * srcStep;
    const unsigned cropUvY = st.cropY / 2u;
    const unsigned maxSrcUvY = cropUvY + (uvH - 1u) * srcStep;

    const uint8_t* srcY = nullptr;
    const uint8_t* srcU = nullptr;
    const uint8_t* srcV = nullptr;
    unsigned yGap = 0;
    unsigned uGap = 0;
    unsigned vGap = 0;

    if (pixOk &&
        frame->data[0] && frame->data[1] && frame->data[2] &&
        isLinearMemory(frame->data[0]) &&
        isLinearMemory(frame->data[1]) &&
        isLinearMemory(frame->data[2]) &&
        frame->height > int(maxSrcY) &&
        (frame->height / 2) > int(maxSrcUvY) &&
        frame->linesize[0] >= int(st.convW) &&
        frame->linesize[1] >= int(uvW) &&
        frame->linesize[2] >= int(uvW) &&
        (srcStep * unsigned(frame->linesize[0])) < 0x8000u &&
        (srcStep * unsigned(frame->linesize[1])) < 0x8000u &&
        (srcStep * unsigned(frame->linesize[2])) < 0x8000u) {
        const unsigned yStride = srcStep * unsigned(frame->linesize[0]);
        const unsigned uStride = srcStep * unsigned(frame->linesize[1]);
        const unsigned vStride = srcStep * unsigned(frame->linesize[2]);
        srcY = frame->data[0] + size_t(st.cropY) * size_t(frame->linesize[0]);
        srcU = frame->data[1] + size_t(cropUvY) * size_t(frame->linesize[1]);
        srcV = frame->data[2] + size_t(cropUvY) * size_t(frame->linesize[2]);
        yGap = yStride - st.convW;
        uGap = uStride - uvW;
        vGap = vStride - uvW;
        st.usedZeroCopy = true;

        const u32 ySpan = u32(st.convH - 1u) * yStride + u32(st.convW);
        const u32 uSpan = u32(uvH - 1u) * uStride + uvW;
        const u32 vSpan = u32(uvH - 1u) * vStride + uvW;
        const uintptr_t p0 = uintptr_t(srcY);
        const uintptr_t p1 = uintptr_t(srcU);
        const uintptr_t p2 = uintptr_t(srcV);
        if (p1 >= p0 && p2 >= p1 && (p2 + vSpan - p0) <= 0x400000u) {
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE, u32(p0), u32(p2 + vSpan - p0));
        } else {
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE, u32(p0), ySpan);
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE, u32(p1), uSpan);
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE, u32(p2), vSpan);
        }
    } else {
        if (!st.fallbackLinearYuv) {
            st.fallbackLinearYuv = static_cast<uint8_t*>(linearAlloc(st.yuvBytes));
            if (!st.fallbackLinearYuv) {
                ++st.y2rFailures;
                return Y2rResult::Fatal;
            }
        }
        packFrameToLinearBuffer(st, frame, st.fallbackLinearYuv);
        srcY = st.fallbackLinearYuv;
        srcU = srcY + yBytes;
        srcV = srcU + uvBytes;
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, u32(uintptr_t(st.fallbackLinearYuv)), u32(st.yuvBytes));
        st.usedFallbackCopy = true;
    }

    Y2RU_ConversionParams params{};
    params.input_format = INPUT_YUV420_INDIV_8;
    params.output_format = OUTPUT_RGB_16_565;
    params.rotation = ROTATION_NONE;
    params.block_alignment = BLOCK_8_BY_8;
    params.input_line_width = s16(st.convW);
    params.input_lines = s16(st.convH);
    params.standard_coefficient = COEFFICIENT_ITU_R_BT_601_SCALING;
    params.unused = 0;
    params.alpha = 0xFF;

    if (texW * 16u >= 0x8000u ||
        R_FAILED(Y2RU_SetConversionParams(&params)) ||
        R_FAILED(Y2RU_SetSendingY(srcY, yBytes, s16(st.convW), s16(yGap))) ||
        R_FAILED(Y2RU_SetSendingU(srcU, uvBytes, s16(uvW), s16(uGap))) ||
        R_FAILED(Y2RU_SetSendingV(srcV, uvBytes, s16(uvW), s16(vGap))) ||
        R_FAILED(Y2RU_SetReceiving(tex->data, yBytes * 2u, s16(st.convW * 16u), s16((texW - st.convW) * 16u)))) {
        ++st.y2rFailures;
        return Y2rResult::Fatal;
    }
    if (y2rEvent) svcClearEvent(y2rEvent);
    if (R_FAILED(Y2RU_StartConversion())) {
        ++st.y2rFailures;
        return Y2rResult::Fatal;
    }
    if (!waitY2rDone(y2rEvent)) {
        ++st.y2rTimeouts;
        Y2RU_StopConversion();
        return Y2rResult::Timeout;
    }
    return Y2rResult::Ok;
}

} // namespace

const char* CanePs3BikName3ds(int index) {
    static constexpr const char* kNames[18] = {
        "secret_origin.bik",
        "secret_origin_alt.bik",
        "making_of.bik",
        "outtakes.bik",
        "underwater_intro.bik",
        "underwater_outro.bik",
        "muggshot_intro.bik",
        "muggshot_outro.bik",
        "voodoo_intro.bik",
        "voodoo_outro.bik",
        "snow_intro.bik",
        "snow_outro.bik",
        "clockwerk_intro.bik",
        "ending_1.bik",
        "ending_2.bik",
        "credits.bik",
        "splash.bik",
        "splash.bik",
    };
    return (index >= 0 && index < 18) ? kNames[index] : nullptr;
}

bool CaneIsValidBikFile3ds(const char* path) {
    if (!fileExists(path)) return false;
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return false;
    uint8_t hdr[8]{};
    bool ok = std::fread(hdr, 1, 4, fp) == 4 && hdr[0] == 'B' && hdr[1] == 'I' && hdr[2] == 'K';
    if (ok && fseeko(fp, 0, SEEK_END) == 0) {
        ok = ftello(fp) >= 64;
    } else {
        ok = false;
    }
    std::fclose(fp);
    return ok;
}

bool CaneIsValidM2vFile3ds(const char* path) {
    if (!fileExists(path)) return false;
    FILE* fp = std::fopen(path, "rb");
    if (!fp) return false;
    uint8_t hdr[4]{};
    bool ok = std::fread(hdr, 1, 4, fp) == 4 && hdr[0] == 0x00 && hdr[1] == 0x00 && hdr[2] == 0x01;
    if (ok && fseeko(fp, 0, SEEK_END) == 0) {
        ok = ftello(fp) >= 1024;
    } else {
        ok = false;
    }
    std::fclose(fp);
    return ok;
}

CaneFmvResolved CaneResolveCutscene3ds(const char* baseDir, int index) {
    if (!baseDir || index < 0 || index >= 18) return {};
    char path[256];

    std::snprintf(path, sizeof(path), "%s/Cutscenes/Cutscene_%02d.bik", baseDir, index);
    if (CaneIsValidBikFile3ds(path)) {
        return { CaneFmvKind::Bik, path, {} };
    }
    if (const char* bikName = CanePs3BikName3ds(index)) {
        std::snprintf(path, sizeof(path), "%s/Cutscenes/%s", baseDir, bikName);
        if (CaneIsValidBikFile3ds(path)) {
            return { CaneFmvKind::Bik, path, {} };
        }
    }

    std::snprintf(path, sizeof(path), "%s/Cutscenes/Cutscene_%02d.cmov", baseDir, index);
    if (fileExists(path)) {
        char wav[256];
        std::snprintf(wav, sizeof(wav), "%s/Cutscenes/Cutscene_%02d.wav", baseDir, index);
        return { CaneFmvKind::Cmov, path, wav };
    }

    std::snprintf(path, sizeof(path), "%s/Cutscenes/Cutscene_%02d.m2v", baseDir, index);
    if (CaneIsValidM2vFile3ds(path)) {
        char wav[256];
        std::snprintf(wav, sizeof(wav), "%s/Cutscenes/Cutscene_%02d.wav", baseDir, index);
        return { CaneFmvKind::M2v, path, wav };
    }

    return {};
}

bool CanePlayDecodedFmv3ds(int index, const CaneFmvResolved& src, const CaneFmvDrawCallbacks& draw) {
    if ((src.kind != CaneFmvKind::Bik && src.kind != CaneFmvKind::M2v) ||
        !draw.beginTarget || !draw.presentFrame || !draw.endTarget) {
        return false;
    }

    const bool isBik = (src.kind == CaneFmvKind::Bik);
    FILE* fp = std::fopen(src.videoPath.c_str(), "rb");
    if (!fp) return false;
    std::setvbuf(fp, nullptr, _IONBF, 0);

    uint8_t* ioBuf = static_cast<uint8_t*>(av_malloc(kAvioBufBytes));
    if (!ioBuf) {
        std::fclose(fp);
        return false;
    }
    AVIOContext* avio = avio_alloc_context(ioBuf, int(kAvioBufBytes), 0, fp, fmvIoRead, nullptr, fmvIoSeek);
    if (!avio) {
        av_free(ioBuf);
        std::fclose(fp);
        return false;
    }

    DecodeState st{};
    st.isBik = isBik;
    st.linearPool.init();
    LightLock_Init(&st.lock);
    LightEvent_Init(&st.slotReadyEvent, RESET_ONESHOT);
    LightEvent_Init(&st.slotFreeEvent, RESET_ONESHOT);

    Handle y2rEvent = 0;
    bool y2rActive = false;
    bool musicPausedByUs = false;
    bool musicWasPaused = false;
    bool drawBegun = false;
    bool audioStarted = false;
    Thread worker = nullptr;

    auto cleanup = [&]() {
        __atomic_store_n(&st.stopRequested, true, __ATOMIC_RELEASE);
        LightEvent_Signal(&st.slotFreeEvent);
        LightEvent_Signal(&st.slotReadyEvent);
        if (worker) {
            threadJoin(worker, U64_MAX);
            threadFree(worker);
            worker = nullptr;
        }
        if (y2rActive) {
            Y2RU_StopConversion();
            if (y2rEvent) {
                svcCloseHandle(y2rEvent);
                y2rEvent = 0;
            }
            y2rExit();
            y2rActive = false;
        }
        if (audioStarted) {
            if (isBik) CaneFmvPcmStop3ds();
            else StopVag();
            SetFmvAudioMode(false);
            audioStarted = false;
        }
        if (musicPausedByUs && !musicWasPaused) {
            ContinueMusicSequencer(true);
            musicPausedByUs = false;
        }
        if (drawBegun) {
            draw.endTarget(draw.ctx);
            drawBegun = false;
        }
        if (st.fallbackLinearYuv) {
            linearFree(st.fallbackLinearYuv);
            st.fallbackLinearYuv = nullptr;
        }
        for (FrameSlot& slot : st.slots) {
            if (slot.frame) av_frame_free(&slot.frame);
        }
        if (st.aFrame) av_frame_free(&st.aFrame);
        if (st.vFrame) av_frame_free(&st.vFrame);
        if (st.pkt) av_packet_free(&st.pkt);
        if (st.audioCtx) avcodec_free_context(&st.audioCtx);
        if (st.videoCtx) avcodec_free_context(&st.videoCtx);
        st.linearPool.reset();
        if (st.fmtCtx) avformat_close_input(&st.fmtCtx);
        if (avio) {
            av_freep(&avio->buffer);
            avio_context_free(&avio);
        }
        std::fclose(fp);
    };

    st.fmtCtx = avformat_alloc_context();
    if (!st.fmtCtx) { cleanup(); return false; }
    st.fmtCtx->pb = avio;
    st.fmtCtx->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (!isBik) {
        st.fmtCtx->probesize = 512 * 1024;
        st.fmtCtx->max_analyze_duration = 500000;
    }

    const AVInputFormat* infmt = av_find_input_format(isBik ? "bink" : "mpegvideo");
    if (!infmt || avformat_open_input(&st.fmtCtx, "", infmt, nullptr) < 0) {
        std::fprintf(stderr, "Movie %02d failed opening %s\n", index, src.videoPath.c_str());
        cleanup();
        return false;
    }
    if (avformat_find_stream_info(st.fmtCtx, nullptr) < 0) {
        std::fprintf(stderr, "Movie %02d failed finding stream info\n", index);
        cleanup();
        return false;
    }

    for (unsigned i = 0; i < st.fmtCtx->nb_streams; ++i) {
        AVStream* stream = st.fmtCtx->streams[i];
        if (st.videoStreamIdx < 0 && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            st.videoStreamIdx = int(i);
        } else if (isBik && st.audioStreamIdx < 0 && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            st.audioStreamIdx = int(i);
        }
    }
    if (st.videoStreamIdx < 0) {
        cleanup();
        return false;
    }
    for (unsigned i = 0; i < st.fmtCtx->nb_streams; ++i) {
        if (int(i) != st.videoStreamIdx && int(i) != st.audioStreamIdx) {
            st.fmtCtx->streams[i]->discard = AVDISCARD_ALL;
        }
    }

    AVStream* vStream = st.fmtCtx->streams[st.videoStreamIdx];
    const AVCodec* vCodec = avcodec_find_decoder(vStream->codecpar->codec_id);
    if (!vCodec) { cleanup(); return false; }
    st.videoCtx = avcodec_alloc_context3(vCodec);
    if (!st.videoCtx || avcodec_parameters_to_context(st.videoCtx, vStream->codecpar) < 0) {
        cleanup();
        return false;
    }
    st.videoCtx->opaque = &st;
    st.videoCtx->get_buffer2 = fmvGetBuffer2;
    if (avcodec_open2(st.videoCtx, vCodec, nullptr) < 0) {
        cleanup();
        return false;
    }

    if (isBik && st.audioStreamIdx >= 0) {
        AVStream* aStream = st.fmtCtx->streams[st.audioStreamIdx];
        const AVCodec* aCodec = avcodec_find_decoder(aStream->codecpar->codec_id);
        if (aCodec) {
            st.audioCtx = avcodec_alloc_context3(aCodec);
            if (st.audioCtx &&
                avcodec_parameters_to_context(st.audioCtx, aStream->codecpar) >= 0 &&
                avcodec_open2(st.audioCtx, aCodec, nullptr) >= 0) {
                st.audioChannels = unsigned(std::clamp(st.audioCtx->ch_layout.nb_channels, 1, 2));
            } else {
                avcodec_free_context(&st.audioCtx);
            }
        }
    }

    const unsigned srcW = unsigned(std::max(16, st.videoCtx->width));
    const unsigned srcH = unsigned(std::max(16, st.videoCtx->height));
    const bool wideAspect = isBik ? isWidescreenPs3(index) : isWidescreenPs2(index);

    unsigned trimTop = 0;
    unsigned trimBottom = 0;
    if (isBik) {
        st.interlacedField = false;
        if (wideAspect && srcW == 640u && srcH >= 420u) {
            st.cropY = 60u;
            st.convW = 640u;
            st.convH = 360u;
        } else {
            st.cropY = 0u;
            st.convW = std::min(1024u, (srcW / 16u) * 16u);
            st.convH = std::min(512u, (srcH / 8u) * 8u);
        }
    } else {
        st.interlacedField = (srcH >= 440u);
        if (st.interlacedField && wideAspect && srcW == 640u && srcH >= 424u) {
            st.cropY = 56u;
            st.convW = 640u;
            st.convH = 184u; // 56..422 step 2 = 184 field rows (multiple of 8); trim 2 top/bottom -> y=60..420
            trimTop = 2u;
            trimBottom = 2u;
        } else if (st.interlacedField) {
            st.cropY = 0u;
            st.convW = std::min(1024u, (srcW / 16u) * 16u);
            st.convH = std::min(512u, ((srcH / 2u) / 8u) * 8u);
        } else {
            st.cropY = 0u;
            st.convW = std::min(1024u, (srcW / 16u) * 16u);
            st.convH = std::min(512u, (srcH / 8u) * 8u);
        }
    }

    if (st.convW < 64u || st.convH < 64u) {
        cleanup();
        return false;
    }

    const unsigned texW = 1024u;
    const unsigned texH = (st.convH <= 256u) ? 256u : 512u;
    const float u0 = 0.0f;
    const float u1 = float(st.convW) / float(texW);
    const float v0 = 1.0f - float(trimTop) / float(texH);
    const float v1 = 1.0f - float(st.convH - trimBottom) / float(texH);
    const float quadW = wideAspect ? 800.0f : 640.0f;
    const float quadH = wideAspect ? 224.0f : 240.0f;

    AVRational fr = av_guess_frame_rate(st.fmtCtx, vStream, nullptr);
    if (fr.num > 0 && fr.den > 0) st.fps = av_q2d(fr);
    else if (vStream->r_frame_rate.num > 0 && vStream->r_frame_rate.den > 0) st.fps = av_q2d(vStream->r_frame_rate);
    else st.fps = 30.0;
    st.fps = std::clamp(st.fps, 1.0, 60.0);
    st.tbMs = (vStream->time_base.num > 0 && vStream->time_base.den > 0)
        ? (1000.0 * av_q2d(vStream->time_base))
        : 0.0;

    st.yuvBytes = size_t(st.convW) * size_t(st.convH) * 3u / 2u;
    for (FrameSlot& slot : st.slots) {
        slot.frame = av_frame_alloc();
        if (!slot.frame) {
            cleanup();
            return false;
        }
    }

    st.pkt = av_packet_alloc();
    st.vFrame = av_frame_alloc();
    st.aFrame = av_frame_alloc();
    if (!st.pkt || !st.vFrame || !st.aFrame) {
        cleanup();
        return false;
    }

    if (R_FAILED(y2rInit())) {
        std::fprintf(stderr, "Movie %02d y2rInit failed\n", index);
        cleanup();
        return false;
    }
    y2rActive = true;
    Y2RU_StopConversion();
    Y2RU_ConversionParams params{};
    params.input_format = INPUT_YUV420_INDIV_8;
    params.output_format = OUTPUT_RGB_16_565;
    params.rotation = ROTATION_NONE;
    params.block_alignment = BLOCK_8_BY_8;
    params.input_line_width = s16(st.convW);
    params.input_lines = s16(st.convH);
    params.standard_coefficient = COEFFICIENT_ITU_R_BT_601_SCALING;
    params.unused = 0;
    params.alpha = 0xFF;
    if (R_FAILED(Y2RU_SetConversionParams(&params)) ||
        R_FAILED(Y2RU_SetTransferEndInterrupt(true)) ||
        R_FAILED(Y2RU_SetSpacialDithering(false)) ||
        R_FAILED(Y2RU_SetTemporalDithering(false)) ||
        R_FAILED(Y2RU_GetTransferEndEvent(&y2rEvent))) {
        cleanup();
        return false;
    }

    C3D_Tex* tex[2]{ nullptr, nullptr };
    if (!draw.beginTarget(draw.ctx, true, texW, texH, st.convW, st.convH, u0, v0, u1, v1, quadW, quadH, &tex[0], &tex[1]) ||
        !tex[0]) {
        cleanup();
        return false;
    }
    drawBegun = true;
    std::memset(tex[0]->data, 0, tex[0]->size);
    C3D_TexFlush(tex[0]);
    if (tex[1]) {
        std::memset(tex[1]->data, 0, tex[1]->size);
        C3D_TexFlush(tex[1]);
    }

    musicWasPaused = FMusicSequencerPaused();
    PauseMusicSequencer();
    musicPausedByUs = true;
    SetFmvAudioMode(true);
    audioStarted = true;

    bool bikAudioReady = false;
    if (isBik) {
        if (st.audioCtx && st.audioChannels > 0) {
            bikAudioReady = CaneFmvPcmStart3ds(unsigned(st.audioCtx->sample_rate), st.audioChannels);
            st.audioEnabled = bikAudioReady;
        }
    } else {
        PreloadVag(src.wavPath.c_str());
    }

    bool isNew3ds = false;
    APT_CheckNew3DS(&isNew3ds);
    if (isNew3ds) {
        s32 mainPrio = 0x30;
        svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
        const s32 prio = std::clamp<s32>(mainPrio, 0x18, 0x3F);
        worker = threadCreate(fmvWorkerEntry, &st, kWorkerStackBytes, prio, 2, false);
        if (!worker) {
            worker = threadCreate(fmvWorkerEntry, &st, kWorkerStackBytes, prio, -2, false);
        }
    }

    const bool threaded = (worker != nullptr);
    std::fprintf(stderr, "Movie %02d started (%s %ux%u->%ux%u @ %.4gfps wide=%d y2r=1 thread=%d audio=%s)\n",
                 index, isBik ? "BIK" : "M2V", srcW, srcH, st.convW, st.convH, st.fps,
                 int(gfxIsWide()), int(threaded),
                 isBik ? (bikAudioReady ? "binkaudio" : "none") : "wav");

    bool valid = true;
    bool release = false;
    bool startLatched = false;
    u64 startMs = 0;
    unsigned texIdx = 0;
    unsigned rendered = 0;
    unsigned skipped = 0;
    unsigned consecutiveSkips = 0;
    unsigned consecutiveTimeouts = 0;

    while (aptMainLoop()) {
        if (isBik) drainDecodedPcmToNdsp(st, false);
        else if (startLatched) CanePumpVagStream3ds();

        hidScanInput();
        const u32 keys = hidKeysHeld();
        if (!keys) release = true;
        const u32 skipButton = CaneSwapBA3ds() ? KEY_A : KEY_B;
        if (release && (keys & (KEY_START | skipButton))) break;

        if (!threaded) {
            bool needDecode = false;
            LightLock_Lock(&st.lock);
            needDecode = !st.slots[st.readIdx].ready && !st.workerEof;
            LightLock_Unlock(&st.lock);
            if (needDecode) decodeNextVideoSlot(st, false);
        }

        AVFrame* currentFrame = nullptr;
        uint64_t currentPtsMs = 0;
        unsigned queuedReady = 0;
        bool haveFrame = false;
        bool eofDone = false;
        bool userSkipped = false;
        for (;;) {
            LightLock_Lock(&st.lock);
            haveFrame = st.slots[st.readIdx].ready;
            eofDone = __atomic_load_n(&st.workerEof, __ATOMIC_ACQUIRE);
            if (haveFrame) {
                currentFrame = st.slots[st.readIdx].frame;
                currentPtsMs = st.slots[st.readIdx].ptsMs;
                queuedReady = 0;
                for (const FrameSlot& slot : st.slots) {
                    if (slot.ready) ++queuedReady;
                }
            }
            LightLock_Unlock(&st.lock);
            if (haveFrame || eofDone || !threaded) break;
            if (isBik) drainDecodedPcmToNdsp(st, false);
            else if (startLatched) CanePumpVagStream3ds();
            hidScanInput();
            const u32 k2 = hidKeysHeld();
            if (!k2) release = true;
            if ((release && (k2 & (KEY_START | skipButton))) || !aptMainLoop()) {
                userSkipped = true;
                break;
            }
            LightEvent_WaitTimeout(&st.slotReadyEvent, 8000000LL);
        }

        if (userSkipped || !haveFrame) {
            if (__atomic_load_n(&st.workerError, __ATOMIC_ACQUIRE)) valid = false;
            break;
        }

        if (!startLatched) {
            if (isBik) drainDecodedPcmToNdsp(st, false);
            else ContinueVag();
            startMs = osGetTime();
            startLatched = true;
        }

        while (aptMainLoop()) {
            const u64 elapsed = osGetTime() - startMs;
            if (elapsed + 2ull >= currentPtsMs) break;
            if (isBik) drainDecodedPcmToNdsp(st, false);
            else CanePumpVagStream3ds();
            hidScanInput();
            const u32 kWait = hidKeysHeld();
            if (!kWait) release = true;
            if (release && (kWait & (KEY_START | skipButton))) {
                startLatched = false;
                break;
            }
            const u64 remMs = currentPtsMs - elapsed - 2ull;
            svcSleepThread(s64(std::min<u64>(4ull, remMs)) * 1000000LL);
        }
        if (!startLatched || !aptMainLoop()) break;

        const u64 nowElapsed = osGetTime() - startMs;
        const bool late = (nowElapsed > currentPtsMs + 80ull);
        if (!isBik) {
            __atomic_store_n(&st.skipNonRef, late, __ATOMIC_RELEASE);
        }

        bool drawThisFrame = true;
        if (late && queuedReady > 1u && consecutiveSkips < 2u) {
            drawThisFrame = false;
            ++skipped;
            ++consecutiveSkips;
        } else {
            consecutiveSkips = 0;
            if (nowElapsed > currentPtsMs + 250ull && queuedReady <= 1u) {
                startMs = osGetTime() - currentPtsMs;
            }
        }

        Y2rResult y2rRes = Y2rResult::Ok;
        C3D_Tex* dstTex = tex[tex[1] ? texIdx : 0];
        if (drawThisFrame) {
            if (!tex[1]) {
                C3D_FrameSync();
                gspWaitForP3D();
            }
            const u64 t0 = svcGetSystemTick();
            y2rRes = convertFrameToTexY2r(st, currentFrame, texW, dstTex, y2rEvent);
            st.y2rTicks += svcGetSystemTick() - t0;
        }

        av_frame_unref(currentFrame);
        LightLock_Lock(&st.lock);
        st.slots[st.readIdx].ready = false;
        st.readIdx = (st.readIdx + 1u) % kNumFrameSlots;
        LightLock_Unlock(&st.lock);
        if (threaded) LightEvent_Signal(&st.slotFreeEvent);

        if (y2rRes == Y2rResult::Fatal) {
            valid = false;
            break;
        }
        if (y2rRes == Y2rResult::Timeout) {
            ++consecutiveTimeouts;
            if (consecutiveTimeouts >= 8u) {
                valid = false;
                break;
            }
            drawThisFrame = false;
        } else {
            consecutiveTimeouts = 0;
        }
        if (drawThisFrame) {
            const u64 d0 = svcGetSystemTick();
            draw.presentFrame(draw.ctx, dstTex);
            st.drawTicks += svcGetSystemTick() - d0;
            if (tex[1]) texIdx ^= 1u;
            ++rendered;
        }
    }

    if (isBik) drainDecodedPcmToNdsp(st, true);
    const u64 elapsedMs = startLatched ? (osGetTime() - startMs) : 0ull;
    const uint32_t decoded = __atomic_load_n(&st.decodedFrames, __ATOMIC_ACQUIRE);
    const double avgFps = (elapsedMs > 0) ? (double(rendered) * 1000.0 / double(elapsedMs)) : 0.0;
    const unsigned y2rFailCount = st.y2rFailures;
    const unsigned y2rToCount = st.y2rTimeouts;
    const double y2rMs = double(st.y2rTicks) / CPU_TICKS_PER_MSEC;
    const double drawMs = double(st.drawTicks) / CPU_TICKS_PER_MSEC;
    const int zcopyFlag = (st.usedZeroCopy && !st.usedFallbackCopy) ? 1 : (st.usedZeroCopy ? 2 : 0);
    const int wideFlag = int(gfxIsWide());
    uint64_t pcmPushed = 0;
    if (isBik) {
        bool active = false;
        unsigned rate = 0, ch = 0;
        uint64_t done = 0;
        CaneFmvPcmStats3ds(&active, &rate, &ch, &pcmPushed, &done);
    } else {
        bool vLoaded = false, vStream = false;
        unsigned vRate = 0, vRead = 0, vTotal = 0;
        float vMix = 0, vCh2 = 0;
        CaneVagStats3ds(&vLoaded, &vStream, &vRate, &vMix, &vCh2, &vRead, &vTotal);
        pcmPushed = vRead;
    }

    cleanup();
    const double decMs = double(st.decTicks) / CPU_TICKS_PER_MSEC;
    std::fprintf(stderr,
                 "Movie %02d ended valid=%d decoded=%u rendered=%u skipped=%u elapsed_ms=%llu avg_fps=%.1f pcm_pushed=%llu y2r_fail=%u y2r_to=%u y2r_ms=%.1f dec_ms=%.1f draw_ms=%.1f zcopy=%d wide=%d\n",
                 index, int(valid), unsigned(decoded), rendered, skipped,
                 (unsigned long long)elapsedMs, avgFps, (unsigned long long)pcmPushed,
                 y2rFailCount, y2rToCount, y2rMs, decMs, drawMs, zcopyFlag, wideFlag);
    return valid;
}
