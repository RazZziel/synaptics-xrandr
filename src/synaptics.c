/*
 * Copyright � 1999 Henry Davies
 * Copyright � 2001 Stefan Gmeiner
 * Copyright � 2002 S. Lehner
 * Copyright � 2002 Peter Osterlund
 * Copyright � 2002 Linuxcare Inc. David Kennedy
 * Copyright � 2003 Hartwig Felger
 * Copyright � 2003 J�rg B�sner
 * Copyright � 2003 Fred Hucht
 * Copyright � 2004 Alexei Gilchrist
 * Copyright � 2004 Matthias Ihmig
 * Copyright � 2006 Stefan Bethge
 * Copyright � 2006 Christian Thaeter
 * Copyright � 2007 Joseph P. Skudlarek
 * Copyright � 2008 Fedor P. Goncharov
 * Copyright � 2008 Red Hat, Inc.
 *
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
 *
 * Authors:
 *      Joseph P. Skudlarek <Jskud@Jskud.com>
 *      Christian Thaeter <chth@gmx.net>
 *      Stefan Bethge <stefan.bethge@web.de>
 *      Matthias Ihmig <m.ihmig@gmx.net>
 *      Alexei Gilchrist <alexei@physics.uq.edu.au>
 *      J�rg B�sner <ich@joerg-boesner.de>
 *      Hartwig Felger <hgfelger@hgfelger.de>
 *      Peter Osterlund <petero2@telia.com>
 *      S. Lehner <sam_x@bluemail.ch>
 *      Stefan Gmeiner <riddlebox@freesurf.ch>
 *      Henry Davies <hdavies@ameritech.net> for the
 *      Linuxcare Inc. David Kennedy <dkennedy@linuxcare.com>
 *      Fred Hucht <fred@thp.Uni-Duisburg.de>
 *      Fedor P. Goncharov <fedgo@gorodok.net>
 *
 * Trademarks are the property of their respective owners.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xorg-server.h>
#include <unistd.h>
#include <misc.h>
#include <xf86.h>
#include <sys/shm.h>
#include <math.h>
#include <stdio.h>
#include <xf86_OSproc.h>
#include <xf86Xinput.h>
#include <exevents.h>

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
#include "mipointer.h"
#endif

#include "synaptics.h"
#include "synapticsstr.h"
#include "synaptics-properties.h"

typedef enum {
    BOTTOM_EDGE = 1,
    TOP_EDGE = 2,
    LEFT_EDGE = 4,
    RIGHT_EDGE = 8,
    LEFT_BOTTOM_EDGE = BOTTOM_EDGE | LEFT_EDGE,
    RIGHT_BOTTOM_EDGE = BOTTOM_EDGE | RIGHT_EDGE,
    RIGHT_TOP_EDGE = TOP_EDGE | RIGHT_EDGE,
    LEFT_TOP_EDGE = TOP_EDGE | LEFT_EDGE
} edge_type;

#define MAX(a, b) (((a)>(b))?(a):(b))
#define MIN(a, b) (((a)<(b))?(a):(b))
#define TIME_DIFF(a, b) ((int)((a)-(b)))

#define SQR(x) ((x) * (x))

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_SQRT1_2
#define M_SQRT1_2  0.70710678118654752440  /* 1/sqrt(2) */
#endif

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 1
#define DBG(a,b)
#endif

/*****************************************************************************
 * Forward declaration
 ****************************************************************************/
static InputInfoPtr SynapticsPreInit(InputDriverPtr drv, IDevPtr dev, int flags);
static void SynapticsUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags);
static Bool DeviceControl(DeviceIntPtr, int);
static void ReadInput(LocalDevicePtr);
static int HandleState(LocalDevicePtr, struct SynapticsHwState*);
static int ControlProc(LocalDevicePtr, xDeviceCtl*);
static void CloseProc(LocalDevicePtr);
static int SwitchMode(ClientPtr, DeviceIntPtr, int);
static Bool ConvertProc(LocalDevicePtr, int, int, int, int, int, int, int, int, int*, int*);
static Bool DeviceInit(DeviceIntPtr);
static Bool DeviceOn(DeviceIntPtr);
static Bool DeviceOff(DeviceIntPtr);
static Bool DeviceClose(DeviceIntPtr);
static Bool QueryHardware(LocalDevicePtr);
static void ReadDevDimensions(LocalDevicePtr);

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 3
void InitDeviceProperties(LocalDevicePtr local);
int SetProperty(DeviceIntPtr dev, Atom property, XIPropertyValuePtr prop,
                BOOL checkonly);
#endif

InputDriverRec SYNAPTICS = {
    1,
    "synaptics",
    NULL,
    SynapticsPreInit,
    SynapticsUnInit,
    NULL,
    0
};

static XF86ModuleVersionInfo VersionRec = {
    "synaptics",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

static pointer
SetupProc(pointer module, pointer options, int *errmaj, int *errmin)
{
    xf86AddInputDriver(&SYNAPTICS, module, 0);
    return module;
}

_X_EXPORT XF86ModuleData synapticsModuleData = {
    &VersionRec,
    &SetupProc,
    NULL
};


/*****************************************************************************
 *	Function Definitions
 ****************************************************************************/

static void
SetDeviceAndProtocol(LocalDevicePtr local)
{
    char *str_par, *device;
    SynapticsPrivate *priv = local->private;
    enum SynapticsProtocol proto = SYN_PROTO_PSAUX;

    device = xf86FindOptionValue(local->options, "Device");
    if (!device) {
	device = xf86FindOptionValue(local->options, "Path");
	if (device) {
	    local->options =
	    	xf86ReplaceStrOption(local->options, "Device", device);
	}
    }
    if (device && strstr(device, "/dev/input/event")) {
#ifdef BUILD_EVENTCOMM
	proto = SYN_PROTO_EVENT;
#endif
    } else {
	str_par = xf86FindOptionValue(local->options, "Protocol");
	if (str_par && !strcmp(str_par, "psaux")) {
	    /* Already set up */
#ifdef BUILD_EVENTCOMM
	} else if (str_par && !strcmp(str_par, "event")) {
	    proto = SYN_PROTO_EVENT;
#endif /* BUILD_EVENTCOMM */
#ifdef BUILD_PSMCOMM
	} else if (str_par && !strcmp(str_par, "psm")) {
	    proto = SYN_PROTO_PSM;
#endif /* BUILD_PSMCOMM */
	} else if (str_par && !strcmp(str_par, "alps")) {
	    proto = SYN_PROTO_ALPS;
	} else { /* default to auto-dev */
#ifdef BUILD_EVENTCOMM
	    if (event_proto_operations.AutoDevProbe(local))
		proto = SYN_PROTO_EVENT;
#endif
	}
    }
    switch (proto) {
    case SYN_PROTO_PSAUX:
	priv->proto_ops = &psaux_proto_operations;
	break;
#ifdef BUILD_EVENTCOMM
    case SYN_PROTO_EVENT:
	priv->proto_ops = &event_proto_operations;
	break;
#endif /* BUILD_EVENTCOMM */
#ifdef BUILD_PSMCOMM
    case SYN_PROTO_PSM:
	priv->proto_ops = &psm_proto_operations;
	break;
#endif /* BUILD_PSMCOMM */
    case SYN_PROTO_ALPS:
	priv->proto_ops = &alps_proto_operations;
	break;
    }
}

/*
 * Allocate and initialize memory for the SynapticsSHM data to hold driver
 * parameter settings.
 * The function will allocate shared memory if priv->shm_config is TRUE.
 * The allocated data is initialized from priv->synpara_default.
 */
static Bool
alloc_param_data(LocalDevicePtr local)
{
    int shmid;
    SynapticsPrivate *priv = local->private;

    if (priv->synpara)
	return TRUE;			    /* Already allocated */

    if (priv->shm_config) {
	if ((shmid = shmget(SHM_SYNAPTICS, 0, 0)) != -1)
	    shmctl(shmid, IPC_RMID, NULL);
	if ((shmid = shmget(SHM_SYNAPTICS, sizeof(SynapticsSHM),
				0777 | IPC_CREAT)) == -1) {
	    xf86Msg(X_ERROR, "%s error shmget\n", local->name);
	    return FALSE;
	}
	if ((priv->synpara = (SynapticsSHM*)shmat(shmid, NULL, 0)) == NULL) {
	    xf86Msg(X_ERROR, "%s error shmat\n", local->name);
	    return FALSE;
	}
    } else {
	priv->synpara = xcalloc(1, sizeof(SynapticsSHM));
	if (!priv->synpara)
	    return FALSE;
    }

    *(priv->synpara) = priv->synpara_default;
    return TRUE;
}

/*
 * Free SynapticsSHM data previously allocated by alloc_param_data().
 */
static void
free_param_data(SynapticsPrivate *priv)
{
    int shmid;

    if (!priv->synpara)
	return;

    if (priv->shm_config) {
	if ((shmid = shmget(SHM_SYNAPTICS, 0, 0)) != -1)
	    shmctl(shmid, IPC_RMID, NULL);
    } else {
	xfree(priv->synpara);
    }

    priv->synpara = NULL;
}

static void set_default_parameters(LocalDevicePtr local)
{
    SynapticsPrivate *priv = local->private; /* read-only */
    pointer opts = local->options; /* read-only */
    SynapticsSHM *pars = &priv->synpara_default; /* modified */

    int horizScrollDelta, vertScrollDelta;		/* pixels */
    int tapMove;					/* pixels */
    int l, r, t, b; /* left, right, top, bottom */
    int edgeMotionMinSpeed, edgeMotionMaxSpeed;		/* pixels/second */
    double accelFactor;					/* 1/pixels */
    int fingerLow, fingerHigh, fingerPress;		/* pressure */
    int emulateTwoFingerMinZ;				/* pressure */
    int emulateTwoFingerMinW;				/* width */
    int edgeMotionMinZ, edgeMotionMaxZ;			/* pressure */
    int pressureMotionMinZ, pressureMotionMaxZ;		/* pressure */
    int palmMinWidth, palmMinZ;				/* pressure */
    int tapButton1, tapButton2, tapButton3;
    int clickFinger1, clickFinger2, clickFinger3;
    Bool vertEdgeScroll, horizEdgeScroll;
    Bool vertTwoFingerScroll, horizTwoFingerScroll;

    /* read the parameters */
    pars->version = (PACKAGE_VERSION_MAJOR*10000+PACKAGE_VERSION_MINOR*100+PACKAGE_VERSION_PATCHLEVEL);

    /* The synaptics specs specify typical edge widths of 4% on x, and 5.4% on
     * y (page 7) [Synaptics TouchPad Interfacing Guide, 510-000080 - A
     * Second Edition, http://www.synaptics.com/support/dev_support.cfm, 8 Sep
     * 2008]
     * If the range was autodetected, apply these edge widths to all four
     * sides.
     */
    if (priv->minx < priv->maxx && priv->miny < priv->maxy)
    {
        int width, height, diag;
        int ewidth, eheight; /* edge width/height */

        width = abs(priv->maxx - priv->minx);
        height = abs(priv->maxy - priv->miny);
        diag = sqrt(width * width + height * height);
        ewidth = width * .04;
        eheight = height * .055;

        l = priv->minx + ewidth;
        r = priv->maxx - ewidth;
        t = priv->miny + eheight;
        b = priv->maxy - eheight;

        /* Again, based on typical x/y range and defaults */
        horizScrollDelta = diag * .020;
        vertScrollDelta = diag * .020;
        tapMove = diag * .044;
        edgeMotionMinSpeed = 1;
        edgeMotionMaxSpeed = diag * .080;
        accelFactor = 50.0 / diag;
    } else {
        l = 1900;
        r = 5400;
        t = 1900;
        b = 4000;

        horizScrollDelta = 100;
        vertScrollDelta = 100;
        tapMove = 220;
        edgeMotionMinSpeed = 1;
        edgeMotionMaxSpeed = 400;
        accelFactor = 0.010;
    }

    if (priv->minp < priv->maxp) {
	int range = priv->maxp - priv->minp;

	/* scaling based on defaults below and a pressure of 256 */
	fingerLow = priv->minp + range * (25.0/256);
	fingerHigh = priv->minp + range * (30.0/256);
	fingerPress = priv->minp + range * 1.000;
	emulateTwoFingerMinZ = priv->minp + range * (282.0/256);
	edgeMotionMinZ = priv->minp + range * (30.0/256);
	edgeMotionMaxZ = priv->minp + range * (160.0/256);
	pressureMotionMinZ = priv->minp + range * (30.0/256);
	pressureMotionMaxZ = priv->minp + range * (160.0/256);
	palmMinZ = priv->minp + range * (200.0/256);
    } else {
	fingerLow = 25;
	fingerHigh = 30;
	fingerPress = 256;
	emulateTwoFingerMinZ = 257;
	edgeMotionMinZ = 30;
	edgeMotionMaxZ = 160;
	pressureMotionMinZ = 30;
	pressureMotionMaxZ = 160;
	palmMinZ = 200;
    }

    if (priv->minw < priv->maxw) {
	int range = priv->maxw - priv->minw;

	/* scaling based on defaults below and a tool width of 16 */
	palmMinWidth = priv->minw + range * (10.0/16);
	emulateTwoFingerMinW = priv->minw + range * (7.0/16);
    } else {
	palmMinWidth = 10;
	emulateTwoFingerMinW = 7;
    }

    /* Enable tap if we don't have a phys left button */
    tapButton1 = priv->has_left ? 0 : 1;
    tapButton2 = priv->has_left ? 0 : 3;
    tapButton3 = priv->has_left ? 0 : 2;

    /* Enable multifinger-click if we don't have right/middle button,
       otherwise clickFinger is always button 1. */
    clickFinger1 = 1;
    clickFinger2 = priv->has_right ? 1 : 3;
    clickFinger3 = priv->has_middle ? 1 : 2;

    /* Enable vert edge scroll if we can't detect doubletap */
    vertEdgeScroll = priv->has_double ? FALSE : TRUE;
    horizEdgeScroll = FALSE;

    /* Enable twofinger scroll if we can detect doubletap */
    vertTwoFingerScroll = priv->has_double ? TRUE : FALSE;
    horizTwoFingerScroll = FALSE;

    /* set the parameters */
    pars->left_edge = xf86SetIntOption(opts, "LeftEdge", l);
    pars->right_edge = xf86SetIntOption(opts, "RightEdge", r);
    pars->top_edge = xf86SetIntOption(opts, "TopEdge", t);
    pars->bottom_edge = xf86SetIntOption(opts, "BottomEdge", b);

    pars->finger_low = xf86SetIntOption(opts, "FingerLow", fingerLow);
    pars->finger_high = xf86SetIntOption(opts, "FingerHigh", fingerHigh);
    pars->finger_press = xf86SetIntOption(opts, "FingerPress", fingerPress);
    pars->tap_time = xf86SetIntOption(opts, "MaxTapTime", 180);
    pars->tap_move = xf86SetIntOption(opts, "MaxTapMove", tapMove);
    pars->tap_time_2 = xf86SetIntOption(opts, "MaxDoubleTapTime", 180);
    pars->click_time = xf86SetIntOption(opts, "ClickTime", 100);
    pars->fast_taps = xf86SetBoolOption(opts, "FastTaps", FALSE);
    pars->emulate_mid_button_time = xf86SetIntOption(opts, "EmulateMidButtonTime", 75);
    pars->emulate_twofinger_z = xf86SetIntOption(opts, "EmulateTwoFingerMinZ", emulateTwoFingerMinZ);
    pars->emulate_twofinger_w = xf86SetIntOption(opts, "EmulateTwoFingerMinW", emulateTwoFingerMinW);
    pars->scroll_dist_vert = xf86SetIntOption(opts, "VertScrollDelta", horizScrollDelta);
    pars->scroll_dist_horiz = xf86SetIntOption(opts, "HorizScrollDelta", vertScrollDelta);
    pars->scroll_edge_vert = xf86SetBoolOption(opts, "VertEdgeScroll", vertEdgeScroll);
    if (xf86CheckIfOptionUsedByName(opts, "RightEdge")) {
      pars->special_scroll_area_right  = FALSE;
    } else {
      pars->special_scroll_area_right  = xf86SetBoolOption(opts, "SpecialScrollAreaRight", TRUE);
    }
    pars->scroll_edge_horiz = xf86SetBoolOption(opts, "HorizEdgeScroll", horizEdgeScroll);
    pars->scroll_edge_corner = xf86SetBoolOption(opts, "CornerCoasting", FALSE);
    pars->scroll_twofinger_vert = xf86SetBoolOption(opts, "VertTwoFingerScroll", vertTwoFingerScroll);
    pars->scroll_twofinger_horiz = xf86SetBoolOption(opts, "HorizTwoFingerScroll", horizTwoFingerScroll);
    pars->edge_motion_min_z = xf86SetIntOption(opts, "EdgeMotionMinZ", edgeMotionMinZ);
    pars->edge_motion_max_z = xf86SetIntOption(opts, "EdgeMotionMaxZ", edgeMotionMaxZ);
    pars->edge_motion_min_speed = xf86SetIntOption(opts, "EdgeMotionMinSpeed", edgeMotionMinSpeed);
    pars->edge_motion_max_speed = xf86SetIntOption(opts, "EdgeMotionMaxSpeed", edgeMotionMaxSpeed);
    pars->edge_motion_use_always = xf86SetBoolOption(opts, "EdgeMotionUseAlways", FALSE);
    pars->updown_button_scrolling = xf86SetBoolOption(opts, "UpDownScrolling", TRUE);
    pars->leftright_button_scrolling = xf86SetBoolOption(opts, "LeftRightScrolling", TRUE);
    pars->updown_button_repeat = xf86SetBoolOption(opts, "UpDownScrollRepeat", TRUE);
    pars->leftright_button_repeat = xf86SetBoolOption(opts, "LeftRightScrollRepeat", TRUE);
    pars->scroll_button_repeat = xf86SetIntOption(opts,"ScrollButtonRepeat", 100);
    pars->touchpad_off = xf86SetIntOption(opts, "TouchpadOff", 0);
    pars->guestmouse_off = xf86SetBoolOption(opts, "GuestMouseOff", FALSE);
    pars->locked_drags = xf86SetBoolOption(opts, "LockedDrags", FALSE);
    pars->locked_drag_time = xf86SetIntOption(opts, "LockedDragTimeout", 5000);
    pars->tap_action[RT_TAP] = xf86SetIntOption(opts, "RTCornerButton", 0);
    pars->tap_action[RB_TAP] = xf86SetIntOption(opts, "RBCornerButton", 0);
    pars->tap_action[LT_TAP] = xf86SetIntOption(opts, "LTCornerButton", 0);
    pars->tap_action[LB_TAP] = xf86SetIntOption(opts, "LBCornerButton", 0);
    pars->tap_action[F1_TAP] = xf86SetIntOption(opts, "TapButton1",     tapButton1);
    pars->tap_action[F2_TAP] = xf86SetIntOption(opts, "TapButton2",     tapButton2);
    pars->tap_action[F3_TAP] = xf86SetIntOption(opts, "TapButton3",     tapButton3);
    pars->click_action[F1_CLICK1] = xf86SetIntOption(opts, "ClickFinger1", clickFinger1);
    pars->click_action[F2_CLICK1] = xf86SetIntOption(opts, "ClickFinger2", clickFinger2);
    pars->click_action[F3_CLICK1] = xf86SetIntOption(opts, "ClickFinger3", clickFinger3);
    pars->circular_scrolling = xf86SetBoolOption(opts, "CircularScrolling", FALSE);
    pars->circular_trigger   = xf86SetIntOption(opts, "CircScrollTrigger", 0);
    pars->circular_pad       = xf86SetBoolOption(opts, "CircularPad", FALSE);
    pars->palm_detect        = xf86SetBoolOption(opts, "PalmDetect", FALSE);
    pars->palm_min_width     = xf86SetIntOption(opts, "PalmMinWidth", palmMinWidth);
    pars->palm_min_z         = xf86SetIntOption(opts, "PalmMinZ", palmMinZ);
    pars->single_tap_timeout = xf86SetIntOption(opts, "SingleTapTimeout", 180);
    pars->press_motion_min_z = xf86SetIntOption(opts, "PressureMotionMinZ", pressureMotionMinZ);
    pars->press_motion_max_z = xf86SetIntOption(opts, "PressureMotionMaxZ", pressureMotionMaxZ);

    pars->min_speed = xf86SetRealOption(opts, "MinSpeed", 0.4);
    pars->max_speed = xf86SetRealOption(opts, "MaxSpeed", 0.7);
    pars->accl = xf86SetRealOption(opts, "AccelFactor", accelFactor);
    pars->trackstick_speed = xf86SetRealOption(opts, "TrackstickSpeed", 40);
    pars->scroll_dist_circ = xf86SetRealOption(opts, "CircScrollDelta", 0.1);
    pars->coasting_speed = xf86SetRealOption(opts, "CoastingSpeed", 0.0);
    pars->press_motion_min_factor = xf86SetRealOption(opts, "PressureMotionMinFactor", 1.0);
    pars->press_motion_max_factor = xf86SetRealOption(opts, "PressureMotionMaxFactor", 1.0);
    pars->grab_event_device = xf86SetBoolOption(opts, "GrabEventDevice", TRUE);
    pars->rotation = xf86SetIntOption(opts, "Rotation", 0);

    /* Warn about (and fix) incorrectly configured TopEdge/BottomEdge parameters */
    if (pars->top_edge > pars->bottom_edge) {
	int tmp = pars->top_edge;
	pars->top_edge = pars->bottom_edge;
	pars->bottom_edge = tmp;
	xf86Msg(X_WARNING, "%s: TopEdge is bigger than BottomEdge. Fixing.\n",
		local->name);
    }
}

/*
 *  called by the module loader for initialization
 */
static InputInfoPtr
SynapticsPreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    LocalDevicePtr local;
    SynapticsPrivate *priv;

    /* allocate memory for SynapticsPrivateRec */
    priv = xcalloc(1, sizeof(SynapticsPrivate));
    if (!priv)
	return NULL;

    /* Allocate a new InputInfoRec and add it to the head xf86InputDevs. */
    local = xf86AllocateInput(drv, 0);
    if (!local) {
	xfree(priv);
	return NULL;
    }

    /* initialize the InputInfoRec */
    local->name                    = dev->identifier;
    local->type_name               = XI_TOUCHPAD;
    local->device_control          = DeviceControl;
    local->read_input              = ReadInput;
    local->control_proc            = ControlProc;
    local->close_proc              = CloseProc;
    local->switch_mode             = SwitchMode;
    local->conversion_proc         = ConvertProc;
    local->reverse_conversion_proc = NULL;
    local->dev                     = NULL;
    local->private                 = priv;
    local->private_flags           = 0;
    local->flags                   = XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS;
    local->conf_idev               = dev;
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
    local->motion_history_proc     = xf86GetMotionEvents;
    local->history_size            = 0;
#endif
    local->always_core_feedback    = 0;

    xf86Msg(X_INFO, "Synaptics touchpad driver version %s\n", PACKAGE_VERSION);

    xf86CollectInputOptions(local, NULL, NULL);

    xf86OptionListReport(local->options);

    /* may change local->options */
    SetDeviceAndProtocol(local);

    /* open the touchpad device */
    local->fd = xf86OpenSerial(local->options);
    if (local->fd == -1) {
	xf86Msg(X_ERROR, "Synaptics driver unable to open device\n");
	goto SetupProc_fail;
    }
    xf86ErrorFVerb(6, "port opened successfully\n");

    /* initialize variables */
    priv->timer = NULL;
    priv->repeatButtons = 0;
    priv->nextRepeat = 0;
    priv->count_packet_finger = 0;
    priv->tap_state = TS_START;
    priv->tap_button = 0;
    priv->tap_button_state = TBS_BUTTON_UP;
    priv->touch_on.millis = 0;

    /* read hardware dimensions */
    ReadDevDimensions(local);

    /* install shared memory or normal memory for parameters */
    priv->shm_config = xf86SetBoolOption(local->options, "SHMConfig", FALSE);

    set_default_parameters(local);

    priv->largest_valid_x = MIN(priv->synpara_default.right_edge, XMAX_NOMINAL);

    if (!alloc_param_data(local))
	goto SetupProc_fail;

    priv->comm.buffer = XisbNew(local->fd, 200);
    DBG(9, XisbTrace(priv->comm.buffer, 1));

    if (!QueryHardware(local)) {
	xf86Msg(X_ERROR, "%s Unable to query/initialize Synaptics hardware.\n", local->name);
	goto SetupProc_fail;
    }

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
    local->history_size = xf86SetIntOption(local->options, "HistorySize", 0);
#endif

    xf86ProcessCommonOptions(local, local->options);
    local->flags |= XI86_CONFIGURED;

    if (local->fd != -1) {
	if (priv->comm.buffer) {
	    XisbFree(priv->comm.buffer);
	    priv->comm.buffer = NULL;
	}
	xf86CloseSerial(local->fd);
    }
    local->fd = -1;

    return local;

 SetupProc_fail:
    if (local->fd >= 0) {
	xf86CloseSerial(local->fd);
	local->fd = -1;
    }

    if (priv->comm.buffer)
	XisbFree(priv->comm.buffer);
    free_param_data(priv);
    xfree(priv);
    local->private = NULL;
    return local;
}


/*
 *  Uninitialize the device.
 */
static void SynapticsUnInit(InputDriverPtr drv,
                            InputInfoPtr   local,
                            int            flags)
{
    xfree(local->private);
    local->private = NULL;
    xf86DeleteInput(local, 0);
}


/*
 *  Alter the control parameters for the mouse. Note that all special
 *  protocol values are handled by dix.
 */
static void
SynapticsCtrl(DeviceIntPtr device, PtrCtrl *ctrl)
{
    DBG(3, ErrorF("SynapticsCtrl called.\n"));
    /*
      pInfo = device->public.devicePrivate;
      pMse = pInfo->private;

      pMse->num       = ctrl->num;
      pMse->den       = ctrl->den;
      pMse->threshold = ctrl->threshold;
    */
}

static Bool
DeviceControl(DeviceIntPtr dev, int mode)
{
    Bool RetValue;

    switch (mode) {
    case DEVICE_INIT:
	RetValue = DeviceInit(dev);
	break;
    case DEVICE_ON:
	RetValue = DeviceOn(dev);
	break;
    case DEVICE_OFF:
	RetValue = DeviceOff(dev);
	break;
    case DEVICE_CLOSE:
	RetValue = DeviceClose(dev);
	break;
    default:
	RetValue = BadValue;
    }

    return RetValue;
}

static Bool
DeviceOn(DeviceIntPtr dev)
{
    LocalDevicePtr local = (LocalDevicePtr) dev->public.devicePrivate;
    SynapticsPrivate *priv = (SynapticsPrivate *) (local->private);

    DBG(3, ErrorF("Synaptics DeviceOn called\n"));

    SetDeviceAndProtocol(local);
    local->fd = xf86OpenSerial(local->options);
    if (local->fd == -1) {
	xf86Msg(X_WARNING, "%s: cannot open input device\n", local->name);
	return !Success;
    }

    priv->proto_ops->DeviceOnHook(local, priv->synpara);

    priv->comm.buffer = XisbNew(local->fd, 64);
    if (!priv->comm.buffer) {
	xf86CloseSerial(local->fd);
	local->fd = -1;
	return !Success;
    }

    xf86FlushInput(local->fd);

    /* reinit the pad */
    QueryHardware(local);
    xf86AddEnabledDevice(local);
    dev->public.on = TRUE;

    return Success;
}

static Bool
DeviceOff(DeviceIntPtr dev)
{
    LocalDevicePtr local = (LocalDevicePtr) dev->public.devicePrivate;
    SynapticsPrivate *priv = (SynapticsPrivate *) (local->private);

    DBG(3, ErrorF("Synaptics DeviceOff called\n"));

    if (local->fd != -1) {
	TimerFree(priv->timer);
	priv->timer = NULL;
	xf86RemoveEnabledDevice(local);
	priv->proto_ops->DeviceOffHook(local);
	if (priv->comm.buffer) {
	    XisbFree(priv->comm.buffer);
	    priv->comm.buffer = NULL;
	}
	xf86CloseSerial(local->fd);
    }
    dev->public.on = FALSE;
    return Success;
}

static Bool
DeviceClose(DeviceIntPtr dev)
{
    Bool RetValue;
    LocalDevicePtr local = (LocalDevicePtr) dev->public.devicePrivate;
    SynapticsPrivate *priv = (SynapticsPrivate *) local->private;

    RetValue = DeviceOff(dev);
    free_param_data(priv);
    return RetValue;
}

static Bool
DeviceInit(DeviceIntPtr dev)
{
    LocalDevicePtr local = (LocalDevicePtr) dev->public.devicePrivate;
    SynapticsPrivate *priv = (SynapticsPrivate *) (local->private);
    unsigned char map[SYN_MAX_BUTTONS + 1];
    int i;

    DBG(3, ErrorF("Synaptics DeviceInit called\n"));

    for (i = 0; i <= SYN_MAX_BUTTONS; i++)
	map[i] = i;

    dev->public.on = FALSE;

    InitPointerDeviceStruct((DevicePtr)dev, map,
			    SYN_MAX_BUTTONS,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
			    miPointerGetMotionEvents,
#elif GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 3
			    GetMotionHistory,
#endif
			    SynapticsCtrl,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
			    miPointerGetMotionBufferSize()
#else
			    GetMotionHistorySize(), 2
#endif
			    );
    /* X valuator */
    if (priv->minx < priv->maxx)
	xf86InitValuatorAxisStruct(dev, 0, priv->minx, priv->maxx, 1, 0, 1);
    else
	xf86InitValuatorAxisStruct(dev, 0, 0, -1, 1, 0, 1);
    xf86InitValuatorDefaults(dev, 0);
    /* Y valuator */
    if (priv->miny < priv->maxy)
	xf86InitValuatorAxisStruct(dev, 1, priv->miny, priv->maxy, 1, 0, 1);
    else
	xf86InitValuatorAxisStruct(dev, 1, 0, -1, 1, 0, 1);
    xf86InitValuatorDefaults(dev, 1);

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
    xf86MotionHistoryAllocate(local);
#endif

    if (!alloc_param_data(local))
	return !Success;

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 3
    InitDeviceProperties(local);
    XIRegisterPropertyHandler(local->dev, SetProperty, NULL, NULL);
#endif

    return Success;
}

static int
move_distance(int dx, int dy)
{
    return sqrt(SQR(dx) + SQR(dy));
}

/*
 * Convert from absolute X/Y coordinates to a coordinate system where
 * -1 corresponds to the left/upper edge and +1 corresponds to the
 * right/lower edge.
 */
static void
relative_coords(SynapticsPrivate *priv, int x, int y,
		double *relX, double *relY)
{
    int minX = priv->synpara->left_edge;
    int maxX = priv->synpara->right_edge;
    int minY = priv->synpara->top_edge;
    int maxY = priv->synpara->bottom_edge;
    double xCenter = (minX + maxX) / 2.0;
    double yCenter = (minY + maxY) / 2.0;

    if ((maxX - xCenter > 0) && (maxY - yCenter > 0)) {
	*relX = (x - xCenter) / (maxX - xCenter);
	*relY = (y - yCenter) / (maxY - yCenter);
    } else {
	*relX = 0;
	*relY = 0;
    }
}

/* return angle of point relative to center */
static double
angle(SynapticsPrivate *priv, int x, int y)
{
    double xCenter = (priv->synpara->left_edge + priv->synpara->right_edge) / 2.0;
    double yCenter = (priv->synpara->top_edge + priv->synpara->bottom_edge) / 2.0;

    return atan2(-(y - yCenter), x - xCenter);
}

/* return angle difference */
static double
diffa(double a1, double a2)
{
    double da = fmod(a2 - a1, 2 * M_PI);
    if (da < 0)
	da += 2 * M_PI;
    if (da > M_PI)
	da -= 2 * M_PI;
    return da;
}

static edge_type
circular_edge_detection(SynapticsPrivate *priv, int x, int y)
{
    edge_type edge = 0;
    double relX, relY, relR;

    relative_coords(priv, x, y, &relX, &relY);
    relR = SQR(relX) + SQR(relY);

    if (relR > 1) {
	/* we are outside the ellipse enclosed by the edge parameters */
	if (relX > M_SQRT1_2)
	    edge |= RIGHT_EDGE;
	else if (relX < -M_SQRT1_2)
	    edge |= LEFT_EDGE;

	if (relY < -M_SQRT1_2)
	    edge |= TOP_EDGE;
	else if (relY > M_SQRT1_2)
	    edge |= BOTTOM_EDGE;
    }

    return edge;
}

static edge_type
edge_detection(SynapticsPrivate *priv, int x, int y)
{
    edge_type edge = 0;

    if (priv->synpara->circular_pad)
	return circular_edge_detection(priv, x, y);

    if (x > priv->synpara->right_edge)
	edge |= RIGHT_EDGE;
    else if (x < priv->synpara->left_edge)
	edge |= LEFT_EDGE;

    if (y < priv->synpara->top_edge)
	edge |= TOP_EDGE;
    else if (y > priv->synpara->bottom_edge)
	edge |= BOTTOM_EDGE;

    return edge;
}

static CARD32
timerFunc(OsTimerPtr timer, CARD32 now, pointer arg)
{
    LocalDevicePtr local = (LocalDevicePtr) (arg);
    SynapticsPrivate *priv = (SynapticsPrivate *) (local->private);
    struct SynapticsHwState hw;
    int delay;
    int sigstate;
    CARD32 wakeUpTime;

    sigstate = xf86BlockSIGIO();

    hw = priv->hwState;
    hw.guest_dx = hw.guest_dy = 0;
    hw.millis = now;
    delay = HandleState(local, &hw);

    /*
     * Workaround for wraparound bug in the TimerSet function. This bug is already
     * fixed in CVS, but this driver needs to work with XFree86 versions 4.2.x and
     * 4.3.x too.
     */
    wakeUpTime = now + delay;
    if (wakeUpTime <= now)
	wakeUpTime = 0xffffffffL;

    priv->timer = TimerSet(priv->timer, TimerAbsolute, wakeUpTime, timerFunc, local);

    xf86UnblockSIGIO(sigstate);

    return 0;
}

static int
clamp(int val, int min, int max)
{
    if (val < min)
	return min;
    else if (val < max)
	return val;
    else
	return max;
}

static Bool
SynapticsGetHwState(LocalDevicePtr local, SynapticsPrivate *priv,
		    struct SynapticsHwState *hw)
{
    return priv->proto_ops->ReadHwState(local, &priv->synhw, priv->proto_ops,
					&priv->comm, hw);
}

/*
 *  called for each full received packet from the touchpad
 */
static void
ReadInput(LocalDevicePtr local)
{
    SynapticsPrivate *priv = (SynapticsPrivate *) (local->private);
    struct SynapticsHwState hw;
    int delay = 0;
    Bool newDelay = FALSE;

    while (SynapticsGetHwState(local, priv, &hw)) {
	hw.millis = GetTimeInMillis();
	priv->hwState = hw;
	delay = HandleState(local, &hw);
	newDelay = TRUE;
    }

    if (newDelay)
	priv->timer = TimerSet(priv->timer, 0, delay, timerFunc, local);
}

static int
HandleMidButtonEmulation(SynapticsPrivate *priv, struct SynapticsHwState *hw, int *delay)
{
    SynapticsSHM *para = priv->synpara;
    Bool done = FALSE;
    int timeleft;
    int mid = 0;

    while (!done) {
	switch (priv->mid_emu_state) {
	case MBE_LEFT_CLICK:
	case MBE_RIGHT_CLICK:
	case MBE_OFF:
	    priv->button_delay_millis = hw->millis;
	    if (hw->left) {
		priv->mid_emu_state = MBE_LEFT;
	    } else if (hw->right) {
		priv->mid_emu_state = MBE_RIGHT;
	    } else {
		done = TRUE;
	    }
	    break;
	case MBE_LEFT:
	    timeleft = TIME_DIFF(priv->button_delay_millis + para->emulate_mid_button_time,
				 hw->millis);
	    if (timeleft > 0)
		*delay = MIN(*delay, timeleft);

            /* timeout, but within the same ReadInput cycle! */
            if ((timeleft <= 0) && !hw->left) {
		priv->mid_emu_state = MBE_LEFT_CLICK;
		done = TRUE;
            } else if ((!hw->left) || (timeleft <= 0)) {
		hw->left = TRUE;
		priv->mid_emu_state = MBE_TIMEOUT;
		done = TRUE;
	    } else if (hw->right) {
		priv->mid_emu_state = MBE_MID;
	    } else {
		hw->left = FALSE;
		done = TRUE;
	    }
	    break;
	case MBE_RIGHT:
	    timeleft = TIME_DIFF(priv->button_delay_millis + para->emulate_mid_button_time,
				 hw->millis);
	    if (timeleft > 0)
		*delay = MIN(*delay, timeleft);

	     /* timeout, but within the same ReadInput cycle! */
            if ((timeleft <= 0) && !hw->right) {
		priv->mid_emu_state = MBE_RIGHT_CLICK;
		done = TRUE;
            } else if (!hw->right || (timeleft <= 0)) {
		hw->right = TRUE;
		priv->mid_emu_state = MBE_TIMEOUT;
		done = TRUE;
	    } else if (hw->left) {
		priv->mid_emu_state = MBE_MID;
	    } else {
		hw->right = FALSE;
		done = TRUE;
	    }
	    break;
	case MBE_MID:
	    if (!hw->left && !hw->right) {
		priv->mid_emu_state = MBE_OFF;
	    } else {
		mid = TRUE;
		hw->left = hw->right = FALSE;
		done = TRUE;
	    }
	    break;
	case MBE_TIMEOUT:
	    if (!hw->left && !hw->right) {
		priv->mid_emu_state = MBE_OFF;
	    } else {
		done = TRUE;
	    }
	}
    }
    return mid;
}

static int
SynapticsDetectFinger(SynapticsPrivate *priv, struct SynapticsHwState *hw)
{
    SynapticsSHM *para = priv->synpara;
    int finger;

    /* finger detection thru pressure and threshold */
    finger = ((hw->z > para->finger_press) && priv->finger_state < FS_PRESSED) ? FS_PRESSED
	: ((hw->z > para->finger_high) && priv->finger_state < FS_TOUCHED) ? FS_TOUCHED
	: ((hw->z < para->finger_low)  &&  priv->finger_state > FS_UNTOUCHED) ? FS_UNTOUCHED
	: priv->finger_state;

    if (!para->palm_detect)
	return finger;

    /* palm detection */
    if (finger) {
	if ((hw->z > para->palm_min_z) && (hw->fingerWidth > para->palm_min_width))
	    priv->palm = TRUE;
    } else {
	priv->palm = FALSE;
    }
    if (hw->x == 0)
	priv->avg_width = 0;
    else
	priv->avg_width += (hw->fingerWidth - priv->avg_width + 1) / 2;
    if (finger && !priv->finger_state) {
	int safe_width = MAX(hw->fingerWidth, priv->avg_width);
	if (hw->numFingers > 1)
	    finger = TRUE;			/* more than one finger -> not a palm */
	else if ((safe_width < 6) && (priv->prev_z < para->finger_high))
	    finger = TRUE;			/* thin finger, distinct touch -> not a palm */
	else if ((safe_width < 7) && (priv->prev_z < para->finger_high / 2))
	    finger = TRUE;			/* thin finger, distinct touch -> not a palm */
	else if (hw->z > priv->prev_z + 1)	/* z not stable, may be a palm */
	    finger = FALSE;
	else if (hw->z < priv->prev_z - 5)	/* z not stable, may be a palm */
	    finger = FALSE;
	else if (hw->z > para->palm_min_z)	/* z too large -> probably palm */
	    finger = FALSE;
	else if (hw->fingerWidth > para->palm_min_width) /* finger width too large -> probably palm */
	    finger = FALSE;
    }
    priv->prev_z = hw->z;

    if (priv->palm)
	finger = FALSE;

    return finger;
}

static void
SelectTapButton(SynapticsPrivate *priv, edge_type edge)
{
    TapEvent tap;

    if (priv->synpara->touchpad_off == 2) {
	priv->tap_button = 0;
	return;
    }

    switch (priv->tap_max_fingers) {
    case 1:
    default:
	switch (edge) {
	case RIGHT_TOP_EDGE:
	    DBG(7, ErrorF("right top edge\n"));
	    tap = RT_TAP;
	    break;
	case RIGHT_BOTTOM_EDGE:
	    DBG(7, ErrorF("right bottom edge\n"));
	    tap = RB_TAP;
	    break;
	case LEFT_TOP_EDGE:
	    DBG(7, ErrorF("left top edge\n"));
	    tap = LT_TAP;
	    break;
	case LEFT_BOTTOM_EDGE:
	    DBG(7, ErrorF("left bottom edge\n"));
	    tap = LB_TAP;
	    break;
	default:
	    DBG(7, ErrorF("no edge\n"));
	    tap = F1_TAP;
	    break;
	}
	break;
    case 2:
	DBG(7, ErrorF("two finger tap\n"));
	tap = F2_TAP;
	break;
    case 3:
	DBG(7, ErrorF("three finger tap\n"));
	tap = F3_TAP;
	break;
    }

    priv->tap_button = priv->synpara->tap_action[tap];
    priv->tap_button = clamp(priv->tap_button, 0, SYN_MAX_BUTTONS);
}

static void
SetTapState(SynapticsPrivate *priv, enum TapState tap_state, int millis)
{
    SynapticsSHM *para = priv->synpara;
    DBG(7, ErrorF("SetTapState - %d -> %d (millis:%d)\n", priv->tap_state, tap_state, millis));
    switch (tap_state) {
    case TS_START:
	priv->tap_button_state = TBS_BUTTON_UP;
	priv->tap_max_fingers = 0;
	break;
    case TS_1:
	priv->tap_button_state = TBS_BUTTON_UP;
	break;
    case TS_2A:
	if (para->fast_taps)
	    priv->tap_button_state = TBS_BUTTON_DOWN;
	else
	    priv->tap_button_state = TBS_BUTTON_UP;
	break;
    case TS_2B:
	priv->tap_button_state = TBS_BUTTON_UP;
	break;
    case TS_3:
	priv->tap_button_state = TBS_BUTTON_DOWN;
	break;
    case TS_SINGLETAP:
	if (para->fast_taps)
	    priv->tap_button_state = TBS_BUTTON_UP;
	else
	    priv->tap_button_state = TBS_BUTTON_DOWN;
	priv->touch_on.millis = millis;
	break;
    default:
	break;
    }
    priv->tap_state = tap_state;
}

static void
SetMovingState(SynapticsPrivate *priv, enum MovingState moving_state, int millis)
{
    DBG(7, ErrorF("SetMovingState - %d -> %d center at %d/%d (millis:%d)\n", priv->moving_state,
		  moving_state,priv->hwState.x, priv->hwState.y, millis));

    if (moving_state == MS_TRACKSTICK) {
	priv->trackstick_neutral_x = priv->hwState.x;
	priv->trackstick_neutral_y = priv->hwState.y;
    }
    priv->moving_state = moving_state;
}

static int
GetTimeOut(SynapticsPrivate *priv)
{
    SynapticsSHM *para = priv->synpara;

    switch (priv->tap_state) {
    case TS_1:
    case TS_3:
    case TS_5:
	return para->tap_time;
    case TS_SINGLETAP:
	return para->click_time;
    case TS_2A:
	return para->single_tap_timeout;
    case TS_2B:
	return para->tap_time_2;
    case TS_4:
	return para->locked_drag_time;
    default:
	return -1;			    /* No timeout */
    }
}

static int
HandleTapProcessing(SynapticsPrivate *priv, struct SynapticsHwState *hw,
		    edge_type edge, enum FingerState finger)
{
    SynapticsSHM *para = priv->synpara;
    Bool touch, release, is_timeout, move;
    int timeleft, timeout;
    int delay = 1000000000;

    if (priv->palm)
	return delay;

    touch = finger && !priv->finger_state;
    release = !finger && priv->finger_state;
    move = ((priv->tap_max_fingers <= ((priv->horiz_scroll_twofinger_on || priv->vert_scroll_twofinger_on)? 2 : 1)) &&
	     ((abs(hw->x - priv->touch_on.x) >= para->tap_move) ||
	     (abs(hw->y - priv->touch_on.y) >= para->tap_move)) && finger);

    if (touch) {
	priv->touch_on.x = hw->x;
	priv->touch_on.y = hw->y;
	priv->touch_on.millis = hw->millis;
    } else if (release) {
	priv->touch_on.millis = hw->millis;
    }
    if (hw->z > para->finger_high)
	if (priv->tap_max_fingers < hw->numFingers)
	    priv->tap_max_fingers = hw->numFingers;
    timeout = GetTimeOut(priv);
    timeleft = TIME_DIFF(priv->touch_on.millis + timeout, hw->millis);
    is_timeout = timeleft <= 0;

 restart:
    switch (priv->tap_state) {
    case TS_START:
	if (touch)
	    SetTapState(priv, TS_1, hw->millis);
	break;
    case TS_1:
	if (move) {
	    SetMovingState(priv, MS_TOUCHPAD_RELATIVE, hw->millis);
	    SetTapState(priv, TS_MOVE, hw->millis);
	    goto restart;
	} else if (is_timeout) {
	    if (finger == FS_TOUCHED) {
		SetMovingState(priv, MS_TOUCHPAD_RELATIVE, hw->millis);
	    } else if (finger == FS_PRESSED) {
		SetMovingState(priv, MS_TRACKSTICK, hw->millis);
	    }
	    SetTapState(priv, TS_MOVE, hw->millis);
	    goto restart;
	} else if (release) {
	    SelectTapButton(priv, edge);
	    SetTapState(priv, TS_2A, hw->millis);
	}
	break;
    case TS_MOVE:
	if (move && priv->moving_state == MS_TRACKSTICK) {
	    SetMovingState(priv, MS_TOUCHPAD_RELATIVE, hw->millis);
	}
	if (release) {
	    SetMovingState(priv, MS_FALSE, hw->millis);
	    SetTapState(priv, TS_START, hw->millis);
	}
	break;
    case TS_2A:
	if (touch)
	    SetTapState(priv, TS_3, hw->millis);
	else if (is_timeout)
	    SetTapState(priv, TS_SINGLETAP, hw->millis);
	break;
    case TS_2B:
	if (touch) {
	    SetTapState(priv, TS_3, hw->millis);
	} else if (is_timeout) {
	    SetTapState(priv, TS_START, hw->millis);
	    priv->tap_button_state = TBS_BUTTON_DOWN_UP;
	}
	break;
    case TS_SINGLETAP:
	if (touch)
	    SetTapState(priv, TS_1, hw->millis);
	else if (is_timeout)
	    SetTapState(priv, TS_START, hw->millis);
	break;
    case TS_3:
	if (move) {
	    SetMovingState(priv, MS_TOUCHPAD_RELATIVE, hw->millis);
	    SetTapState(priv, TS_DRAG, hw->millis);
	    goto restart;
	} else if (is_timeout) {
	    if (finger == FS_TOUCHED) {
		SetMovingState(priv, MS_TOUCHPAD_RELATIVE, hw->millis);
	    } else if (finger == FS_PRESSED) {
		SetMovingState(priv, MS_TRACKSTICK, hw->millis);
	    }
	    SetTapState(priv, TS_DRAG, hw->millis);
	    goto restart;
	} else if (release) {
	    SetTapState(priv, TS_2B, hw->millis);
	}
	break;
    case TS_DRAG:
	if (move)
	    SetMovingState(priv, MS_TOUCHPAD_RELATIVE, hw->millis);
	if (release) {
	    SetMovingState(priv, MS_FALSE, hw->millis);
	    if (para->locked_drags) {
		SetTapState(priv, TS_4, hw->millis);
	    } else {
		SetTapState(priv, TS_START, hw->millis);
	    }
	}
	break;
    case TS_4:
	if (is_timeout) {
	    SetTapState(priv, TS_START, hw->millis);
	    goto restart;
	}
	if (touch)
	    SetTapState(priv, TS_5, hw->millis);
	break;
    case TS_5:
	if (is_timeout || move) {
	    SetTapState(priv, TS_DRAG, hw->millis);
	    goto restart;
	} else if (release) {
	    SetMovingState(priv, MS_FALSE, hw->millis);
	    SetTapState(priv, TS_START, hw->millis);
	}
	break;
    }

    timeout = GetTimeOut(priv);
    if (timeout >= 0) {
	timeleft = TIME_DIFF(priv->touch_on.millis + timeout, hw->millis);
	delay = clamp(timeleft, 1, delay);
    }
    return delay;
}

#define HIST(a) (priv->move_hist[((priv->hist_index - (a) + SYNAPTICS_MOVE_HISTORY) % SYNAPTICS_MOVE_HISTORY)])

static void
store_history(SynapticsPrivate *priv, int x, int y, unsigned int millis)
{
    int idx = (priv->hist_index + 1) % SYNAPTICS_MOVE_HISTORY;
    priv->move_hist[idx].x = x;
    priv->move_hist[idx].y = y;
    priv->move_hist[idx].millis = millis;
    priv->hist_index = idx;
}

/*
 * Estimate the slope for the data sequence [x3, x2, x1, x0] by using
 * linear regression to fit a line to the data and use the slope of the
 * line.
 */
static double
estimate_delta(double x0, double x1, double x2, double x3)
{
    return x0 * 0.3 + x1 * 0.1 - x2 * 0.1 - x3 * 0.3;
}

static int
ComputeDeltas(SynapticsPrivate *priv, struct SynapticsHwState *hw,
	      edge_type edge, int *dxP, int *dyP)
{
    SynapticsSHM *para = priv->synpara;
    enum MovingState moving_state;
    int dist;
    double dx, dy;
    double speed, integral;
    int delay = 1000000000;

    dx = dy = 0;

    moving_state = priv->moving_state;
    if (moving_state == MS_FALSE) {
	switch (priv->tap_state) {
	case TS_MOVE:
	case TS_DRAG:
	    moving_state = MS_TOUCHPAD_RELATIVE;
	    break;
	case TS_1:
	case TS_3:
	case TS_5:
	    if (hw->numFingers == 1)
		moving_state = MS_TOUCHPAD_RELATIVE;
	    break;
	default:
	    break;
	}
    }
    if (moving_state && !priv->palm &&
	!priv->vert_scroll_edge_on && !priv->horiz_scroll_edge_on &&
	!priv->vert_scroll_twofinger_on && !priv->horiz_scroll_twofinger_on &&
	!priv->circ_scroll_on) {
	delay = MIN(delay, 13);
	if (priv->count_packet_finger > 3) { /* min. 3 packets */
	    double tmpf;
	    int x_edge_speed = 0;
	    int y_edge_speed = 0;
	    double dtime = (hw->millis - HIST(0).millis) / 1000.0;

	    if (priv->moving_state == MS_TRACKSTICK) {
		dx = (hw->x - priv->trackstick_neutral_x);
		dy = (hw->y - priv->trackstick_neutral_y);

		dx = dx * dtime * para->trackstick_speed;
		dy = dy * dtime * para->trackstick_speed;
	    } else if (moving_state == MS_TOUCHPAD_RELATIVE) {
		dx = estimate_delta(hw->x, HIST(0).x, HIST(1).x, HIST(2).x);
		dy = estimate_delta(hw->y, HIST(0).y, HIST(1).y, HIST(2).y);

		if ((priv->tap_state == TS_DRAG) || para->edge_motion_use_always) {
		    int minZ = para->edge_motion_min_z;
		    int maxZ = para->edge_motion_max_z;
		    int minSpd = para->edge_motion_min_speed;
		    int maxSpd = para->edge_motion_max_speed;
		    int edge_speed;

		    if (hw->z <= minZ) {
			edge_speed = minSpd;
		    } else if (hw->z >= maxZ) {
			edge_speed = maxSpd;
		    } else {
			edge_speed = minSpd + (hw->z - minZ) * (maxSpd - minSpd) / (maxZ - minZ);
		    }
		    if (!priv->synpara->circular_pad) {
			/* on rectangular pad */
			if (edge & RIGHT_EDGE) {
			    x_edge_speed = edge_speed;
			} else if (edge & LEFT_EDGE) {
			    x_edge_speed = -edge_speed;
			}
			if (edge & TOP_EDGE) {
			    y_edge_speed = -edge_speed;
			} else if (edge & BOTTOM_EDGE) {
			    y_edge_speed = edge_speed;
			}
		    } else if (edge) {
			/* at edge of circular pad */
			double relX, relY;

			relative_coords(priv, hw->x, hw->y, &relX, &relY);
			x_edge_speed = (int)(edge_speed * relX);
			y_edge_speed = (int)(edge_speed * relY);
		    }
		}
	    }

	    /* speed depending on distance/packet */
	    dist = move_distance(dx, dy);
	    speed = dist * para->accl;
	    if (speed > para->max_speed) {  /* set max speed factor */
		speed = para->max_speed;
	    } else if (speed < para->min_speed) { /* set min speed factor */
		speed = para->min_speed;
	    }

	    /* modify speed according to pressure */
	    if (priv->moving_state == MS_TOUCHPAD_RELATIVE) {
		int minZ = para->press_motion_min_z;
		int maxZ = para->press_motion_max_z;
		double minFctr = para->press_motion_min_factor;
		double maxFctr = para->press_motion_max_factor;

		if (hw->z <= minZ) {
		    speed *= minFctr;
		} else if (hw->z >= maxZ) {
		    speed *= maxFctr;
		} else {
		    speed *= minFctr + (hw->z - minZ) * (maxFctr - minFctr) / (maxZ - minZ);
		}
	    }

	    /* save the fraction, report the integer part */
	    tmpf = dx * speed + x_edge_speed * dtime + priv->frac_x;
	    priv->frac_x = modf(tmpf, &integral);
	    dx = integral;
	    tmpf = dy * speed + y_edge_speed * dtime + priv->frac_y;
	    priv->frac_y = modf(tmpf, &integral);
	    dy = integral;
	}

	priv->count_packet_finger++;
    } else {				    /* reset packet counter */
	priv->count_packet_finger = 0;
    }

    /* Add guest device movements */
    if (!para->guestmouse_off) {
	dx += hw->guest_dx;
	dy += hw->guest_dy;
    }

    *dxP = dx;
    *dyP = dy;

    /* generate a history of the absolute positions */
    store_history(priv, hw->x, hw->y, hw->millis);

    return delay;
}

struct ScrollData {
    int left, right, up, down;
};

static void
start_coasting(SynapticsPrivate *priv, struct SynapticsHwState *hw, edge_type edge,
	       Bool vertical)
{
    SynapticsSHM *para = priv->synpara;

    priv->autoscroll_y = 0.0;
    priv->autoscroll_x = 0.0;

    if ((priv->scroll_packet_count > 3) && (para->coasting_speed > 0.0)) {
	double pkt_time = (HIST(0).millis - HIST(3).millis) / 1000.0;
	if (vertical) {
	    double dy = estimate_delta(HIST(0).y, HIST(1).y, HIST(2).y, HIST(3).y);
	    int sdelta = para->scroll_dist_vert;
	    if ((edge & RIGHT_EDGE) && pkt_time > 0 && sdelta > 0) {
		double scrolls_per_sec = dy / pkt_time / sdelta;
		if (fabs(scrolls_per_sec) >= para->coasting_speed) {
		    priv->autoscroll_yspd = scrolls_per_sec;
		    priv->autoscroll_y = (hw->y - priv->scroll_y) / (double)sdelta;
		}
	    }
	} else {
	    double dx = estimate_delta(HIST(0).x, HIST(1).x, HIST(2).x, HIST(3).x);
	    int sdelta = para->scroll_dist_horiz;
	    if ((edge & BOTTOM_EDGE) && pkt_time > 0 && sdelta > 0) {
		double scrolls_per_sec = dx / pkt_time / sdelta;
		if (fabs(scrolls_per_sec) >= para->coasting_speed) {
		    priv->autoscroll_xspd = scrolls_per_sec;
		    priv->autoscroll_x = (hw->x - priv->scroll_x) / (double)sdelta;
		}
	    }
	}
    }
    priv->scroll_packet_count = 0;
}

static void
stop_coasting(SynapticsPrivate *priv)
{
    priv->autoscroll_xspd = 0;
    priv->autoscroll_yspd = 0;
    priv->scroll_packet_count = 0;
}

static int
HandleScrolling(SynapticsPrivate *priv, struct SynapticsHwState *hw,
		edge_type edge, Bool finger, struct ScrollData *sd)
{
    SynapticsSHM *para = priv->synpara;
    int delay = 1000000000;

    sd->left = sd->right = sd->up = sd->down = 0;

    if (priv->synpara->touchpad_off == 2) {
	stop_coasting(priv);
	priv->circ_scroll_on = FALSE;
	priv->vert_scroll_edge_on = FALSE;
	priv->horiz_scroll_edge_on = FALSE;
	priv->vert_scroll_twofinger_on = FALSE;
	priv->horiz_scroll_twofinger_on = FALSE;
	return delay;
    }

    /* scroll detection */
    if (finger && !priv->finger_state) {
	stop_coasting(priv);
	if (para->circular_scrolling) {
	    if ((para->circular_trigger == 0 && edge) ||
		(para->circular_trigger == 1 && edge & TOP_EDGE) ||
		(para->circular_trigger == 2 && edge & TOP_EDGE && edge & RIGHT_EDGE) ||
		(para->circular_trigger == 3 && edge & RIGHT_EDGE) ||
		(para->circular_trigger == 4 && edge & RIGHT_EDGE && edge & BOTTOM_EDGE) ||
		(para->circular_trigger == 5 && edge & BOTTOM_EDGE) ||
		(para->circular_trigger == 6 && edge & BOTTOM_EDGE && edge & LEFT_EDGE) ||
		(para->circular_trigger == 7 && edge & LEFT_EDGE) ||
		(para->circular_trigger == 8 && edge & LEFT_EDGE && edge & TOP_EDGE)) {
		priv->circ_scroll_on = TRUE;
		priv->circ_scroll_vert = TRUE;
		priv->scroll_a = angle(priv, hw->x, hw->y);
		DBG(7, ErrorF("circular scroll detected on edge\n"));
	    }
	}
    }
    if (!priv->circ_scroll_on) {
	if (finger) {
	    if (hw->numFingers == 2) {
		if (!priv->vert_scroll_twofinger_on &&
		    (para->scroll_twofinger_vert) && (para->scroll_dist_vert != 0)) {
		    priv->vert_scroll_twofinger_on = TRUE;
		    priv->vert_scroll_edge_on = FALSE;
		    priv->scroll_y = hw->y;
		    DBG(7, ErrorF("vert two-finger scroll detected\n"));
		}
		if (!priv->horiz_scroll_twofinger_on &&
		    (para->scroll_twofinger_horiz) && (para->scroll_dist_horiz != 0)) {
		    priv->horiz_scroll_twofinger_on = TRUE;
		    priv->horiz_scroll_edge_on = FALSE;
		    priv->scroll_x = hw->x;
		    DBG(7, ErrorF("horiz two-finger scroll detected\n"));
		}
	    }
	}
	if (finger && !priv->finger_state) {
	    if (!priv->vert_scroll_twofinger_on && !priv->horiz_scroll_twofinger_on) {
		if ((para->scroll_edge_vert) && (para->scroll_dist_vert != 0) &&
		    (edge & RIGHT_EDGE)) {
		    priv->vert_scroll_edge_on = TRUE;
		    priv->scroll_y = hw->y;
		    DBG(7, ErrorF("vert edge scroll detected on right edge\n"));
		}
		if ((para->scroll_edge_horiz) && (para->scroll_dist_horiz != 0) &&
		    (edge & BOTTOM_EDGE)) {
		    priv->horiz_scroll_edge_on = TRUE;
		    priv->scroll_x = hw->x;
		    DBG(7, ErrorF("horiz edge scroll detected on bottom edge\n"));
		}
	    }
	}
    }
    {
	Bool oldv = priv->vert_scroll_edge_on || (priv->circ_scroll_on && priv->circ_scroll_vert);
	Bool oldh = priv->horiz_scroll_edge_on || (priv->circ_scroll_on && !priv->circ_scroll_vert);
	if (priv->circ_scroll_on && !finger) {
	    /* circular scroll locks in until finger is raised */
	    DBG(7, ErrorF("cicular scroll off\n"));
	    priv->circ_scroll_on = FALSE;
	}

	if (!finger || hw->numFingers < 2) {
	    if (priv->vert_scroll_twofinger_on) {
		DBG(7, ErrorF("vert two-finger scroll off\n"));
		priv->vert_scroll_twofinger_on = FALSE;
	    }
	    if (priv->horiz_scroll_twofinger_on) {
		DBG(7, ErrorF("horiz two-finger scroll off\n"));
		priv->horiz_scroll_twofinger_on = FALSE;
	    }
	}

	if (priv->vert_scroll_edge_on && (!(edge & RIGHT_EDGE) || !finger)) {
	    DBG(7, ErrorF("vert edge scroll off\n"));
	    priv->vert_scroll_edge_on = FALSE;
	}
	if (priv->horiz_scroll_edge_on && (!(edge & BOTTOM_EDGE) || !finger)) {
	    DBG(7, ErrorF("horiz edge scroll off\n"));
	    priv->horiz_scroll_edge_on = FALSE;
	}
	/* If we were corner edge scrolling (coasting),
	 * but no longer in corner or raised a finger, then stop coasting. */
	if (para->scroll_edge_corner && (priv->autoscroll_xspd || priv->autoscroll_yspd)) {
	    Bool is_in_corner =
		((edge & RIGHT_EDGE)  && (edge & (TOP_EDGE | BOTTOM_EDGE))) ||
		((edge & BOTTOM_EDGE) && (edge & (LEFT_EDGE | RIGHT_EDGE))) ;
	    if (!is_in_corner || !finger) {
		DBG(7, ErrorF("corner edge scroll off\n"));
		stop_coasting(priv);
	    }
	}
	/* if we were scrolling, but couldn't corner edge scroll,
	 * and are no longer scrolling, then start coasting */
	if ((oldv || oldh) && !para->scroll_edge_corner &&
	    !(priv->circ_scroll_on || priv->vert_scroll_edge_on ||
	      priv->horiz_scroll_edge_on)) {
	    start_coasting(priv, hw, edge, oldv);
	}
    }

    /* if hitting a corner (top right or bottom right) while vertical
     * scrolling is active, consider starting corner edge scrolling or
     * switching over to circular scrolling smoothly */
    if (priv->vert_scroll_edge_on && !priv->horiz_scroll_edge_on &&
	(edge & RIGHT_EDGE) && (edge & (TOP_EDGE | BOTTOM_EDGE))) {
	if (para->scroll_edge_corner) {
	    if (priv->autoscroll_yspd == 0) {
		/* FYI: We can generate multiple start_coasting requests if
		 * we're in the corner, but we were moving so slowly when we
		 * got here that we didn't actually start coasting. */
		DBG(7, ErrorF("corner edge scroll on\n"));
		start_coasting(priv, hw, edge, TRUE);
	    }
	} else if (para->circular_scrolling) {
	    priv->vert_scroll_edge_on = FALSE;
	    priv->circ_scroll_on = TRUE;
	    priv->circ_scroll_vert = TRUE;
	    priv->scroll_a = angle(priv, hw->x, hw->y);
	    DBG(7, ErrorF("switching to circular scrolling\n"));
	}
    }
    /* Same treatment for horizontal scrolling */
    if (priv->horiz_scroll_edge_on && !priv->vert_scroll_edge_on &&
	(edge & BOTTOM_EDGE) && (edge & (LEFT_EDGE | RIGHT_EDGE))) {
	if (para->scroll_edge_corner) {
	    if (priv->autoscroll_xspd == 0) {
		/* FYI: We can generate multiple start_coasting requests if
		 * we're in the corner, but we were moving so slowly when we
		 * got here that we didn't actually start coasting. */
		DBG(7, ErrorF("corner edge scroll on\n"));
		start_coasting(priv, hw, edge, FALSE);
	    }
	} else if (para->circular_scrolling) {
	    priv->horiz_scroll_edge_on = FALSE;
	    priv->circ_scroll_on = TRUE;
	    priv->circ_scroll_vert = FALSE;
	    priv->scroll_a = angle(priv, hw->x, hw->y);
	    DBG(7, ErrorF("switching to circular scrolling\n"));
	}
    }

    if (priv->vert_scroll_edge_on || priv->horiz_scroll_edge_on ||
	priv->vert_scroll_twofinger_on || priv->horiz_scroll_twofinger_on ||
	priv->circ_scroll_on) {
	priv->scroll_packet_count++;
    }

    if (priv->vert_scroll_edge_on || priv->vert_scroll_twofinger_on) {
	/* + = down, - = up */
	int delta = para->scroll_dist_vert;
	if (delta > 0) {
	    while (hw->y - priv->scroll_y > delta) {
		sd->down++;
		priv->scroll_y += delta;
	    }
	    while (hw->y - priv->scroll_y < -delta) {
		sd->up++;
		priv->scroll_y -= delta;
	    }
	}
    }
    if (priv->horiz_scroll_edge_on || priv->horiz_scroll_twofinger_on) {
	/* + = right, - = left */
	int delta = para->scroll_dist_horiz;
	if (delta > 0) {
	    while (hw->x - priv->scroll_x > delta) {
		sd->right++;
		priv->scroll_x += delta;
	    }
	    while (hw->x - priv->scroll_x < -delta) {
		sd->left++;
		priv->scroll_x -= delta;
	    }
	}
    }
    if (priv->circ_scroll_on) {
	/* + = counter clockwise, - = clockwise */
	double delta = para->scroll_dist_circ;
	if (delta >= 0.005) {
	    while (diffa(priv->scroll_a, angle(priv, hw->x, hw->y)) > delta) {
		if (priv->circ_scroll_vert)
		    sd->up++;
		else
		    sd->right++;
		priv->scroll_a += delta;
		if (priv->scroll_a > M_PI)
		    priv->scroll_a -= 2 * M_PI;
	    }
	    while (diffa(priv->scroll_a, angle(priv, hw->x, hw->y)) < -delta) {
		if (priv->circ_scroll_vert)
		    sd->down++;
		else
		    sd->left++;
		priv->scroll_a -= delta;
		if (priv->scroll_a < -M_PI)
		    priv->scroll_a += 2 * M_PI;
	    }
	}
    }

    if (priv->autoscroll_yspd) {
	double dtime = (hw->millis - HIST(0).millis) / 1000.0;
	priv->autoscroll_y += priv->autoscroll_yspd * dtime;
	delay = MIN(delay, 20);
	while (priv->autoscroll_y > 1.0) {
	    sd->down++;
	    priv->autoscroll_y -= 1.0;
	}
	while (priv->autoscroll_y < -1.0) {
	    sd->up++;
	    priv->autoscroll_y += 1.0;
	}
    }
    if (priv->autoscroll_xspd) {
	double dtime = (hw->millis - HIST(0).millis) / 1000.0;
	priv->autoscroll_x += priv->autoscroll_xspd * dtime;
	delay = MIN(delay, 20);
	while (priv->autoscroll_x > 1.0) {
	    sd->right++;
	    priv->autoscroll_x -= 1.0;
	}
	while (priv->autoscroll_x < -1.0) {
	    sd->left++;
	    priv->autoscroll_x += 1.0;
	}
    }

    return delay;
}

static void
HandleClickWithFingers(SynapticsSHM *para, struct SynapticsHwState *hw)
{
    int action = 0;
    switch(hw->numFingers){
        case 1:
            action = para->click_action[F1_CLICK1];
            break;
        case 2:
            action = para->click_action[F2_CLICK1];
            break;
        case 3:
            action = para->click_action[F3_CLICK1];
            break;
    }
    switch(action){
        case 1:
            hw->left = 1;
            break;
        case 2:
            hw->left = 0;
            hw->middle = 1;
            break;
        case 3:
            hw->left = 0;
            hw->right = 1;
            break;
    }
}


/*
 * React on changes in the hardware state. This function is called every time
 * the hardware state changes. The return value is used to specify how many
 * milliseconds to wait before calling the function again if no state change
 * occurs.
 */
static int
HandleState(LocalDevicePtr local, struct SynapticsHwState *hw)
{
    SynapticsPrivate *priv = (SynapticsPrivate *) (local->private);
    SynapticsSHM *para = priv->synpara;
    int finger;
    int dx, dy, buttons, rep_buttons, id;
    edge_type edge;
    int change;
    struct ScrollData scroll;
    int double_click, repeat_delay;
    int delay = 1000000000;
    int timeleft;
    int i;

    /* update hardware state in shared memory */
    para->x = hw->x;
    para->y = hw->y;
    para->z = hw->z;
    para->numFingers = hw->numFingers;
    para->fingerWidth = hw->fingerWidth;
    para->left = hw->left;
    para->right = hw->right;
    para->up = hw->up;
    para->down = hw->down;
    for (i = 0; i < 8; i++)
	para->multi[i] = hw->multi[i];
    para->middle = hw->middle;
    para->guest_left = hw->guest_left;
    para->guest_mid = hw->guest_mid;
    para->guest_right = hw->guest_right;
    para->guest_dx = hw->guest_dx;
    para->guest_dy = hw->guest_dy;

    /* If touchpad is switched off, we skip the whole thing and return delay */
    if (para->touchpad_off == 1)
	return delay;

    /* Treat the first two multi buttons as up/down for now. */
    hw->up |= hw->multi[0];
    hw->down |= hw->multi[1];

    if (!para->guestmouse_off) {
	hw->left |= hw->guest_left;
	hw->middle |= hw->guest_mid;
	hw->right |= hw->guest_right;
    }

    /* 3rd button emulation */
    hw->middle |= HandleMidButtonEmulation(priv, hw, &delay);

    /* Fingers emulate other buttons */
    if(hw->left && hw->numFingers >= 1){
        HandleClickWithFingers(para, hw);
    }

    /* Two finger emulation */
    if (hw->numFingers == 1 && hw->z >= para->emulate_twofinger_z &&
        hw->fingerWidth >= para->emulate_twofinger_w) {
	hw->numFingers = 2;
    }

    /* Up/Down button scrolling or middle/double click */
    double_click = FALSE;
    if (!para->updown_button_scrolling) {
	if (hw->down) {		/* map down button to middle button */
	    hw->middle = TRUE;
	}

	if (hw->up) {		/* up button generates double click */
	    if (!priv->prev_up)
		double_click = TRUE;
	}
	priv->prev_up = hw->up;

	/* reset up/down button events */
	hw->up = hw->down = FALSE;
    }

    /* Left/right button scrolling, or middle clicks */
    if (!para->leftright_button_scrolling) {
	if (hw->multi[2] || hw->multi[3])
	    hw->middle = TRUE;

	/* reset left/right button events */
	hw->multi[2] = hw->multi[3] = FALSE;
    }

    /*
     * Some touchpads have a scroll wheel region where a very large X
     * coordinate is reported. In this case for eliminate discontinuity,
     * we adjust X and simulate new zone which adjacent to right edge.
     */
    if (hw->x <= XMAX_VALID) {
	if (priv->largest_valid_x < hw->x)
	    priv->largest_valid_x = hw->x;
    } else {
	hw->x = priv->largest_valid_x + 1;
    /*
     * If user didn't set right_edge manualy, auto-adjust to bounds of
     * hardware scroll area.
     */
	if (para->special_scroll_area_right)
	  priv->synpara->right_edge = priv->largest_valid_x;
    }

    edge = edge_detection(priv, hw->x, hw->y);

    finger = SynapticsDetectFinger(priv, hw);

    /* tap and drag detection */
    timeleft = HandleTapProcessing(priv, hw, edge, finger);
    if (timeleft > 0)
	delay = MIN(delay, timeleft);

    timeleft = HandleScrolling(priv, hw, edge, finger, &scroll);
    if (timeleft > 0)
	delay = MIN(delay, timeleft);

    timeleft = ComputeDeltas(priv, hw, edge, &dx, &dy);
    delay = MIN(delay, timeleft);

    rep_buttons = ((para->updown_button_repeat ? 0x18 : 0) |
		   (para->leftright_button_repeat ? 0x60 : 0));

    buttons = ((hw->left     ? 0x01 : 0) |
	       (hw->middle   ? 0x02 : 0) |
	       (hw->right    ? 0x04 : 0) |
	       (hw->up       ? 0x08 : 0) |
	       (hw->down     ? 0x10 : 0) |
	       (hw->multi[2] ? 0x20 : 0) |
	       (hw->multi[3] ? 0x40 : 0));

    if (priv->tap_button > 0) {
	int tap_mask = 1 << (priv->tap_button - 1);
	if (priv->tap_button_state == TBS_BUTTON_DOWN_UP) {
	    if (tap_mask != (priv->lastButtons & tap_mask)) {
		xf86PostButtonEvent(local->dev, FALSE, priv->tap_button, TRUE, 0, 0);
		priv->lastButtons |= tap_mask;
	    }
	    priv->tap_button_state = TBS_BUTTON_UP;
	}
	if (priv->tap_button_state == TBS_BUTTON_DOWN)
	    buttons |= tap_mask;
    }

    /* Post events */
    if (dx || dy)
	xf86PostMotionEvent(local->dev, 0, 0, 2, dx, dy);

    if (priv->mid_emu_state == MBE_LEFT_CLICK)
    {
	xf86PostButtonEvent(local->dev, FALSE, 1, 1, 0, 0);
	xf86PostButtonEvent(local->dev, FALSE, 1, 0, 0, 0);
	priv->mid_emu_state = MBE_OFF;
    } else if (priv->mid_emu_state == MBE_RIGHT_CLICK)
    {
	xf86PostButtonEvent(local->dev, FALSE, 3, 1, 0, 0);
	xf86PostButtonEvent(local->dev, FALSE, 3, 0, 0, 0);
	priv->mid_emu_state = MBE_OFF;
    }

    change = buttons ^ priv->lastButtons;
    while (change) {
	id = ffs(change); /* number of first set bit 1..32 is returned */
	change &= ~(1 << (id - 1));
	xf86PostButtonEvent(local->dev, FALSE, id, (buttons & (1 << (id - 1))), 0, 0);
    }

    while (scroll.up-- > 0) {
	xf86PostButtonEvent(local->dev, FALSE, 4, !hw->up, 0, 0);
	xf86PostButtonEvent(local->dev, FALSE, 4, hw->up, 0, 0);
    }
    while (scroll.down-- > 0) {
	xf86PostButtonEvent(local->dev, FALSE, 5, !hw->down, 0, 0);
	xf86PostButtonEvent(local->dev, FALSE, 5, hw->down, 0, 0);
    }
    while (scroll.left-- > 0) {
	xf86PostButtonEvent(local->dev, FALSE, 6, TRUE, 0, 0);
	xf86PostButtonEvent(local->dev, FALSE, 6, FALSE, 0, 0);
    }
    while (scroll.right-- > 0) {
	xf86PostButtonEvent(local->dev, FALSE, 7, TRUE, 0, 0);
	xf86PostButtonEvent(local->dev, FALSE, 7, FALSE, 0, 0);
    }
    if (double_click) {
	int i;
	for (i = 0; i < 2; i++) {
	    xf86PostButtonEvent(local->dev, FALSE, 1, !hw->left, 0, 0);
	    xf86PostButtonEvent(local->dev, FALSE, 1, hw->left, 0, 0);
	}
    }

    /* Handle auto repeat buttons */
    repeat_delay = clamp(para->scroll_button_repeat, SBR_MIN, SBR_MAX);
    if (((hw->up || hw->down) && para->updown_button_repeat &&
	 para->updown_button_scrolling) ||
	((hw->multi[2] || hw->multi[3]) && para->leftright_button_repeat &&
	 para->leftright_button_scrolling)) {
	priv->repeatButtons = buttons & rep_buttons;
	if (!priv->nextRepeat) {
	    priv->nextRepeat = hw->millis + repeat_delay * 2;
	}
    } else {
	priv->repeatButtons = 0;
	priv->nextRepeat = 0;
    }

    if (priv->repeatButtons) {
	timeleft = TIME_DIFF(priv->nextRepeat, hw->millis);
	if (timeleft > 0)
	    delay = MIN(delay, timeleft);
	if (timeleft <= 0) {
	    int change, id;
	    change = priv->repeatButtons;
	    while (change) {
		id = ffs(change);
		change &= ~(1 << (id - 1));
		xf86PostButtonEvent(local->dev, FALSE, id, FALSE, 0, 0);
		xf86PostButtonEvent(local->dev, FALSE, id, TRUE, 0, 0);
	    }

	    priv->nextRepeat = hw->millis + repeat_delay;
	    delay = MIN(delay, repeat_delay);
	}
    }

    /* Save old values of some state variables */
    priv->finger_state = finger;
    priv->lastButtons = buttons;

    return delay;
}

static int
ControlProc(LocalDevicePtr local, xDeviceCtl * control)
{
    DBG(3, ErrorF("Control Proc called\n"));
    return Success;
}


static void
CloseProc(LocalDevicePtr local)
{
    DBG(3, ErrorF("Close Proc called\n"));
}

static int
SwitchMode(ClientPtr client, DeviceIntPtr dev, int mode)
{
    ErrorF("SwitchMode called\n");
    return Success;
}

static Bool
ConvertProc(LocalDevicePtr local,
	    int first,
	    int num,
	    int v0,
	    int v1,
	    int v2,
	    int v3,
	    int v4,
	    int v5,
	    int *x,
	    int *y)
{
    if (first != 0 || num != 2)
	return FALSE;

    *x = v0;
    *y = v1;

    return TRUE;
}


static void
ReadDevDimensions(LocalDevicePtr local)
{
    SynapticsPrivate *priv = (SynapticsPrivate *) local->private;

    if (priv->proto_ops->ReadDevDimensions)
	priv->proto_ops->ReadDevDimensions(local);
}

static Bool
QueryHardware(LocalDevicePtr local)
{
    SynapticsPrivate *priv = (SynapticsPrivate *) local->private;
    SynapticsSHM *para = priv->synpara;

    priv->comm.protoBufTail = 0;

    if (priv->proto_ops->QueryHardware(local, &priv->synhw)) {
	para->synhw = priv->synhw;
    } else {
	xf86Msg(X_PROBED, "%s: no supported touchpad found\n", local->name);
	priv->proto_ops->DeviceOffHook(local);
    }

    return TRUE;
}

