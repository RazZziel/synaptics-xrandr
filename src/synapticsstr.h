/*
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of Red Hat
 * not be used in advertising or publicity pertaining to distribution
 * of the software without specific, written prior permission.  Red
 * Hat makes no representations about the suitability of this software
 * for any purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE AUTHORS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_SYNAPTICSSTR_H_
#define _SYNAPTICSSTR_H_

#include "synproto.h"

/******************************************************************************
 *		Definitions
 *					structs, typedefs, #defines, enums
 *****************************************************************************/
#define SYNAPTICS_MOVE_HISTORY	5

typedef struct _SynapticsMoveHist
{
    int x, y;
    int millis;
} SynapticsMoveHistRec;

enum FingerState {		/* Note! The order matters. Compared with < operator. */
    FS_UNTOUCHED,
    FS_TOUCHED,
    FS_PRESSED
};

enum MovingState {
    MS_FALSE,
    MS_TOUCHPAD_RELATIVE,
    MS_TRACKSTICK		/* trackstick is always relative */
};

enum MidButtonEmulation {
    MBE_OFF,			/* No button pressed */
    MBE_LEFT,			/* Left button pressed, waiting for right button or timeout */
    MBE_RIGHT,			/* Right button pressed, waiting for left button or timeout */
    MBE_MID,			/* Left and right buttons pressed, waiting for both buttons
				   to be released */
    MBE_TIMEOUT,		/* Waiting for both buttons to be released. */
    MBE_LEFT_CLICK,		/* Emulate left button click. */
    MBE_RIGHT_CLICK,		/* Emulate right button click. */
};

/* See docs/tapndrag.dia for a state machine diagram */
enum TapState {
    TS_START,			/* No tap/drag in progress */
    TS_1,			/* After first touch */
    TS_MOVE,			/* Pointer movement enabled */
    TS_2A,			/* After first release */
    TS_2B,			/* After second/third/... release */
    TS_SINGLETAP,		/* After timeout after first release */
    TS_3,			/* After second touch */
    TS_DRAG,			/* Pointer drag enabled */
    TS_4,			/* After release when "locked drags" enabled */
    TS_5			/* After touch when "locked drags" enabled */
};

enum TapButtonState {
    TBS_BUTTON_UP,		/* "Virtual tap button" is up */
    TBS_BUTTON_DOWN,		/* "Virtual tap button" is down */
    TBS_BUTTON_DOWN_UP		/* Send button down event + set up state */
};

enum Rotation {
    R_NORMAL,
    R_LEFT,
    R_INVERTED,
    R_RIGHT
};

typedef struct _SynapticsPrivateRec
{
    SynapticsSHM synpara_default;	/* Default parameter settings, read from
					   the X config file */
    SynapticsSHM *synpara;		/* Current parameter settings. Will point to
					   shared memory if shm_config is true */
    struct SynapticsProtocolOperations* proto_ops;

    struct SynapticsHwState hwState;

    struct SynapticsHwInfo synhw;	/* Data read from the touchpad */
    Bool shm_config;			/* True when shared memory area allocated */

    OsTimerPtr timer;			/* for up/down-button repeat, tap processing, etc */

    struct CommData comm;

    SynapticsMoveHistRec move_hist[SYNAPTICS_MOVE_HISTORY]; /* movement history */
    int hist_index;			/* Last added entry in move_hist[] */
    int largest_valid_x;		/* Largest valid X coordinate seen so far */
    int scroll_y;			/* last y-scroll position */
    int scroll_x;			/* last x-scroll position */
    double scroll_a;			/* last angle-scroll position */
    int count_packet_finger;		/* packet counter with finger on the touchpad */
    int button_delay_millis;		/* button delay for 3rd button emulation */
    Bool prev_up;			/* Previous up button value, for double click emulation */
    enum FingerState finger_state;	/* previous finger state */

    enum TapState tap_state;		/* State of tap processing */
    int tap_max_fingers;		/* Max number of fingers seen since entering start state */
    int tap_button;			/* Which button started the tap processing */
    enum TapButtonState tap_button_state; /* Current tap action */
    SynapticsMoveHistRec touch_on;	/* data when the touchpad is touched/released */

    enum MovingState moving_state;	/* previous moving state */
    Bool vert_scroll_edge_on;		/* Keeps track of currently active scroll modes */
    Bool horiz_scroll_edge_on;		/* Keeps track of currently active scroll modes */
    Bool vert_scroll_twofinger_on;	/* Keeps track of currently active scroll modes */
    Bool horiz_scroll_twofinger_on;	/* Keeps track of currently active scroll modes */
    Bool circ_scroll_on;		/* Keeps track of currently active scroll modes */
    Bool circ_scroll_vert;		/* True: Generate vertical scroll events
					   False: Generate horizontal events */
    int trackstick_neutral_x;		/* neutral x position for trackstick mode */
    int trackstick_neutral_y;		/* neutral y position for trackstick mode */
    double autoscroll_xspd;		/* Horizontal coasting speed */
    double autoscroll_yspd;		/* Vertical coasting speed */
    double autoscroll_x;		/* Accumulated horizontal coasting scroll */
    double autoscroll_y;		/* Accumulated vertical coasting scroll */
    int scroll_packet_count;		/* Scroll duration */
    double frac_x, frac_y;		/* absolute -> relative fraction */
    enum MidButtonEmulation mid_emu_state;	/* emulated 3rd button */
    int repeatButtons;			/* buttons for repeat */
    int nextRepeat;			/* Time when to trigger next auto repeat event */
    int lastButtons;			/* last state of the buttons */
    int palm;				/* Set to true when palm detected, reset to false when
					   palm/finger contact disappears */
    int prev_z;				/* previous z value, for palm detection */
    int avg_width;			/* weighted average of previous fingerWidth values */

    int minx, maxx, miny, maxy;         /* min/max dimensions as detected */
    int minp, maxp, minw, maxw;		/* min/max pressure and finger width as detected */
    Bool has_left;			/* left button detected for this device */
    Bool has_right;			/* right button detected for this device */
    Bool has_middle;			/* middle button detected for this device */
    Bool has_double;			/* double click detected for this device */
    Bool has_triple;			/* triple click detected for this device */
    Bool has_pressure;			/* device reports pressure */
} SynapticsPrivate;

#endif /* _SYNAPTICSSTR_H_ */
