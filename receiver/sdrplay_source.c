/*
 * Wefax receiver: SDRplay source (RSP1A, RSP1B, RSP2, RSPdx, RSPdx-R2).
 *
 * The receiver samples at 2.016 MS/s and decimates by 8 itself: 252 kS/s, 20 kHz below the
 * facsimile channel.
 *
 * The sequence of calls to the SDRplay API is the one of the Navtex receiver, which comes from
 * Navtex by bartelvdh (MIT licence, see LICENSE-MIT-original); the choice by serial number and
 * the rest are new.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2024 bartelvdh
 * Copyright (c) 2026 Manuel - EA7JRS
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "iq_source.h"

#ifdef HAVE_LIBSDRPLAY_API
#include "sdrplay_api.h"

#define DECIMATION 8
#define DEVICE_RATE ((double) IQ_SAMPLE_RATE * DECIMATION)

static sdrplay_api_DeviceT *chosen = NULL;

static void stream_a(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int n, unsigned int reset, void *ctx)
{
    (void) params; (void) reset; (void) ctx;
    if (!stop_requested) iq_push_short(xi, xq, n);
}

static void stream_b(short *xi, short *xq, sdrplay_api_StreamCbParamsT *params, unsigned int n, unsigned int reset, void *ctx)
{
    (void) xi; (void) xq; (void) params; (void) n; (void) reset; (void) ctx;
}

static void on_event(sdrplay_api_EventT id, sdrplay_api_TunerSelectT tuner, sdrplay_api_EventParamsT *params, void *ctx)
{
    (void) ctx;
    switch (id) {
    case sdrplay_api_PowerOverloadChange:
        printf("sdrplay: overload %s\n", params->powerOverloadParams.powerOverloadChangeType == sdrplay_api_Overload_Detected ? "detected" : "corrected");
        /* the API wants the overload message acknowledged */
        if (chosen != NULL) sdrplay_api_Update(chosen->dev, tuner, sdrplay_api_Update_Ctrl_OverloadMsgAck, sdrplay_api_Update_Ext1_None);
        break;
    case sdrplay_api_DeviceRemoved:
        printf("sdrplay: the receiver was unplugged\n");
        device_lost = 1;
        break;
    default:
        break;
    }
}

static int supported(unsigned char hw)
{
    return hw == SDRPLAY_RSPdx_ID || hw == SDRPLAY_RSPdxR2_ID || hw == SDRPLAY_RSP1A_ID || hw == SDRPLAY_RSP1B_ID || hw == SDRPLAY_RSP2_ID;
}

int sdrplay_source_run(void)
{
    static sdrplay_api_DeviceT devs[6];
    unsigned int ndev = 0, i, pick;
    float ver = 0.0f;
    sdrplay_api_ErrT err;
    sdrplay_api_DeviceParamsT *dp = NULL;
    sdrplay_api_RxChannelParamsT *ch;
    sdrplay_api_CallbackFnsT cb;
    int failed = 1;

    if ((err = sdrplay_api_Open()) != sdrplay_api_Success) {
        printf("sdrplay: cannot open the API (%s). Is its service running?\n", sdrplay_api_GetErrorString(err));
        return 1;
    }
    if (sdrplay_api_ApiVersion(&ver) != sdrplay_api_Success || ver != SDRPLAY_API_VERSION) {
        printf("sdrplay: the API installed (%.2f) is not the one this program was built with (%.2f)\n", ver, SDRPLAY_API_VERSION);
        goto close_api;
    }

    sdrplay_api_LockDeviceApi();
    if ((err = sdrplay_api_GetDevices(devs, &ndev, sizeof(devs) / sizeof(devs[0]))) != sdrplay_api_Success) {
        printf("sdrplay: cannot list the receivers (%s)\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_UnlockDeviceApi();
        goto close_api;
    }
    pick = ndev;
    for (i = 0; i < ndev; i++) {
        printf("sdrplay: receiver %u: serial %s, model %d\n", i, devs[i].SerNo, devs[i].hwVer);
        if (pick == ndev && supported(devs[i].hwVer) &&
            (iq_opt.sdrplay_serial == NULL || iq_opt.sdrplay_serial[0] == '\0' || strcmp(devs[i].SerNo, iq_opt.sdrplay_serial) == 0))
            pick = i;
    }
    if (pick == ndev) {
        printf(ndev == 0 ? "sdrplay: no receiver found (unplugged, or in use by another program)\n" : "sdrplay: none of the receivers found can be used (model or serial number)\n");
        sdrplay_api_UnlockDeviceApi();
        goto close_api;
    }
    chosen = &devs[pick];
    if ((err = sdrplay_api_SelectDevice(chosen)) != sdrplay_api_Success) {
        printf("sdrplay: cannot take the receiver (%s)\n", sdrplay_api_GetErrorString(err));
        sdrplay_api_UnlockDeviceApi();
        chosen = NULL;
        goto close_api;
    }
    sdrplay_api_UnlockDeviceApi();

    if ((err = sdrplay_api_GetDeviceParams(chosen->dev, &dp)) != sdrplay_api_Success || dp == NULL || dp->devParams == NULL || dp->rxChannelA == NULL) {
        printf("sdrplay: cannot read the receiver's settings (%s)\n", sdrplay_api_GetErrorString(err));
        goto release;
    }
    dp->devParams->mode = sdrplay_api_ISOCH;
    dp->devParams->fsFreq.fsHz = DEVICE_RATE;
    if (chosen->hwVer == SDRPLAY_RSPdx_ID || chosen->hwVer == SDRPLAY_RSPdxR2_ID) {
        dp->devParams->rspDxParams.antennaSel = iq_opt.antenna_port == 'B' ? sdrplay_api_RspDx_ANTENNA_B :
                                                iq_opt.antenna_port == 'C' ? sdrplay_api_RspDx_ANTENNA_C : sdrplay_api_RspDx_ANTENNA_A;
        dp->devParams->rspDxParams.rfNotchEnable = 0;
        dp->devParams->rspDxParams.rfDabNotchEnable = 1;
        dp->devParams->rspDxParams.biasTEnable = (unsigned char) iq_opt.bias_t;
    }
    if (chosen->hwVer == SDRPLAY_RSP1A_ID || chosen->hwVer == SDRPLAY_RSP1B_ID) {
        dp->devParams->rsp1aParams.rfNotchEnable = 0;
        dp->devParams->rsp1aParams.rfDabNotchEnable = 1;
        dp->rxChannelA->rsp1aTunerParams.biasTEnable = (unsigned char) iq_opt.bias_t;
    }
    if (chosen->hwVer == SDRPLAY_RSP2_ID) {
        /* HF goes in through the high impedance port */
        dp->rxChannelA->rsp2TunerParams.amPortSel = sdrplay_api_Rsp2_AMPORT_1;
        dp->rxChannelA->rsp2TunerParams.antennaSel = sdrplay_api_Rsp2_ANTENNA_B;
        dp->rxChannelA->rsp2TunerParams.rfNotchEnable = 0;
        dp->rxChannelA->rsp2TunerParams.biasTEnable = 0;
    }
    ch = dp->rxChannelA;
    ch->tunerParams.loMode = sdrplay_api_LO_Auto;
    ch->tunerParams.rfFreq.rfHz = (double) iq_tuned_hz;
    ch->tunerParams.bwType = sdrplay_api_BW_0_200;
    ch->tunerParams.ifType = sdrplay_api_IF_Zero;
    ch->tunerParams.gain.gRdB = 40;
    ch->tunerParams.gain.LNAstate = 2;                    /* HF with a wire antenna: some reduction; the AGC does the rest */
    ch->ctrlParams.decimation.enable = 1;
    ch->ctrlParams.decimation.decimationFactor = DECIMATION;
    ch->ctrlParams.agc.enable = sdrplay_api_AGC_5HZ;

    cb.StreamACbFn = stream_a;
    cb.StreamBCbFn = stream_b;
    cb.EventCbFn = on_event;

    if (iq_ring_init() != 0) { printf("out of memory\n"); goto release; }
    if ((err = sdrplay_api_Init(chosen->dev, &cb, NULL)) != sdrplay_api_Success) {
        printf("sdrplay: cannot start the receiver (%s)\n", sdrplay_api_GetErrorString(err));
        iq_ring_free();
        goto release;
    }
    printf("sdrplay: serial %s tuned to %ld Hz, %d S/s, antenna %c\n", chosen->SerNo, iq_tuned_hz, IQ_SAMPLE_RATE, iq_opt.antenna_port);

    consume_loop();
    failed = device_lost ? 1 : 0;
    if (stop_requested) printf("Stop requested - releasing the receiver\n");
    if (!device_lost) sdrplay_api_Uninit(chosen->dev);
    iq_ring_free();
release:
    sdrplay_api_ReleaseDevice(chosen);
    chosen = NULL;
close_api:
    sdrplay_api_Close();
    return failed;
}
#else
int sdrplay_source_run(void)
{
    printf("This build has no SDRplay support (the SDRplay API was not found when it was compiled)\n");
    return 1;
}
#endif
