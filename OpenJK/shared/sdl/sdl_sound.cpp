/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include <stdlib.h>
#include <stdio.h>
#include <atomic>

#include <SDL3/SDL.h>

#include "qcommon/q_shared.h"
#include "client/client.h"
#include "client/snd_local.h"

extern dma_t		dma;
SDL_AudioStream	*dev = NULL;
qboolean snd_inited = qfalse;

cvar_t *s_sdlBits;
cvar_t *s_sdlSpeed;
cvar_t *s_sdlChannels;
cvar_t *s_sdlDevSamps;
cvar_t *s_sdlMixSamps;

/* The audio callback. All the magic happens here. */
static int dmapos = 0;
static int dmasize = 0;
static std::atomic<int> firstCallbackBytes{0};
static std::atomic<int> firstCallbackQueuedBytes{0};
static std::atomic<int> largestCallbackBytes{0};
static qboolean callbackStatsPrinted = qfalse;

/*
===============
SNDDMA_AudioCallback

SDL3 stream-callback signature: SDL calls us on its audio thread when it
needs more data. We push bytes from the engine's dma.buffer (the mixer
output) into the stream via SDL_PutAudioStreamData(). SDL may request more
than one contiguous portion of the engine ring, so copy in chunks until the
whole request has been supplied.

NB: The SDL3 stream's internal mutex is held while this callback runs (see
SDL_audio.h docs for SDL_LockAudioStream), so SNDDMA_BeginPainting/Submit
locking the stream serialises mixer writes against callback reads -- the same
protection SDL2's SDL_LockAudioDevice gave us.
===============
*/
static void SNDDMA_AudioCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	int expected = 0;
	if (firstCallbackBytes.compare_exchange_strong(expected, additional_amount))
	{
		firstCallbackQueuedBytes.store(total_amount);
	}
	int previousLargest = largestCallbackBytes.load();
	while (additional_amount > previousLargest &&
		!largestCallbackBytes.compare_exchange_weak(previousLargest, additional_amount))
	{
	}

	if (!snd_inited || dma.buffer == NULL || dmasize <= 0)
	{
		return;
	}

	const int bytesPerSample = dma.samplebits / 8;
	int remaining = additional_amount;
	while (remaining > 0)
	{
		int pos = dmapos * bytesPerSample;
		if (pos >= dmasize)
		{
			dmapos = 0;
			pos = 0;
		}
		const int chunk = SDL_min(remaining, dmasize - pos);
		if (chunk <= 0)
		{
			break;
		}
		SDL_PutAudioStreamData(stream, dma.buffer + pos, chunk);
		dmapos += chunk / bytesPerSample;
		if (dmapos >= dma.samples)
		{
			dmapos = 0;
		}
		remaining -= chunk;
	}
}

static struct
{
	SDL_AudioFormat	enumFormat;
	const char	*stringFormat;
} formatToStringTable[ ] =
{
	{ SDL_AUDIO_U8,    "SDL_AUDIO_U8" },
	{ SDL_AUDIO_S8,    "SDL_AUDIO_S8" },
	{ SDL_AUDIO_S16LE, "SDL_AUDIO_S16LE" },
	{ SDL_AUDIO_S16BE, "SDL_AUDIO_S16BE" },
	{ SDL_AUDIO_S32LE, "SDL_AUDIO_S32LE" },
	{ SDL_AUDIO_S32BE, "SDL_AUDIO_S32BE" },
	{ SDL_AUDIO_F32LE, "SDL_AUDIO_F32LE" },
	{ SDL_AUDIO_F32BE, "SDL_AUDIO_F32BE" }
};

static const size_t formatToStringTableSize = ARRAY_LEN( formatToStringTable );

/*
===============
SNDDMA_PrintAudiospec
===============
*/
static void SNDDMA_PrintAudiospec(const char *str, const SDL_AudioSpec *spec)
{
	const char	*fmt = NULL;

	Com_Printf( "%s:\n", str );

	for( size_t i = 0; i < formatToStringTableSize; i++ ) {
		if( spec->format == formatToStringTable[ i ].enumFormat ) {
			fmt = formatToStringTable[ i ].stringFormat;
		}
	}

	if( fmt ) {
		Com_Printf( "  Format:   %s\n", fmt );
	} else {
		Com_Printf( "  Format:   " S_COLOR_RED "UNKNOWN (%d)\n", (int)spec->format);
	}

	Com_Printf( "  Freq:     %d\n", (int) spec->freq );
	Com_Printf( "  Channels: %d\n", (int) spec->channels );
}

static int SNDDMA_ExpandSampleFrequencyKHzToHz(int khz)
{
	switch (khz)
	{
		default:
		case 44: return 44100;
		case 22: return 22050;
		case 11: return 11025;
	}
}

/*
===============
SNDDMA_Init

SDL3 opens an audio device as an SDL_AudioStream via SDL_OpenAudioDeviceStream().
The requested SDL_AudioSpec (format/channels/freq) is honoured directly -- there
is no separate "obtained" spec as in SDL2, so dma.* is derived from what we
asked for. Devices open paused and must be resumed with SDL_ResumeAudioStreamDevice.
===============
*/
qboolean SNDDMA_Init(int sampleFrequencyInKHz)
{
	SDL_AudioSpec desired;
	int bits;
	int devSamples;
	int mixSamples;

	if (snd_inited)
		return qtrue;

	if (!s_sdlBits) {
		s_sdlBits = Cvar_Get("s_sdlBits", "16", CVAR_ARCHIVE_ND);
		s_sdlChannels = Cvar_Get("s_sdlChannels", "2", CVAR_ARCHIVE_ND);
		s_sdlDevSamps = Cvar_Get("s_sdlDevSamps", "0", CVAR_ARCHIVE_ND);
		s_sdlMixSamps = Cvar_Get("s_sdlMixSamps", "0", CVAR_ARCHIVE_ND);
	}

	Com_Printf( "SDL_Init( SDL_INIT_AUDIO )... " );

	if (!SDL_WasInit(SDL_INIT_AUDIO))
	{
		if (!SDL_Init(SDL_INIT_AUDIO))
		{
			Com_Printf( "FAILED (%s)\n", SDL_GetError( ) );
			return qfalse;
		}
	}

	Com_Printf( "OK\n" );

	Com_Printf( "SDL audio driver is \"%s\".\n", SDL_GetCurrentAudioDriver( ) );

	memset(&desired, '\0', sizeof (desired));

	bits = ((int) s_sdlBits->value);
	if ((bits != 16) && (bits != 8))
		bits = 16;

	desired.freq = SNDDMA_ExpandSampleFrequencyKHzToHz(sampleFrequencyInKHz);
	desired.format = ((bits == 16) ? SDL_AUDIO_S16 : SDL_AUDIO_U8);
	desired.channels = (int) s_sdlChannels->value;

	// I dunno if this is the best idea, but I'll give it a try...
	//  should probably check a cvar for this...
	if (s_sdlDevSamps->value)
		devSamples = s_sdlDevSamps->value;
	else
	{
		// just pick a sane default (sample frames per device period).
		// SDL3 manages the device period internally; this is only used to
		// size dma.samples so the mixer's ring buffer stays comfortably large.
		if (desired.freq <= 11025)
			devSamples = 256;
		else if (desired.freq <= 22050)
			devSamples = 512;
		else if (desired.freq <= 44100)
			devSamples = 1024;
		else
			devSamples = 2048;  // (*shrug*)
	}

	char sampleFrames[16];
	SDL_snprintf(sampleFrames, sizeof(sampleFrames), "%d", devSamples);
	if (!SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, sampleFrames))
	{
		Com_Printf("SDL audio: could not set %s=%s\n",
			SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, sampleFrames);
	}

	dev = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, SNDDMA_AudioCallback, NULL);
	if (!dev)
	{
		Com_Printf("SDL_OpenAudioDeviceStream() failed: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return qfalse;
	}

	SNDDMA_PrintAudiospec("SDL_AudioSpec", &desired);

	// dma.samples needs to be big, or id's mixer will just refuse to
	//  work at all; we need to keep it significantly bigger than the
	//  amount of SDL callback samples, and just copy a little each time
	//  the callback runs.
	// 32768 is what the OSS driver filled in here on my system. I don't
	//  know if it's a good value overall, but at least we know it's
	//  reasonable...this is why I let the user override.
	mixSamples = s_sdlMixSamps->value;
	if (!mixSamples)
		mixSamples = (devSamples * desired.channels) * 10;

	if (mixSamples & (mixSamples - 1))  // not a power of two? Seems to confuse something.
	{
		int val = 1;
		while (val < mixSamples)
			val <<= 1;

		mixSamples = val;
	}

	dmapos = 0;
	dma.samplebits = bits;  // SDL3 format is an enum, not bits-encoded: set explicitly (16 or 8).
	dma.channels = desired.channels;
	dma.samples = mixSamples;
	dma.submission_chunk = 1;
	dma.speed = desired.freq;
	dmasize = (dma.samples * (dma.samplebits/8));
	dma.buffer = (byte *)calloc(1, dmasize);
	firstCallbackBytes.store(0);
	firstCallbackQueuedBytes.store(0);
	largestCallbackBytes.store(0);
	callbackStatsPrinted = qfalse;

	Com_Printf("Starting SDL audio callback...\n");
	snd_inited = qtrue;
	if (!SDL_ResumeAudioStreamDevice(dev))
	{
		Com_Printf("SDL_ResumeAudioStreamDevice() failed: %s\n", SDL_GetError());
		snd_inited = qfalse;
		SDL_DestroyAudioStream(dev);
		dev = NULL;
		free(dma.buffer);
		dma.buffer = NULL;
		dmapos = dmasize = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return qfalse;
	}

	Com_Printf("SDL audio initialized.\n");
	return qtrue;
}

/*
===============
SNDDMA_GetDMAPos
===============
*/
int SNDDMA_GetDMAPos(void)
{
	const int firstBytes = firstCallbackBytes.load();
	if (!callbackStatsPrinted && firstBytes > 0)
	{
		Com_Printf("SDL audio callback: first=%d queued=%d largest=%d ring=%d bytes\n",
			firstBytes, firstCallbackQueuedBytes.load(), largestCallbackBytes.load(), dmasize);
		callbackStatsPrinted = qtrue;
	}
	return dmapos;
}

/*
===============
SNDDMA_Shutdown
===============
*/
void SNDDMA_Shutdown(void)
{
	Com_Printf("Closing SDL audio device...\n");
	SDL_PauseAudioStreamDevice(dev);
	SDL_DestroyAudioStream(dev);  // also closes the associated audio device.
	dev = NULL;
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	free(dma.buffer);
	dma.buffer = NULL;
	dmapos = dmasize = 0;
	snd_inited = qfalse;
	Com_Printf("SDL audio device shut down.\n");
}

/*
===============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer.
SDL3: release the stream lock acquired by SNDDMA_BeginPainting so the audio
thread callback may run again and consume the freshly mixed dma.buffer.
===============
*/
void SNDDMA_Submit(void)
{
	SDL_UnlockAudioStream(dev);
}

/*
===============
SNDDMA_BeginPainting
SDL3: lock the stream so the audio-thread callback cannot read dma.buffer
while the mixer is writing it (the stream's mutex is held during callbacks).
===============
*/
void SNDDMA_BeginPainting (void)
{
	SDL_LockAudioStream(dev);
}

#ifdef USE_OPENAL
extern int s_UseOpenAL;
#endif

// (De)activates sound playback
void SNDDMA_Activate( qboolean activate )
{
#ifdef USE_OPENAL
	if ( s_UseOpenAL )
	{
		S_AL_MuteAllSounds( (qboolean)!activate );
	}
#endif

	if ( activate )
	{
		S_ClearSoundBuffer();
	}

	if ( activate )
		SDL_ResumeAudioStreamDevice( dev );
	else
		SDL_PauseAudioStreamDevice( dev );
}
