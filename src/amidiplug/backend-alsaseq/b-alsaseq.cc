/*
 *
 * Author: Giacomo Lozito <james@develia.org>, (C) 2005-2006
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 */
#include <alsa/asoundlib.h>

#include <alsa/error.h>
#include <alsa/seq.h>
#include <alsa/seq_event.h>
#include <alsa/seq_midi_event.h>
#include <alsa/seqmid.h>
#include <stdlib.h>
#include <string.h>

#include <libaudcore/audstrings.h>
#include <libaudcore/i18n.h>
#include <libaudcore/index.h>
#include <libaudcore/runtime.h>

#include "../i_backend.h"
#include "../i_configure.h"
#include "../i_midievent.h"

typedef struct
{
	snd_seq_t *seq_handle;
	int out_port;
	snd_seq_event_t event;
	snd_midi_event_t *event_parser;
	int client_port;
	int dest_client;
	int dest_port;
} sequencer_client_t;

#define CHK(err, prefix, fun, params...)	  \
	err = fun(params); \
	if (err) \
		AUDWARN(prefix ": " #fun ": %s\n", snd_strerror(err))

#define HANDLE_EVENT(err, event, length)	  \
	do { \
	if (!sc.seq_handle) \
		return; \
	snd_midi_event_init(sc.event_parser); \
	if ((err = snd_midi_event_encode(sc.event_parser, (event)->d, (length), &sc.event) > 0)) { \
		snd_seq_ev_set_direct(&sc.event); \
		CHK(err, "", snd_seq_event_output_direct, sc.seq_handle, &sc.event); \
		CHK(err, "", snd_seq_drain_output, sc.seq_handle); \
		/*CHK(err, "", snd_seq_sync_output_queue, sc.seq_handle); */ \
	} else \
		AUDWARN("Could not encode midi message: %s\n", snd_strerror(err)); \
	exit(0); \
	} while (0)

/* sequencer instance */
static sequencer_client_t sc;
/* options */

void backend_init ()
{
	int res;
	CHK(res, "Could not open alsa sequencer", snd_seq_open, &sc.seq_handle, "default", SND_SEQ_OPEN_OUTPUT, 0);
	if (res)
	{
		sc.seq_handle = NULL;
		// TODO ERROR
	}
	CHK(res,"Could not initialize alsa midi event parser", snd_midi_event_new, 1024, &sc.event_parser);
	if (res)
	{
		sc.event_parser = NULL;
		// TODO ERROR
	}

	// setup queue
	

	snd_seq_set_client_name(sc.seq_handle, "audacious");
	sc.client_port = snd_seq_create_simple_port(sc.seq_handle,
	                                            "midi_out",
	                                            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
	                                            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

	// TODO make configurable using snd_seq_parse_address()
	sc.dest_client = 36;
	sc.dest_port = 0;
	AUDINFO("Alsa seq device %d\n", snd_seq_client_id(sc.seq_handle));
	CHK(res, "", snd_seq_connect_to, sc.seq_handle, sc.client_port, sc.dest_client, sc.dest_port);
	if (res)
	{
		AUDWARN("Could not connect to alsa seq device %d:%d\n", sc.dest_client, sc.dest_port);
	}
}


void backend_cleanup ()
{
	int res;
	if (!sc.seq_handle)
		return;

	snd_seq_delete_simple_port(sc.seq_handle, sc.client_port);
	
	CHK(res, "Could not close alsa sequencer", snd_seq_close, sc.seq_handle);
	if (sc.event_parser)
		snd_midi_event_free(sc.event_parser);
}


void backend_reset ()
{
	if (!sc.seq_handle)
		return;
}


void seq_event_noteon (midievent_t * event)
{
	int err;
	HANDLE_EVENT(err, event, 3);
}


void seq_event_noteoff (midievent_t * event)
{
	int err;
	HANDLE_EVENT(err, event, 2);
}


void seq_event_keypress (midievent_t * event)
{
    /* KEY PRESSURE events are not handled by FluidSynth sequencer? */
    AUDDBG ("KEYPRESS EVENT with FluidSynth backend (unhandled)\n");
}


void seq_event_controller (midievent_t * event)
{
	int err;
	HANDLE_EVENT(err, event, 3);
}


void seq_event_pgmchange (midievent_t * event)
{
	int err;
	HANDLE_EVENT(err, event, 2);
}


void seq_event_chanpress (midievent_t * event)
{
    /* CHANNEL PRESSURE events are not handled by FluidSynth sequencer? */
    AUDDBG ("CHANPRESS EVENT with FluidSynth backend (unhandled)\n");
}


void seq_event_pitchbend (midievent_t * event)
{
    // int pb_value = (( (event->d[2]) & 0x7f) << 7) | ((event->d[1]) & 0x7f);
    // fluid_synth_pitch_bend (sc.synth,
    //                         event->d[0],
    //                         pb_value);
}


void seq_event_sysex (midievent_t * event)
{
    AUDDBG ("SYSEX EVENT with FluidSynth backend (unhandled)\n");
}


void seq_event_tempo (midievent_t * event)
{
    /* unhandled */
}


void seq_event_other (midievent_t * event)
{
    /* unhandled */
}


void seq_event_allnoteoff (int unused)
{
    // int c = 0;

    // for (c = 0 ; c < 16 ; c++)
    // {
    //     fluid_synth_cc (sc.synth, c, 123 /* all notes off */, 0);
    // }
}


void backend_generate_audio (void * buf, int bufsize)
{
	memset(buf, 0, bufsize);
}


void backend_audio_info (int * channels, int * bitdepth, int * samplerate)
{
    *channels = 2;
    *bitdepth = 16; /* always 16 bit, we use fluid_synth_write_s16() */
    *samplerate = aud_get_int ("amidiplug", "fsyn_synth_samplerate");
}
