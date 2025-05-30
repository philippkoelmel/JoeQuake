/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

/*
Note: This is basically the QuakeSpasm implementation for sound with SDL from
their file snd_sdl.c adapted to the JoeQuake code. Luckily barely anything had
to be changed and seems to work quite well.
*/

#include <SDL.h>
#include "quakedef.h"


static int	buffersize;

void S_BlockSound(void)
{
	// Do we need to do something here?
}

static void SDLCALL paint_audio (void *unused, Uint8 *stream, int len)
{
	int	pos, tobufend;
	int	len1, len2;

	if (!shm)
	{	/* shouldn't happen, but just in case */
		memset(stream, 0, len);
		return;
	}

	pos = (shm->samplepos * (shm->samplebits / 8));
	if (pos >= buffersize)
		shm->samplepos = pos = 0;

	tobufend = buffersize - pos;  /* bytes to buffer's end. */
	len1 = len;
	len2 = 0;

	if (len1 > tobufend)
	{
		len1 = tobufend;
		len2 = len - len1;
	}

	memcpy(stream, shm->buffer + pos, len1);

	if (len2 <= 0)
	{
		shm->samplepos += (len1 / (shm->samplebits / 8));
	}
	else
	{	/* wraparound? */
		memcpy(stream + len1, shm->buffer, len2);
		shm->samplepos = (len2 / (shm->samplebits / 8));
	}

	if (shm->samplepos >= buffersize)
		shm->samplepos = 0;
}

qboolean SNDDMA_Init (void)
{
	SDL_AudioSpec desired = { 0 };
	int		tmp, val, i, format_bits = 0;
	char	drivername[128];
	char	*s;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
	{
		Con_Printf("Couldn't init SDL audio: %s\n", SDL_GetError());
		return false;
	}

// set sample bits & speed
	if ((s = getenv("QUAKE_SOUND_SAMPLEBITS")))
		format_bits = Q_atoi(s);
	else if ((i = COM_CheckParm("-sndbits")) && i + 1 < com_argc)
		format_bits = Q_atoi(com_argv[i+1]);

	if (format_bits == 8)
	{
		desired.format = AUDIO_U8;
	}
	else
	{
		desired.format = AUDIO_S16SYS;
	}

	if ((s = getenv("QUAKE_SOUND_SPEED")))
		desired.freq = Q_atoi(s);
	else if ((i = COM_CheckParm("-sndspeed")) && i + 1 < com_argc)
		desired.freq = Q_atoi(com_argv[i+1]);
	else if (s_khz.value == 44)
		desired.freq = 44100;
	else if (s_khz.value == 22)
		desired.freq = 22050;
	else
	{
		desired.freq = 11025;
	}

	if ((s = getenv("QUAKE_SOUND_CHANNELS")))
		desired.channels = Q_atoi(s);
	else if ((i = COM_CheckParm("-sndmono")))
		desired.channels = 1;
	else if ((i = COM_CheckParm("-sndstereo")))
		desired.channels = 2;
	else
		desired.channels = 2;

	if (desired.freq <= 11025)
		desired.samples = 256;
	else if (desired.freq <= 22050)
		desired.samples = 512;
	else if (desired.freq <= 44100)
		desired.samples = 1024;
	else if (desired.freq <= 56000)
		desired.samples = 2048; /* for 48 kHz */
	else
		desired.samples = 4096; /* for 96 kHz */
	desired.callback = paint_audio;
	desired.userdata = NULL;

	/* Open the audio device */
	if (SDL_OpenAudio(&desired, NULL) == -1)
	{
		Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return false;
	}

	shm = &sn;
	shm->splitbuffer = 0;

	/* Fill the audio DMA information block */
	/* Since we passed NULL as the 'obtained' spec to SDL_OpenAudio(),
	 * SDL will convert to hardware format for us if needed, hence we
	 * directly use the desired values here. */
	shm->samplebits = (desired.format & 0xFF); /* first byte of format is bits */
	printf("desired.format: %i\n", desired.format);
	shm->speed = desired.freq;
	shm->channels = desired.channels;
	tmp = (desired.samples * desired.channels) * 10;
	if (tmp & (tmp - 1))
	{	/* make it a power of two */
		val = 1;
		while (val < tmp)
			val <<= 1;

		tmp = val;
	}
	shm->samples = tmp;
	shm->samplepos = 0;
	shm->submission_chunk = 1;

	Con_Printf ("SDL audio spec  : %d Hz, %d samples, %d channels\n",
			desired.freq, desired.samples, desired.channels);
	{
		const char *driver = SDL_GetCurrentAudioDriver();
		SDL_GetNumAudioDevices(SDL_FALSE);
		const char *device = SDL_GetAudioDeviceName(0, SDL_FALSE);
		snprintf(drivername, sizeof(drivername), "%s - %s",
			driver != NULL ? driver : "(UNKNOWN)",
			device != NULL ? device : "(UNKNOWN)");
	}

	buffersize = shm->samples * (shm->samplebits / 8);
	Con_Printf ("SDL audio driver: %s, %d bytes buffer\n", drivername, buffersize);

	shm->buffer = (unsigned char *) calloc (1, buffersize);
	if (!shm->buffer)
	{
		SDL_CloseAudio();
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		shm = NULL;
		Con_Printf ("Failed allocating memory for SDL audio\n");
		return false;
	}

	SDL_PauseAudio(0);

	return true;
}

int SNDDMA_GetDMAPos (void)
{
	return shm->samplepos;
}

void SNDDMA_Shutdown (void)
{
	if (shm)
	{
		Con_Printf ("Shutting down SDL sound\n");
		SDL_CloseAudio();
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		if (shm->buffer)
			free (shm->buffer);
		shm->buffer = NULL;
		shm = NULL;
	}
}

/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void SNDDMA_Submit (void)
{
}
