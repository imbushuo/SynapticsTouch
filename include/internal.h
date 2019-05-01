// Copyright (c) Microsoft Corporation. All Rights Reserved. 
// Copyright (c) Bingxing Wang. All Rights Reserved. 

#pragma once

#include "controller.h"

//
// Device context
//

typedef struct _DEVICE_EXTENSION
{
    //
    // HID Touch input mode (touch vs. mouse)
    // 
    UCHAR InputMode;

    //
    // Device related
    //
    WDFDEVICE FxDevice;
    WDFQUEUE DefaultQueue;
    WDFQUEUE PingPongQueue;

    //
    // Interrupt servicing
    //
    WDFINTERRUPT InterruptObject;
    BOOLEAN ServiceInterruptsAfterD0Entry;
    
    //
    // Spb (I2C) related members used for the lifetime of the device
    //
    SPB_CONTEXT I2CContext;

    //
    // Test related
    //
    WDFQUEUE TestQueue;
    volatile LONG TestSessionRefCnt;
    BOOLEAN DiagnosticMode;

    // 
    // Power related
    //
    WDFQUEUE IdleQueue;

    //
    // Touch related members used for the lifetime of the device
    //
    VOID *TouchContext;

	//
	// PTP New
	//
	BOOLEAN PtpInputOn;
	BOOLEAN PtpReportButton;
	BOOLEAN PtpReportTouch;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, GetDeviceContext)
