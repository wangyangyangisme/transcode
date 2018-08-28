#ifndef _ENCORE_ENCORE_H
#define _ENCORE_ENCORE_H
// This is the header file describing
// the entrance function of the encoder core
// or the encore ...

#ifdef __cplusplus
extern "C"
{
#endif
/**
    Structure passed as an argument when creating encoder.
    You have to initialize at least x_dim and y_dim ( valid range:
	0<x_dim<=1920, 0<y_dim<=1280, both dimensions should be even ).
    You can set all other values to 0, in which case they'll be initialized
    to default values, or specify them directly.
    On success 'handle' member will contain non-zero handle to initialized
    encoder.
**/

typedef struct
{
    int non_mpeg_4;
    int use_bidirect;
    int obmc;
    int data_partitioning;
    int mpeg_2_quant;
    int quarter_pel;
    int bidir_quant_multiplier; // / 0x1000
    int intra_frame_threshold; // 1 ... 100, 0 default value ( 50 )
    int psychovisual;
    double pv_strength_frame;
    double pv_strength_MB;
    int testing_param;      /* only used in testing */
    int use_gmc;
    int gmc_sensitivity;

    /* IVTC/deinterlace */
    int interlace_mode;
    int top_field_first;

    /* crop/resize things */
    int enable_crop;
	int enable_resize;
    int resize_width;
    int resize_height;
    int crop_left;
    int crop_right;
    int crop_top;
    int crop_bottom;
    int resize_mode; // 0==bilinear, 1==bicubic
    double bicubic_B; // spline parameter
    double bicubic_C; // spline parameter

    /* tsfilter things */
    int temporal_enable;
    int spatial_passes ;
    double temporal_level ;
    double spatial_level  ;

	const char* mv_file;
	int mv_direction;
// also here:
// any other special features
} EXTENSIONS;


typedef struct _ENC_PARAM_
{
    int x_dim;		// the x dimension of the frames to be encoded
    int y_dim;		// the y dimension of the frames to be encoded
    float framerate;	// the frame rate of the sequence before postprocessing in frames/second
    int bitrate;		// the bitrate of the target encoded stream, in bits/second
    int rc_period;		// the intended rate control averaging period
    int rc_reaction_period;	// the reaction period for rate control
    int rc_reaction_ratio;	// the ratio for down/up rate control
    int max_quantizer;	// the upper limit of the quantizer
    int min_quantizer;	// the lower limit of the quantizer
    int max_key_interval;	// the maximum interval between key frames
    int deinterlace;	// fast deinterlace
    int quality;		// the quality of compression ( 1 - fastest, 5 - best )
    void *handle;		// will be filled by encore
    EXTENSIONS extensions;
}
ENC_PARAM;

/**
    Structure passed as a first argument when encoding a frame.
    Both pointers should be non-NULL. You are responsible for allocation
    of bitstream buffer, its size should be large enough to hold a frame.
    Theoretical upper limit of frame size is around 6 bytes/pixel
    or 2.5 Mb for 720x576 frame.
    On success 'length' will contain number of bytes written into the stream.
**/
typedef struct _ENC_FRAME_
{
    const void *image;	// the image frame to be encoded
    void *bitstream;	// the buffer for encoded bitstream
    int length;		// the length of the encoded bitstream - if (length < 0) then bitstream is invalid
    int colorspace;	// the format of image frame
    int quant;		// quantizer for this frame; only used in VBR modes
    int intra;		// force this frame to be intra/inter; only used in VBR 2-pass
    const void *mvs;	// optional pointer to array of motion vectors
}
ENC_FRAME;

/**
    Structure passed as a second optional argument when encoding a frame.
    On successful return its members are filled with parameters of encoded
    stream.
**/
    typedef struct _ENC_RESULT_
    {
		int is_key_frame;	// the current frame is encoded as a key frame
		int quantizer;		// the quantizer used for this frame
		int texture_bits;	// amount of bits spent on coding DCT coeffs
		int motion_bits;	// amount of bits spend on coding motion
		int total_bits;		// sum of two previous fields
    }
    ENC_RESULT;

// the prototype of the encore() - main encode engine entrance
int encore(void *handle,	// handle               - the handle of the calling entity, must be unique
	       int enc_opt,	// enc_opt              - the option for encoding, see below
	       void *param1,	// param1               - the parameter 1 (its actually meaning depends on enc_opt
	       void *param2);	// param2               - the parameter 2 (its actually meaning depends on enc_opt

// encore options (the enc_opt parameter of encore())
#define ENC_OPT_INIT    0	// initialize the encoder, return a handle
#define ENC_OPT_RELEASE 1	// release all the resource associated with the handle
#define ENC_OPT_ENCODE  2	// encode a single frame
#define ENC_OPT_ENCODE_VBR 3	// encode a single frame, not using internal rate control algorithm
#define ENC_OPT_VERSION	4
#define ENC_OPT_INTERNAL_ERROR_INFO 5

#define ENCORE_VERSION		20020304
#define ENCORE_MAJOR_VERSION	5010


// return code of encore()
#define ENC_BUFFER		-2
#define ENC_FAIL		-1
#define ENC_OK			0
#define	ENC_MEMORY		1
#define ENC_BAD_FORMAT		2
#define ENC_INTERNAL		3

/** Common 24-bit RGB, order of components b-g-r **/
#define ENC_CSP_RGB24 	0

/** Planar YUV, U & V subsampled by 2 in both directions,
    average 12 bit per pixel; order of components y-v-u **/
#define ENC_CSP_YV12	1

/** Packed YUV, U and V subsampled by 2 horizontally,
    average 16 bit per pixel; order of components y-u-y-v **/
#define ENC_CSP_YUY2	2

/** Same as above, but order of components is u-y-v-y **/
#define ENC_CSP_UYVY	3

/** Same as ENC_CSP_YV12, but chroma components are swapped ( order y-u-v ) **/
#define ENC_CSP_I420	4

/** Same as above **/
#define ENC_CSP_IYUV	ENC_CSP_I420

/** 32-bit RGB, order of components b-g-r, one byte unused **/
#define ENC_CSP_RGB32	5

#ifdef __cplusplus
}
#endif

#endif
