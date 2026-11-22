// Native output for the upstream 989SND MIDI/AME sequencer. Instruments are
// shared DSP-memory buffers, not copied for every note or retained on the CPU heap.
Music989State s_music989;
static bool sequencerPaused=false,musicLoaded=false;
static u64 musicLastTick=0;
static unsigned musicNotes=0,musicDrops=0,musicTicks=0,musicBytes=0;
float s_musicVolume=1,s_dialogMusicDuck=1;
static float& s_userMusicVolume=music;
bool s_stereoOutputEnabled=true;
static constexpr int kMusicMixerChannel=1;
std::array<float,4> s_channelVolumes={1,1,1,1};
static constexpr float XAUDIO2_MIN_FREQ_RATIO=.0625f;
struct CaneMusicSample {
    int16_t* pcm=nullptr;unsigned count=0;
    ~CaneMusicSample(){
        if(!pcm)return;
        if(audioReady)pendingLinearFrees.push_back({pcm,ndspGetFrameCount()+2});
        else linearFree(pcm);
        pcm=nullptr;
    }
};
static std::map<unsigned,std::unique_ptr<CaneMusicSample>> musicSamples;
static void TraceMusicEvent(const char*,int=-1,int=-1) {}
static void RetireMusicVoice(MusicVoice& active) {delete active.voice;active.voice=nullptr;}
static void ResetMusicSegmentForLoop(MusicSegment&);
static void StopAllMusicVoicesForSegment(uint32_t segment) {
    for(auto& voice:s_music989.voices)if(voice.voice && voice.segment==segment) {
        RetireMusicVoice(voice);voice.envelope.phase=MusicEnvelopePhase::Stopped;
    }
}
static void SetVoiceVolumeChecked(IXAudio2SourceVoice* voice,float value,const char*) {
    if(!voice || voice->channel<0)return;
    voice->mixVolume=value;
    if(!audioReady)return;
    float pan=std::sin(voice->pan*3.14159265358979323846f/180);
    float mix[12]{};value=std::clamp(value*master,0.0f,1.0f);
    mix[0]=value*(stereo?(1-pan)*.5f:.5f);mix[1]=value*(stereo?(1+pan)*.5f:.5f);
    ndspChnSetMix(voice->channel,mix);
}
static void SetVoiceFrequencyRatioChecked(IXAudio2SourceVoice* voice,float value,const char*) {
    if(voice && voice->channel>=0 && audioReady)ndspChnSetRate(voice->channel,48000*std::clamp(value,.0625f,kSpuMaximumFrequencyRatio));
}
void SetMusicVoicePan(IXAudio2SourceVoice* voice,int pan) {if(voice)voice->pan=pan%360;}

bool DecodeMusicAdpcm(const std::vector<uint8_t>& data,size_t begin,size_t end,MusicTone& tone) {
    if(begin>=end || end>data.size())return false;
    static constexpr int coef[16][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
    int h1=0,h2=0;unsigned samples=0;size_t loopStart=begin,stop=begin;bool loopMark=false,ended=false;
    auto block=[&](size_t pos,int16_t* out,unsigned& cursor) {
        unsigned predictor=data[pos]>>4,shift=data[pos]&15;
        for(unsigned i=0;i<28;++i) {
            int nibble=(data[pos+2+i/2]>>(4*(i%2)))&15;if(nibble&8)nibble-=16;
            int value=std::clamp(((nibble*4096)>>shift)+((coef[predictor][0]*h1+coef[predictor][1]*h2+32)>>6),-32768,32767);
            h2=h1;h1=value;if(out)out[cursor]=int16_t(value);++cursor;
        }
    };
    for(size_t pos=begin;pos+16<=end;pos+=16) {
        unsigned flags=data[pos+1];if(flags&4) {loopStart=pos;loopMark=true;}
        block(pos,nullptr,samples);
        if(flags&1) {stop=pos+16;ended=true;tone.loop=(flags&2) && loopMark;break;}
    }
    if(!ended || !samples)return false;
    unsigned loopSamples=tone.loop?unsigned((stop-loopStart)/16*28):0,passes=0;
    if(loopSamples)for(unsigned pass=0;pass<32;++pass) {
        int previous1=h1,previous2=h2;unsigned ignored=0;
        for(size_t pos=loopStart;pos<stop;pos+=16)block(pos,nullptr,ignored);
        ++passes;if(h1==previous1 && h2==previous2)break;
    }
    tone.loopBegin=passes?samples+(passes-1)*loopSamples:0;tone.loopLength=loopSamples;
    tone.format={};tone.format.formatTag=1;tone.format.channels=1;tone.format.samplesPerSec=48000;
    tone.format.bitsPerSample=16;tone.format.blockAlign=2;tone.format.avgBytesPerSec=96000;
    if(musicSamples.count(tone.sampleOffset))return true;
    unsigned count=samples+passes*loopSamples;
    if(count>4*1024*1024 || uint64_t(musicBytes)+uint64_t(count)*2>8*1024*1024)return false;
    auto decoded=std::make_unique<CaneMusicSample>();decoded->count=count;
    decoded->pcm=allocLinearPcm(count*2);if(!decoded->pcm)return false;
    h1=h2=0;unsigned cursor=0;
    for(size_t pos=begin;pos<stop;pos+=16)block(pos,decoded->pcm,cursor);
    for(unsigned pass=0;pass<passes;++pass)for(size_t pos=loopStart;pos<stop;pos+=16)block(pos,decoded->pcm,cursor);
    DSP_FlushDataCache(decoded->pcm,count*2);musicBytes+=count*2;
    musicSamples.emplace(tone.sampleOffset,std::move(decoded));return true;
}

#include "native_music_shared.inc"

void StartMusicTone(MusicSegment& segment,uint8_t channel,uint8_t note,uint8_t velocity,const MusicTone* onlyTone,uint32_t playBegin) {
    if(!audioReady || sequencerPaused || channel>=16 || segment.muted[channel])return;
    for(auto& tone:s_music989.tones) {
        if((onlyTone && &tone!=onlyTone) || tone.program!=segment.programs[channel] || note<tone.mapLow || note>tone.mapHigh)continue;
        auto sample=musicSamples.find(tone.sampleOffset);if(sample==musicSamples.end())continue;
        unsigned sampleCount=sample->second->count;
        if(!sampleCount)continue;
        unsigned count=0;MusicVoice* victim=nullptr;
        for(auto& voice:s_music989.voices)if(voice.voice) {
            ++count;if(!victim || voice.priority<victim->priority || (voice.priority==victim->priority && voice.allocationSerial<victim->allocationSerial))victim=&voice;
        }
        if(count>=16) {
            if(!victim || victim->priority>tone.priority) {++musicDrops;continue;}
            RetireMusicVoice(*victim);
        }
        int hardware=allocatePlaybackChannel(true,tone.priority);
        if(hardware<0) {++musicDrops;continue;}
        auto voice=std::make_unique<IXAudio2SourceVoice>();voice->channel=hardware;channels[hardware]=true;
        voice->ownsMemory=false;voice->memory=sample->second->pcm;voice->sampleCount=sampleCount;
        ndspChnReset(hardware);ndspChnSetFormat(hardware,NDSP_FORMAT_MONO_PCM16);ndspChnSetInterp(hardware,NDSP_INTERP_POLYPHASE);
        if(tone.loop && tone.loopLength && playBegin>=tone.loopBegin)playBegin=tone.loopBegin+(playBegin-tone.loopBegin)%tone.loopLength;
        playBegin=std::min<uint32_t>(playBegin,sampleCount-1);
        voice->playOffset=playBegin;voice->looping=tone.loop;
        submitChannelWaveBufs(hardware,voice->memory,playBegin,sampleCount,tone.loop,tone.loopBegin,tone.loopLength);
        MusicVoice active{};active.voice=voice.release();active.tone=&tone;active.segment=segment.id;active.channel=channel;
        active.note=note;active.velocity=velocity;active.priority=tone.priority;active.allocationSerial=++musicNotes;
        const bool ps1=tone.centerNote>=0;
        active.baseFrequencyRatio=float(std::exp2((double(note)-std::abs(tone.centerNote)+tone.centerFine/128.0)/12)*(ps1 && tone.applyPs1SampleRateCorrection?44100.0/48000:1));
        active.pitchBendLow=tone.pitchBendLow;active.pitchBendHigh=tone.pitchBendHigh;
        int bend=segment.pitchBends[channel];float semitones=float(bend)*(bend>=0?tone.pitchBendHigh:tone.pitchBendLow)/(bend>=0?32767:32768);
        SetVoiceFrequencyRatioChecked(active.voice,active.baseFrequencyRatio*std::exp2(semitones/12),"Music pitch");
        StartMusicEnvelope(active.envelope,tone.adsr1,tone.adsr2);
        RefreshMusicVoiceControllerState(active,segment);s_music989.voices.push_back(std::move(active));
    }
}
static void CaneStopMusicVoices() {for(auto& voice:s_music989.voices)RetireMusicVoice(voice);s_music989.voices.clear();}
static void CaneKillMusic() {
    CaneStopMusicVoices();s_music989={};musicSamples.clear();musicBytes=0;musicLoaded=false;musicLastTick=osGetTime();
}
static void CaneStartMusic(MUSID id) {
    CaneKillMusic();if(int(id)<0)return;
    char path[128];std::snprintf(path,sizeof path,"sdmc:/3ds/ThieviusCTR/Worlds/Music/MUSID_%02X.mus",unsigned(id));
    if(!LoadMusic989Mus(path)) {std::fprintf(stderr,"Music unavailable: %s\n",path);CaneKillMusic();return;}
    musicLoaded=true;sequencerPaused=false;s_musicVolume=1;s_music989.registers=s_amRegisters;
    s_music989.activeSegments.reserve(64);s_music989.voices.reserve(48);
    QueueMusicSegment(0);ActivateQueuedMusicSegments();PrimeMusicSequencerStartup();musicLastTick=osGetTime();
    std::fprintf(stderr,"Music started id=%02X instruments=%lu samples=%u segments=%lu\n",unsigned(id),(unsigned long)s_music989.tones.size(),musicBytes,(unsigned long)s_music989.segmentOffsets.size());
}
static void CaneUpdateMusic() {
    u64 now=osGetTime();
    u64 elapsed=now>=musicLastTick?now-musicLastTick:0;
    musicLastTick=now;
    if(!musicLoaded || sequencerPaused || !audioReady)return;
    s_music989.tickAccumulator+=std::min<u64>(elapsed,250)*.24;
    while(s_music989.tickAccumulator>=1) {
        for(auto& segment:s_music989.activeSegments)StepMusicSegment(segment);
        for(auto& segment:s_music989.activeSegments)if(segment.complete)for(auto& voice:s_music989.voices)
            if(voice.voice && voice.segment==segment.id)ReleaseMusicEnvelope(voice.envelope);
        UpdateMusicEnvelopes();ActivateQueuedMusicSegments();
        auto& segments=s_music989.activeSegments;segments.erase(std::remove_if(segments.begin(),segments.end(),[](const auto& s){return s.complete;}),segments.end());
        s_music989.tickAccumulator-=1;++musicTicks;
    }
    for(auto& voice:s_music989.voices)if(voice.voice && voice.voice->done())RetireMusicVoice(voice);
    auto& voices=s_music989.voices;voices.erase(std::remove_if(voices.begin(),voices.end(),[](const auto& v){return !v.voice;}),voices.end());
}
void CaneMusicStats3ds(unsigned* notes,unsigned* active,unsigned* bytes,unsigned* drops) {
    *notes=musicNotes;*active=s_music989.voices.size();*bytes=musicBytes;*drops=musicDrops;
}
