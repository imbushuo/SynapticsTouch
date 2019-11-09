// Copyright (c) Microsoft Corporation. All Rights Reserved. 
// Copyright (c) Bingxing Wang. All Rights Reserved. 

#include <compat.h>
#include <controller.h>
#include <rmiinternal.h>
#include <HidCommon.h>
#include <spb.h>
#include <report.tmh>

const USHORT gOEMVendorID = 0x7379;    // "sy"
const USHORT gOEMProductID = 0x726D;    // "rm"
const USHORT gOEMVersionID = 3400;

const PWSTR gpwstrManufacturerID = L"Synaptics";
const PWSTR gpwstrProductID = L"3400";
const PWSTR gpwstrSerialNumber = L"4";

NTSTATUS
RmiGetTouchesFromController(
    IN VOID *ControllerContext,
    IN SPB_CONTEXT *SpbContext,
    IN RMI4_F11_DATA_REGISTERS *Data
    )
/*++

Routine Description:

    This routine reads raw touch messages from hardware. If there is
    no touch data available (if a non-touch interrupt fired), the 
    function will not return success and no touch data was transferred.

Arguments:

    ControllerContext - Touch controller context
    SpbContext - A pointer to the current i2c context
    Data - A pointer to any returned F11 touch data

Return Value:

    NTSTATUS, where only success indicates data was returned

--*/
{
    NTSTATUS status;
    RMI4_CONTROLLER_CONTEXT* controller;

    int index, i, x, y, fingers;

	BYTE fingerStatus[RMI4_MAX_TOUCHES] = { 0 };
	BYTE* data1;
	BYTE* controllerData;

    controller = (RMI4_CONTROLLER_CONTEXT*) ControllerContext;

    //
    // Locate RMI data base address of 2D touch function
    //
    index = RmiGetFunctionIndex(
        controller->Descriptors,
        controller->FunctionCount,
        RMI4_F12_2D_TOUCHPAD_SENSOR);

    if (index == controller->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Unexpected - RMI Function 12 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        controller->FunctionOnPage[index]);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INIT,
            "Could not change register page");

        goto exit;
    }

	controllerData = ExAllocatePoolWithTag(
		NonPagedPoolNx,
		controller->PacketSize,
		TOUCH_POOL_TAG_F12
	);

	if (controllerData == NULL)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	// 
	// Packets we need is determined by context
	//
	status = SpbReadDataSynchronously(
		SpbContext,
		controller->Descriptors[index].DataBase,
		controllerData,
		(ULONG) controller->PacketSize
	);

	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_INTERRUPT,
			"Error reading finger status data - %!STATUS!",
			status);

		goto free_buffer;
	}

	data1 = &controllerData[controller->Data1Offset];
	fingers = 0;

	if (data1 != NULL)
	{
		for (i = 0; i < controller->MaxFingers; i++)
		{
			switch (data1[0]) 
			{
			case RMI_F12_OBJECT_FINGER:
			case RMI_F12_OBJECT_STYLUS:
				fingerStatus[i] = RMI4_FINGER_STATE_PRESENT_WITH_ACCURATE_POS;
				fingers++;
				break;
			default:
				fingerStatus[i] = RMI4_FINGER_STATE_NOT_PRESENT;
				break;
			}

			x = (data1[2] << 8) | data1[1];
			y = (data1[4] << 8) | data1[3];

			Data->Finger[i].X = x;
			Data->Finger[i].Y = y;

			data1 += F12_DATA1_BYTES_PER_OBJ;
		}
	}
	else
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_INTERRUPT,
			"Error reading finger status data - empty buffer"
		);

		goto free_buffer;
	}

	// Synchronize status back
	Data->Status.FingerState0 = fingerStatus[0];
	Data->Status.FingerState1 = fingerStatus[1];
	Data->Status.FingerState2 = fingerStatus[2];
	Data->Status.FingerState3 = fingerStatus[3];
	Data->Status.FingerState4 = fingerStatus[4];
	Data->Status.FingerState5 = fingerStatus[5];
	Data->Status.FingerState6 = fingerStatus[6];
	Data->Status.FingerState7 = fingerStatus[7];
	Data->Status.FingerState8 = fingerStatus[8];
	Data->Status.FingerState9 = fingerStatus[9];

free_buffer:
	ExFreePoolWithTag(
		controllerData,
		TOUCH_POOL_TAG_F12
	);

exit:
    return status;
}

VOID
RmiUpdateLocalFingerCache(
    IN RMI4_F11_DATA_REGISTERS *Data,
    IN RMI4_FINGER_CACHE *Cache
    )
/*++

Routine Description:

    This routine takes raw data reported by the Synaptics hardware and
    parses it to update a local cache of finger states. This routine manages
    removing lifted touches from the cache, and manages a map between the
    order of reported touches in hardware, and the order the driver should
    use in reporting.

Arguments:

    Data - A pointer to the new data returned from hardware
    Cache - A data structure holding various current finger state info

Return Value:

    None.

--*/
{
    int fingerStatus[RMI4_MAX_TOUCHES] = {0};        
    int i, j;

    //
    // Unpack the finger statuses into an array to ease dealing with each
    // finger uniformly in loops
    //
    fingerStatus[0] = Data->Status.FingerState0;
    fingerStatus[1] = Data->Status.FingerState1;
    fingerStatus[2] = Data->Status.FingerState2;
    fingerStatus[3] = Data->Status.FingerState3;
    fingerStatus[4] = Data->Status.FingerState4;
    fingerStatus[5] = Data->Status.FingerState5;
    fingerStatus[6] = Data->Status.FingerState6;
    fingerStatus[7] = Data->Status.FingerState7;
    fingerStatus[8] = Data->Status.FingerState8;
    fingerStatus[9] = Data->Status.FingerState9;

    //
    // When hardware was last read, if any slots reported as lifted, we
    // must clean out the slot and old touch info. There may be new
    // finger data using the slot.
    //
    for (i=0; i<RMI4_MAX_TOUCHES; i++)
    {
        //
        // Sweep for a slot that needs to be cleaned
        //
        if (!(Cache->FingerSlotDirty & (1 << i)))
        {
            continue;
        }

        NT_ASSERT(Cache->FingerDownCount > 0);

        //
        // Find the slot in the reporting list 
        //
        for (j=0; j<RMI4_MAX_TOUCHES; j++)
        {
            if (Cache->FingerDownOrder[j] == i)
            {
                break;
            }
        }

        NT_ASSERT(j != RMI4_MAX_TOUCHES);

        //
        // Remove the slot. If the finger lifted was the last in the list,
        // we just decrement the list total by one. If it was not last, we
        // shift the trailing list items up by one.
        //
        for (; (j<Cache->FingerDownCount-1) && (j<RMI4_MAX_TOUCHES-1); j++)
        {
            Cache->FingerDownOrder[j] = Cache->FingerDownOrder[j+1];
        }
        Cache->FingerDownCount--;

        //
        // Finished, clobber the dirty bit
        //
        Cache->FingerSlotDirty &= ~(1 << i);
    }

    //
    // Cache the new set of finger data reported by hardware
    //
    for (i=0; i<RMI4_MAX_TOUCHES; i++)
    {
        //
        // Take actions when a new contact is first reported as down
        //
        if ((fingerStatus[i] != RMI4_FINGER_STATE_NOT_PRESENT) &&
            ((Cache->FingerSlotValid & (1 << i)) == 0) &&
            (Cache->FingerDownCount < RMI4_MAX_TOUCHES))
        {
            Cache->FingerSlotValid |= (1 << i);
            Cache->FingerDownOrder[Cache->FingerDownCount++] = i;
        }

        //
        // Ignore slots with no new information
        //
        if (!(Cache->FingerSlotValid & (1 << i)))
        {
            continue;   
        }

        //
        // When finger is down, update local cache with new information from
        // the controller. When finger is up, we'll use last cached value
        //
        Cache->FingerSlot[i].fingerStatus = (UCHAR) fingerStatus[i];
        if (Cache->FingerSlot[i].fingerStatus)
        {
            Cache->FingerSlot[i].x = Data->Finger[i].X;
            Cache->FingerSlot[i].y = Data->Finger[i].Y;
        }

        //
        // If a finger lifted, note the slot is now inactive so that any
        // cached data is cleaned out before we read hardware again.
        //
        if (Cache->FingerSlot[i].fingerStatus == RMI4_FINGER_STATE_NOT_PRESENT)
        {
            Cache->FingerSlotDirty |= (1 << i);
            Cache->FingerSlotValid &= ~(1 << i);
        }
    }

    //
    // Get current scan time (in 100us units)
    //
    ULONG64 QpcTimeStamp;
    Cache->ScanTime = KeQueryInterruptTimePrecise(&QpcTimeStamp) / 1000;
}

VOID
RmiFillNextHidReportFromCache(
    IN PPTP_REPORT HidReport,
    IN RMI4_FINGER_CACHE *Cache,
    IN PTOUCH_SCREEN_PROPERTIES Props,
    IN int *TouchesReported,
    IN int TouchesTotal
    )
/*++

Routine Description:

    This routine fills a HID report with the next touch entries in
    the local device finger cache. 

    The routine also adjusts X/Y coordinates to match the desired display
    coordinates.

Arguments:

    HidReport - pointer to the HID report structure to fill
    Cache - pointer to the local device finger cache
    Props - information on how to adjust X/Y coordinates to match the display
    TouchesReported - On entry, the number of touches (against total) that
        have already been reported. As touches are transferred from the local
        device cache to a HID report, this number is incremented.
    TouchesTotal - total number of touches in the touch cache

Return Value:

    None.

--*/
{
    int currentFingerIndex;
	int fingersToReport = min(TouchesTotal - *TouchesReported, 5);
	USHORT SctatchX = 0, ScratchY = 0;

    HidReport->ReportID = REPORTID_MULTITOUCH;

    //
    // There are only 16-bits for ScanTime, truncate it
    //
	HidReport->ScanTime = Cache->ScanTime & 0xFFFF;

	//
	// No button in our context
	// 
	HidReport->IsButtonClicked = FALSE;

    //
    // Report the count
    // We're sending touches using hybrid mode with 5 fingers in our
    // report descriptor. The first report must indicate the
    // total count of touch fingers detected by the digitizer.
    // The remaining reports must indicate 0 for the count.
    // The first report will have the TouchesReported integer set to 0
    // The others will have it set to something else.
    //
    if (*TouchesReported == 0)
    {
        HidReport->ContactCount = (UCHAR)TouchesTotal;
    }
    else
    {
        HidReport->ContactCount = 0;
    }

	//
	// Only five fingers supported yet
	//
	for (currentFingerIndex = 0; currentFingerIndex < fingersToReport; currentFingerIndex++)
	{
        int currentlyReporting = Cache->FingerDownOrder[*TouchesReported];

		HidReport->Contacts[currentFingerIndex].ContactID = (UCHAR)currentlyReporting;
		SctatchX = (USHORT)Cache->FingerSlot[currentlyReporting].x;
		ScratchY = (USHORT)Cache->FingerSlot[currentlyReporting].y;
		HidReport->Contacts[currentFingerIndex].Confidence = 1;

		//
		// Perform per-platform x/y adjustments to controller coordinates
		//
		TchTranslateToDisplayCoordinates(
			&SctatchX,
			&ScratchY,
			Props);

		HidReport->Contacts[currentFingerIndex].X = SctatchX;
		HidReport->Contacts[currentFingerIndex].Y = ScratchY;

		if (Cache->FingerSlot[currentlyReporting].fingerStatus)
		{
			HidReport->Contacts[currentFingerIndex].TipSwitch = FINGER_STATUS;
		}

        (*TouchesReported)++;
	}
}

NTSTATUS
RmiServiceTouchDataInterrupt(
    IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN PPTP_REPORT HidReport,
    IN UCHAR InputMode,
    OUT BOOLEAN* PendingTouches
    )
/*++

Routine Description:

    Called when a touch interrupt needs service.

Arguments:

    ControllerContext - Touch controller context
    SpbContext - A pointer to the current SPB context (I2C, etc)
    HidReport- Buffer to fill with a hid report if touch data is available
    InputMode - Specifies mouse, single-touch, or multi-touch reporting modes
    PendingTouches - Notifies caller if there are more touches to report, to 
        complete reporting the full state of fingers on the screen

Return Value:

    NTSTATUS indicating whether or not the current hid report buffer was filled

    PendingTouches also indicates whether the caller should expect more than
        one request to be completed to indicate the full state of fingers on 
        the screen
--*/
{
    RMI4_F11_DATA_REGISTERS data;
    NTSTATUS status;

	UNREFERENCED_PARAMETER(InputMode);

    status = STATUS_SUCCESS;
    RtlZeroMemory(&data, sizeof(data));
    NT_ASSERT(PendingTouches != NULL);
    *PendingTouches = FALSE;

    //
    // If no touches are unreported in our cache, read the next set of touches
    // from hardware.
    //
    if (ControllerContext->TouchesReported == ControllerContext->TouchesTotal)
    {
        //
        // See if new touch data is available
        //
        status = RmiGetTouchesFromController(
            ControllerContext,
            SpbContext,
            &data
            );

        if (!NT_SUCCESS(status))
        {
            Trace(
               TRACE_LEVEL_VERBOSE,
                TRACE_SAMPLES,
                "No touch data to report - %!STATUS!",
                status);

            goto exit;
        }

        //
        // Process the new touch data by updating our cached state
        //
        //
        RmiUpdateLocalFingerCache(
            &data,
            &ControllerContext->Cache);

        //
        // Prepare to report touches via HID reports
        //
        ControllerContext->TouchesReported = 0;
        ControllerContext->TouchesTotal = 
            ControllerContext->Cache.FingerDownCount;

        //
        // If no touches are present return that no data needed to be reported
        //
        if (ControllerContext->TouchesTotal == 0)
        {
            status = STATUS_NO_DATA_DETECTED;
            goto exit;
        }
    }

    RtlZeroMemory(HidReport, sizeof(PTP_REPORT));

    //
    // Fill report with the next cached touches
    //
    RmiFillNextHidReportFromCache(
        HidReport,
        &ControllerContext->Cache,
        &ControllerContext->Props,
        &ControllerContext->TouchesReported,
        ControllerContext->TouchesTotal);

    //
    // Update the caller if we still have outstanding touches to report
    //
    if (ControllerContext->TouchesReported < ControllerContext->TouchesTotal)
    {
        *PendingTouches = TRUE;
    }
    else
    {
        *PendingTouches = FALSE;
    }

exit:
    
    return status;
}


NTSTATUS
TchServiceInterrupts(
    IN VOID *ControllerContext,
    IN SPB_CONTEXT *SpbContext,
    IN PPTP_REPORT HidReport,
    IN UCHAR InputMode,
    IN BOOLEAN *ServicingComplete
    )
/*++

Routine Description:

    This routine is called in response to an interrupt. The driver will
    service chip interrupts, and if data is available to report to HID,
    fill the Request object buffer with a HID report.

Arguments:

    ControllerContext - Touch controller context
    SpbContext - A pointer to the current i2c context
    HidReport - Pointer to a HID_INPUT_REPORT structure to report to the OS
    InputMode - Specifies mouse, single-touch, or multi-touch reporting modes
    ServicingComplete - Notifies caller if there are more reports needed to 
        complete servicing interrupts coming from the hardware.

Return Value:

    NTSTATUS indicating whether or not the current HidReport has been filled

    ServicingComplete indicates whether or not a new report buffer is required
        to complete interrupt processing.
--*/
{
    NTSTATUS status = STATUS_NO_DATA_DETECTED;
    RMI4_CONTROLLER_CONTEXT* controller;

    controller = (RMI4_CONTROLLER_CONTEXT*) ControllerContext;

    NT_ASSERT(ServicingComplete != NULL);

    //
    // Grab a waitlock to ensure the ISR executes serially and is 
    // protected against power state transitions
    //
    WdfWaitLockAcquire(controller->ControllerLock, NULL);

    //
    // Check the interrupt source if no interrupts are pending processing
    //
    if (controller->InterruptStatus == 0)
    {
        status = RmiCheckInterrupts(
            controller,
            SpbContext, 
            &controller->InterruptStatus);

        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_INTERRUPT,
                "Error servicing interrupts - %!STATUS!",
                status);

            *ServicingComplete = FALSE;
            goto exit;
        }
    }

    //
    // Driver only services 0D cap button and 2D touch messages currently
    //
    if (controller->InterruptStatus & 
        ~(RMI4_INTERRUPT_BIT_0D_CAP_BUTTON | RMI4_INTERRUPT_BIT_2D_TOUCH))
    {
        Trace(
            TRACE_LEVEL_WARNING,
            TRACE_INTERRUPT,
            "Ignoring following interrupt flags - %!STATUS!",
            controller->InterruptStatus & 
                ~(RMI4_INTERRUPT_BIT_0D_CAP_BUTTON | 
                RMI4_INTERRUPT_BIT_2D_TOUCH));

        //
        // Mask away flags we don't service
        //
        controller->InterruptStatus &=
            (RMI4_INTERRUPT_BIT_0D_CAP_BUTTON | 
            RMI4_INTERRUPT_BIT_2D_TOUCH);
    }

    //
    // RmiServiceXXX routine will change status to STATUS_SUCCESS if there
    // is a HID report to process.
    //
    status = STATUS_UNSUCCESSFUL;

    //
    // Service a touch data event if indicated by hardware 
    //
    if (controller->InterruptStatus & RMI4_INTERRUPT_BIT_2D_TOUCH)
    {
        BOOLEAN pendingTouches = FALSE;

        status = RmiServiceTouchDataInterrupt(
			ControllerContext,
            SpbContext,
			HidReport,
			InputMode,
			&pendingTouches);

        //
        // If there are more touches to report, servicing is incomplete
        //
        if (pendingTouches == FALSE)
        {
            controller->InterruptStatus &= ~RMI4_INTERRUPT_BIT_2D_TOUCH;
        }

        //
        // Success indicates the report is ready to be sent, otherwise,
        // continue to service interrupts.
        //
        if (NT_SUCCESS(status))
        {
            goto exit;
        }
        else
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_INTERRUPT,
                "Error processing touch event - %!STATUS!",
                status);
        }
    }

    //
    // Add servicing for additional touch interrupts here
    //

exit:

    //
    // Indicate whether or not we're done servicing interrupts
    //
    if (controller->InterruptStatus == 0)
    {
        *ServicingComplete = TRUE;
    }
    else
    {
        *ServicingComplete = FALSE;
    }

    WdfWaitLockRelease(controller->ControllerLock);

    return status;
}
