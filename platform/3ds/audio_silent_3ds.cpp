// Explicitly muted backend for the first native gameplay milestone.
// No PC mixer, device threads, sound-bank PCM or movie decoders are started.
#include "sound.h"
#include "sw.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
IEXC g_iexcHyst=0,s_iexcCurHigh=0;
int s_sfxidLast=-1,g_fSneakyFeet=0;
std::array<uint8_t,16> s_amRegisters{};
uint8_t s_globalExcite=0;
LM s_lmRepeatDefault={4,6},s_lmDistDefault={3000,8000};
namespace {float music=1,sfx=1,dialogue=1,master=1;bool stereo=true;}
void SetEffectReverbTypeDepth(REVERBK,int) {}
void SetGlobalExciteValue(int value) {s_globalExcite=std::clamp(value,0,127);}
void BeginDialogAudioDuck() {}
void EndDialogAudioDuck() {}
void ContinueMusic() {}
void PauseMusic() {}
void KillMusic() {}
void KillSoundSystem() {}
void ContinuePamb(AMB*) {}
void PausePamb(AMB*) {}
void DropPamb(AMB** amb) {if(amb)*amb=nullptr;}
void StopSound(AMB*,int) {}
void ContinueVag() {}
void PauseVag() {}
void StopVag() {}
void PreloadVag(const char*) {}
void PreloadVagFileName(const char*) {}
void ResetCommentaryAudio() {}
bool FContinuousSound(SFXID) {return false;}
bool FPauseForVag() {return false;}
bool FPlayingCommentaryAudio() {return false;}
int FVagPlaying() {return 0;}
void NextSneakyFootstep() {}
void InitializeSoundOptions() {}
void SetMasterVolume(float value) {master=std::clamp(value,0.0f,1.0f);}
float GetMasterVolume() {return master;}
void SetMvgkRvol(float,int,int) {}
void SetFmvAudioMode(bool) {}
void SetMusicSequencerGameplayClock(bool) {}
void SetPambFrq(AMB*,float) {}
void SetPambVol(AMB*,float) {}
void StartAloAmbient(float,float,ALO*,AMB** amb) {if(amb)*amb=nullptr;}
void StartMusidSong(MUSID,const char*) {}
void StartSound(SFXID id,AMB** amb,ALO*,glm::vec3*,float,float,float,float,float,LM*,LM*) {
    s_sfxidLast=int(id);if(amb)*amb=nullptr;
}
SoundBankPtr SbpEnsureBank(int,const char*) {return nullptr;}
void StartSwIntermittentSounds(SW*) {}
void UpdateSounds() {}
void SetAMRegister(int reg,byte value) {if(reg>=0 && reg<16)s_amRegisters[reg]=value;}
void RefreshMidiRegister(int) {}
std::string GetAudioOutputDeviceId() {return "none";}
std::string GetAudioOutputDeviceName() {return "Muted prototype";}
float GetUserDialogueVolume() {return dialogue;}
float GetUserMusicVolume() {return music;}
float GetUserSfxVolume() {return sfx;}
void SetUserDialogueVolume(float value) {dialogue=std::clamp(value,0.0f,1.0f);}
void SetUserMusicVolume(float value) {music=std::clamp(value,0.0f,1.0f);}
void SetUserSfxVolume(float value) {sfx=std::clamp(value,0.0f,1.0f);}
bool FUserStereoEnabled() {return stereo;}
void SetUserStereoEnabled(bool value) {stereo=value;}

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

void HandleWipeVolumes(int,float) {}
void PlayCommentaryAudio(char*) {}
void ResumeVag() {}
void SelectAudioOutputDevice(int) {}
void SetAttractSoundOption(bool) {}
void SetAttractVolume(bool) {}
namespace {bool sequencerPaused=false;}
void PauseMusicSequencer() {sequencerPaused=true;}
void ContinueMusicSequencer(bool) {sequencerPaused=false;}
bool FMusicSequencerPaused() {return sequencerPaused;}
bool CaneFmvPcmStart3ds(unsigned, unsigned) {return false;}
unsigned CaneFmvPcmFreeFrames3ds() {return 0;}
void CaneFmvPcmPush3ds(const int16_t*, unsigned) {}
void CaneFmvPcmPump3ds(bool) {}
void CaneFmvPcmStop3ds() {}
void CaneFmvPcmStats3ds(bool* active, unsigned* rate, unsigned* ch, uint64_t* pushedFrames, uint64_t* doneFrames) {
    if (active) *active = false;
    if (rate) *rate = 0;
    if (ch) *ch = 0;
    if (pushedFrames) *pushedFrames = 0;
    if (doneFrames) *doneFrames = 0;
}
