/*
 * Copyright (C) 2001,2002,2003,2004,2005 Bastien Nocera <hadess@hadess.net>
 *
 * SPDX-License-Identifier: GPL-3-or-later
 *
 */

#pragma once

#include <gtk/gtk.h>

#define BACON_TYPE_VIDEO_WIDGET		     (bacon_video_widget_get_type ())
G_DECLARE_FINAL_TYPE(BaconVideoWidget, bacon_video_widget, BACON, VIDEO_WIDGET, GtkBin)
#define BVW_ERROR bacon_video_widget_error_quark ()

/**
 * BvwError:
 * @BVW_ERROR_NO_PLUGIN_FOR_FILE: A required GStreamer plugin is missing.
 * @BVW_ERROR_BROKEN_FILE: The movie file is broken and cannot be decoded.
 * @BVW_ERROR_FILE_GENERIC: A generic error for problems with movie files.
 * @BVW_ERROR_FILE_PERMISSION: Permission was refused to access the stream, or authentication was required.
 * @BVW_ERROR_FILE_ENCRYPTED: The stream is encrypted and cannot be played.
 * @BVW_ERROR_FILE_NOT_FOUND: The stream cannot be found.
 * @BVW_ERROR_DVD_ENCRYPTED: The DVD is encrypted and libdvdcss is not installed.
 * @BVW_ERROR_INVALID_DEVICE: The device given in an MRL (e.g. DVD drive or DVB tuner) did not exist.
 * @BVW_ERROR_UNKNOWN_HOST: The host for a given stream could not be resolved.
 * @BVW_ERROR_NETWORK_UNREACHABLE: The host for a given stream could not be reached.
 * @BVW_ERROR_CONNECTION_REFUSED: The server for a given stream refused the connection.
 * @BVW_ERROR_INVALID_LOCATION: An MRL was malformed, or CDDB playback was attempted (which is now unsupported).
 * @BVW_ERROR_GENERIC: A generic error occurred.
 * @BVW_ERROR_CODEC_NOT_HANDLED: The audio or video codec required by the stream is not supported.
 * @BVW_ERROR_CANNOT_CAPTURE: Error determining frame capture support for a video with bacon_video_widget_can_get_frames().
 * @BVW_ERROR_READ_ERROR: A generic error for problems reading streams.
 * @BVW_ERROR_PLUGIN_LOAD: A library or plugin could not be loaded.
 * @BVW_ERROR_EMPTY_FILE: A movie file was empty.
 *
 * Error codes for #BaconVideoWidget operations.
 **/
typedef enum {
	/* Plugins */
	BVW_ERROR_NO_PLUGIN_FOR_FILE,
	/* File */
	BVW_ERROR_BROKEN_FILE,
	BVW_ERROR_FILE_GENERIC,
	BVW_ERROR_FILE_PERMISSION,
	BVW_ERROR_FILE_ENCRYPTED,
	BVW_ERROR_FILE_NOT_FOUND,
	/* Devices */
	BVW_ERROR_DVD_ENCRYPTED,
	BVW_ERROR_INVALID_DEVICE,
	/* Network */
	BVW_ERROR_UNKNOWN_HOST,
	BVW_ERROR_NETWORK_UNREACHABLE,
	BVW_ERROR_CONNECTION_REFUSED,
	/* Generic */
	BVW_ERROR_INVALID_LOCATION,
	BVW_ERROR_GENERIC,
	BVW_ERROR_CODEC_NOT_HANDLED,
	BVW_ERROR_CANNOT_CAPTURE,
	BVW_ERROR_READ_ERROR,
	BVW_ERROR_PLUGIN_LOAD,
	BVW_ERROR_EMPTY_FILE
} BvwError;

GQuark bacon_video_widget_error_quark		 (void) G_GNUC_CONST;
GType bacon_video_widget_get_type                (void);
GOptionGroup* bacon_video_widget_get_option_group (void);

GtkWidget *bacon_video_widget_new		 (void);
gboolean bacon_video_widget_check_init		 (BaconVideoWidget  *bvw,
						  GError           **error);

/* Actions */
void bacon_video_widget_open			 (BaconVideoWidget *bvw, gint fileidx);
gboolean bacon_video_widget_play                 (BaconVideoWidget *bvw, GError **error);
void bacon_video_widget_pause			 (BaconVideoWidget *bvw);
gboolean bacon_video_widget_is_playing           (BaconVideoWidget *bvw);

/* Seeking and length */
gboolean bacon_video_widget_update_and_get_seekable          (BaconVideoWidget *bvw);
gboolean bacon_video_widget_seek		 (BaconVideoWidget *bvw,
						  double position,
						  GError **error);
gboolean bacon_video_widget_seek_time		 (BaconVideoWidget *bvw,
						  gint64 _time,
						  gboolean accurate,
						  GError **error);
// gboolean bacon_video_widget_step		 (BaconVideoWidget *bvw,
// 						  gboolean forward,
// 						  GError **error);
gboolean bacon_video_widget_can_direct_seek	 (BaconVideoWidget *bvw);
double bacon_video_widget_get_position           (BaconVideoWidget *bvw);
gint64 bacon_video_widget_get_current_time       (BaconVideoWidget *bvw);
gint64 bacon_video_widget_update_and_get_stream_length      (BaconVideoWidget *bvw);

void bacon_video_widget_stop                     (BaconVideoWidget *bvw);
void bacon_video_widget_close                    (BaconVideoWidget *bvw);

/* Audio volume */
gboolean bacon_video_widget_can_set_volume       (BaconVideoWidget *bvw);
void bacon_video_widget_set_volume               (BaconVideoWidget *bvw,
						  double volume);
double bacon_video_widget_get_volume             (BaconVideoWidget *bvw);

/* Properties */



/**
 * BVW_MIN_RATE:
 *
 * The minimum playback rate as a float.
 */
#define BVW_MIN_RATE 0.5

/**
 * BVW_MAX_RATE:
 *
 * The maximum playback rate as a float.
 */
#define BVW_MAX_RATE 3.0

gboolean bacon_video_widget_set_rate		 (BaconVideoWidget *bvw,
						  gfloat new_rate);
gfloat bacon_video_widget_get_rate		 (BaconVideoWidget *bvw);

void bacon_video_widget_set_show_cursor          (BaconVideoWidget *bvw,
						  gboolean          show_cursor);

/* Metadata */
/**
 * BvwMetadataType:
 * @BVW_INFO_TITLE: the stream's title
 * @BVW_INFO_ARTIST: the artist who created the work
 * @BVW_INFO_YEAR: the year in which the work was created
 * @BVW_INFO_COMMENT: a comment attached to the stream
 * @BVW_INFO_ALBUM: the album in which the work was released
 * @BVW_INFO_DURATION: the stream's duration, in seconds
 * @BVW_INFO_TRACK_NUMBER: the track number of the work on the album
 * @BVW_INFO_CONTAINER: the type of stream container
 * @BVW_INFO_HAS_VIDEO: whether the stream has video
 * @BVW_INFO_DIMENSION_X: the video's width, in pixels
 * @BVW_INFO_DIMENSION_Y: the video's height, in pixels
 * @BVW_INFO_VIDEO_BITRATE: the video's bitrate, in kilobits per second
 * @BVW_INFO_VIDEO_CODEC: the video's codec
 * @BVW_INFO_FPS: the number of frames per second in the video
 * @BVW_INFO_HAS_AUDIO: whether the stream has audio
 * @BVW_INFO_AUDIO_BITRATE: the audio's bitrate, in kilobits per second
 * @BVW_INFO_AUDIO_CODEC: the audio's codec
 * @BVW_INFO_AUDIO_SAMPLE_RATE: the audio sample rate, in bits per second
 * @BVW_INFO_AUDIO_CHANNELS: a string describing the number of audio channels in the stream
 *
 * The different metadata available for querying from a #BaconVideoWidget
 * stream with bacon_video_widget_get_metadata().
 **/
typedef enum {
	BVW_INFO_TITLE,
	BVW_INFO_ARTIST,
	BVW_INFO_YEAR,
	BVW_INFO_COMMENT,
	BVW_INFO_ALBUM,
	BVW_INFO_DURATION,
	BVW_INFO_TRACK_NUMBER,
	BVW_INFO_CONTAINER,
	/* Video */
	BVW_INFO_HAS_VIDEO,
	BVW_INFO_DIMENSION_X,
	BVW_INFO_DIMENSION_Y,
	BVW_INFO_VIDEO_BITRATE,
	BVW_INFO_VIDEO_CODEC,
	BVW_INFO_FPS,
	/* Audio */
	BVW_INFO_HAS_AUDIO,
	BVW_INFO_AUDIO_BITRATE,
	BVW_INFO_AUDIO_CODEC,
	BVW_INFO_AUDIO_SAMPLE_RATE,
	BVW_INFO_AUDIO_CHANNELS
} BvwMetadataType;

void bacon_video_widget_get_metadata		 (BaconVideoWidget *bvw,
						  BvwMetadataType type,
						  GValue *value);



/* Picture settings */
/**
 * BvwVideoProperty:
 * @BVW_VIDEO_BRIGHTNESS: the video brightness
 * @BVW_VIDEO_CONTRAST: the video contrast
 * @BVW_VIDEO_SATURATION: the video saturation
 * @BVW_VIDEO_HUE: the video hue
 *
 * The video properties queryable with bacon_video_widget_get_video_property(),
 * and settable with bacon_video_widget_set_video_property().
 **/
typedef enum {
	BVW_VIDEO_BRIGHTNESS,
	BVW_VIDEO_CONTRAST,
	BVW_VIDEO_SATURATION,
	BVW_VIDEO_HUE
} BvwVideoProperty;


/**
 * BvwAspectRatio:
 * @BVW_RATIO_AUTO: automatic
 * @BVW_RATIO_SQUARE: square (1:1)
 * @BVW_RATIO_FOURBYTHREE: four-by-three (4:3)
 * @BVW_RATIO_ANAMORPHIC: anamorphic (16:9)
 * @BVW_RATIO_DVB: DVB (20:9)
 *
 * The pixel aspect ratios available in which to display videos using
 * @bacon_video_widget_set_aspect_ratio().
 **/
typedef enum {
	BVW_RATIO_AUTO = 0,
	BVW_RATIO_SQUARE = 1,
	BVW_RATIO_FOURBYTHREE = 2,
	BVW_RATIO_ANAMORPHIC = 3,
	BVW_RATIO_DVB = 4
} BvwAspectRatio;




/**
 * BvwZoomMode:
 * @BVW_ZOOM_NONE: No video zooming/cropping
 * @BVW_ZOOM_EXPAND: Fill area with video, and crop the excess
 *
 * The zoom mode used by the video widget, as set by
 * bacon_video_widget_set_zoom().
 **/
typedef enum {
	BVW_ZOOM_NONE = 0,
	BVW_ZOOM_EXPAND = 1
} BvwZoomMode;



/**
 * BvwRotation:
 * @BVW_ROTATION_R_ZERO: No rotation
 * @BVW_ROTATION_R_90R: Rotate 90 degrees to the right
 * @BVW_ROTATION_R_180: Rotate 180 degrees
 * @BVW_ROTATION_R_90L: Rotate 90 degrees to the left
 *
 * The rotation is used by the video widget, as set by
 * bacon_video_widget_set_rotation().
 **/
typedef enum {
	BVW_ROTATION_R_ZERO = 0,
	BVW_ROTATION_R_90R  = 1,
	BVW_ROTATION_R_180  = 2,
	BVW_ROTATION_R_90L  = 3
} BvwRotation;


void bacon_video_widget_set_aspect_ratio         (BaconVideoWidget *bvw,
						  BvwAspectRatio ratio);
BvwAspectRatio bacon_video_widget_get_aspect_ratio
						 (BaconVideoWidget *bvw);

void bacon_video_widget_set_zoom		 (BaconVideoWidget *bvw,
						  BvwZoomMode       mode);
BvwZoomMode bacon_video_widget_get_zoom		 (BaconVideoWidget *bvw);

void bacon_video_widget_set_rotation		 (BaconVideoWidget *bvw,
						  BvwRotation       rotation);
BvwRotation bacon_video_widget_get_rotation	 (BaconVideoWidget *bvw);

int bacon_video_widget_get_video_property        (BaconVideoWidget *bvw,
						  BvwVideoProperty type);
void bacon_video_widget_set_video_property       (BaconVideoWidget *bvw,
						  BvwVideoProperty type,
						  int value);






/**
 * BvwTrackType:
 * @BVW_TRACK_TYPE_AUDIO: an audio track
 * @BVW_TRACK_TYPE_SUBTITLE: a subtitle track
 * @BVW_TRACK_TYPE_VIDEO: a video track
 *
 * A type of media track.
 */
typedef enum {
	BVW_TRACK_TYPE_AUDIO,
	BVW_TRACK_TYPE_SUBTITLE,
	BVW_TRACK_TYPE_VIDEO
} BvwTrackType;



/* Audio-out functions */
/**
 * BvwAudioOutputType:
 * @BVW_AUDIO_SOUND_STEREO: stereo output
 * @BVW_AUDIO_SOUND_4CHANNEL: 4-channel output
 * @BVW_AUDIO_SOUND_41CHANNEL: 4.1-channel output
 * @BVW_AUDIO_SOUND_5CHANNEL: 5-channel output
 * @BVW_AUDIO_SOUND_51CHANNEL: 5.1-channel output
 * @BVW_AUDIO_SOUND_AC3PASSTHRU: AC3 passthrough output
 *
 * The audio output types available for use with bacon_video_widget_set_audio_output_type().
 **/
typedef enum {
	BVW_AUDIO_SOUND_STEREO,
	BVW_AUDIO_SOUND_4CHANNEL,
	BVW_AUDIO_SOUND_41CHANNEL,
	BVW_AUDIO_SOUND_5CHANNEL,
	BVW_AUDIO_SOUND_51CHANNEL,
	BVW_AUDIO_SOUND_AC3PASSTHRU
} BvwAudioOutputType;

BvwAudioOutputType bacon_video_widget_get_audio_output_type
						 (BaconVideoWidget *bvw);
void bacon_video_widget_set_audio_output_type    (BaconVideoWidget *bvw,
						  BvwAudioOutputType type);


void 
bacon_video_widget_retrieve_btdemux_video_info (BaconVideoWidget *bvw, GList **  videos_info_addr);


void
bvw_g_object_association_clear (BaconVideoWidget *bvw, const gchar* name);
