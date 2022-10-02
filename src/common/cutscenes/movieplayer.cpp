/*
** movieplayer.cpp
**
**---------------------------------------------------------------------------
** Copyright 2020 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "types.h"
#include "screenjob.h"
#include "i_time.h"
#include "v_2ddrawer.h"
#include "animlib.h"
#include "v_draw.h"
#include "s_soundinternal.h"
#include "animtexture.h"
#include "gamestate.h"
#include "SmackerDecoder.h"
#include "playmve.h"
#include <vpx/vpx_decoder.h>
#include <vpx/vp8dx.h>
#include "filesystem.h"
#include "vm.h"
#include "printf.h"
#include <atomic>
#include <cmath>
#include <zmusic.h>
#include "filereadermusicinterface.h"

class MoviePlayer
{
protected:
	enum EMovieFlags
	{
		NOSOUNDCUTOFF = 1,
		FIXEDVIEWPORT = 2,	// Forces fixed 640x480 screen size like for Blood's intros.
		NOMUSICCUTOFF = 4,
	};

	int flags;
public:
	virtual void Start() {}
	virtual bool Frame(uint64_t clock) = 0;
	virtual void Stop() {}
	virtual ~MoviePlayer() = default;
	virtual FTextureID GetTexture() = 0;
};

// A simple filter is used to smooth out jittery timers
static const double AudioAvgFilterCoeff{std::pow(0.01, 1.0/10.0)};
// A threshold is in place to avoid constantly skipping due to imprecise timers.
static constexpr double AudioSyncThreshold{0.03};

//---------------------------------------------------------------------------
//
// 
//
//---------------------------------------------------------------------------

class AnmPlayer : public MoviePlayer
{
	// This doesn't need its own class type
	anim_t anim;
	TArray<uint8_t> buffer;
	int numframes = 0;
	int curframe = 1;
	int frametime = 0;
	int nextframetime = 0;
	AnimTextures animtex;
	const TArray<int> animSnd;
	int frameTicks[3];

public:
	bool isvalid() { return numframes > 0; }

	AnmPlayer(FileReader& fr, TArray<int>& ans, const int *frameticks, int flags_)
		: animSnd(std::move(ans))
	{
		memcpy(frameTicks, frameticks, 3 * sizeof(int));
		flags = flags_;
		buffer = fr.ReadPadded(1);
		fr.Close();

		if (ANIM_LoadAnim(&anim, buffer.Data(), buffer.Size() - 1) < 0)
		{
			return;
		}
		numframes = ANIM_NumFrames(&anim);
		animtex.SetSize(AnimTexture::Paletted, 320, 200);
	}

	//---------------------------------------------------------------------------
	//
	// 
	//
	//---------------------------------------------------------------------------

	bool Frame(uint64_t clock) override
	{
		int currentclock = int(clock * 120 / 1'000'000'000);

		if (currentclock < nextframetime - 1)
		{
			return true;
		}

		animtex.SetFrame(ANIM_GetPalette(&anim), ANIM_DrawFrame(&anim, curframe));
		frametime = currentclock;

		int delay = 20;
		if (curframe == 1) delay = frameTicks[0];
		else if (curframe < numframes - 2) delay = frameTicks[1];
		else delay = frameTicks[2];
		nextframetime += delay;

		bool nostopsound = (flags & NOSOUNDCUTOFF);
		for (unsigned i = 0; i < animSnd.Size(); i+=2)
		{
			if (animSnd[i] == curframe)
			{
				int sound = animSnd[i+1];
				if (sound == -1)
					soundEngine->StopAllChannels();
				else
					soundEngine->StartSound(SOURCE_None, nullptr, nullptr, CHAN_AUTO, nostopsound? CHANF_UI : CHANF_NONE, sound, 1.f, ATTN_NONE);
			}
		}
		if (!nostopsound && curframe == numframes && soundEngine->GetSoundPlayingInfo(SOURCE_None, nullptr, -1)) return true;
		curframe++;
		return curframe < numframes;
	}

	void Stop() override
	{
		bool nostopsound = (flags & NOSOUNDCUTOFF);
		if (!nostopsound) soundEngine->StopAllChannels();
	}


	~AnmPlayer()
	{
		buffer.Reset();
		animtex.Clean();
	}

	FTextureID GetTexture() override
	{
		return animtex.GetFrameID();
	}
};

//---------------------------------------------------------------------------
//
// 
//
//---------------------------------------------------------------------------

class MvePlayer : public MoviePlayer
{
	InterplayDecoder decoder;
	bool failed = false;

public:
	bool isvalid() { return !failed; }

	MvePlayer(FileReader& fr) : decoder(SoundEnabled())
	{
		failed = !decoder.Open(fr);
	}

	//---------------------------------------------------------------------------
	//
	// 
	//
	//---------------------------------------------------------------------------

	bool Frame(uint64_t clock) override
	{
		if (failed) return false;
		bool playon = decoder.RunFrame(clock);
		return playon;
	}

	~MvePlayer()
	{
		decoder.Close();
	}

	FTextureID GetTexture() override
	{
		return decoder.animTex().GetFrameID();
	}
};

//---------------------------------------------------------------------------
//
// 
//
//---------------------------------------------------------------------------

class VpxPlayer : public MoviePlayer
{
	bool failed = false;
	FileReader fr;
	AnimTextures animtex;
	const TArray<int> animSnd;

	ZMusic_MusicStream MusicStream = nullptr;
	SoundStream *AudioStream = nullptr;
	std::atomic<uint64_t> clocktime = 0;
	int64_t audiooffset = 0;
	double ClockDiffAvg = 0;
	int samplerate = 0;
	int framesize = 0;

	unsigned width, height;
	TArray<uint8_t> Pic;
	TArray<uint8_t> readBuf;
	vpx_codec_ctx_t codec{};
	vpx_codec_iter_t iter = nullptr;

	uint32_t convnumer;
	uint32_t convdenom;

	uint64_t nsecsperframe;
	uint64_t nextframetime;

	int decstate = 0;
	int framenum = 0;
	int numframes;
	int lastsoundframe = -1;
public:
	int soundtrack = -1;

	double FilterDelay(double diff)
	{
		ClockDiffAvg = ClockDiffAvg*AudioAvgFilterCoeff + diff;
		diff = ClockDiffAvg*(1.0 - AudioAvgFilterCoeff);
		if(diff < AudioSyncThreshold/2.0 && diff > -AudioSyncThreshold)
			return 0.0;

		return diff;
	}

	bool StreamCallback(SoundStream *stream, void *buff, int len)
	{
		// Calculate the difference in time between the movie clock and the audio position.
		auto pos = stream->GetPlayPosition();
		uint64_t clock = clocktime.load(std::memory_order_acquire);
		double delay = FilterDelay(clock / 1'000'000'000.0 -
			double(int64_t(pos.samplesplayed)+audiooffset) / samplerate +
			pos.latency.count() / 1'000'000'000.0);

		if(delay > 0.0)
		{
			// If diff > 0, skip samples. Don't skip more than a full update at once.
			int skip = std::min(int(delay*samplerate)*framesize, len);
			if(!ZMusic_FillStream(MusicStream, buff, len))
				return false;

			// Offset the measured audio position to account for the skipped samples.
			audiooffset += skip/framesize;

			if(skip == len)
				return ZMusic_FillStream(MusicStream, buff, len);
			memmove(buff, (char*)buff+skip, len-skip);
			if(!ZMusic_FillStream(MusicStream, (char*)buff+len-skip, skip))
				memset((char*)buff+len-skip, 0, skip);

			return true;
		}

		if(delay < 0.0)
		{
			// If diff < 0, duplicate samples. Don't duplicate a full update (we need at
			// least one new sample frame to duplicate).
			int dup = std::min(int(-delay*samplerate)*framesize, len-framesize);
			if(!ZMusic_FillStream(MusicStream, (char*)buff+dup, len-dup))
				return false;

			if(framesize == 1)
				memset(buff, ((char*)buff)[dup], dup);
			else
			{
				for(int i=0;i < dup;++i)
					((char*)buff)[i] = ((char*)buff+dup)[i%framesize];
			}

			// Offset the measured audio position to account for the duplicated samples.
			audiooffset -= dup/framesize;
			return true;
		}

		return ZMusic_FillStream(MusicStream, buff, len);
	}
	static bool StreamCallbackC(SoundStream *stream, void *buff, int len, void *userdata)
	{ return static_cast<VpxPlayer*>(userdata)->StreamCallback(stream, buff, len); }

public:
	bool isvalid() { return !failed; }

	VpxPlayer(FileReader& fr_, TArray<int>& animSnd_, int flags_, int origframedelay, FString& error) : animSnd(std::move(animSnd_))
	{
		fr = std::move(fr_);
		flags = flags_;

		if (!ReadIVFHeader(origframedelay))
		{
			// We should never get here, because any file failing this has been eliminated before this constructor got called.
			error.Format("Failed reading IVF header\n");
			failed = true;
		}

		Pic.Resize(width * height * 4);


		// Todo: Support VP9 as well?
		vpx_codec_dec_cfg_t cfg = { 1, width, height };
		if (vpx_codec_dec_init(&codec, &vpx_codec_vp8_dx_algo, &cfg, 0))
		{
			error.Format("Error initializing VPX codec.\n");
			failed = true;
		}
	}

	//---------------------------------------------------------------------------
	//
	// 
	//
	//---------------------------------------------------------------------------

	bool ReadIVFHeader(int origframedelay)
	{
		// IVF format: http://wiki.multimedia.cx/index.php?title=IVF
		uint32_t magic; fr.Read(&magic, 4); // do not byte swap!
		if (magic != MAKE_ID('D', 'K', 'I', 'F')) return false;
		uint16_t version = fr.ReadUInt16();
		if (version != 0) return false;
		uint16_t length = fr.ReadUInt16();
		if (length != 32) return false;
		fr.Read(&magic, 4);
		if (magic != MAKE_ID('V', 'P', '8', '0')) return false;

		width = fr.ReadUInt16();
		height = fr.ReadUInt16();
		uint32_t fpsdenominator = fr.ReadUInt32();
		uint32_t fpsnumerator = fr.ReadUInt32();
		numframes = fr.ReadUInt32();
		if (numframes == 0) return false;
		fr.Seek(4, FileReader::SeekCur);

		if (fpsdenominator > 1000 || fpsnumerator == 0 || fpsdenominator == 0)
		{
			// default to 30 fps if the header does not provide useful info.
			fpsdenominator = 30;
			fpsnumerator = 1;
		}

		convnumer = 120 * fpsnumerator;
		convdenom = fpsdenominator * origframedelay;

		nsecsperframe = int64_t(fpsnumerator) * 1'000'000'000 / fpsdenominator;
		nextframetime = 0;

		return true;
	}

	//---------------------------------------------------------------------------
	//
	// 
	//
	//---------------------------------------------------------------------------

	bool ReadFrame()
	{
		int corrupted = 0;
		int framesize = fr.ReadInt32();
		fr.Seek(8, FileReader::SeekCur);
		if (framesize == 0) return false;

		readBuf.Resize(framesize);
		if (fr.Read(readBuf.Data(), framesize) != framesize) return false;
		if (vpx_codec_decode(&codec, readBuf.Data(), readBuf.Size(), NULL, 0) != VPX_CODEC_OK) return false;
		if (vpx_codec_control(&codec, VP8D_GET_FRAME_CORRUPTED, &corrupted) != VPX_CODEC_OK) return false;
		return true;
	}

	//---------------------------------------------------------------------------
	//
	// 
	//
	//---------------------------------------------------------------------------

	vpx_image_t *GetFrameData()
	{
		vpx_image_t *img;
		do
		{
			if (decstate == 0)  // first time / begin
			{
				if (!ReadFrame()) return nullptr;
				decstate = 1;
			}

			img = vpx_codec_get_frame(&codec, &iter);
			if (img == nullptr)
			{
				decstate = 0;
				iter = nullptr;
			}
		} while (img == nullptr);

		return img->d_w == width && img->d_h == height? img : nullptr;
	}

	//---------------------------------------------------------------------------
	//
	// 
	//
	//---------------------------------------------------------------------------

	void SetPixel(uint8_t* dest, uint8_t y, uint8_t u, uint8_t v)
	{
		dest[0] = y;
		dest[1] = u;
		dest[2] = v;
	}

	bool CreateNextFrame()
	{
		auto img = GetFrameData();
		if (!img) return false;
		uint8_t const* const yplane = img->planes[VPX_PLANE_Y];
		uint8_t const* const uplane = img->planes[VPX_PLANE_U];
		uint8_t const* const vplane = img->planes[VPX_PLANE_V];

		const int ystride = img->stride[VPX_PLANE_Y];
		const int ustride = img->stride[VPX_PLANE_U];
		const int vstride = img->stride[VPX_PLANE_V];

		for (unsigned int y = 0; y < height; y++)
		{
			for (unsigned int x = 0; x < width; x++)
			{
				uint8_t u = uplane[ustride * (y >> 1) + (x >> 1)];
				uint8_t v = vplane[vstride * (y >> 1) + (x >> 1)];

				SetPixel(&Pic[(x + y * width) << 2], yplane[ystride * y + x], u, v);
			}
		}

		return true;
	}

	//---------------------------------------------------------------------------
	//
	// 
	//
	//---------------------------------------------------------------------------

	void Start() override
	{
		if (soundtrack >= 0 && !AudioStream)
		{
			S_StopMusic(true);
			FileReader reader = fileSystem.OpenFileReader(soundtrack);
			if (reader.isOpen())
			{
				MusicStream = ZMusic_OpenSong(GetMusicReader(reader), MDEV_DEFAULT, nullptr);
			}
			if (MusicStream)
			{
				SoundStreamInfo info{};
				ZMusic_GetStreamInfo(MusicStream, &info);
				// if mBufferSize == 0, the music stream is played externally (e.g.
				// Windows' MIDI synth), which we can't keep synced. Play anyway?
				if (info.mBufferSize > 0 && ZMusic_Start(MusicStream, 0, false))
				{
					int channels = abs(info.mNumChannels);
					samplerate = info.mSampleRate;
					framesize = channels * ((info.mNumChannels < 0) ? sizeof(int16_t) : sizeof(float));
					int bufsize = 40 * info.mSampleRate / 1000 * framesize;
					AudioStream = S_CreateCustomStream(bufsize, info.mSampleRate, channels,
						(info.mNumChannels < 0) ? MusicSamples16bit : MusicSamplesFloat,
						&StreamCallbackC, this);
				}
				if (!AudioStream)
				{
					ZMusic_Close(MusicStream);
					MusicStream = nullptr;
				}
			}
		}
		else if (AudioStream)
		{
			AudioStream->SetPaused(false);
		}
		animtex.SetSize(AnimTexture::YUV, width, height);
	}

	//---------------------------------------------------------------------------
	//
	// 
	//
	//---------------------------------------------------------------------------

	bool Frame(uint64_t clock) override
	{
		clocktime.store(clock, std::memory_order_release);

		bool stop = false;
		if (clock > nextframetime)
		{
			nextframetime += nsecsperframe;

			if (!CreateNextFrame())
			{
				Printf(PRINT_BOLD, "Failed reading next frame\n");
				stop = true;
			}
			else
			{
				animtex.SetFrame(nullptr, Pic.Data());
			}
			framenum++;
			if (framenum >= numframes) stop = true;

			bool nostopsound = (flags & NOSOUNDCUTOFF);
			int soundframe = convdenom ? Scale(framenum, convnumer, convdenom) : framenum;
			if (soundframe > lastsoundframe)
			{
				if (soundtrack == -1)
				{
					for (unsigned i = 0; i < animSnd.Size(); i += 2)
					{
						if (animSnd[i] == soundframe)
						{
							int sound = animSnd[i + 1];
							if (sound == -1)
								soundEngine->StopAllChannels();
							else
								soundEngine->StartSound(SOURCE_None, nullptr, nullptr, CHAN_AUTO, nostopsound ? CHANF_UI : CHANF_NONE, sound, 1.f, ATTN_NONE);
						}
					}
				}
				lastsoundframe = soundframe;
			}
		}
		return !stop;
	}

	void Stop() override
	{
		if (AudioStream)
		{
			AudioStream->SetPaused(true);
		}
		bool nostopsound = (flags & NOSOUNDCUTOFF);
		if (!nostopsound) soundEngine->StopAllChannels();
	}

	~VpxPlayer()
	{
		vpx_codec_destroy(&codec);
		animtex.Clean();
		if(AudioStream)
		{
			S_StopCustomStream(AudioStream);
			ZMusic_Close(MusicStream);
		}
	}

	FTextureID GetTexture() override
	{
		return animtex.GetFrameID();
	}
};

//---------------------------------------------------------------------------
//
//
//
//---------------------------------------------------------------------------

struct AudioData
{
	SmackerAudioInfo inf;

	std::vector<uint16_t> samples;
	int nWrite = 0;
	int nRead = 0;
};

class SmkPlayer : public MoviePlayer
{
	SmackerHandle hSMK{};
	int numAudioTracks;
	AudioData adata;
	uint32_t nWidth, nHeight;
	uint8_t palette[768];
	AnimTextures animtex;
	TArray<uint8_t> pFrame;
	TArray<uint8_t> audioBuffer;
	int nFrames;
	bool fullscreenScale;
	uint64_t nFrameNs;
	int nFrame = 0;
	const TArray<int> animSnd;
	FString filename;
	SoundStream* stream = nullptr;
	bool hassound = false;
	std::atomic<uint64_t> clocktime = 0;
	int64_t audiooffset = 0;
	double ClockDiffAvg = 0;
	int samplerate = 0;
	int framesize = 0;

public:
	bool isvalid() { return hSMK.isValid; }

	double FilterDelay(double diff)
	{
		ClockDiffAvg = ClockDiffAvg*AudioAvgFilterCoeff + diff;
		diff = ClockDiffAvg*(1.0 - AudioAvgFilterCoeff);
		if(diff < AudioSyncThreshold/2.0 && diff > -AudioSyncThreshold)
			return 0.0;

		return diff;
	}

	bool StreamCallback(SoundStream* stream, void* buff, int len)
	{
		auto pos = stream->GetPlayPosition();
		uint64_t clock = clocktime.load(std::memory_order_acquire);
		double delay = FilterDelay(clock / 1'000'000'000.0 -
			double(int64_t(pos.samplesplayed)+audiooffset) / samplerate +
			pos.latency.count() / 1'000'000'000.0);

		int avail = (adata.nWrite - adata.nRead) * 2;

		if(delay > 0.0)
		{
			// If diff > 0, skip samples. Don't skip more than a full update at once.
			int skip = std::min(int(delay*samplerate)*framesize, len);
			while (skip > 0)
			{
				if (avail >= skip)
				{
					audiooffset += skip / framesize;
					adata.nRead += skip / 2;
					if (adata.nRead >= adata.nWrite)
						adata.nRead = adata.nWrite = 0;
					avail -= skip;
					break;
				}

				audiooffset += avail / framesize;
				adata.nWrite = 0;
				adata.nRead = 0;
				skip -= avail;
				avail = 0;

				auto read = Smacker_GetAudioData(hSMK, 0, (int16_t*)audioBuffer.Data());
				if (read == 0) break;

				if (adata.inf.bitsPerSample == 8) copy8bitSamples(read);
				else copy16bitSamples(read);
				avail += read;
			}
		}
		else if(delay < 0.0)
		{
			// If diff < 0, duplicate samples. Don't duplicate more than a full update.
			int dup = std::min(int(-delay*samplerate)*framesize, len-framesize);

			if(avail == 0)
			{
				auto read = Smacker_GetAudioData(hSMK, 0, (int16_t*)audioBuffer.Data());
				if (read == 0) return false;

				if (adata.inf.bitsPerSample == 8) copy8bitSamples(read);
				else copy16bitSamples(read);
				avail += read;
			}

			// Offset the measured audio position to account for the duplicated samples.
			audiooffset -= dup/framesize;

			char *src = (char*)&adata.samples[adata.nRead];
			char *dst = (char*)buff;

			for(int i=0;i < dup;++i)
				*(dst++) = src[i%framesize];

			buff = dst;
			len -= dup;
		}

		int wrote = 0;
		while(wrote < len)
		{
			if (avail == 0)
			{
				auto read = Smacker_GetAudioData(hSMK, 0, (int16_t*)audioBuffer.Data());
				if (read == 0)
				{
					if (wrote == 0)
						return false;
					break;
				}

				if (adata.inf.bitsPerSample == 8) copy8bitSamples(read);
				else copy16bitSamples(read);
				avail += read;
			}

			int todo = std::min(len-wrote, avail);

			memcpy((char*)buff+wrote, &adata.samples[adata.nRead], todo);
			adata.nRead += todo / 2;
			if(adata.nRead == adata.nWrite)
				adata.nRead = adata.nWrite = 0;
			avail -= todo;
			wrote += todo;
		}

		if (wrote < len)
			memset((char*)buff+wrote, 0, len-wrote);
		return true;
	}
	static bool StreamCallbackC(SoundStream* stream, void* buff, int len, void* userdata)
	{ return static_cast<SmkPlayer*>(userdata)->StreamCallback(stream, buff, len); }

	void copy8bitSamples(unsigned count)
	{
		for (unsigned i = 0; i < count;)
		{
			unsigned todo = std::min<unsigned>(count-i, std::size(adata.samples)-adata.nWrite);
			for (unsigned j = 0;j < todo;++j)
				adata.samples[adata.nWrite+j] = (audioBuffer[i+j] - 128) << 8;
			adata.nWrite += todo;
			if (adata.nWrite >= (int)std::size(adata.samples)) adata.nWrite = 0;
			i += todo;
		}
	}

	void copy16bitSamples(unsigned count)
	{
		auto ptr = (uint16_t*)audioBuffer.Data();
		count /= 2;
		for (unsigned i = 0; i < count;)
		{
			unsigned todo = std::min<unsigned>(count-i, std::size(adata.samples)-adata.nWrite);
			memcpy(&adata.samples[adata.nWrite], ptr, todo*2);
			adata.nWrite += todo;
			if (adata.nWrite >= (int)std::size(adata.samples)) adata.nWrite = 0;
			i += todo;
		}
	}


	SmkPlayer(const char *fn, TArray<int>& ans, int flags_) : animSnd(std::move(ans))
	{
		hSMK = Smacker_Open(fn);
		if (!hSMK.isValid)
		{
			return;
		}
		flags = flags_;
		Smacker_GetFrameSize(hSMK, nWidth, nHeight);
		pFrame.Resize(nWidth * nHeight + max(nWidth, nHeight));
		float frameRate = Smacker_GetFrameRate(hSMK);
		nFrameNs = uint64_t(1'000'000'000 / frameRate);
		nFrames = Smacker_GetNumFrames(hSMK);
		Smacker_GetPalette(hSMK, palette);

		numAudioTracks = Smacker_GetNumAudioTracks(hSMK);
		if (numAudioTracks && SoundEnabled())
		{
			adata.nWrite = 0;
			adata.nRead = 0;
			adata.inf = Smacker_GetAudioTrackDetails(hSMK, 0);
			if (adata.inf.idealBufferSize > 0)
			{
				audioBuffer.Resize(adata.inf.idealBufferSize);
				adata.samples.resize(adata.inf.idealBufferSize);
				hassound = true;
			}
		}
		if (!hassound)
		{
			adata.inf = {};
			Smacker_DisableAudioTrack(hSMK, 0);
		}
		for (int i = 1;i < numAudioTracks;++i)
			Smacker_DisableAudioTrack(hSMK, i);
	}

	//---------------------------------------------------------------------------
//
// 
//
//---------------------------------------------------------------------------

	void Start() override
	{
		animtex.SetSize(AnimTexture::Paletted, nWidth, nHeight);
		if (stream) stream->SetPaused(false);
	}

	//---------------------------------------------------------------------------
	//
	//
	//
	//---------------------------------------------------------------------------

	bool Frame(uint64_t clock) override
	{
		clocktime.store(clock, std::memory_order_release);
		int frame = int(clock / nFrameNs);

		twod->ClearScreen();
		if (frame > nFrame)
		{
			nFrame++;
			Smacker_GetNextFrame(hSMK);
			Smacker_GetPalette(hSMK, palette);
			Smacker_GetFrame(hSMK, pFrame.Data());
			animtex.SetFrame(palette, pFrame.Data());

			if (!stream && hassound)
			{
				S_StopMusic(true);

				samplerate = adata.inf.sampleRate;
				framesize = adata.inf.nChannels * 2;

				int bufsize = 40 * samplerate / 1000 * framesize;
				stream = S_CreateCustomStream(bufsize, adata.inf.sampleRate, adata.inf.nChannels, MusicSamples16bit, StreamCallbackC, this);
				if (!stream)
					Smacker_DisableAudioTrack(hSMK, 0);
			}

			bool nostopsound = (flags & NOSOUNDCUTOFF);
			if (!hassound) for (unsigned i = 0; i < animSnd.Size(); i += 2)
			{
				if (animSnd[i] == nFrame)
				{
					int sound = animSnd[i + 1];
					if (sound == -1)
						soundEngine->StopAllChannels();
					else
						soundEngine->StartSound(SOURCE_None, nullptr, nullptr, CHAN_AUTO, nostopsound ? CHANF_UI : CHANF_NONE, sound, 1.f, ATTN_NONE);
				}
			}
		}

		return nFrame < nFrames;
	}

	void Stop() override
	{
		if (stream) stream->SetPaused(true);
		bool nostopsound = (flags & NOSOUNDCUTOFF);
		if (!nostopsound && !hassound) soundEngine->StopAllChannels();
	}

	~SmkPlayer()
	{
		if (stream) S_StopCustomStream(stream);
		Smacker_Close(hSMK);
		animtex.Clean();
	}

	FTextureID GetTexture() override
	{
		return animtex.GetFrameID();
	}

};

//---------------------------------------------------------------------------
//
// 
//
//---------------------------------------------------------------------------

MoviePlayer* OpenMovie(const char* filename, TArray<int>& ans, const int* frameticks, int flags, FString& error)
{
	FileReader fr;
	// first try as .ivf - but only if sounds are provided - the decoder is video only.
	if (ans.Size())
	{
		auto fn = StripExtension(filename);
		DefaultExtension(fn, ".ivf");
		fr = fileSystem.OpenFileReader(fn);
	}

	if (!fr.isOpen()) fr = fileSystem.OpenFileReader(filename);
	if (!fr.isOpen())
	{
		size_t nLen = strlen(filename);
		// Strip the drive letter and retry.
		if (nLen >= 3 && isalpha(filename[0]) && filename[1] == ':' && filename[2] == '/')
		{
			filename += 3;
			fr = fileSystem.OpenFileReader(filename);
		}
		if (!fr.isOpen())
		{
			error.Format("%s: Unable to open video\n", filename);
			return nullptr;
		}
	}
	char id[20] = {};

	fr.Read(&id, 20);
	fr.Seek(-20, FileReader::SeekCur);

	if (!memcmp(id, "LPF ", 4))
	{
		auto anm = new AnmPlayer(fr, ans, frameticks, flags);
		if (!anm->isvalid())
		{
			error.Format("%s: invalid ANM file.\n", filename);
			delete anm;
			return nullptr;
		}
		return anm;
	}
	else if (!memcmp(id, "SMK2", 4))
	{
		fr.Close();
		auto anm = new SmkPlayer(filename, ans, flags);
		if (!anm->isvalid())
		{
			error.Format("%s: invalid SMK file.\n", filename);
			delete anm;
			return nullptr;
		}
		return anm;
	}
	else if (!memcmp(id, "Interplay MVE File", 18))
	{
		auto anm = new MvePlayer(fr);
		if (!anm->isvalid())
		{
			delete anm;
			return nullptr;
		}
		return anm;
	}
	else if (!memcmp(id, "DKIF\0\0 \0VP80", 12))
	{
		auto anm = new VpxPlayer(fr, ans, frameticks ? frameticks[1] : 0, flags, error);
		if (!anm->isvalid())
		{
			delete anm;
			return nullptr;
		}
		// VPX files have no sound track, so look for a same-named sound file with a known extension as the soundtrack to be played.
		static const char* knownSoundExts[] = { "OGG",	"FLAC",	"MP3",	"OPUS", "WAV" };
		FString name = StripExtension(filename);
		anm->soundtrack = fileSystem.FindFileWithExtensions(name, knownSoundExts, countof(knownSoundExts));
		return anm;
	}
	// add more formats here.
	else
	{
		error.Format("%s: Unknown video format\n", filename);
		return nullptr;
	}
}

//---------------------------------------------------------------------------
//
// 
//
//---------------------------------------------------------------------------

DEFINE_ACTION_FUNCTION(_MoviePlayer, Create)
{
	PARAM_PROLOGUE;
	PARAM_STRING(filename);
	PARAM_POINTER(sndinf, TArray<int>);
	PARAM_INT(flags);
	PARAM_INT(frametime);
	PARAM_INT(firstframetime);
	PARAM_INT(lastframetime);

	FString error;
	if (firstframetime == -1) firstframetime = frametime;
	if (lastframetime == -1) lastframetime = frametime;
	int frametimes[] = { firstframetime, frametime, lastframetime };
	auto movie = OpenMovie(filename, *sndinf, frametime == -1? nullptr : frametimes, flags, error);
	if (!movie)
	{
		Printf(TEXTCOLOR_YELLOW "%s", error.GetChars());
	}
	ACTION_RETURN_POINTER(movie);
}

DEFINE_ACTION_FUNCTION(_MoviePlayer, Start)
{
	PARAM_SELF_STRUCT_PROLOGUE(MoviePlayer);
	I_FreezeTime(true);
	self->Start();
	I_FreezeTime(false);
	return 0;
}

DEFINE_ACTION_FUNCTION(_MoviePlayer, Frame)
{
	PARAM_SELF_STRUCT_PROLOGUE(MoviePlayer);
	PARAM_FLOAT(clock);
	ACTION_RETURN_INT(self->Frame(int64_t(clock)));
}

DEFINE_ACTION_FUNCTION(_MoviePlayer, Destroy)
{
	PARAM_SELF_STRUCT_PROLOGUE(MoviePlayer);
	self->Stop();
	delete self;
	return 0;
}

DEFINE_ACTION_FUNCTION(_MoviePlayer, GetTexture)
{
	PARAM_SELF_STRUCT_PROLOGUE(MoviePlayer);
	ACTION_RETURN_INT(self->GetTexture().GetIndex());
}
