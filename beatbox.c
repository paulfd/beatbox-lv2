/*
  Beatbox LV2 plugin

  Copyright 2019, Paul Ferrand <paul@ferrand.cc>

  This file was based on skeleton and example code from the LV2 plugin 
  distribution available at http://lv2plug.in/

  The LV2 sample plugins have the following copyright and notice, which are 
  extended to the current work:
  Copyright 2011-2016 David Robillard <d@drobilla.net>
  Copyright 2011 Gabriel M. Beddingfield <gabriel@teuton.org>
  Copyright 2011 James Morris <jwm.art.net@gmail.com>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/patch/patch.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/log/logger.h>
#include <lv2/log/log.h>

#include <math.h>
#include <sfizz.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SFZ_FILE ""
#define BEATBOX_URI "http://sfztools.github.io/beatbox"
#define BEATBOX__beatDescription "http://sfztools.github.io/beatbox:beatdescription"
#define BEATBOX__status "http://sfztools.github.io/beatbox:status"
#define MAIN_SWITCH_ON "Switch on!"
#define MAIN_SWITCH_OFF "Switch off!"
#define CHANNEL_MASK 0x0F
#define MIDI_CHANNEL(byte) (byte & CHANNEL_MASK)
#define MIDI_STATUS(byte) (byte & ~CHANNEL_MASK)
#define MAX_BLOCK_SIZE 8192
#define MAX_PATH_SIZE 1024
// #define MAX_VOICES 256
#define DEFAULT_OUTPUT_CHANNEL 10
#define UNUSED(x) (void)(x)

typedef struct
{
    // Features
    LV2_URID_Map *map;
    LV2_URID_Unmap *unmap;
    LV2_Worker_Schedule *worker;
    LV2_Log_Log *log;

    // Ports
    const LV2_Atom_Sequence *input_p;
    LV2_Atom_Sequence *output_p;
    const float *output_channel_p;
    const float *main_p;
    const float *accent_p;

    // Atom forge
    LV2_Atom_Forge forge;              ///< Forge for writing atoms in run thread
    LV2_Atom_Forge_Frame notify_frame; ///< Cached for worker replies

    // Logger
    LV2_Log_Logger logger;

    // URIs
    LV2_URID midi_event_uri;
    LV2_URID options_interface_uri;
    LV2_URID max_block_length_uri;
    LV2_URID nominal_block_length_uri;
    LV2_URID sample_rate_uri;
    LV2_URID atom_object_uri;
    LV2_URID atom_float_uri;
    LV2_URID atom_int_uri;
    LV2_URID atom_urid_uri;
    LV2_URID atom_string_uri;
    LV2_URID atom_path_uri;
    LV2_URID patch_set_uri;
    LV2_URID patch_get_uri;
    LV2_URID patch_put_uri;
    LV2_URID patch_property_uri;
    LV2_URID patch_value_uri;
    LV2_URID patch_body_uri;
    LV2_URID state_changed_uri;
    LV2_URID bb_beat_description_uri;
    LV2_URID bb_status_uri;

    // Sfizz related data
    // sfizz_synth_t *synth;
    bool expect_nominal_block_length;
    char beat_file_path[MAX_PATH_SIZE];
    // int num_voices;
    // bool changing_voices;
    int max_block_size;
    unsigned int output_channel;
    bool main_switched;
    bool accent_switched;
    float sample_rate;
    unsigned int main_switched_count;
} beatbox_plugin_t;

enum
{
    INPUT_PORT = 0,
    OUTPUT_PORT,
    OUTPUT_CHANNEL_PORT,
    MAIN_SWITCH_PORT,
    ACCENT_SWITCH_PORT,
};

static void
sfizz_lv2_map_required_uris(beatbox_plugin_t *self)
{
    LV2_URID_Map *map = self->map;
    self->midi_event_uri = map->map(map->handle, LV2_MIDI__MidiEvent);
    self->max_block_length_uri = map->map(map->handle, LV2_BUF_SIZE__maxBlockLength);
    self->nominal_block_length_uri = map->map(map->handle, LV2_BUF_SIZE__nominalBlockLength);
    self->sample_rate_uri = map->map(map->handle, LV2_PARAMETERS__sampleRate);
    self->atom_float_uri = map->map(map->handle, LV2_ATOM__Float);
    self->atom_int_uri = map->map(map->handle, LV2_ATOM__Int);
    self->atom_path_uri = map->map(map->handle, LV2_ATOM__Path);
    self->atom_string_uri = map->map(map->handle, LV2_ATOM__String);
    self->atom_urid_uri = map->map(map->handle, LV2_ATOM__URID);
    self->atom_object_uri = map->map(map->handle, LV2_ATOM__Object);
    self->patch_set_uri = map->map(map->handle, LV2_PATCH__Set);
    self->patch_get_uri = map->map(map->handle, LV2_PATCH__Get);
    self->patch_put_uri = map->map(map->handle, LV2_PATCH__Put);
    self->patch_body_uri = map->map(map->handle, LV2_PATCH__body);
    self->patch_property_uri = map->map(map->handle, LV2_PATCH__property);
    self->patch_value_uri = map->map(map->handle, LV2_PATCH__value);
    self->state_changed_uri = map->map(map->handle, LV2_STATE__StateChanged);
    self->bb_beat_description_uri = map->map(map->handle, BEATBOX__beatDescription);
    self->bb_status_uri = map->map(map->handle, BEATBOX__status);
}

static void
connect_port(LV2_Handle instance,
             uint32_t port,
             void *data)
{
    beatbox_plugin_t *self = (beatbox_plugin_t *)instance;
    // lv2_log_note(&self->logger, "[connect_port] Called for index %d on address %p\n", port, data);
    switch (port)
    {
    case INPUT_PORT:
        self->input_p = (const LV2_Atom_Sequence *)data;
        break;
    case OUTPUT_PORT:
        self->output_p = (LV2_Atom_Sequence *)data;
        break;
    case OUTPUT_CHANNEL_PORT:
        self->output_channel_p = (const float *)data;
        break;
    case MAIN_SWITCH_PORT:
        self->main_p = (const float *)data;
        break;
    case ACCENT_SWITCH_PORT:
        self->accent_p = (const float *)data;
        break;
    default:
        break;
    }
}

static LV2_Handle
instantiate(const LV2_Descriptor *descriptor,
            double rate,
            const char *path,
            const LV2_Feature *const *features)
{
    UNUSED(descriptor);
    UNUSED(path);
    LV2_Options_Option *options;
    bool supports_bounded_block_size = false;
    bool options_has_block_size = false;
    bool supports_fixed_block_size = false;

    // Allocate and initialise instance structure.
    beatbox_plugin_t *self = (beatbox_plugin_t *)calloc(1, sizeof(beatbox_plugin_t));
    if (!self)
        return NULL;

    // Set defaults
    self->max_block_size = MAX_BLOCK_SIZE;
    self->sample_rate = (float)rate;
    self->expect_nominal_block_length = false;
    self->beat_file_path[0] = '\0';
    self->output_channel = DEFAULT_OUTPUT_CHANNEL;
    self->main_switched = false;
    self->accent_switched = false;
    self->main_switched_count = 0;

    // Get the features from the host and populate the structure
    for (const LV2_Feature *const *f = features; *f; f++)
    {
        // lv2_log_note(&self->logger, "Feature URI: %s\n", (**f).URI);

        if (!strcmp((**f).URI, LV2_URID__map))
            self->map = (**f).data;

        if (!strcmp((**f).URI, LV2_URID__unmap))
            self->unmap = (**f).data;

        if (!strcmp((**f).URI, LV2_BUF_SIZE__boundedBlockLength))
            supports_bounded_block_size = true;

        if (!strcmp((**f).URI, LV2_BUF_SIZE__fixedBlockLength))
            supports_fixed_block_size = true;

        if (!strcmp((**f).URI, LV2_OPTIONS__options))
            options = (**f).data;

        if (!strcmp((**f).URI, LV2_WORKER__schedule))
            self->worker = (**f).data;

        if (!strcmp((**f).URI, LV2_LOG__log))
            self->log = (**f).data;
    }

    // Setup the loggers
    lv2_log_logger_init(&self->logger, self->map, self->log);

    // The map feature is required
    if (!self->map)
    {
        lv2_log_error(&self->logger, "Map feature not found, aborting...\n");
        free(self);
        return NULL;
    }

    // The worker feature is required
    if (!self->worker)
    {
        lv2_log_error(&self->logger, "Worker feature not found, aborting...\n");
        free(self);
        return NULL;
    }

    // Map the URIs we will need
    sfizz_lv2_map_required_uris(self);

    // Initialize the forge
    lv2_atom_forge_init(&self->forge, self->map);

    // Check the options for the block size and sample rate parameters
    if (options)
    {
        for (const LV2_Options_Option *opt = options; opt->value; ++opt)
        {
            if (opt->key == self->sample_rate_uri)
            {
                if (opt->type != self->atom_float_uri)
                {
                    lv2_log_warning(&self->logger, "Got a sample rate but the type was wrong\n");
                    continue;
                }
                self->sample_rate = *(float *)opt->value;
            }
            else if (!self->expect_nominal_block_length && opt->key == self->max_block_length_uri)
            {
                if (opt->type != self->atom_int_uri)
                {
                    lv2_log_warning(&self->logger, "Got a max block size but the type was wrong\n");
                    continue;
                }
                self->max_block_size = *(int *)opt->value;
                options_has_block_size = true;
            }
            else if (opt->key == self->nominal_block_length_uri)
            {
                if (opt->type != self->atom_int_uri)
                {
                    lv2_log_warning(&self->logger, "Got a nominal block size but the type was wrong\n");
                    continue;
                }
                self->max_block_size = *(int *)opt->value;
                self->expect_nominal_block_length = true;
                options_has_block_size = true;
            }
        }
    }
    else
    {
        lv2_log_warning(&self->logger,
                        "No option array was given upon instantiation; will use default values\n.");
    }

    // We need _some_ information on the block size
    if (!supports_bounded_block_size && !supports_fixed_block_size && !options_has_block_size)
    {
        lv2_log_error(&self->logger,
                      "Bounded block size not supported and options gave no block size, aborting...\n");
        free(self);
        return NULL;
    }

    return (LV2_Handle)self;
}

static void
cleanup(LV2_Handle instance)
{
    beatbox_plugin_t *self = (beatbox_plugin_t *)instance;
    free(self);
}

static void
activate(LV2_Handle instance)
{
    beatbox_plugin_t *self = (beatbox_plugin_t *)instance;
    // self->synth = sfizz_create_synth();
    // sfizz_set_samples_per_block(self->synth, self->max_block_size);
    // sfizz_set_sample_rate(self->synth, self->sample_rate);
    if (self->beat_file_path && strlen(self->beat_file_path) > 0)
    {
        lv2_log_note(&self->logger, "Current file is: %s\n", self->beat_file_path);
    }
}

static void
deactivate(LV2_Handle instance)
{
    UNUSED(instance);
    // beatbox_plugin_t *self = (beatbox_plugin_t *)instance;
    // sfizz_free(self->synth);
}

static void
sfizz_lv2_handle_atom_object(beatbox_plugin_t *self, const LV2_Atom_Object *obj)
{
    const LV2_Atom *property = NULL;
    lv2_atom_object_get(obj, self->patch_property_uri, &property, 0);
    if (!property)
    {
        lv2_log_error(&self->logger,
                      "[handle_object] Could not get the property from the patch object, aborting.\n");
        return;
    }

    if (property->type != self->atom_urid_uri)
    {
        lv2_log_error(&self->logger,
                      "[handle_object] Atom type was not a URID, aborting.\n");
        return;
    }

    const uint32_t key = ((const LV2_Atom_URID *)property)->body;
    const LV2_Atom *atom = NULL;
    lv2_atom_object_get(obj, self->patch_value_uri, &atom, 0);
    if (!atom)
    {
        lv2_log_error(&self->logger, "[handle_object] Error retrieving the atom, aborting.\n");
        if (self->unmap)
            lv2_log_warning(&self->logger,
                            "Atom URI: %s\n",
                            self->unmap->unmap(self->unmap->handle, key));
        return;
    }

    if (key == self->bb_beat_description_uri)
    {

        const uint32_t original_atom_size = lv2_atom_total_size((const LV2_Atom *)atom);
        const uint32_t null_terminated_atom_size = original_atom_size + 1;
        char atom_buffer[null_terminated_atom_size];
        memcpy(&atom_buffer, atom, original_atom_size);
        atom_buffer[original_atom_size] = 0; // Null terminate the string for safety
        LV2_Atom *sfz_file_path = (LV2_Atom *)&atom_buffer;
        sfz_file_path->type = self->bb_beat_description_uri;

        // If the parameter is different from the current one we send it through
        if (strcmp(self->beat_file_path, LV2_ATOM_BODY_CONST(sfz_file_path)))
            self->worker->schedule_work(self->worker->handle, null_terminated_atom_size, sfz_file_path);
        lv2_log_note(&self->logger, "[handle_object] Received a description file: %s\n", (char *)LV2_ATOM_BODY_CONST(sfz_file_path));
    }
    else
    {
        lv2_log_warning(&self->logger, "[handle_object] Unknown or unsupported object.\n");
        if (self->unmap)
            lv2_log_warning(&self->logger,
                            "Object URI: %s\n",
                            self->unmap->unmap(self->unmap->handle, key));
        return;
    }

    // Pfiou, we got a valid parameter, so send it to the worker
}

static void
sfizz_lv2_process_midi_event(beatbox_plugin_t *self, const LV2_Atom_Event *ev)
{
    const uint8_t *const msg = (const uint8_t *)(ev + 1);
    switch (lv2_midi_message_type(msg))
    {
    case LV2_MIDI_MSG_NOTE_ON:
        lv2_log_note(&self->logger,
                     "[process_midi] Received note on %d/%d at time %ld\n", msg[0], msg[1], ev->time.frames);
        // sfizz_send_note_on(self->synth,
        //                    (int)ev->time.frames,
        //                    (int)MIDI_CHANNEL(msg[0]) + 1,
        //                    (int)msg[1],
        //                    msg[2]);
        break;
    case LV2_MIDI_MSG_NOTE_OFF:
        lv2_log_note(&self->logger,
                     "[process_midi] Received note off %d/%d at time %ld\n", msg[0], msg[1], ev->time.frames);
        // sfizz_send_note_off(self->synth,
        //                     (int)ev->time.frames,
        //                     (int)MIDI_CHANNEL(msg[0]) + 1,
        //                     (int)msg[1],
        //                     msg[2]);
        break;
    case LV2_MIDI_MSG_CONTROLLER:
        lv2_log_note(&self->logger,
                     "[process_midi] Received CC %d/%d at time %ld\n", msg[0], msg[1], ev->time.frames);
        // sfizz_send_cc(self->synth,
        //               (int)ev->time.frames,
        //               (int)MIDI_CHANNEL(msg[0]) + 1,
        //               (int)msg[1],
        //               msg[2]);
        break;
    default:
        break;
    }
}

static void sfizz_lv2_send_file_path(beatbox_plugin_t *self)
{
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&self->forge, 0);
    lv2_atom_forge_object(&self->forge, &frame, 0, self->patch_set_uri);
    lv2_atom_forge_key(&self->forge, self->patch_property_uri);
    lv2_atom_forge_urid(&self->forge, self->bb_beat_description_uri);
    lv2_atom_forge_key(&self->forge, self->patch_value_uri);
    lv2_atom_forge_path(&self->forge, self->beat_file_path, strlen(self->beat_file_path));
    lv2_atom_forge_pop(&self->forge, &frame);
}

static void sfizz_lv2_send_status(beatbox_plugin_t *self)
{
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&self->forge, 0);
    lv2_atom_forge_object(&self->forge, &frame, 0, self->patch_set_uri);
    lv2_atom_forge_key(&self->forge, self->patch_property_uri);
    lv2_atom_forge_urid(&self->forge, self->bb_status_uri);
    lv2_atom_forge_key(&self->forge, self->patch_value_uri);
    if (self->main_switched)
        lv2_atom_forge_string(&self->forge, MAIN_SWITCH_ON, strlen(MAIN_SWITCH_ON));
    else
        lv2_atom_forge_string(&self->forge, MAIN_SWITCH_OFF, strlen(MAIN_SWITCH_OFF));

    lv2_atom_forge_pop(&self->forge, &frame);
}

// static void sfizz_lv2_send_all(beatbox_plugin_t *self)
// {
//     static const char *status_line = "Here is my status!";
//     LV2_Atom_Forge_Frame frame;
//     lv2_atom_forge_frame_time(&self->forge, 0);
//     lv2_atom_forge_object(&self->forge, &frame, 0, self->patch_set_uri);
//     lv2_atom_forge_key(&self->forge, self->patch_property_uri);
//     lv2_atom_forge_urid(&self->forge, self->bb_beat_description_uri);
//     lv2_atom_forge_key(&self->forge, self->patch_value_uri);
//     lv2_atom_forge_path(&self->forge, self->beat_file_path, strlen(self->beat_file_path));
//     lv2_atom_forge_key(&self->forge, self->patch_property_uri);
//     lv2_atom_forge_urid(&self->forge, self->bb_status_uri);
//     lv2_atom_forge_key(&self->forge, self->patch_value_uri);
//     lv2_atom_forge_string(&self->forge, status_line, strlen(status_line));
//     lv2_atom_forge_pop(&self->forge, &frame);
// }

static void
run(LV2_Handle instance, uint32_t sample_count)
{
    UNUSED(sample_count);
    beatbox_plugin_t *self = (beatbox_plugin_t *)instance;
    if (!self->input_p || !self->output_p)
        return;

    // Set up forge to write directly to notify output port.
    const size_t notify_capacity = self->output_p->atom.size;
    lv2_atom_forge_set_buffer(&self->forge, (uint8_t *)self->output_p, notify_capacity);

    // Start a sequence in the notify output port.
    lv2_atom_forge_sequence_head(&self->forge, &self->notify_frame, 0);

    LV2_ATOM_SEQUENCE_FOREACH(self->input_p, ev)
    {
        // If the received atom is an object/patch message
        if (ev->body.type == self->atom_object_uri)
        {
            const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;
            if (obj->body.otype == self->patch_set_uri)
            {
                sfizz_lv2_handle_atom_object(self, obj);
                lv2_log_warning(&self->logger, "Got an Patch SET.\n");
            }
            else if (obj->body.otype == self->patch_get_uri)
            {
                const LV2_Atom_URID *property = NULL;
                lv2_atom_object_get(obj, self->patch_property_uri, &property, 0);
                lv2_log_warning(&self->logger, "Got an Patch GET.\n");
                if (!property) // Send the full state
                {
                    lv2_log_warning(&self->logger, "Got an Patch GET with no body.\n");
                    sfizz_lv2_send_file_path(self);
                    sfizz_lv2_send_status(self);
                }
                else if (property->body == self->bb_beat_description_uri)
                {
                    lv2_log_warning(&self->logger, "Got an Patch GET for the beat description.\n");
                    sfizz_lv2_send_file_path(self);
                }
                else if (property->body == self->bb_status_uri)
                {
                    lv2_log_warning(&self->logger, "Got an Patch GET for the status.\n");
                    sfizz_lv2_send_status(self);
                }
            }
            else
            {
                lv2_log_warning(&self->logger, "Got an Object atom but it was not supported.\n");
                if (self->unmap)
                    lv2_log_warning(&self->logger,
                                    "Object URI: %s\n",
                                    self->unmap->unmap(self->unmap->handle, obj->body.otype));
                continue;
            }
            // Got an atom that is a MIDI event
        }
        else if (ev->body.type == self->midi_event_uri)
        {
            sfizz_lv2_process_midi_event(self, ev);
        }
    }

    unsigned int output_channel = (unsigned int)*self->output_channel_p;
    if (output_channel != self->output_channel)
    {
        lv2_log_note(&self->logger, "[run] Changed output channel to %d\n", output_channel);
        self->output_channel = output_channel;
    }
    // if ((bool)*self->main_p)
    // {
    //     self->main_switched = true;
    //     sfizz_lv2_send_status(self);
    // }
    // else
    // {
    //     self->main_switched = false;
    //     sfizz_lv2_send_status(self);
    // }

    const float *const main_sentinel = self->main_p + sample_count;
    for (const float *main = self->main_p; main < main_sentinel; main++)
    {
        if ((bool)*main)
        {
            if (!self->main_switched)
            {
                self->main_switched = true;
                lv2_log_note(&self->logger, "[run] Main switch on %ld (float value %f)\n", main - self->main_p, *main);
                sfizz_lv2_send_status(self);
            }
        }
        else
        {
            if (self->main_switched)
            {
                self->main_switched = false;
                lv2_log_note(&self->logger, "[run] Main switch off %ld (float value %f)\n", main - self->main_p, *main);
                sfizz_lv2_send_status(self);
            }
        }
    }

    // const float *const accent_sentinel = self->accent_p + sample_count;
    // for (const float *accent = self->accent_p; accent < accent_sentinel; accent++)
    // {
    //     if ((bool)*accent)
    //     {
    //         if (self->accent_switched)
    //         {
    //             self->accent_switched = false;
    //             lv2_log_note(&self->logger, "[run] accent switch off %ld (float value %f)\n", accent - self->accent_p, *accent);
    //         }
    //     }
    //     else
    //     {
    //         if (!self->accent_switched)
    //         {
    //             self->accent_switched = true;
    //             lv2_log_note(&self->logger, "[run] accent switch on %ld (float value %f)\n", accent - self->accent_p, *accent);
    //         }
    //     }
    // }
}

static uint32_t
lv2_get_options(LV2_Handle instance, LV2_Options_Option *options)
{
    UNUSED(instance);
    UNUSED(options);
    // We have no options
    return LV2_OPTIONS_ERR_UNKNOWN;
}

static uint32_t
lv2_set_options(LV2_Handle instance, const LV2_Options_Option *options)
{
    beatbox_plugin_t *self = (beatbox_plugin_t *)instance;

    // Update the block size and sample rate as needed
    for (const LV2_Options_Option *opt = options; opt->value; ++opt)
    {
        if (opt->key == self->sample_rate_uri)
        {
            if (opt->type != self->atom_float_uri)
            {
                lv2_log_warning(&self->logger, "Got a sample rate but the type was wrong\n");
                continue;
            }
            self->sample_rate = *(float *)opt->value;
            // sfizz_set_sample_rate(self->synth, self->sample_rate);
        }
        else if (!self->expect_nominal_block_length && opt->key == self->max_block_length_uri)
        {
            if (opt->type != self->atom_int_uri)
            {
                lv2_log_warning(&self->logger, "Got a max block size but the type was wrong\n");
                continue;
            }
            self->max_block_size = *(int *)opt->value;
            // sfizz_set_samples_per_block(self->synth, self->max_block_size);
        }
        else if (opt->key == self->nominal_block_length_uri)
        {
            if (opt->type != self->atom_int_uri)
            {
                lv2_log_warning(&self->logger, "Got a nominal block size but the type was wrong\n");
                continue;
            }
            self->max_block_size = *(int *)opt->value;
            // sfizz_set_samples_per_block(self->synth, self->max_block_size);
        }
    }
    return LV2_OPTIONS_SUCCESS;
}

static LV2_State_Status
restore(LV2_Handle instance,
        LV2_State_Retrieve_Function retrieve,
        LV2_State_Handle handle,
        uint32_t flags,
        const LV2_Feature *const *features)
{
    UNUSED(flags);
    UNUSED(features);
    beatbox_plugin_t *self = (beatbox_plugin_t *)instance;

    // Fetch back the saved file path, if any
    size_t size;
    uint32_t type;
    uint32_t val_flags;
    const void *value;
    value = retrieve(handle, self->bb_beat_description_uri, &size, &type, &val_flags);
    if (value)
    {
        lv2_log_note(&self->logger, "Restoring the file %s\n", (const char *)value);
        // if (sfizz_load_file(self->synth, (const char *)value))
        //     strcpy(self->sfz_file_path, (const char *)value);
        strcpy(self->beat_file_path, (const char *)value);
    }
    return LV2_STATE_SUCCESS;
}

static LV2_State_Status
save(LV2_Handle instance,
     LV2_State_Store_Function store,
     LV2_State_Handle handle,
     uint32_t flags,
     const LV2_Feature *const *features)
{
    UNUSED(flags);
    UNUSED(features);
    beatbox_plugin_t *self = (beatbox_plugin_t *)instance;
    // Save the file path
    store(handle,
          self->bb_beat_description_uri,
          self->beat_file_path,
          strlen(self->beat_file_path) + 1,
          self->atom_path_uri,
          LV2_STATE_IS_POD);

    return LV2_STATE_SUCCESS;
}

// This runs in a lower priority thread
static LV2_Worker_Status
work(LV2_Handle instance,
     LV2_Worker_Respond_Function respond,
     LV2_Worker_Respond_Handle handle,
     uint32_t size,
     const void *data)
{
    beatbox_plugin_t *self = (beatbox_plugin_t *)instance;
    if (!data)
    {
        lv2_log_error(&self->logger, "[worker] Got an empty data.\n");
        return LV2_WORKER_ERR_UNKNOWN;
    }

    const LV2_Atom *atom = (const LV2_Atom *)data;
    if (atom->type == self->bb_beat_description_uri)
    {
        const char *sfz_file_path = LV2_ATOM_BODY_CONST(atom);
        lv2_log_note(&self->logger, "[work] Loading file: %s\n", sfz_file_path);
        // sfizz_load_file(self->synth, sfz_file_path);
    }
    else
    {
        lv2_log_error(&self->logger, "[worker] Got an unknown atom.\n");
        if (self->unmap)
            lv2_log_error(&self->logger,
                          "URI: %s\n",
                          self->unmap->unmap(self->unmap->handle, atom->type));
        return LV2_WORKER_ERR_UNKNOWN;
    }

    respond(handle, size, data);
    return LV2_WORKER_SUCCESS;
}

// This runs in the audio thread
static LV2_Worker_Status
work_response(LV2_Handle instance,
              uint32_t size,
              const void *data)
{
    UNUSED(size);
    beatbox_plugin_t *self = (beatbox_plugin_t *)instance;

    if (!data)
        return LV2_WORKER_ERR_UNKNOWN;

    const LV2_Atom *atom = (const LV2_Atom *)data;
    if (atom->type == self->bb_beat_description_uri)
    {
        const char *beat_file_path = LV2_ATOM_BODY_CONST(atom);
        strcpy(self->beat_file_path, beat_file_path);
        lv2_log_note(&self->logger, "[work_response] File changed to: %s\n", self->beat_file_path);
    }
    else
    {
        lv2_log_error(&self->logger, "[work_response] Got an unknown atom.\n");
        if (self->unmap)
            lv2_log_error(&self->logger,
                          "URI: %s\n",
                          self->unmap->unmap(self->unmap->handle, atom->type));
        return LV2_WORKER_ERR_UNKNOWN;
    }

    return LV2_WORKER_SUCCESS;
}

static const void *
extension_data(const char *uri)
{
    static const LV2_Options_Interface options = {lv2_get_options, lv2_set_options};
    static const LV2_State_Interface state = {save, restore};
    static const LV2_Worker_Interface worker = {work, work_response, NULL};

    // Advertise the extensions we support
    if (!strcmp(uri, LV2_OPTIONS__interface))
        return &options;
    else if (!strcmp(uri, LV2_STATE__interface))
        return &state;
    else if (!strcmp(uri, LV2_WORKER__interface))
        return &worker;

    return NULL;
}

static const LV2_Descriptor descriptor = {
    BEATBOX_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data};

LV2_SYMBOL_EXPORT
const LV2_Descriptor *
lv2_descriptor(uint32_t index)
{
    switch (index)
    {
    case 0:
        return &descriptor;
    default:
        return NULL;
    }
}
