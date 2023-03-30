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
#include <stdint.h>
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

/* sequencer instance */
static sequencer_client_t sc;

uint8_t note_map[16][128];

#define CHK(err, prefix, fun, params...)                                       \
    do                                                                         \
    {                                                                          \
        err = fun(params);                                                     \
        if (err < 0)                                                           \
            AUDWARN(prefix ": " #fun ": %s\n", snd_strerror(err));             \
    } while (0)

#define PREPARE_EVENT(err)                                                     \
    do                                                                         \
    {                                                                          \
        if (!sc.seq_handle)                                                    \
            return;                                                            \
        snd_seq_ev_clear(&sc.event);                                           \
        snd_seq_ev_set_source(&sc.event, sc.client_port);                      \
        snd_seq_ev_set_subs(&sc.event);                                        \
        snd_seq_ev_set_direct(&sc.event);                                      \
    } while (0)

#define PRINT_EVENT(ev, length)                                                \
    do                                                                         \
    {                                                                          \
        if (ev)                                                                \
        {                                                                      \
            switch (length)                                                    \
            {                                                                  \
            case 1:                                                            \
                AUDWARN("Audacious Event: %x\n", ev->d[0]);                    \
                break;                                                         \
            case 2:                                                            \
                AUDWARN("Audacious Event: %x, %x\n", ev->d[0], ev->d[1]);      \
                break;                                                         \
            case 3:                                                            \
                AUDWARN("Audacious Event: %x, %x, %x\n", ev->d[0], ev->d[1],   \
                        ev->d[2]);                                             \
                break;                                                         \
            default:                                                           \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        AUDWARN(                                                               \
            "ALSA Event: %x, %x, %x, %x, %x, %x, %x, %x, %x, %x, %x, %x\n",    \
            sc.event.data.raw8.d[0], sc.event.data.raw8.d[1],                  \
            sc.event.data.raw8.d[2], sc.event.data.raw8.d[3],                  \
            sc.event.data.raw8.d[4], sc.event.data.raw8.d[5],                  \
            sc.event.data.raw8.d[6], sc.event.data.raw8.d[7],                  \
            sc.event.data.raw8.d[8], sc.event.data.raw8.d[9],                  \
            sc.event.data.raw8.d[10], sc.event.data.raw8.d[11]);               \
    } while (0)



#define SEND_EVENT(err, ev, length)                                            \
    do                                                                         \
    {                                                                          \
        CHK(err, "", snd_seq_event_output_direct, sc.seq_handle, &sc.event);   \
        if (err < 0)                                                           \
        {                                                                      \
            PRINT_EVENT(ev, length);                                           \
        }                                                                      \
        /* CHK(err, "", snd_seq_drain_output, sc.seq_handle); */               \
    } while (0)


/* options */

void connect_client ()
{
	int res;
	if (!sc.seq_handle)
		return;

    // TODO make configurable using snd_seq_parse_address()
	sc.dest_client = 28;
	sc.dest_port = 0;
	AUDINFO("Alsa seq device %d\n", snd_seq_client_id(sc.seq_handle));
	res = snd_seq_connect_to(sc.seq_handle, sc.client_port, sc.dest_client, sc.dest_port);

    // exclude error code for already connected port
	if (res < 0 && res != -EBUSY)
	{
		AUDWARN("snd_seq_connect_to: %s", snd_strerror(res));
		AUDWARN("Could not connect to alsa seq device %d:%d\n", sc.dest_client, sc.dest_port);
	}
}

void backend_init ()
{
	// clear note map
	memset(note_map, 0, 16 * 128);

	// setup alsa seq interface
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

	snd_seq_set_client_name(sc.seq_handle, "audacious");
	sc.client_port = snd_seq_create_simple_port(sc.seq_handle,
	                                            "midi_out",
	                                            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
	                                            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
	connect_client();
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

	connect_client();

	// clear all remaining notes
	int err;
	midievent_t *dummy = NULL;

	for (int ch=0; ch < 16; ch++) {
		for (int note=0; note < 128; note++) {
			while (note_map[ch][note]) {
				PREPARE_EVENT(err);
				snd_seq_ev_set_noteoff(&sc.event, ch, note, 0);
				SEND_EVENT(err, dummy, 0);
				note_map[ch][note]--;
			}
		}
	}
}


void seq_event_noteon (midievent_t * event)
{
	int err;
	uint8_t ch = event->d[0] & 0xf;
	uint8_t note = event->d[1];

	// Increment note counter before sending.
	// That way if another thread calls backend_reset() it will always clear all
	// active notes. Sending an erraneous note-off, if we get intercepted before
	// actually sending the note is totally fine.
	uint8_t old_count = note_map[ch][note];
	if (__sync_val_compare_and_swap(& note_map[ch][note], old_count, old_count + 1) != old_count)
		return;

	PREPARE_EVENT(err);
	snd_seq_ev_set_noteon(&sc.event, ch, note, event->d[2]);
	SEND_EVENT(err, event, 3);
}


void seq_event_noteoff (midievent_t * event)
{
	int err;
	uint8_t ch = event->d[0] & 0xf;
	uint8_t note = event->d[1];

	PREPARE_EVENT(err);
	snd_seq_ev_set_noteoff(&sc.event, event->d[0] & 0xf, event->d[1], event->d[2]);
	// PRINT_EVENT(event, 3);
	SEND_EVENT(err, event, 2);


    // Update the note-counter only if no other thread has modified (i.e. reset)
	// the value. If we don't decrement an erraneous note-off will be sent,
	// otherwise the note-counter might underflow and the next note-on for the
	// same combination of channel and note would overflow it back to 0 and
	// therefore be missed when clearing all notes.
	uint8_t old_count = note_map[ch][note];
	__sync_val_compare_and_swap(& note_map[ch][note], old_count, old_count - 1);
}


void seq_event_keypress (midievent_t * event)
{
    AUDWARN("Poly Aftertouch not implemented yet\n");
}


void seq_event_controller (midievent_t * event)
{
	int err;
	PREPARE_EVENT(err);
	snd_seq_ev_set_controller(&sc.event, event->d[0] & 0xf, event->d[1], event->d[2]);
	SEND_EVENT(err, event, 3);
}


void seq_event_pgmchange (midievent_t * event)
{
	int err;
	PREPARE_EVENT(err);
	snd_seq_ev_set_pgmchange(&sc.event, event->d[0] & 0xf, event->d[1]);
	SEND_EVENT(err, event, 2);
}


void seq_event_chanpress (midievent_t * event)
{
    /* CHANNEL PRESSURE events are not handled by FluidSynth sequencer? */
    AUDWARN("Mono Aftertouch not implemented yet\n");
}


void seq_event_pitchbend (midievent_t * event)
{
	int err;
	PREPARE_EVENT(err);
	int val = ((int)(event->d[2]) << 7) + (int)event->d[1] - 8192;
	AUDWARN("Raw PB val: %x %x\n", event->d[1], event->d[2]);
	AUDWARN("PB val: %d\n", val);
	snd_seq_ev_set_pitchbend(&sc.event, event->d[0] & 0xf, val);
	PRINT_EVENT(event, 2);
	SEND_EVENT(err, event, 2);
}


void seq_event_sysex (midievent_t * event)
{
    AUDWARN("SYSEX not implemented yet\n");
}


void seq_event_tempo (midievent_t * event)
{
    /* unhandled */
    AUDWARN("Tempo not implemented yet\n");
}


void seq_event_other (midievent_t * event)
{
    /* unhandled */
    AUDWARN("Unspecified MIDI event not implemented yet\n");
}


void seq_event_allnoteoff (int unused)
{
    AUDWARN("AllNoteOff not implemented yet\n");
    // int c = 0;

    // for (c = 0 ; c < 16 ; c++)
    // {
    //     fluid_synth_cc (sc.synth, c, 123 /* all notes off */, 0);
    // }
}


void backend_generate_audio (void * buf, int bufsize)
{
	// write dummy audio
	memset(buf, 0, bufsize);
}


void backend_audio_info (int * channels, int * bitdepth, int * samplerate)
{
    *channels = 2;
    *bitdepth = 16; /* always 16 bit, we use fluid_synth_write_s16() */
    *samplerate = aud_get_int ("amidiplug", "fsyn_synth_samplerate");
}
