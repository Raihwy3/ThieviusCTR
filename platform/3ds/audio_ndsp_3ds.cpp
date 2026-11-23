// Native DSP output; original bank parsing, ADPCM and effect command scheduling.
// Original music sequencing feeds DSP instrument voices; effect reverb/LFOs remain unfinished.
#include "sound.h"
#include "sw.h"
#include "cm.h"
#include "po.h"
#include "util.h"
#include "transition.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <3ds.h>
#include <opendsp.h>

IEXC g_iexcHyst = -100, s_iexcCurHigh = 0;
int s_cframeExcitementHyst = 0;
int s_sfxidLast = -1, g_fSneakyFeet = 0;

float SDistSoundEar(const glm::vec3& posEar, const glm::vec3& posSound);
std::array<uint8_t, 16> s_amRegisters{};
uint8_t s_globalExcite = 20;
LM s_lmRepeatDefault = { 4, 6 }, s_lmDistDefault = { 3000, 8000 };
std::array<std::array<float, 4>, 11> s_relativeVolumes = [] {
    std::array<std::array<float, 4>, 11> init{};
    for (auto& g : init) g.fill(1.0f);
    return init;
}();

namespace {
float music = 1.0f, sfx = 1.0f, dialogue = 1.0f, master = 1.0f;
bool stereo = true;
}

using UINT32 = uint32_t;
static constexpr int kVagChannel = 23;
static constexpr size_t kMaxSfxCacheBytes = 1536 * 1024;
static bool audioReady = false;
static bool stockDsp = false;
static int requestedDsp = 0;
static std::string dspStatus = "OpenDSP";
static std::array<bool, 24> channels{};
static ndspWaveBuf chnWaveBufs[24][2]{};
static std::array<u32, 24> chnCooldownFrame{};
struct PendingLinearFree {
    int16_t* ptr = nullptr;
    u32 frame = 0;
};
static std::vector<PendingLinearFree> pendingLinearFrees;
static std::array<std::unique_ptr<EFFECTBANK>, 7> banks;
static std::array<FILE*, 7> bankFiles{};
struct CachedSfx {
    std::vector<int16_t> pcm;
    bool loop = false;
    unsigned loopBegin = 0;
    unsigned loopLength = 0;
};
static std::map<uint64_t, CachedSfx> sfxCache;
static size_t sfxCacheBytes = 0;
std::array<int8_t, 32> s_effectGlobalRegisters{};
static unsigned audioVoices = 0, audioFailures = 0;
static uint64_t s_effectVoiceSerial = 0;
static const char* bankNames[] = { "Common", "Intro", "Underwater", "Muggshot", "Voodoo", "Snow", "Clockwerk" };
static std::string bankPath(int index) {
    return "sdmc:/3ds/ThieviusCTR/Worlds/Sound Banks/" + std::string(bankNames[index]) + ".bnk";
}

static void drainPendingLinearFrees(bool forceAll) {
    if (pendingLinearFrees.empty()) return;
    const u32 nowFrame = audioReady ? ndspGetFrameCount() : 0;
    for (size_t i = 0; i < pendingLinearFrees.size();) {
        if (forceAll || !audioReady || int32_t(nowFrame - pendingLinearFrees[i].frame) >= 0) {
            linearFree(pendingLinearFrees[i].ptr);
            pendingLinearFrees[i] = pendingLinearFrees.back();
            pendingLinearFrees.pop_back();
        } else {
            ++i;
        }
    }
}

static int16_t* allocLinearPcm(size_t bytes) {
    if (!bytes) return nullptr;
    drainPendingLinearFrees(false);
    int16_t* ptr = static_cast<int16_t*>(linearAlloc(bytes));
    for (int attempt = 0; !ptr && !pendingLinearFrees.empty() && attempt < 3; ++attempt) {
        gspWaitForVBlank();
        drainPendingLinearFrees(false);
        ptr = static_cast<int16_t*>(linearAlloc(bytes));
    }
    return ptr;
}

static void submitChannelWaveBufs(int channel, int16_t* data, unsigned offset, unsigned totalSamples,
                                  bool loop, unsigned loopBegin, unsigned loopLength) {
    if (channel < 0 || channel >= 24 || !data || !totalSamples || !audioReady) return;
    chnWaveBufs[channel][0] = {};
    chnWaveBufs[channel][1] = {};
    const bool canLoop = loop && loopLength > 0 && loopBegin < totalSamples &&
        loopLength <= totalSamples - loopBegin && offset < loopBegin + loopLength;
    if (canLoop) {
        unsigned firstCount = 0;
        if (offset == 0 && loopBegin == 0) firstCount = 0;
        else if (offset < loopBegin) firstCount = loopBegin - offset;
        else firstCount = (loopBegin + loopLength) - offset;
        auto& first = chnWaveBufs[channel][0];
        auto& repeat = chnWaveBufs[channel][1];
        first.data_pcm16 = data + offset;
        first.nsamples = firstCount;
        repeat.data_pcm16 = data + loopBegin;
        repeat.nsamples = loopLength;
        repeat.looping = true;
        if (firstCount > 0) ndspChnWaveBufAdd(channel, &first);
        else first.status = NDSP_WBUF_DONE;
        ndspChnWaveBufAdd(channel, &repeat);
    } else if (offset < totalSamples) {
        auto& first = chnWaveBufs[channel][0];
        first.data_pcm16 = data + offset;
        first.nsamples = totalSamples - offset;
        ndspChnWaveBufAdd(channel, &first);
    } else {
        chnWaveBufs[channel][0].status = NDSP_WBUF_DONE;
    }
}

struct IXAudio2SourceVoice {
    int channel = -1;
    int16_t* memory = nullptr;
    bool ownsMemory = true;
    unsigned sampleCount = 0;
    unsigned playOffset = 0;
    int pan = 0;
    float mixVolume = 1.0f;
    bool looping = false;
    bool streaming = false;
    ~IXAudio2SourceVoice() {
        if (channel >= 0) {
            if (audioReady) {
                float silence[12]{};
                ndspChnSetMix(channel, silence);
                ndspChnWaveBufClear(channel);
                chnCooldownFrame[channel] = ndspGetFrameCount() + 2;
            }
            channels[channel] = false;
            channel = -1;
        }
        if (memory && ownsMemory) {
            if (audioReady) pendingLinearFrees.push_back({ memory, ndspGetFrameCount() + 2 });
            else linearFree(memory);
            memory = nullptr;
        }
    }
    bool done() const;
};

static IXAudio2SourceVoice* vag = nullptr;
static bool vagStarted = false;
static bool s_fFmvAudioMode = false;
static unsigned vagRate = 32000;
void CaneFmvPcmStop3ds();
static constexpr unsigned kFmvPcmSlots = 6;
static constexpr unsigned kFmvPcmSlotFrames = 4096;
static constexpr unsigned kFmvPcmRingFrames = 32768;
static ndspWaveBuf s_fmvWaveBufs[kFmvPcmSlots]{};
static int16_t* s_fmvWaveMem[kFmvPcmSlots]{};
static bool s_fmvSlotQueued[kFmvPcmSlots]{};
static std::vector<int16_t> s_fmvPcmRing;
static unsigned s_fmvRingHead = 0;
static unsigned s_fmvRingTail = 0;
static unsigned s_fmvRingCount = 0;
static unsigned s_fmvSampleRate = 0;
static unsigned s_fmvChannels = 0;
static unsigned s_fmvSlotsSubmitted = 0;
static uint64_t s_fmvPushedFrames = 0;
static uint64_t s_fmvDoneFrames = 0;
static bool s_fmvPcmActive = false;
static bool s_fmvPaused = true;
static constexpr size_t kMaxResidentVagBytes = 512 * 1024;
static constexpr unsigned kVagStreamChunkSamples = 16384;
static FILE* s_vagStreamFile = nullptr;
static long s_vagStreamDataStart = 0;
static unsigned s_vagStreamTotalSamples = 0;
static unsigned s_vagStreamReadSamples = 0;
static unsigned s_vagStreamBufBase[2]{};
static int16_t* s_vagStreamMem[2]{};

bool IXAudio2SourceVoice::done() const {
    if (channel < 0) return true;
    if (streaming && s_vagStreamReadSamples < s_vagStreamTotalSamples) return false;
    const uint8_t s0 = __atomic_load_n(&chnWaveBufs[channel][0].status, __ATOMIC_ACQUIRE);
    const uint8_t s1 = __atomic_load_n(&chnWaveBufs[channel][1].status, __ATOMIC_ACQUIRE);
    const bool d0 = !chnWaveBufs[channel][0].nsamples || s0 == NDSP_WBUF_DONE || s0 == NDSP_WBUF_FREE;
    const bool d1 = !chnWaveBufs[channel][1].nsamples || s1 == NDSP_WBUF_DONE || s1 == NDSP_WBUF_FREE;
    return d0 && d1;
}

static void clearVagStreamResources() {
    if (s_vagStreamFile) {
        std::fclose(s_vagStreamFile);
        s_vagStreamFile = nullptr;
    }
    for (int i = 0; i < 2; ++i) {
        if (s_vagStreamMem[i]) {
            if (audioReady) pendingLinearFrees.push_back({ s_vagStreamMem[i], ndspGetFrameCount() + 2 });
            else linearFree(s_vagStreamMem[i]);
            s_vagStreamMem[i] = nullptr;
        }
        s_vagStreamBufBase[i] = 0;
    }
    s_vagStreamDataStart = 0;
    s_vagStreamTotalSamples = 0;
    s_vagStreamReadSamples = 0;
}

static bool s_aptSuspended = false;
static bool s_vagSleepSaved = false;
static bool s_vagWasUserPaused = false;
static unsigned s_vagSleepPos = 0;
static bool s_ambientsPausedBySeq = false;
static aptHookCookie s_aptAudioHook{};
static bool s_aptAudioHooked = false;

void CanePumpVagStream3ds() {
    if (!vag || !vag->streaming || !s_vagStreamFile || !audioReady || s_aptSuspended) return;
    if (s_vagStreamReadSamples >= s_vagStreamTotalSamples) return;
    for (int b = 0; b < 2 && s_vagStreamReadSamples < s_vagStreamTotalSamples; ++b) {
        const uint8_t st = __atomic_load_n(&chnWaveBufs[kVagChannel][b].status, __ATOMIC_ACQUIRE);
        if (chnWaveBufs[kVagChannel][b].nsamples && st != NDSP_WBUF_DONE && st != NDSP_WBUF_FREE) continue;
        if (!s_vagStreamMem[b]) continue;
        const unsigned remain = s_vagStreamTotalSamples - s_vagStreamReadSamples;
        const unsigned want = std::min(remain, kVagStreamChunkSamples);
        const size_t got = std::fread(s_vagStreamMem[b], sizeof(int16_t), want, s_vagStreamFile);
        if (got == 0) {
            s_vagStreamReadSamples = s_vagStreamTotalSamples;
            break;
        }
        s_vagStreamBufBase[b] = s_vagStreamReadSamples;
        s_vagStreamReadSamples += unsigned(got);
        DSP_FlushDataCache(s_vagStreamMem[b], (got * sizeof(int16_t) + 31u) & ~31u);
        chnWaveBufs[kVagChannel][b] = {};
        chnWaveBufs[kVagChannel][b].data_pcm16 = s_vagStreamMem[b];
        chnWaveBufs[kVagChannel][b].nsamples = unsigned(got);
        ndspChnWaveBufAdd(kVagChannel, &chnWaveBufs[kVagChannel][b]);
    }
}

static int allocatePlaybackChannel(bool forMusic, int minPriority);
static void refreshAllLiveAudio();

void ApplyEffectPitchBend(AMB& amb, int bend) {
    amb.currentPitchBend = std::clamp(bend, -32768, 32767);
    if (!audioReady) return;
    for (auto& voice : amb.voices) {
        if (voice.voice && voice.voice->channel >= 0) {
            float semitones = float(amb.currentPitchBend) *
                (amb.currentPitchBend < 0 ? voice.pitchBendLow : voice.pitchBendHigh) / 32768.0f;
            ndspChnSetRate(voice.voice->channel,
                48000.0f * std::clamp(voice.baseFrequencyRatio * std::exp2(semitones / 12.0f),
                    0.0625f, kSpuMaximumFrequencyRatio));
        }
    }
}

void RetireSourceVoice(IXAudio2SourceVoice*& voice, std::vector<int16_t>& pcm) {
    delete voice;
    voice = nullptr;
    pcm.clear();
}

void ReleaseEffectEnvelope(EFFECTVOICE& voice) {
    RetireSourceVoice(voice.voice, voice.pcm);
}

void ConfigureEffectLfo(AMB&, const EFFECTBANK::GRAIN&) {} // SPU LFOs not yet ported.

void StartEffectChild(AMB& parent, const EFFECTBANK::GRAIN& grain) {
    int32_t sound = 0, volume = 0;
    std::memcpy(&volume, grain.data.data(), 4);
    std::memcpy(&sound, grain.data.data() + 12, 4);
    auto* bank = static_cast<EFFECTBANK*>(parent.bnk);
    if (!bank || sound < 0 || sound >= int(bank->sounds.size())) return;
    static unsigned depth = 0;
    if (depth >= 4) return;
    ++depth;
    StartSound(SFXID(kEffectBankStarts[bank->ibnk] + sound), nullptr, parent.palo,
        parent.palo ? nullptr : &parent.pos, parent.sStart, parent.sFull,
        parent.uVolAtSource * std::clamp<int32_t>(std::abs(volume), 0, 127) / 127.0f,
        parent.frq, 0, nullptr, nullptr);
    --depth;
}

static void applyMixPan(int channel, float gain, float panRatio) {
    if (!audioReady || channel < 0 || channel >= 24) return;
    const float v = std::clamp(gain, 0.0f, 1.0f);
    const float p = std::clamp(panRatio, -1.0f, 1.0f);
    float mix[12]{};
    mix[0] = v * (stereo ? (1.0f - p) * 0.5f : 0.5f);
    mix[1] = v * (stereo ? (1.0f + p) * 0.5f : 0.5f);
    ndspChnSetMix(channel, mix);
}

static IXAudio2SourceVoice* play(const std::vector<int16_t>& pcm, float rate, float volume, float panRatio,
                                 bool loop = false, unsigned loopBegin = 0, unsigned loopLength = 0) {
    if (!audioReady || pcm.empty() || pcm.size() > 4 * 1024 * 1024) return nullptr;
    const int channel = allocatePlaybackChannel(false, 0);
    if (channel < 0) { ++audioFailures; return nullptr; }
    auto result = std::make_unique<IXAudio2SourceVoice>();
    result->memory = allocLinearPcm(pcm.size() * 2);
    if (!result->memory) { ++audioFailures; return nullptr; }
    channels[channel] = true;
    result->channel = channel;
    result->sampleCount = unsigned(pcm.size());
    result->looping = loop;
    std::memcpy(result->memory, pcm.data(), pcm.size() * 2);
    DSP_FlushDataCache(result->memory, pcm.size() * 2);
    ndspChnReset(channel);
    ndspChnSetFormat(channel, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetInterp(channel, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(channel, std::clamp(rate, 4000.0f, 192000.0f));
    applyMixPan(channel, volume * master, panRatio);
    submitChannelWaveBufs(channel, result->memory, 0, result->sampleCount, loop, loopBegin, loopLength);
    ++audioVoices;
    return result.release();
}

static FILE* ensureBankFile(int ibnk) {
    if (ibnk < 0 || ibnk >= 7) return nullptr;
    if (!bankFiles[ibnk]) bankFiles[ibnk] = std::fopen(bankPath(ibnk).c_str(), "rb");
    return bankFiles[ibnk];
}

static void closeBankFiles(bool includeCommon = true) {
    for (int i = includeCommon ? 0 : 1; i < 7; ++i) {
        if (bankFiles[i]) { std::fclose(bankFiles[i]); bankFiles[i] = nullptr; }
    }
}

bool PlayEffectTone(AMB& amb, const EFFECTBANK& bank, const EFFECTBANK::SOUND& sound,
                    const EFFECTBANK::TONE& tone, float volume, float pitch) {
    if (!audioReady || (tone.flags & 8)) return false; // Noise generator needs a separate native implementation.
    if (tone.sampleEndOffset <= tone.sampleOffset || tone.sampleEndOffset > bank.sampleSize) return false;
    const uint32_t sampleBytes = tone.sampleEndOffset - tone.sampleOffset;
    if (sampleBytes > 1024 * 1024) return false;

    const uint64_t cacheKey = (uint64_t(uint32_t(bank.ibnk)) << 32) | uint64_t(tone.sampleOffset);
    auto cached = sfxCache.find(cacheKey);
    const std::vector<int16_t>* pcmPtr = nullptr;
    bool loop = false;
    unsigned loopBegin = 0, loopLength = 0;
    DecodedEffectSample decoded;

    if (cached != sfxCache.end()) {
        pcmPtr = &cached->second.pcm;
        loop = cached->second.loop;
        loopBegin = cached->second.loopBegin;
        loopLength = cached->second.loopLength;
    } else {
        FILE* file = ensureBankFile(bank.ibnk);
        if (!file) return false;
        EFFECTBANK sample;
        sample.sampleSize = sampleBytes;
        sample.bytes.resize(sampleBytes);
        if (std::fseek(file, long(bank.sampleOffset + tone.sampleOffset), SEEK_SET) != 0 ||
            std::fread(sample.bytes.data(), 1, sampleBytes, file) != sampleBytes) {
            return false;
        }
        if (!DecodeEffectAdpcm(sample, 0, sample.sampleSize, decoded)) return false;
        pcmPtr = &decoded.pcm;
        loop = decoded.loop;
        loopBegin = decoded.loopBegin;
        loopLength = decoded.loopLength;
        const size_t pcmBytes = decoded.pcm.size() * sizeof(int16_t);
        if (pcmBytes <= 256 * 1024 && sfxCacheBytes + pcmBytes <= kMaxSfxCacheBytes) {
            CachedSfx entry;
            entry.pcm = decoded.pcm;
            entry.loop = loop;
            entry.loopBegin = loopBegin;
            entry.loopLength = loopLength;
            sfxCacheBytes += pcmBytes;
            pcmPtr = &sfxCache.emplace(cacheKey, std::move(entry)).first->second.pcm;
        }
    }

    int toneVolume = tone.volume;
    if (toneVolume < 0) toneVolume = toneVolume >= -4 ? amb.registers[-toneVolume - 1] : 127;
    EFFECTVOICE active;
    active.baseFrequencyRatio = std::clamp(
        float(std::exp2((60.0 - std::abs(int(tone.centerNote)) + tone.centerFine / 128.0 + double(pitch) * 1524.0 / 128.0) / 12.0)),
        0.0625f, kSpuMaximumFrequencyRatio);
    active.pitchBendLow = tone.pitchBendLow ? tone.pitchBendLow : 2;
    active.pitchBendHigh = tone.pitchBendHigh ? tone.pitchBendHigh : 2;
    active.soundVolume = std::max(0, int(sound.volume)) / 127.0f;
    active.toneVolume = std::clamp(toneVolume, 0, 127) / 127.0f;
    active.loop = loop;
    active.priority = tone.priority;
    active.allocationSerial = ++s_effectVoiceSerial;
    const float effectiveGain = volume * sfx * s_channelVolumes[0] * active.soundVolume * active.toneVolume;
    active.voice = play(*pcmPtr, 48000.0f * active.baseFrequencyRatio, effectiveGain, amb.pan,
        loop, loopBegin, loopLength);
    if (!active.voice) return false;
    amb.voices.push_back(std::move(active));
    return true;
}

#include "native_audio_shared.inc"
#include "music_3ds.inc"

static int allocatePlaybackChannel(bool forMusic, int /*minPriority*/) {
    const u32 nowFrame = audioReady ? ndspGetFrameCount() : 0;
    for (int i = 0; i < kVagChannel; ++i) {
        if (!channels[i] && int32_t(nowFrame - chnCooldownFrame[i]) >= 0) return i;
    }
    for (int i = 0; i < kVagChannel; ++i) {
        if (!channels[i]) return i;
    }
    if (!forMusic) {
        // Prefer stealing the lowest-priority/oldest music voice so critical SFX always land.
        MusicVoice* victimMusic = nullptr;
        for (auto& mv : s_music989.voices) {
            if (!mv.voice || mv.voice->channel < 0 || mv.voice->channel >= kVagChannel) continue;
            if (!victimMusic || mv.priority < victimMusic->priority ||
                (mv.priority == victimMusic->priority && mv.allocationSerial < victimMusic->allocationSerial)) {
                victimMusic = &mv;
            }
        }
        if (victimMusic) {
            int ch = victimMusic->voice->channel;
            RetireMusicVoice(*victimMusic);
            return ch;
        }
        // Fallback: steal the oldest non-looping active SFX voice.
        EFFECTVOICE* victimSfx = nullptr;
        if (g_psw) {
            for (auto& owned : g_psw->ambOwners) {
                if (!owned) continue;
                for (auto& ev : owned->voices) {
                    if (!ev.voice || ev.voice->channel < 0 || ev.voice->channel >= kVagChannel || ev.loop) continue;
                    if (!victimSfx || ev.allocationSerial < victimSfx->allocationSerial) victimSfx = &ev;
                }
            }
        }
        if (victimSfx) {
            int ch = victimSfx->voice->channel;
            RetireSourceVoice(victimSfx->voice, victimSfx->pcm);
            return ch;
        }
    }
    return -1;
}

static float vagMixGain() {
    return dialogue * master * (s_fFmvAudioMode ? 1.0f : s_channelVolumes[2]);
}

static void applyVagMix() {
    if (!audioReady) return;
    if (vag && vag->channel >= 0) applyMixPan(vag->channel, vagMixGain(), 0.0f);
    if (s_fmvPcmActive) applyMixPan(kVagChannel, vagMixGain(), 0.0f);
}

void SetFmvAudioMode(bool enabled) {
    s_fFmvAudioMode = enabled;
    applyVagMix();
}

void CaneFmvPcmPump3ds(bool flushTail) {
    if (!s_fmvPcmActive || !audioReady || s_aptSuspended) return;
    for (unsigned i = 0; i < kFmvPcmSlots; ++i) {
        if (!s_fmvSlotQueued[i]) continue;
        const uint8_t st = __atomic_load_n(&s_fmvWaveBufs[i].status, __ATOMIC_ACQUIRE);
        if (st == NDSP_WBUF_DONE || st == NDSP_WBUF_FREE) {
            s_fmvDoneFrames += s_fmvWaveBufs[i].nsamples;
            s_fmvSlotQueued[i] = false;
        }
    }
    while (s_fmvRingCount >= kFmvPcmSlotFrames || (flushTail && s_fmvRingCount > 0)) {
        int freeSlot = -1;
        for (unsigned i = 0; i < kFmvPcmSlots; ++i) {
            if (!s_fmvSlotQueued[i]) {
                freeSlot = int(i);
                break;
            }
        }
        if (freeSlot < 0) break;
        const unsigned take = std::min(s_fmvRingCount, kFmvPcmSlotFrames);
        const unsigned first = std::min(take, kFmvPcmRingFrames - s_fmvRingHead);
        int16_t* dst = s_fmvWaveMem[freeSlot];
        std::memcpy(dst, &s_fmvPcmRing[size_t(s_fmvRingHead) * s_fmvChannels],
                    size_t(first) * s_fmvChannels * sizeof(int16_t));
        if (take > first) {
            std::memcpy(dst + size_t(first) * s_fmvChannels, &s_fmvPcmRing[0],
                        size_t(take - first) * s_fmvChannels * sizeof(int16_t));
        }
        s_fmvRingHead = (s_fmvRingHead + take) % kFmvPcmRingFrames;
        s_fmvRingCount -= take;
        const size_t bytes = size_t(take) * s_fmvChannels * sizeof(int16_t);
        DSP_FlushDataCache(dst, (bytes + 31u) & ~31u);
        s_fmvWaveBufs[freeSlot] = {};
        s_fmvWaveBufs[freeSlot].data_pcm16 = dst;
        s_fmvWaveBufs[freeSlot].nsamples = take;
        ndspChnWaveBufAdd(kVagChannel, &s_fmvWaveBufs[freeSlot]);
        s_fmvSlotQueued[freeSlot] = true;
        ++s_fmvSlotsSubmitted;
    }
    if (s_fmvPaused && (s_fmvSlotsSubmitted >= 2u || (flushTail && s_fmvSlotsSubmitted >= 1u))) {
        ndspChnSetPaused(kVagChannel, false);
        s_fmvPaused = false;
    }
}

void CaneFmvPcmStop3ds() {
    if (s_fmvPcmActive) {
        if (audioReady) {
            float silence[12]{};
            ndspChnSetMix(kVagChannel, silence);
            ndspChnWaveBufClear(kVagChannel);
            chnCooldownFrame[kVagChannel] = ndspGetFrameCount() + 2;
        }
        channels[kVagChannel] = false;
    }
    for (unsigned i = 0; i < kFmvPcmSlots; ++i) {
        s_fmvWaveBufs[i] = {};
        s_fmvSlotQueued[i] = false;
        if (s_fmvWaveMem[i]) {
            if (audioReady) pendingLinearFrees.push_back({ s_fmvWaveMem[i], ndspGetFrameCount() + 2 });
            else linearFree(s_fmvWaveMem[i]);
            s_fmvWaveMem[i] = nullptr;
        }
    }
    s_fmvPcmRing.clear();
    s_fmvPcmRing.shrink_to_fit();
    s_fmvRingHead = 0;
    s_fmvRingTail = 0;
    s_fmvRingCount = 0;
    s_fmvSampleRate = 0;
    s_fmvChannels = 0;
    s_fmvSlotsSubmitted = 0;
    s_fmvPushedFrames = 0;
    s_fmvDoneFrames = 0;
    s_fmvPcmActive = false;
    s_fmvPaused = true;
}

bool CaneFmvPcmStart3ds(unsigned sampleRate, unsigned ch) {
    StopVag();
    CaneFmvPcmStop3ds();
    if (!audioReady || (ch != 1u && ch != 2u) || sampleRate < 4000u || sampleRate > 96000u) {
        return false;
    }
    if (channels[kVagChannel]) {
        ndspChnWaveBufClear(kVagChannel);
        channels[kVagChannel] = false;
    }
    const size_t slotBytes = size_t(kFmvPcmSlotFrames) * ch * sizeof(int16_t);
    for (unsigned i = 0; i < kFmvPcmSlots; ++i) {
        s_fmvWaveMem[i] = allocLinearPcm(slotBytes);
        if (!s_fmvWaveMem[i]) {
            CaneFmvPcmStop3ds();
            return false;
        }
    }
    s_fmvPcmRing.assign(size_t(kFmvPcmRingFrames) * ch, 0);
    s_fmvRingHead = 0;
    s_fmvRingTail = 0;
    s_fmvRingCount = 0;
    s_fmvSampleRate = sampleRate;
    s_fmvChannels = ch;
    s_fmvSlotsSubmitted = 0;
    s_fmvPushedFrames = 0;
    s_fmvDoneFrames = 0;
    s_fmvPcmActive = true;
    s_fmvPaused = true;
    channels[kVagChannel] = true;
    ndspChnReset(kVagChannel);
    ndspChnSetFormat(kVagChannel, ch == 2u ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(kVagChannel, float(sampleRate));
    ndspChnSetInterp(kVagChannel, NDSP_INTERP_POLYPHASE);
    applyMixPan(kVagChannel, vagMixGain(), 0.0f);
    ndspChnSetPaused(kVagChannel, s_fmvPaused);
    return true;
}

unsigned CaneFmvPcmFreeFrames3ds() {
    return s_fmvPcmActive ? (kFmvPcmRingFrames - s_fmvRingCount) : 0u;
}

void CaneFmvPcmPush3ds(const int16_t* samples, unsigned frames) {
    if (!s_fmvPcmActive || !samples || !frames) return;
    const unsigned take = std::min(frames, kFmvPcmRingFrames - s_fmvRingCount);
    if (take > 0) {
        const unsigned first = std::min(take, kFmvPcmRingFrames - s_fmvRingTail);
        std::memcpy(&s_fmvPcmRing[size_t(s_fmvRingTail) * s_fmvChannels], samples,
                    size_t(first) * s_fmvChannels * sizeof(int16_t));
        if (take > first) {
            std::memcpy(&s_fmvPcmRing[0], samples + size_t(first) * s_fmvChannels,
                        size_t(take - first) * s_fmvChannels * sizeof(int16_t));
        }
        s_fmvRingTail = (s_fmvRingTail + take) % kFmvPcmRingFrames;
        s_fmvRingCount += take;
        s_fmvPushedFrames += take;
    }
    CaneFmvPcmPump3ds(false);
}

void CaneFmvPcmStats3ds(bool* active, unsigned* rate, unsigned* ch,
                        uint64_t* pushedFrames, uint64_t* doneFrames) {
    if (active) *active = s_fmvPcmActive;
    if (rate) *rate = s_fmvSampleRate;
    if (ch) *ch = s_fmvChannels;
    if (pushedFrames) *pushedFrames = s_fmvPushedFrames;
    if (doneFrames) *doneFrames = s_fmvDoneFrames;
}

void CaneVagStats3ds(bool* loaded, bool* streaming, unsigned* rate, float* mixGain,
                     float* ch2Vol, unsigned* readSamples, unsigned* totalSamples) {
    if (loaded) *loaded = (vag != nullptr);
    if (streaming) *streaming = (vag != nullptr && vag->streaming);
    if (rate) *rate = vagRate;
    if (mixGain) *mixGain = vagMixGain();
    if (ch2Vol) *ch2Vol = s_channelVolumes[2];
    if (readSamples) *readSamples = (vag && vag->streaming) ? s_vagStreamReadSamples : (vag ? vag->sampleCount : 0);
    if (totalSamples) *totalSamples = (vag && vag->streaming) ? s_vagStreamTotalSamples : (vag ? vag->sampleCount : 0);
}

static void applyAmbVoicesMix(AMB& amb) {
    if (!audioReady) return;
    for (auto& ev : amb.voices) {
        if (!ev.voice || ev.voice->channel < 0) continue;
        const float gain = std::max(0.0f, amb.volAttenuated) * sfx * master * s_channelVolumes[0] *
            ev.soundVolume * ev.toneVolume;
        applyMixPan(ev.voice->channel, gain, amb.pan);
    }
}

static void refreshAllMusicVoiceMixes() {
    const int mc = kMusicMixerChannel;
    for (auto& mv : s_music989.voices) {
        if (!mv.voice || mv.voice->channel < 0) continue;
        const float v = mv.baseVolume * (mv.envelope.level / 32767.0f) *
            s_channelVolumes[mc] * s_musicVolume * s_dialogMusicDuck * music;
        SetVoiceVolumeChecked(mv.voice, v, "Refresh music mix");
    }
}

static void refreshAllLiveAudio() {
    if (!audioReady) return;
    refreshAllMusicVoiceMixes();
    applyVagMix();
    if (g_psw) {
        for (auto& owned : g_psw->ambOwners) {
            if (owned) applyAmbVoicesMix(*owned);
        }
    }
}

static unsigned captureVagSamplePos(IXAudio2SourceVoice* v) {
    if (!v || !audioReady || v->channel < 0) return v ? v->playOffset : 0;
    if (v->streaming) {
        for (int b = 0; b < 2; ++b) {
            const auto& buf = chnWaveBufs[v->channel][b];
            const uint8_t st = __atomic_load_n(&buf.status, __ATOMIC_ACQUIRE);
            if (st == NDSP_WBUF_PLAYING) {
                const u32 rel = ndspChnGetSamplePos(v->channel);
                return std::min<unsigned>(s_vagStreamTotalSamples, s_vagStreamBufBase[b] + std::min<unsigned>(rel, buf.nsamples));
            }
        }
        unsigned earliestQueued = s_vagStreamReadSamples;
        bool anyQueued = false;
        for (int b = 0; b < 2; ++b) {
            const auto& buf = chnWaveBufs[v->channel][b];
            const uint8_t st = __atomic_load_n(&buf.status, __ATOMIC_ACQUIRE);
            if (buf.nsamples && st == NDSP_WBUF_QUEUED) {
                if (!anyQueued || s_vagStreamBufBase[b] < earliestQueued) {
                    earliestQueued = s_vagStreamBufBase[b];
                    anyQueued = true;
                }
            }
        }
        return earliestQueued;
    }
    const auto& buf = chnWaveBufs[v->channel][0];
    const uint8_t st = __atomic_load_n(&buf.status, __ATOMIC_ACQUIRE);
    if (st == NDSP_WBUF_DONE) return v->sampleCount;
    const u32 rel = ndspChnGetSamplePos(v->channel);
    if (st == NDSP_WBUF_PLAYING) return std::min<unsigned>(v->sampleCount, v->playOffset + std::min<unsigned>(rel, buf.nsamples));
    return v->playOffset;
}

static bool resubmitVagFromOffset(IXAudio2SourceVoice* v, unsigned offset, bool paused) {
    if (!v || !audioReady) return false;
    if (v->streaming) {
        if (!s_vagStreamFile || !s_vagStreamMem[0] || !s_vagStreamMem[1] || offset >= s_vagStreamTotalSamples) return false;
        channels[kVagChannel] = true;
        v->channel = kVagChannel;
        v->playOffset = offset;
        chnWaveBufs[kVagChannel][0] = {};
        chnWaveBufs[kVagChannel][1] = {};
        ndspChnReset(kVagChannel);
        ndspChnSetFormat(kVagChannel, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetRate(kVagChannel, float(vagRate));
        ndspChnSetInterp(kVagChannel, NDSP_INTERP_POLYPHASE);
        ndspChnSetPaused(kVagChannel, paused);
        applyVagMix();
        if (std::fseek(s_vagStreamFile, s_vagStreamDataStart + long(offset) * 2L, SEEK_SET) != 0) return false;
        s_vagStreamReadSamples = offset;
        CanePumpVagStream3ds();
        return !v->done();
    }
    if (!v->memory || offset >= v->sampleCount) return false;
    channels[kVagChannel] = true;
    v->channel = kVagChannel;
    v->playOffset = offset;
    DSP_FlushDataCache(v->memory, v->sampleCount * 2);
    ndspChnReset(kVagChannel);
    ndspChnSetFormat(kVagChannel, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(kVagChannel, float(vagRate));
    ndspChnSetInterp(kVagChannel, NDSP_INTERP_POLYPHASE);
    ndspChnSetPaused(kVagChannel, paused);
    applyVagMix();
    submitChannelWaveBufs(kVagChannel, v->memory, offset, v->sampleCount, false, 0, 0);
    return true;
}

static bool startBackend(int choice) {
    stockDsp = (choice == 1);
    Result result;
    if (stockDsp) {
        ndspUseComponent(nullptr, 0, 0, 0);
        result = ndspInit();
        dspStatus = "NDSP";
        if (R_FAILED(result)) {
            stockDsp = false;
            result = opendspInit();
            dspStatus = R_SUCCEEDED(result) ? "NDSP unavailable; using OpenDSP" : "Audio unavailable";
        }
    } else {
        result = opendspInit();
        dspStatus = "OpenDSP";
        if (R_FAILED(result)) {
            stockDsp = true;
            ndspUseComponent(nullptr, 0, 0, 0);
            result = ndspInit();
            dspStatus = R_SUCCEEDED(result) ? "OpenDSP unavailable; using NDSP" : "Audio unavailable";
        }
    }
    audioReady = R_SUCCEEDED(result);
    if (audioReady) {
        ndspSetOutputMode(stereo ? NDSP_OUTPUT_STEREO : NDSP_OUTPUT_MONO);
        ndspSetMasterVol(1.0f);
    } else {
        dspStatus = "Audio unavailable";
    }
    std::fprintf(stderr, "Audio backend=%s result=%08lx ready=%d\n",
        dspStatus.c_str(), (unsigned long)result, int(audioReady));
    return audioReady;
}

static void onAptAudioEvent(APT_HookType hook, void*) {
    if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP) {
        if (s_aptSuspended) return;
        s_aptSuspended = true;
        if (audioReady) {
            if (vag && vag->channel >= 0 && !s_vagSleepSaved) {
                s_vagSleepPos = captureVagSamplePos(vag);
                s_vagWasUserPaused = ndspChnIsPaused(vag->channel);
                s_vagSleepSaved = true;
                ndspChnSetPaused(vag->channel, true);
            }
            if (s_fmvPcmActive) {
                ndspChnSetPaused(kVagChannel, true);
            }
            for (auto& mv : s_music989.voices) {
                if (mv.voice && mv.voice->channel >= 0) ndspChnSetPaused(mv.voice->channel, true);
            }
            if (g_psw) {
                for (auto& owned : g_psw->ambOwners) {
                    if (!owned) continue;
                    for (auto& ev : owned->voices) {
                        if (ev.voice && ev.voice->channel >= 0) ndspChnSetPaused(ev.voice->channel, true);
                    }
                }
            }
        }
        return;
    }
    if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) {
        if (!s_aptSuspended) return;
        s_aptSuspended = false;
        if (!audioReady || !dspIsComponentLoaded()) {
            if (vag) {
                vag->channel = -1;
            }
            for (auto& mv : s_music989.voices) {
                if (mv.voice) mv.voice->channel = -1;
            }
            if (g_psw) {
                for (auto& owned : g_psw->ambOwners) {
                    if (!owned) continue;
                    for (auto& ev : owned->voices) {
                        if (ev.voice) ev.voice->channel = -1;
                    }
                }
            }
            if (audioReady) {
                if (stockDsp) ndspExit();
                else opendspExit();
                audioReady = false;
            }
            drainPendingLinearFrees(true);
            channels.fill(false);
            chnCooldownFrame.fill(0);
            std::memset(chnWaveBufs, 0, sizeof(chnWaveBufs));
            startBackend(requestedDsp);
        }
        musicLastTick = osGetTime();
        if (audioReady) {
            ndspSetOutputMode(stereo ? NDSP_OUTPUT_STEREO : NDSP_OUTPUT_MONO);
            ndspSetMasterVol(1.0f);
            for (auto& entry : musicSamples) {
                if (entry.second && entry.second->pcm) {
                    DSP_FlushDataCache(entry.second->pcm, entry.second->count * 2);
                }
            }
            for (auto& mv : s_music989.voices) {
                if (!mv.voice) continue;
                int ch = mv.voice->channel;
                if (ch < 0 || (!ndspChnIsPlaying(ch) && !ndspChnIsPaused(ch))) {
                    RetireMusicVoice(mv);
                    mv.envelope.phase = MusicEnvelopePhase::Stopped;
                } else {
                    ndspChnSetPaused(ch, sequencerPaused);
                }
            }
            if (g_psw) {
                for (auto& owned : g_psw->ambOwners) {
                    if (!owned) continue;
                    for (auto& ev : owned->voices) {
                        if (!ev.voice) continue;
                        int ch = ev.voice->channel;
                        if (ch < 0 || (!ndspChnIsPlaying(ch) && !ndspChnIsPaused(ch))) {
                            RetireSourceVoice(ev.voice, ev.pcm);
                        } else {
                            ndspChnSetPaused(ch, owned->paused || (sequencerPaused && s_ambientsPausedBySeq));
                        }
                    }
                    owned->voices.erase(
                        std::remove_if(owned->voices.begin(), owned->voices.end(),
                            [](const EFFECTVOICE& ev) { return ev.voice == nullptr; }),
                        owned->voices.end());
                    if (owned->fContinuous && !owned->fStopped && !owned->fOutOfRange && owned->voices.empty()) {
                        owned->handlerDone = false;
                        owned->nextGrain = 0;
                        owned->grainCountdown = 0;
                    }
                }
            }
            if (vag) {
                int ch = vag->channel;
                const bool dropped = (ch < 0) || (!ndspChnIsPlaying(ch) && !ndspChnIsPaused(ch) && !vag->done());
                if (dropped) {
                    const unsigned pos = s_vagSleepSaved ? s_vagSleepPos : vag->playOffset;
                    const bool paused = s_vagSleepSaved ? s_vagWasUserPaused : !vagStarted;
                    vag->channel = -1;
                    if (!resubmitVagFromOffset(vag, pos, paused)) {
                        delete vag;
                        vag = nullptr;
                        clearVagStreamResources();
                        vagStarted = false;
                    }
                } else {
                    ndspChnSetPaused(ch, s_vagSleepSaved ? s_vagWasUserPaused : !vagStarted);
                }
            }
            if (s_fmvPcmActive) {
                channels[kVagChannel] = true;
                if (!ndspChnIsPlaying(kVagChannel) && !ndspChnIsPaused(kVagChannel)) {
                    for (unsigned i = 0; i < kFmvPcmSlots; ++i) {
                        s_fmvSlotQueued[i] = false;
                        s_fmvWaveBufs[i] = {};
                    }
                    ndspChnReset(kVagChannel);
                    ndspChnSetFormat(kVagChannel, s_fmvChannels == 2u ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
                    ndspChnSetRate(kVagChannel, float(s_fmvSampleRate));
                    ndspChnSetInterp(kVagChannel, NDSP_INTERP_POLYPHASE);
                }
                ndspChnSetPaused(kVagChannel, s_fmvPaused);
            }
            refreshAllLiveAudio();
        }
        s_vagSleepSaved = false;
    }
}

bool CaneStartAudio3ds() {
    if (FILE* file = std::fopen("sdmc:/3ds/ThieviusCTR/audio-backend.cfg", "r")) {
        int choice = 0, wide = 1;
        float mv = 1.0f, sv = 1.0f, dv = 1.0f;
        int fields = std::fscanf(file, "%d %f %f %f %d", &choice, &mv, &sv, &dv, &wide);
        if (fields >= 1 && choice == 1) requestedDsp = 1;
        if (fields >= 4) {
            music = std::isfinite(mv) ? std::clamp(mv, 0.0f, 1.0f) : 1.0f;
            sfx = std::isfinite(sv) ? std::clamp(sv, 0.0f, 1.0f) : 1.0f;
            dialogue = std::isfinite(dv) ? std::clamp(dv, 0.0f, 1.0f) : 1.0f;
        }
        if (fields >= 5) stereo = s_stereoOutputEnabled = (wide != 0);
        std::fclose(file);
    }
    if (!s_aptAudioHooked) {
        aptHook(&s_aptAudioHook, onAptAudioEvent, nullptr);
        s_aptAudioHooked = true;
    }
    return startBackend(requestedDsp);
}

const char* CaneAudioStatus3ds() { return dspStatus.c_str(); }
int CaneAudioChoice3ds() { return requestedDsp; }
std::string GetAudioOutputDeviceName() {
    if (dspStatus == "NDSP unavailable; using OpenDSP") return "OpenDSP (fallback)";
    if (dspStatus == "OpenDSP unavailable; using NDSP") return "NDSP (fallback)";
    return dspStatus;
}
std::string GetAudioOutputDeviceId() { return stockDsp ? "ndsp" : "opendsp"; }

SoundBankPtr SbpEnsureBank(int index, const char*) {
    if (index < 0 || index >= 7) return nullptr;
    if (banks[index]) return banks[index].get();
    static const char* names[] = { "Common", "Intro", "Underwater", "Muggshot", "Voodoo", "Snow", "Clockwerk" };
    std::string path = "sdmc:/3ds/ThieviusCTR/Worlds/Sound Banks/" + std::string(names[index]) + ".bnk";
    std::ifstream input(path, std::ios::binary);
    if (!input) return nullptr;
    auto bank = std::make_unique<EFFECTBANK>();
    bank->ibnk = index;
    // Read once into a correctly sized allocation instead of growing a vector
    // through a byte iterator (including for invalid, oversized files).
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length <= 0 || length > 8 * 1024 * 1024) return nullptr;
    input.seekg(0, std::ios::beg);
    bank->bytes.resize(static_cast<size_t>(length));
    if (!input.read(reinterpret_cast<char*>(bank->bytes.data()), length) || !Parse989SfxBank(*bank)) return nullptr;
    std::vector<uint8_t>().swap(bank->bytes);
    banks[index] = std::move(bank);
    return banks[index].get();
}

void InitializeSoundOptions() {
    master = 1.0f;
    s_relativeVolumes[6].fill(1.0f);
    for (int i = 0; i < 4; ++i) RecalculateChannelVolume(i);
    refreshAllLiveAudio();
    SbpEnsureBank(0, nullptr);
    const int levelBank = (g_psw && g_psw->ibnk > 0 && g_psw->ibnk < 7) ? g_psw->ibnk : 1;
    SbpEnsureBank(levelBank, nullptr);
}

bool FContinuousSound(SFXID id) {
    int index = int(id);
    if (index < 0 || index >= int(kSfxCount)) return false;
    int bank = 0;
    while (index >= int(kEffectBankStarts[bank + 1])) ++bank;
    auto* parsed = static_cast<EFFECTBANK*>(SbpEnsureBank(bank, nullptr));
    index -= kEffectBankStarts[bank];
    return parsed && index < int(parsed->sounds.size()) && parsed->sounds[index].IsLooper();
}

void StopSound(AMB* amb, int) {
    if (!amb || amb->fStopped) return;
    amb->fStopped = 1;
    if (amb->ppamb && *amb->ppamb == amb) *amb->ppamb = nullptr;
    amb->ppamb = nullptr;
    for (auto& active : amb->voices) RetireSourceVoice(active.voice, active.pcm);
    amb->voices.clear();
}

void DropPamb(AMB** handle) {
    if (handle && *handle) {
        StopSound(*handle, 0);
        *handle = nullptr;
    }
}

void PausePamb(AMB* amb) {
    if (!amb) return;
    amb->paused = true;
    if (!audioReady) return;
    for (auto& v : amb->voices) {
        if (v.voice && v.voice->channel >= 0) ndspChnSetPaused(v.voice->channel, true);
    }
}

void ContinuePamb(AMB* amb) {
    if (!amb) return;
    amb->paused = false;
    if (!audioReady || s_aptSuspended) return;
    for (auto& v : amb->voices) {
        if (v.voice && v.voice->channel >= 0) ndspChnSetPaused(v.voice->channel, false);
    }
}

void SetPambFrq(AMB* amb, float pitch) {
    if (amb) {
        amb->frq = pitch;
        ApplyEffectPitchBend(*amb, amb->currentPitchBend);
    }
}

static void updateAmbSpatial(AMB& amb) {
    if (amb.palo) amb.pos = amb.palo->xf.posWorld;
    float attenuated = amb.uVolAtSource;
    float pan = 0.0f;
    if (amb.fAttached) {
        const glm::vec3* earPos = PposSoundEar();
        if (earPos) {
            const float dist = SDistSoundEar(*earPos, amb.pos);
            CalculateVolPan(dist, &amb.pos, &attenuated, &pan, amb.uVolAtSource, amb.sStart, amb.sFull);
            amb.fOutOfRange = (dist > amb.sStart) ? 1 : 0;
        }
    }
    amb.volAttenuated = attenuated;
    amb.pan = pan;
}

void SetPambVol(AMB* amb, float volume) {
    if (!amb) return;
    amb->uVolAtSource = volume;
    updateAmbSpatial(*amb);
    applyAmbVoicesMix(*amb);
}

void StartSound(SFXID id, AMB** handle, ALO* object, glm::vec3* position,
                float start, float full, float volume, float pitch, float, LM*, LM*) {
    int index = int(id);
    if (!audioReady || !g_psw || index < 0 || index >= int(kSfxCount)) return;
    int bankId = 0;
    while (index >= int(kEffectBankStarts[bankId + 1])) ++bankId;
    if (bankId && bankId != g_psw->ibnk) return;
    auto* bank = static_cast<EFFECTBANK*>(SbpEnsureBank(bankId, nullptr));
    int local = index - kEffectBankStarts[bankId];
    if (!bank || local >= int(bank->sounds.size()) || bank->sounds[local].grains.empty()) return;
    EFFECTBANK::SOUND& sound = bank->sounds[local];
    if (handle && *handle) StopSound(*handle, 0);

    if ((sound.flags & 8u) != 0 && sound.instanceLimit > 0) {
        std::vector<AMB*> instances;
        for (const auto& slot : g_psw->ambOwners) {
            if (slot && slot->bnk == bank && slot->soundIndex == local && !slot->fStopped) {
                instances.push_back(slot.get());
            }
        }
        if (int(instances.size()) >= sound.instanceLimit) {
            AMB* victim = nullptr;
            if ((sound.flags & 0x10u) != 0) {
                for (AMB* inst : instances) if (!victim || inst->uVolAtSource < victim->uVolAtSource) victim = inst;
            } else {
                for (AMB* inst : instances) if (!victim || inst->tStarted < victim->tStarted) victim = inst;
            }
            if (!victim || ((sound.flags & 0x10u) != 0 && victim->uVolAtSource >= volume)) return;
            StopSound(victim, 0);
        }
    }

    if (g_psw->ambOwners.size() > 128) return;
    const bool continuous = FContinuousSound(id);
    bool positioned = (object != nullptr || position != nullptr);
    glm::vec3 pos = object ? object->xf.posWorld : position ? *position : glm::vec3(0.0f);
    bool outOfRange = false;
    if (positioned) {
        const glm::vec3* earPos = PposSoundEar();
        if (earPos) {
            const float distance = SDistSoundEar(*earPos, pos);
            if (start > 0.0f && distance > start) {
                if (!continuous) return;
                outOfRange = true;
            }
        }
    }

    auto amb = std::make_unique<AMB>();
    amb->sfxid = id;
    amb->tStarted = g_clock.t;
    amb->bnk = bank;
    amb->soundIndex = local;
    amb->palo = object;
    amb->pos = pos;
    amb->sStart = start;
    amb->sFull = full;
    amb->uVolAtSource = volume;
    amb->volAttenuated = outOfRange ? 0.0f : volume;
    amb->frq = pitch;
    amb->fContinuous = continuous ? 1 : 0;
    amb->fAttached = positioned ? 1 : 0;
    amb->fOutOfRange = outOfRange ? 1 : 0;
    amb->nextGrain = 0;
    amb->grainCountdown = sound.grains[0].delay;
    amb->handlerDone = outOfRange;
    if (amb->fAttached) updateAmbSpatial(*amb);
    if (handle) {
        *handle = amb.get();
        amb->ppamb = handle;
    }
    AMB* rawAmb = amb.get();
    g_psw->ambOwners.push_back(std::move(amb));
    if (!outOfRange) {
        int budget = 64;
        while (!rawAmb->handlerDone && rawAmb->grainCountdown <= 0 && budget-- > 0) {
            ExecuteEffectGrain(*rawAmb);
        }
    }
}

void StartAloAmbient(float, float, ALO*, AMB** handle) { if (handle) *handle = nullptr; }
void StartSwIntermittentSounds(SW*) {}

static std::string currentLevelDialogDir() {
    const LEVELINFO* info = g_transition.m_plevelCurrent;
    if (!info) return {};
    const char* episode = nullptr;
    switch (info->levelID >> 8) {
        case GAMEWORLD_Underwater: episode = "Tide of Terror"; break;
        case GAMEWORLD_Muggshot:   episode = "Sunset Snake Eyes"; break;
        case GAMEWORLD_Voodoo:     episode = "Vicious Voodoo"; break;
        case GAMEWORLD_Snow:       episode = "Fire in the sky"; break;
        case GAMEWORLD_Clockwerk:  episode = "The Cold Heart of Hate"; break;
        default: break;
    }
    std::string levelFolder;
    if (info->levelID == 3) levelFolder = "Paris";
    else if (info->levelID == 4) levelFolder = "The Hideout";
    else levelFolder = info->levelName;
    if (levelFolder.empty()) return {};
    std::string dir = "Worlds/";
    if (episode) { dir += episode; dir += '/'; }
    dir += levelFolder;
    dir += "/Dialog/";
    return dir;
}

static std::string resolveCommentaryWav(const std::string& filename) {
    unsigned levelId = 0;
    int parsed = 0;
    if (std::sscanf(filename.c_str(), "Commentary_%x.wav%n", &levelId, &parsed) != 1 ||
        parsed != int(filename.size())) return {};
    std::ifstream map("sdmc:/3ds/ThieviusCTR/Worlds/Sounds/Commentary/CommentaryMap.txt");
    if (!map) return {};
    std::string line;
    while (std::getline(map, line)) {
        char wavName[128]{};
        unsigned rowLevel = 0;
        if (std::sscanf(line.c_str(), "%127s | levelId 0x%x", wavName, &rowLevel) == 2 && rowLevel == levelId) {
            return std::string("sdmc:/3ds/ThieviusCTR/Worlds/Sounds/Commentary/") + wavName;
        }
    }
    return {};
}

void StopVag() {
    delete vag;
    vag = nullptr;
    clearVagStreamResources();
    vagStarted = false;
    s_vagSleepSaved = false;
}

void ContinueVag() {
    if (vag && audioReady && vag->channel >= 0 && !s_aptSuspended) {
        CanePumpVagStream3ds();
        ndspChnSetPaused(vag->channel, false);
        vagStarted = true;
    }
}

void ResumeVag() { ContinueVag(); }

void PauseVag() {
    if (vag && audioReady && vag->channel >= 0) {
        ndspChnSetPaused(vag->channel, true);
    }
}

int FVagPlaying() {
    CanePumpVagStream3ds();
    if (vag && vag->done()) {
        StopVag();
        return 0;
    }
    return vag ? 1 : 0;
}

bool FPauseForVag() {
    CanePumpVagStream3ds();
    if (vag && !vagStarted && !vag->done()) {
        ContinueVag();
        return false;
    }
    return vag && !vag->done() && audioReady && vag->channel >= 0 && ndspChnIsPaused(vag->channel);
}

void PreloadVag(const char* name) {
    StopVag();
    CaneFmvPcmStop3ds();
    if (!name || !audioReady) return;
    std::vector<std::string> candidates;
    if (std::strncmp(name, "sdmc:/", 6) == 0) {
        std::string explicitPath(name);
        auto dot = explicitPath.find_last_of('.');
        auto slash = explicitPath.find_last_of('/');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) explicitPath.resize(dot);
        explicitPath += ".wav";
        candidates.push_back(std::move(explicitPath));
    } else {
        std::string filename(name);
        auto slash = filename.find_last_of("/\\");
        if (slash != std::string::npos) filename.erase(0, slash + 1);
        auto extension = filename.find_last_of('.');
        if (extension != std::string::npos) filename.resize(extension);
        filename += ".wav";
        if (std::string mapped = resolveCommentaryWav(filename); !mapped.empty()) {
            candidates.push_back(std::move(mapped));
        }
        std::vector<std::string> dirs;
        if (std::string curDir = currentLevelDialogDir(); !curDir.empty()) dirs.push_back(std::move(curDir));
        for (const char* d : { "Cutscenes/", "Worlds/Sounds/Prompt/", "Worlds/Sounds/JLO Ambient/", "Worlds/Sounds/Commentary/", "Worlds/Paris/Dialog/", "Worlds/The Hideout/Dialog/" }) {
            dirs.emplace_back(d);
        }
        for (const auto& dir : dirs) {
            candidates.push_back("sdmc:/3ds/ThieviusCTR/" + dir + filename);
        }
    }

    for (const auto& fullPath : candidates) {
        FILE* fp = std::fopen(fullPath.c_str(), "rb");
        if (!fp) continue;
        char header[12];
        if (std::fread(header, 1, 12, fp) != 12 || std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4)) {
            std::fclose(fp);
            continue;
        }
        unsigned ch = 0, bits = 0, format = 0;
        for (unsigned chunk = 0; chunk < 32; ++chunk) {
            char tag[4];
            uint32_t bytes = 0;
            if (std::fread(tag, 1, 4, fp) != 4 || std::fread(&bytes, 4, 1, fp) != 1 || bytes > 64u * 1024u * 1024u) break;
            if (!std::memcmp(tag, "fmt ", 4)) {
                if (bytes < 16 || bytes > 1024) break;
                std::vector<uint8_t> info(bytes);
                if (std::fread(info.data(), 1, bytes, fp) != bytes) break;
                uint16_t value;
                std::memcpy(&value, info.data(), 2); format = value;
                std::memcpy(&value, info.data() + 2, 2); ch = value;
                std::memcpy(&vagRate, info.data() + 4, 4);
                std::memcpy(&value, info.data() + 14, 2); bits = value;
            } else if (!std::memcmp(tag, "data", 4)) {
                if (format != 1 || ch != 1 || bits != 16 || bytes == 0 || (bytes % 2) != 0 ||
                    vagRate < 4000 || vagRate > 192000) break;
                auto next = std::make_unique<IXAudio2SourceVoice>();
                if (channels[kVagChannel] && audioReady) ndspChnWaveBufClear(kVagChannel);
                if (bytes <= kMaxResidentVagBytes) {
                    next->memory = allocLinearPcm(bytes);
                    if (!next->memory) { std::fclose(fp); ++audioFailures; return; }
                    if (std::fread(next->memory, 1, bytes, fp) != bytes) { std::fclose(fp); ++audioFailures; return; }
                    std::fclose(fp);
                    next->sampleCount = bytes / 2;
                    channels[kVagChannel] = true;
                    next->channel = kVagChannel;
                    next->playOffset = 0;
                    DSP_FlushDataCache(next->memory, bytes);
                    ndspChnReset(kVagChannel);
                    ndspChnSetFormat(kVagChannel, NDSP_FORMAT_MONO_PCM16);
                    ndspChnSetRate(kVagChannel, float(vagRate));
                    ndspChnSetInterp(kVagChannel, NDSP_INTERP_POLYPHASE);
                    applyMixPan(kVagChannel, vagMixGain(), 0.0f);
                    ndspChnSetPaused(kVagChannel, true);
                    submitChannelWaveBufs(kVagChannel, next->memory, 0, next->sampleCount, false, 0, 0);
                    vag = next.release();
                    vagStarted = false;
                    s_vagSleepSaved = false;
                    ++audioVoices;
                    return;
                }
                s_vagStreamMem[0] = allocLinearPcm(kVagStreamChunkSamples * sizeof(int16_t));
                s_vagStreamMem[1] = allocLinearPcm(kVagStreamChunkSamples * sizeof(int16_t));
                if (!s_vagStreamMem[0] || !s_vagStreamMem[1]) {
                    std::fclose(fp);
                    clearVagStreamResources();
                    ++audioFailures;
                    return;
                }
                s_vagStreamFile = fp;
                s_vagStreamDataStart = std::ftell(fp);
                s_vagStreamTotalSamples = bytes / 2;
                s_vagStreamReadSamples = 0;
                s_vagStreamBufBase[0] = 0;
                s_vagStreamBufBase[1] = 0;
                next->streaming = true;
                next->ownsMemory = false;
                next->memory = nullptr;
                next->sampleCount = s_vagStreamTotalSamples;
                channels[kVagChannel] = true;
                next->channel = kVagChannel;
                next->playOffset = 0;
                chnWaveBufs[kVagChannel][0] = {};
                chnWaveBufs[kVagChannel][1] = {};
                ndspChnReset(kVagChannel);
                ndspChnSetFormat(kVagChannel, NDSP_FORMAT_MONO_PCM16);
                ndspChnSetRate(kVagChannel, float(vagRate));
                ndspChnSetInterp(kVagChannel, NDSP_INTERP_POLYPHASE);
                applyMixPan(kVagChannel, vagMixGain(), 0.0f);
                ndspChnSetPaused(kVagChannel, true);
                vag = next.release();
                vagStarted = false;
                s_vagSleepSaved = false;
                ++audioVoices;
                CanePumpVagStream3ds();
                return;
            } else {
                if (std::fseek(fp, long(bytes), SEEK_CUR) != 0) break;
            }
            if (bytes & 1) std::fseek(fp, 1, SEEK_CUR);
        }
        std::fclose(fp);
    }
    std::fprintf(stderr, "Missing native voice: %s\n", name);
}

void PreloadVagFileName(const char* name) { PreloadVag(name); }

void UpdateSounds() {
    drainPendingLinearFrees(false);
    CaneUpdateMusic();
    CanePumpVagStream3ds();
    if (vag && vag->done()) {
        StopVag();
    }
    if (g_iexcHyst != -100) {
        if (s_iexcCurHigh > g_iexcHyst && s_iexcCurHigh > 0) g_iexcHyst = s_iexcCurHigh;
        else if (--s_cframeExcitementHyst < 0) {
            if (g_iexcHyst < s_iexcCurHigh) ++g_iexcHyst;
            else if (g_iexcHyst > s_iexcCurHigh) --g_iexcHyst;
            s_cframeExcitementHyst += 3;
        }
        g_fSneakyFeet = g_iexcHyst < 0 ? 1 : 0;
        SetGlobalExciteValue(g_iexcHyst + 20);
        if (g_iexcHyst < 0) {
            const float uSneak = std::clamp(static_cast<float>(g_iexcHyst) * -0.05f, 0.0f, 1.0f);
            SetMvgkRvol((1.0f - uSneak) + uSneak * (2.0f / 3.0f), 7, 1);
        } else {
            SetMvgkRvol(1.0f, 7, 1);
        }
        if (g_iexcHyst == s_iexcCurHigh) g_iexcHyst = -100;
    }
    if (!g_psw) return;
    for (auto& owned : g_psw->ambOwners) {
        if (!owned) continue;
        auto& amb = *owned;
        if (amb.palo != nullptr && !FIsLoInWorld(reinterpret_cast<LO*>(amb.palo))) {
            StopSound(&amb, 0);
            continue;
        }
        for (auto it = amb.voices.begin(); it != amb.voices.end();) {
            if (!it->voice || it->voice->done()) {
                delete it->voice;
                it = amb.voices.erase(it);
            } else {
                ++it;
            }
        }
        const bool wasOutOfRange = amb.fOutOfRange != 0;
        if (amb.fAttached && !amb.fStopped) {
            updateAmbSpatial(amb);
            applyAmbVoicesMix(amb);
        }
        if (amb.fOutOfRange && !amb.fContinuous) {
            StopSound(&amb, 0);
            continue;
        }
        if (amb.fContinuous && amb.fOutOfRange && !wasOutOfRange) {
            for (auto& active : amb.voices) RetireSourceVoice(active.voice, active.pcm);
            amb.voices.clear();
            amb.handlerDone = true;
        } else if (amb.fContinuous && !amb.fOutOfRange && wasOutOfRange && amb.voices.empty()) {
            amb.nextGrain = 0;
            amb.grainCountdown = 0;
            amb.handlerDone = false;
        }
        if (!amb.fStopped && !amb.paused && !(sequencerPaused && s_ambientsPausedBySeq)) {
            for (int tick = 0; tick < 8; ++tick) {
                unsigned budget = 64;
                while (!amb.handlerDone && amb.grainCountdown <= 0 && budget--) ExecuteEffectGrain(amb);
                --amb.grainCountdown;
            }
        }
        if (((amb.handlerDone && !amb.fContinuous && amb.voices.empty()) || amb.fStopped)) {
            StopSound(&amb, 0);
        }
    }
    auto& owners = g_psw->ambOwners;
    owners.erase(std::remove_if(owners.begin(), owners.end(),
        [](const auto& amb) { return !amb || amb->fStopped; }), owners.end());
}

void KillSoundSystem() {
    KillMusic();
    StopVag();
    CaneFmvPcmStop3ds();
    s_fFmvAudioMode = false;
    closeBankFiles(false);
    if (g_psw) {
        for (auto& amb : g_psw->ambOwners) if (amb) StopSound(amb.get(), 0);
        g_psw->ambOwners.clear();
    }
    for (auto& group : s_relativeVolumes) group.fill(1.0f);
    s_channelVolumes.fill(1.0f);
    s_iexcCurHigh = 0;
    s_globalExcite = 20;
    g_iexcHyst = -100;
    s_cframeExcitementHyst = 0;
    g_fSneakyFeet = 0;
    s_musicVolume = 1.0f;
    s_dialogMusicDuck = 1.0f;
    s_ambientsPausedBySeq = false;
    sequencerPaused = false;
    s_effectGlobalRegisters.fill(0);
    s_amRegisters.fill(0);
    s_music989.registers.fill(0);
    drainPendingLinearFrees(false);
}

void CaneStopAudio3ds() {
    if (s_aptAudioHooked) {
        aptUnhook(&s_aptAudioHook);
        s_aptAudioHooked = false;
    }
    CaneStopMusicVoices();
    KillSoundSystem();
    closeBankFiles();
    sfxCache.clear();
    sfxCacheBytes = 0;
    if (audioReady) {
        if (stockDsp) ndspExit();
        else opendspExit();
    }
    audioReady = false;
    drainPendingLinearFrees(true);
    channels.fill(false);
    chnCooldownFrame.fill(0);
    std::memset(chnWaveBufs, 0, sizeof(chnWaveBufs));
}

static void CaneSaveAudioConfig() {
    const char* path = "sdmc:/3ds/ThieviusCTR/audio-backend.cfg";
    const char* temp = "sdmc:/3ds/ThieviusCTR/audio-backend.cfg.tmp";
    if (FILE* file = std::fopen(temp, "w")) {
        const bool ok = std::fprintf(file, "%d %.3f %.3f %.3f %d\n",
            requestedDsp, music, sfx, dialogue, int(stereo)) > 0;
        const bool closed = (std::fclose(file) == 0);
        if (!(ok && closed && std::rename(temp, path) == 0)) std::remove(temp);
    }
}

bool CaneChooseAudio3ds(int choice) {
    requestedDsp = (choice == 1) ? 1 : 0;
    IXAudio2SourceVoice* saved = vag;
    vag = nullptr;
    const bool playing = vagStarted;
    const bool paused = saved && audioReady && saved->channel >= 0 && ndspChnIsPaused(saved->channel);
    unsigned offset = saved ? captureVagSamplePos(saved) : 0;
    if (saved) {
        if (audioReady && saved->channel >= 0) {
            ndspChnWaveBufClear(saved->channel);
            channels[saved->channel] = false;
        }
        saved->channel = -1;
    }

    // Stop active hardware voices WITHOUT clearing s_music989, musicSamples, or world AMBs!
    CaneStopMusicVoices();
    if (g_psw) {
        for (auto& owned : g_psw->ambOwners) {
            if (!owned) continue;
            for (auto& ev : owned->voices) RetireSourceVoice(ev.voice, ev.pcm);
            owned->voices.clear();
            if (owned->fContinuous && !owned->fStopped && !owned->fOutOfRange) {
                owned->nextGrain = 0;
                owned->grainCountdown = 0;
                owned->handlerDone = false;
            }
        }
    }
    if (audioReady) {
        if (stockDsp) ndspExit();
        else opendspExit();
        audioReady = false;
    }
    drainPendingLinearFrees(true);
    channels.fill(false);
    chnCooldownFrame.fill(0);
    std::memset(chnWaveBufs, 0, sizeof(chnWaveBufs));

    const bool ok = startBackend(requestedDsp);
    musicLastTick = osGetTime();
    if (ok) {
        for (auto& entry : musicSamples) {
            if (entry.second && entry.second->pcm) {
                DSP_FlushDataCache(entry.second->pcm, entry.second->count * 2);
            }
        }
        if (saved) {
            if (resubmitVagFromOffset(saved, offset, paused)) {
                vag = saved;
                vagStarted = playing;
            } else {
                delete saved;
                clearVagStreamResources();
                vagStarted = false;
            }
        }
    } else if (saved) {
        delete saved;
        clearVagStreamResources();
        vagStarted = false;
    }
    CaneSaveAudioConfig();
    return ok;
}

void CaneAudioStats(unsigned* played, unsigned* failed) {
    *played = audioVoices;
    *failed = audioFailures;
}

const glm::vec3* PposSoundEar() {
    PO* ppo = PpoCur();
    if (ppo != nullptr && ppo->pvtpo && ppo->pvtpo->pfnFIsPoSoundBase && ppo->pvtpo->pfnFIsPoSoundBase(ppo)) {
        return &ppo->xf.posWorld;
    }
    return g_pcm != nullptr ? &g_pcm->pos : nullptr;
}

float SDistSoundEar(const glm::vec3& posEar, const glm::vec3& posSound) {
    const glm::vec3 displacement = posSound - posEar;
    const LOOKK lookk = g_pcm != nullptr ? LookkCurCplook(&g_pcm->cplook) : LOOKK_Nil;
    if (g_pcm == nullptr || FActiveCplcy(static_cast<CPLCY*>(&g_pcm->cplook)) == 0 ||
        lookk < LOOKK_Sniper || lookk > LOOKK_SniperTrack) {
        return glm::length(displacement);
    }
    const glm::vec3 forward = g_pcm->mat[0];
    const float forwardDistance = glm::dot(displacement, forward);
    const glm::vec3 perpendicular = displacement - forward * forwardDistance;
    const float zoomScale = 1.0f / ((1.0f - g_pcm->cplook.uZoom) + g_pcm->cplook.uZoom * 10.0f);
    return glm::length(perpendicular + forward * (forwardDistance * zoomScale));
}

void CalculateVolPan(float sDist, glm::vec3* pposSound, float* pvol, float* ppan,
                     float uvol, float sStart, float sFull) {
    if (!pvol || !ppan) return;
    if (pposSound != nullptr) {
        if (sStart > 0.0f && sStart < sDist) {
            *ppan = 0.0f;
            *pvol = 0.0f;
            return;
        }
        if (sFull < sDist && sStart > sFull) {
            const float attenuation = (sStart - sDist) / (sStart - sFull);
            float side = 0.0f;
            if (g_pcm != nullptr) {
                const glm::vec3 displacement = *pposSound - g_pcm->pos;
                const float lenSq = glm::dot(displacement, displacement);
                if (lenSq > 0.00000001f) side = glm::dot(displacement / std::sqrt(lenSq), g_pcm->mat[1]);
            }
            *ppan = -side * 0.7f * (1.0f - attenuation);
            *pvol = uvol * attenuation;
            return;
        }
    }
    *pvol = uvol;
    *ppan = 0.0f;
}

void SetEffectReverbTypeDepth(REVERBK, int) {}
void SetGlobalExciteValue(int value) {
    s_globalExcite = std::clamp(value, 0, 127);
    ReapplyMusicAmeGroups(0);
}
void BeginDialogAudioDuck() { s_dialogMusicDuck = 0.2f; refreshAllMusicVoiceMixes(); }
void EndDialogAudioDuck() { s_dialogMusicDuck = 1.0f; refreshAllMusicVoiceMixes(); }
void ContinueMusic() { s_musicVolume = 1.0f; musicLastTick = osGetTime(); refreshAllMusicVoiceMixes(); }
void PauseMusic() { s_musicVolume = 0.0f; refreshAllMusicVoiceMixes(); }
void KillMusic() { CaneKillMusic(); }
void ResetCommentaryAudio() { StopVag(); }
bool FPlayingCommentaryAudio() { return false; }

int HsNextFootFall() {
    static constexpr int aadhs[7][6] = {
        { -5, -5, -5, -5, -5, -5 },
        { -5, -5, -5, -5, -5,  1 },
        { -5, -5, -5, -5,  1,  1 },
        { -5, -5, -5,  1,  1,  1 },
        { -5, -5,  1,  1,  1,  1 },
        { -5,  1,  1,  1,  1,  1 },
        {  1,  1,  1,  1,  1,  1 }
    };
    static constexpr int iadhs[25] = {
        6, 6, 6, 6, 6,
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
        4, 3, 2, 1, 0
    };
    static int hsFootfallCurrent = 0;
    const int randomChoice = NRandInRange(0, 5);
    hsFootfallCurrent += aadhs[iadhs[hsFootfallCurrent + 12]][randomChoice];
    return hsFootfallCurrent;
}

void NextSneakyFootstep() {
    if (g_fSneakyFeet == 0) return;
    const int hsFootfall = HsNextFootFall();
    StartSound(static_cast<SFXID>(119), nullptr, nullptr, nullptr, 0.0f, 0.0f, 0.5f,
        static_cast<float>(hsFootfall) * (1.0f / 12.0f), 0.0f, nullptr, nullptr);
}

void SetMasterVolume(float value) {
    master = std::clamp(value, 0.0f, 1.0f);
    refreshAllLiveAudio();
}
float GetMasterVolume() { return master; }

void RecalculateChannelVolume(int iChannel) {
    if (iChannel < 0 || iChannel >= int(s_channelVolumes.size())) return;
    float product = 1.0f;
    for (const auto& group : s_relativeVolumes) product *= group[iChannel];
    s_channelVolumes[iChannel] = std::clamp(product, 0.0f, 1.0f);
    if (!audioReady) return;
    if (iChannel == 0 && g_psw) {
        for (auto& owned : g_psw->ambOwners) if (owned) applyAmbVoicesMix(*owned);
    } else if (iChannel == 1) {
        refreshAllMusicVoiceMixes();
    } else if (iChannel == 2) {
        applyVagMix();
    }
}

void SetMvgkRvol(float value, int group, int channel) {
    if (group < 0 || group >= int(s_relativeVolumes.size()) ||
        channel < 0 || channel >= int(s_channelVolumes.size())) return;
    s_relativeVolumes[group][channel] = std::clamp(value, 0.0f, 1.0f);
    RecalculateChannelVolume(channel);
}

void SetMusicSequencerGameplayClock(bool) {}
void StartMusidSong(MUSID id, const char*) { CaneStartMusic(id); }
void SetAMRegister(int reg, byte value) {
    if (reg >= 0 && reg < 16) {
        s_amRegisters[reg] = value & 127;
        s_music989.registers[reg] = value & 127;
        ReapplyMusicAmeGroups(reg + 1);
    }
}
void RefreshMidiRegister(int reg) { if (reg >= 0 && reg < 16) s_amRegisters[reg] = s_music989.registers[reg]; }
float GetUserDialogueVolume() { return dialogue; }
float GetUserMusicVolume() { return music; }
float GetUserSfxVolume() { return sfx; }
void SetUserDialogueVolume(float value) {
    dialogue = std::clamp(value, 0.0f, 1.0f);
    applyVagMix();
    CaneSaveAudioConfig();
}
void SetUserMusicVolume(float value) {
    music = std::clamp(value, 0.0f, 1.0f);
    refreshAllMusicVoiceMixes();
    CaneSaveAudioConfig();
}
void SetUserSfxVolume(float value) {
    sfx = std::clamp(value, 0.0f, 1.0f);
    if (g_psw) for (auto& owned : g_psw->ambOwners) if (owned) applyAmbVoicesMix(*owned);
    CaneSaveAudioConfig();
}
bool FUserStereoEnabled() { return stereo; }
void SetUserStereoEnabled(bool value) {
    stereo = value;
    s_stereoOutputEnabled = value;
    if (audioReady) ndspSetOutputMode(stereo ? NDSP_OUTPUT_STEREO : NDSP_OUTPUT_MONO);
    refreshAllLiveAudio();
    CaneSaveAudioConfig();
}

// Original ProjectCane bookkeeping from sound.cpp.
void UnsetExcitementHyst(EXC* pexc)
{
	if (pexc == nullptr)
		return;
	const IEXC removedExcitement = pexc->iexc;
	RemoveExc(pexc);

	if (removedExcitement == s_iexcCurHigh)
	{
		g_iexcHyst = removedExcitement;
		SetIexcCurHigh(nullptr);
	}
}

EXC* PexcAlloc()
{
	if (g_psw == nullptr)
		return nullptr;
	for (std::shared_ptr<EXC>& slot : g_psw->excOwners)
	{
		if (slot)
			continue;
		slot = std::make_shared<EXC>();
		EXC* pexc = slot.get();
		AppendDlEntry(&g_psw->dlExc, pexc);
		return pexc;
	}
	return nullptr;
}

void RemoveExc(EXC* pexc)
{
	if (pexc == nullptr || g_psw == nullptr)
		return;
	RemoveDlEntry(&g_psw->dlExc, pexc);
	for (std::shared_ptr<EXC>& slot : g_psw->excOwners)
	{
		if (slot.get() == pexc)
		{
			slot.reset();
			return;
		}
	}
}

void SetIexcCurHigh(EXC*)
{
	if (g_psw == nullptr || FIsDlEmpty(&g_psw->dlExc) != 0)
	{
		s_iexcCurHigh = 0;
		return;
	}
	s_iexcCurHigh = -20;
	for (EXC* current = g_psw->dlExc.pexcFirst; current != nullptr;
		current = current->dle.pexcNext)
		s_iexcCurHigh = (std::max)(s_iexcCurHigh, current->iexc);
}

EXC* PexcSetExcitement(IEXC iexc)
{
	if (g_psw == nullptr)
		return nullptr;

	const bool wasEmpty = FIsDlEmpty(&g_psw->dlExc) != 0;
	EXC* pexc = PexcAlloc();
	if (pexc == nullptr)
		return nullptr;
	pexc->iexc = std::clamp(iexc, -20, 107);

	if (wasEmpty || pexc->iexc > s_iexcCurHigh)
	{
		g_iexcHyst = s_iexcCurHigh;
		s_iexcCurHigh = pexc->iexc;
	}

	return pexc;
}

void UnsetExcitement(EXC* pexc)
{
	if (pexc == nullptr || g_psw == nullptr)
		return;

	const IEXC removedExcitement = pexc->iexc;
	RemoveExc(pexc);

	if (removedExcitement != s_iexcCurHigh)
		return;

	SetIexcCurHigh(nullptr);
	if (s_iexcCurHigh >= 0)
		g_fSneakyFeet = 0;
	g_iexcHyst = s_iexcCurHigh;
	SetGlobalExciteValue(s_iexcCurHigh + 20);
	SetMvgkRvol(1.0f, 7, 1);
	g_iexcHyst = -100;
}

void PushSwReverb(SW* psw, REVERBK rvrbk, int depth)
{
	if (!psw || psw->irse < 0 || psw->irse >= 4)
		return;

	psw->arse[psw->irse].rvrbk = rvrbk;
	psw->arse[psw->irse].depth = depth;
	++psw->irse;
	SetEffectReverbTypeDepth(rvrbk, depth);
}

void PopSwReverb(SW* psw)
{
	if (!psw || psw->irse <= 1)
		return;

	--psw->irse;
	const RSE& restored = psw->arse[psw->irse - 1];
	SetEffectReverbTypeDepth(restored.rvrbk, restored.depth);
}

void SetSwDefaultReverb(SW* psw, REVERBK rvrbk, int depth)
{
	if (!psw)
		return;

	psw->arse[0].rvrbk = rvrbk;
	psw->arse[0].depth = depth;
	psw->irse = 1;
	SetEffectReverbTypeDepth(rvrbk, depth);
}

void SetSwIntermittentSound(SW* psw, SFXID sfxid)
{
	if (psw == nullptr)
		return;

	if (psw->cisi >= 16)
		return;

	ISI& isi = psw->aisi[psw->cisi++];

	isi.sfxid = sfxid;
	isi.lmRepeat = s_lmRepeatDefault;
	isi.lmRepDist = s_lmDistDefault;
}

void SetSwIntermittentVolPct(SW* psw, const LM* plmVolPct)
{
	if (psw == nullptr)
		return;

	if (psw->cisi == 0 || plmVolPct == nullptr)
		return;

	ISI& isi = psw->aisi[psw->cisi - 1];

	isi.lmRepDist.gMin = 8000.0f - plmVolPct->gMax * 50.0f;
	isi.lmRepDist.gMax = 8000.0f - plmVolPct->gMin * 50.0f;
}

void SetSwIntermittentRepeat(SW* psw, const LM* plmRepeat)
{
	if (psw == nullptr)
		return;

	if (psw->cisi == 0 || plmRepeat == nullptr)
		return;

	psw->aisi[psw->cisi - 1].lmRepeat = *plmRepeat;
}

void HandleWipeVolumes(int fMusicWipes, float u) {
    SetMvgkRvol(u, 6, 0);
    SetMvgkRvol(u, 6, 2);
    SetMvgkRvol(u, 6, 3);
    if (fMusicWipes) SetMvgkRvol(u, 6, 1);
}
void PlayCommentaryAudio(char*) {}
void SelectAudioOutputDevice(int direction) { if (direction) CaneChooseAudio3ds(!CaneAudioChoice3ds()); }
void SetAttractSoundOption(bool) {}
void SetAttractVolume(bool fMuted) { SetMvgkRvol(fMuted ? 0.0f : 1.0f, 3, 1); }
void PauseMusicSequencer() {
    sequencerPaused = true;
    if (audioReady) {
        for (auto& voice : s_music989.voices) {
            if (voice.voice && voice.voice->channel >= 0) ndspChnSetPaused(voice.voice->channel, true);
        }
        if (g_psw) {
            s_ambientsPausedBySeq = true;
            for (auto& owned : g_psw->ambOwners) {
                if (!owned || owned->paused) continue;
                for (auto& ev : owned->voices) {
                    if (ev.voice && ev.voice->channel >= 0) ndspChnSetPaused(ev.voice->channel, true);
                }
            }
        }
    }
}
void ContinueMusicSequencer(bool continueAmbientSounds) {
    sequencerPaused = false;
    musicLastTick = osGetTime();
    if (audioReady && !s_aptSuspended) {
        for (auto& voice : s_music989.voices) {
            if (voice.voice && voice.voice->channel >= 0) ndspChnSetPaused(voice.voice->channel, false);
        }
        if (continueAmbientSounds && g_psw) {
            s_ambientsPausedBySeq = false;
            for (auto& owned : g_psw->ambOwners) {
                if (!owned || owned->paused) continue;
                for (auto& ev : owned->voices) {
                    if (ev.voice && ev.voice->channel >= 0) ndspChnSetPaused(ev.voice->channel, false);
                }
            }
        }
    }
}
bool FMusicSequencerPaused() { return sequencerPaused; }
