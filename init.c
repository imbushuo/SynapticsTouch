/*++
    Copyright (c) Microsoft Corporation. All Rights Reserved. 
    Sample code. Dealpoint ID #843729.

    Module Name:

        init.c

    Abstract:

        Contains Synaptics initialization code

    Environment:

        Kernel mode

    Revision History:

--*/

#include "rmiinternal.h"
#include "spb.h"
#include "init.tmh"


#pragma warning(push)
#pragma warning(disable:4242) // Conversion, possible loss of data

//
// The logical values come from the registry and are hence DWORDs but the 
// physical registers are only 8 bits wide so we use the lower 8 bits of the
// logical value.
//
#define LOGICAL_TO_PHYSICAL(LOGICAL_VALUE) ((LOGICAL_VALUE) & 0xff)


NTSTATUS
RmiChangePage(
    IN RMI4_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN int DesiredPage
    )
/*++
 
  Routine Description:

    This utility function changes the current register address page.

  Arguments:

    ControllerContext - A pointer to the current touch controller context
    SpbContext - A pointer to the current i2c context
    DesiredPage - The page the caller expects to be mapped in

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    BYTE page;
    NTSTATUS status;

    //
    // If we're on this page already return success
    //
    if (ControllerContext->CurrentPage == DesiredPage)
    {
        status = STATUS_SUCCESS;
    }
    else
    {
        page = (BYTE) DesiredPage;

        status = SpbWriteDataSynchronously(
            SpbContext,
            RMI4_PAGE_SELECT_ADDRESS,
            &page,
            sizeof(BYTE));

        if (NT_SUCCESS(status))
        {
            ControllerContext->CurrentPage = DesiredPage;
        }
    }

    return status;
}

int
RmiGetFunctionIndex(
    IN RMI4_FUNCTION_DESCRIPTOR* FunctionDescriptors,
    IN int FunctionCount,
    IN int FunctionDesired
    )
/*++
 
  Routine Description:

    Returns the descriptor table index that corresponds to the
    desired RMI function.

  Arguments:

    FunctionDescriptors - A pointer to the touch controllers
    full list of function descriptors
    
    FunctionCount - The count of function descriptors contained
    in the above FunctionDescriptors list

    FunctionDesired - The RMI function number (note they are always
    in hexadecimal the RMI4 specification)

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    UCHAR i;

    for (i=0; i < FunctionCount; i++)
    {
        //
        // Break if we found the index
        //
        if (FunctionDescriptors[i].Number == FunctionDesired)
        {     
             break;
        }
    }

    //
    // Return the count if the index wasn't found
    //
    return i;
}

NTSTATUS
RmiGetFirmwareVersion(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++
 
  Routine Description:

    This function queries the firmware version of the current chip for
    debugging purposes.

  Arguments:

    ControllerContext - A pointer to the current touch controller context
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    int index;
    NTSTATUS status;

    //
    // Find RMI device control function and configure it
    // 
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F01_RMI_DEVICE_CONTROL);

    if (index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Unexpected - RMI Function 01 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not change register page");

        goto exit;
    }

    //
    // Store all F01 query registers, which contain the product ID
    //
    // TODO: Fix transfer size when SPB can support larger I2C 
    //       transactions
    //
    status = SpbReadDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].QueryBase,
        &ControllerContext->F01QueryRegisters,
        sizeof(BYTE) * FIELD_OFFSET(RMI4_F01_QUERY_REGISTERS, ProductID10));

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Error reading RMI F01 Query registers - %!STATUS!",
            status);

        goto exit;
    }

exit:

    return status;
}

VOID
RmiConvertF01ToPhysical(
    IN RMI4_F01_CTRL_REGISTERS_LOGICAL* Logical,
    IN RMI4_F01_CTRL_REGISTERS* Physical
    )
/*++
 
  Routine Description:

    Registry configuration values for F01 must be specified as
    4-byte REG_DWORD values logically, however the chip interprets these
    values as bits or bytes physically. This function converts
    the registry parameters into a structure that can be programmed
    into the controller's memory.

  Arguments:

    Logical - a pointer to the logical settings

    Physical - a pointer to the controller memory-mapped structure

  Return Value:

    None. Function may print warnings in the future when truncating.

--*/
{
    RtlZeroMemory(Physical, sizeof(RMI4_F01_CTRL_REGISTERS));

    //
    // Note that truncation of registry values is possible if 
    // the data was incorrectly provided by the OEM, we may
    // print warning messages in the future.
    // 

    Physical->DeviceControl.SleepMode  = LOGICAL_TO_PHYSICAL(Logical->SleepMode);
    Physical->DeviceControl.NoSleep    = LOGICAL_TO_PHYSICAL(Logical->NoSleep);
    Physical->DeviceControl.ReportRate = LOGICAL_TO_PHYSICAL(Logical->ReportRate);
    Physical->DeviceControl.Configured = LOGICAL_TO_PHYSICAL(Logical->Configured);

    Physical->InterruptEnable = LOGICAL_TO_PHYSICAL(Logical->InterruptEnable);
    Physical->DozeInterval    = LOGICAL_TO_PHYSICAL(Logical->DozeInterval);
    Physical->DozeThreshold   = LOGICAL_TO_PHYSICAL(Logical->DozeThreshold);
    Physical->DozeHoldoff     = LOGICAL_TO_PHYSICAL(Logical->DozeHoldoff);
}

NTSTATUS
RmiReadRegisterDescriptor(
	IN SPB_CONTEXT *Context,
	IN UCHAR Address,
	IN PRMI_REGISTER_DESCRIPTOR Rdesc
)
{
	NTSTATUS Status;

	BYTE size_presence_reg;
	BYTE buf[35];
	int presense_offset = 1;
	BYTE *struct_buf;
	int reg;
	int offset = 0;
	int map_offset = 0;
	int i;
	int b;

	Status = SpbReadDataSynchronously(
		Context,
		Address,
		&size_presence_reg,
		sizeof(BYTE)
	);

	if (!NT_SUCCESS(Status)) goto i2c_read_fail;

	++Address;

	if (size_presence_reg < 0 || size_presence_reg > 35)
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"size_presence_reg has invalid size, either less than 0 or larger than 35");
		Status = STATUS_INVALID_PARAMETER;
		goto exit;
	}

	memset(buf, 0, sizeof(buf));

	/*
	* The presence register contains the size of the register structure
	* and a bitmap which identified which packet registers are present
	* for this particular register type (ie query, control, or data).
	*/
	Status = SpbReadDataSynchronously(
		Context,
		Address,
		buf,
		size_presence_reg
	);
	if (!NT_SUCCESS(Status)) goto i2c_read_fail;
	++Address;

	if (buf[0] == 0) 
	{
		presense_offset = 3;
		Rdesc->StructSize = buf[1] | (buf[2] << 8);
	}
	else 
	{
		Rdesc->StructSize = buf[0];
	}

	for (i = presense_offset; i < size_presence_reg; i++) 
	{
		for (b = 0; b < 8; b++) 
		{
			if (buf[i] & (0x1 << b)) bitmap_set(Rdesc->PresenceMap, map_offset, 1);
			++map_offset;
		}
	}

	Rdesc->NumRegisters = (UINT8) bitmap_weight(Rdesc->PresenceMap, RMI_REG_DESC_PRESENSE_BITS);
	Rdesc->Registers = ExAllocatePoolWithTag(
		NonPagedPoolNx,
		Rdesc->NumRegisters * sizeof(RMI_REGISTER_DESC_ITEM),
		TOUCH_POOL_TAG_F12
	);

	if (Rdesc->Registers == NULL)
	{
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	/*
	* Allocate a temporary buffer to hold the register structure.
	* I'm not using devm_kzalloc here since it will not be retained
	* after exiting this function
	*/
	struct_buf = ExAllocatePoolWithTag(
		NonPagedPoolNx,
		Rdesc->StructSize,
		TOUCH_POOL_TAG_F12
	);

	if (struct_buf == NULL)
	{
		Status = STATUS_INSUFFICIENT_RESOURCES;
		goto exit;
	}

	/*
	* The register structure contains information about every packet
	* register of this type. This includes the size of the packet
	* register and a bitmap of all subpackets contained in the packet
	* register.
	*/
	Status = SpbReadDataSynchronously(
		Context,
		Address,
		struct_buf,
		Rdesc->StructSize
	);

	if (!NT_SUCCESS(Status)) goto free_buffer;

	reg = find_first_bit(Rdesc->PresenceMap, RMI_REG_DESC_PRESENSE_BITS);
	for (i = 0; i < Rdesc->NumRegisters; i++)
	{
		PRMI_REGISTER_DESC_ITEM item = &Rdesc->Registers[i];
		int reg_size = struct_buf[offset];

		++offset;
		if (reg_size == 0) 
		{
			reg_size = struct_buf[offset] |
				(struct_buf[offset + 1] << 8);
			offset += 2;
		}

		if (reg_size == 0) 
		{
			reg_size = struct_buf[offset] |
				(struct_buf[offset + 1] << 8) |
				(struct_buf[offset + 2] << 16) |
				(struct_buf[offset + 3] << 24);
			offset += 4;
		}

		item->Register = (USHORT) reg;
		item->RegisterSize = reg_size;

		map_offset = 0;

		do {
			for (b = 0; b < 7; b++) {
				if (struct_buf[offset] & (0x1 << b))
					bitmap_set(item->SubPacketMap, map_offset, 1);
				++map_offset;
			}
		} while (struct_buf[offset++] & 0x80);

		item->NumSubPackets = (BYTE) bitmap_weight(item->SubPacketMap, RMI_REG_DESC_SUBPACKET_BITS);

		Trace(
			TRACE_LEVEL_INFORMATION,
			TRACE_FLAG_INIT,
			"%s: reg: %d reg size: %ld subpackets: %d\n",
			__func__,
			item->Register, item->RegisterSize, item->NumSubPackets
		);

		reg = find_next_bit(Rdesc->PresenceMap, RMI_REG_DESC_PRESENSE_BITS, reg + 1);
	}

free_buffer:
	ExFreePoolWithTag(
		struct_buf,
		TOUCH_POOL_TAG_F12
	);

exit:
	return Status;

i2c_read_fail:
	Trace(
		TRACE_LEVEL_ERROR,
		TRACE_FLAG_INIT,
		"Failed to read general info register - %!STATUS!",
		Status);
	goto exit;
}

size_t 
RmiRegisterDescriptorCalcSize(
	IN PRMI_REGISTER_DESCRIPTOR Rdesc
)
{
	PRMI_REGISTER_DESC_ITEM item;
	int i;
	size_t size = 0;

	for (i = 0; i < Rdesc->NumRegisters; i++) 
	{
		item = &Rdesc->Registers[i];
		size += item->RegisterSize;
	}
	return size;
}

const PRMI_REGISTER_DESC_ITEM RmiGetRegisterDescItem(
	PRMI_REGISTER_DESCRIPTOR Rdesc, 
	USHORT reg
)
{
	PRMI_REGISTER_DESC_ITEM item;
	int i;

	for (i = 0; i < Rdesc->NumRegisters; i++) 
	{
		item = &Rdesc->Registers[i];
		if (item->Register == reg) return item;
	}

	return NULL;
}

NTSTATUS
RmiConfigureFunctions(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++
 
  Routine Description:

    RMI4 devices such as this Synaptics touch controller are organized
    as collections of logical functions. Discovered functions must be
    configured, which is done in this function (things like sleep 
    timeouts, interrupt enables, report rates, etc.)

  Arguments:

    ControllerContext - A pointer to the current touch controller
    context
    
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    int index;
    NTSTATUS status;

    RMI4_F01_CTRL_REGISTERS controlF01 = {0};

	BYTE queryF12Addr = 0;
	char buf;
	USHORT data_offset = 0;
	PRMI_REGISTER_DESC_ITEM item;

    //
    // Find 2D touch sensor function and configure it
    //
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F12_2D_TOUCHPAD_SENSOR);

    if (index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Unexpected - RMI Function 12 missing");

        status = STATUS_INVALID_DEVICE_STATE;
		goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not change register page");

        goto exit;
    }

	// Retrieve base address for queries
	queryF12Addr = ControllerContext->Descriptors[index].QueryBase;
	status = SpbReadDataSynchronously(
		SpbContext,
		queryF12Addr,
		&buf,
		sizeof(char)
	);

	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Failed to read general info register - %!STATUS!",
			status);
		goto exit;
	}

	++queryF12Addr;

	if (!(buf & BIT(0))) 
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Behavior of F12 without register descriptors is undefined."
		);
		
		status = STATUS_INVALID_PARAMETER;
		goto exit;
	}

	ControllerContext->HasDribble = !!(buf & BIT(3));

	status = RmiReadRegisterDescriptor(
		SpbContext,
		queryF12Addr,
		&ControllerContext->QueryRegDesc
	);

	if (!NT_SUCCESS(status)) {

		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Failed to read the Query Register Descriptor - %!STATUS!",
			status);
		goto exit;
	}
	queryF12Addr += 3;

	status = RmiReadRegisterDescriptor(
		SpbContext,
		queryF12Addr,
		&ControllerContext->ControlRegDesc
	);

	if (!NT_SUCCESS(status)) {

		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Failed to read the Control Register Descriptor - %!STATUS!",
			status);
		goto exit;
	}
	queryF12Addr += 3;

	status = RmiReadRegisterDescriptor(
		SpbContext,
		queryF12Addr,
		&ControllerContext->DataRegDesc
	);

	if (!NT_SUCCESS(status)) {

		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_INIT,
			"Failed to read the Data Register Descriptor - %!STATUS!",
			status);
		goto exit;
	}
	queryF12Addr += 3;
	ControllerContext->PacketSize = RmiRegisterDescriptorCalcSize(
		&ControllerContext->DataRegDesc
	);

	// Skip rmi_f12_read_sensor_tuning for the prototype.

	/*
	* Figure out what data is contained in the data registers. HID devices
	* may have registers defined, but their data is not reported in the
	* HID attention report. Registers which are not reported in the HID
	* attention report check to see if the device is receiving data from
	* HID attention reports.
	*/
	item = RmiGetRegisterDescItem(&ControllerContext->DataRegDesc, 0);
	if (item) data_offset += (USHORT) item->RegisterSize;

	item = RmiGetRegisterDescItem(&ControllerContext->DataRegDesc, 1);
	if (item != NULL)
	{
		ControllerContext->Data1Offset = data_offset;
		ControllerContext->MaxFingers = item->NumSubPackets;
	}
	else
	{
		status = STATUS_INVALID_DEVICE_STATE;
		goto exit;
	}

    //
    // Find 0D capacitive button sensor function and configure it if it exists
    //
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F1A_0D_CAP_BUTTON_SENSOR);

    if (index != ControllerContext->FunctionCount)
    {
        ControllerContext->HasButtons = TRUE;

        //
        // TODO: Get configuration data from registry once Synaptics
        //       provides sane default values. Until then, assume the
        //       object is configured for the desired product scenario
        //       by default.
        //
    }

    //
    // Find RMI device control function and configure it
    // 
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F01_RMI_DEVICE_CONTROL);

    if (index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Unexpected - RMI Function 01 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not change register page");

        goto exit;
    }

    RmiConvertF01ToPhysical(
        &ControllerContext->Config.DeviceSettings,
        &controlF01);	

    //
    // Write settings to controller
    //
    status = SpbWriteDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].ControlBase,
        &controlF01,
        sizeof(controlF01)
        );

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Error writing RMI F01 Ctrl settings - %!STATUS!",
            status);
        goto exit;
    }

    //
    // Note whether the device configuration settings initialized the
    // controller in an operating state, to prevent a double-start from 
    // the D0 entry dispatch routine (TchWakeDevice)
    //
    if (RMI4_F01_DEVICE_CONTROL_SLEEP_MODE_OPERATING ==
        controlF01.DeviceControl.SleepMode)
    {
        ControllerContext->DevicePowerState = PowerDeviceD0;
    }
    else
    {
        ControllerContext->DevicePowerState = PowerDeviceD3;
    }

exit:

    return status;
}

NTSTATUS
RmiBuildFunctionsTable(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++
 
  Routine Description:

    RMI4 devices such as this Synaptics touch controller are organized
    as collections of logical functions. When initially communicating
    with the chip, a driver must build a table of available functions,
    as is done in this routine.

  Arguments:

    ControllerContext - A pointer to the current touch controller context
    
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    UCHAR address;
    int function;
    int page;
    NTSTATUS status;


    //
    // First function is at a fixed address 
    //
    function = 0;
    address = RMI4_FIRST_FUNCTION_ADDRESS;
    page = 0;

    //
    // Discover chip functions one by one
    //
    do
    {
        //
        // Read function descriptor
        //
        status = SpbReadDataSynchronously(
            SpbContext,
            address,
            &ControllerContext->Descriptors[function],
            sizeof(RMI4_FUNCTION_DESCRIPTOR));

        if (!(NT_SUCCESS(status)))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INIT,
                "Error returned from SPB/I2C read attempt %d - %!STATUS!",
                function,
                status);
            goto exit;
        }

        //
        // Function number 0 implies "last function" on this register page,
        // and if this "last function" is the first function on the page, there
        // are no more functions to discover.
        //
        if (ControllerContext->Descriptors[function].Number == 0 &&
            address == RMI4_FIRST_FUNCTION_ADDRESS)
        {
            break;
        }
        //
        // If we've exhausted functions on this page, look for more functoins
        // on the next register page
        //
        else if (ControllerContext->Descriptors[function].Number == 0 &&
            address != RMI4_FIRST_FUNCTION_ADDRESS)
        {
            page++;
            address = RMI4_FIRST_FUNCTION_ADDRESS;

            status = RmiChangePage(
                ControllerContext,
                SpbContext,
                page);

            if (!NT_SUCCESS(status))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    TRACE_FLAG_INIT,
                    "Error attempting to change page - %!STATUS!",
                    status);
                goto exit;
            }
        }
        //
        // Descriptor stored, look for next or terminator
        //
        else
        {
            Trace(
                TRACE_LEVEL_VERBOSE,
                TRACE_FLAG_INIT,
                "Discovered function $%x",
                ControllerContext->Descriptors[function].Number);

            ControllerContext->FunctionOnPage[function] = page;
            function++;
            address = address - sizeof(RMI4_FUNCTION_DESCRIPTOR);
        }

    } while (
        (address > 0) && 
        (function < RMI4_MAX_FUNCTIONS));

    //
    // If we swept the address space without finding an "end function"
    // or maxed-out the total number of functions supported by the 
    // driver, note the error and exit.
    //
    if (function >= RMI4_MAX_FUNCTIONS)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Error, encountered more than %d functions, must extend driver",
            RMI4_MAX_FUNCTIONS);

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }
    if (address <= 0) 
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Error, did not find terminator function 0, address down to %d",
            address);

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    //
    // Note the total number of functions that exist
    //
    ControllerContext->FunctionCount = function;

    Trace(
        TRACE_LEVEL_VERBOSE,
        TRACE_FLAG_INIT,
        "Discovered %d RMI functions total",
        function);

exit:

    return status;
}

NTSTATUS
RmiCheckInterrupts(
    IN RMI4_CONTROLLER_CONTEXT *ControllerContext,
    IN SPB_CONTEXT *SpbContext,
    IN ULONG* InterruptStatus
    )
/*++
 
  Routine Description:

    This function handles controller interrupts. It currently only
    supports valid touch interrupts. Any other interrupt sources (such as
    device losing configuration or being reset) are unhandled, but noted
    in the controller context.

  Arguments:

    ControllerContext - A pointer to the current touch controller
    context
    
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    RMI4_F01_DATA_REGISTERS data;
    int index;
    NTSTATUS status;

    RtlZeroMemory(&data, sizeof(data));
    *InterruptStatus = 0;

    //
    // Locate RMI data base address
    //
    index = RmiGetFunctionIndex(
        ControllerContext->Descriptors,
        ControllerContext->FunctionCount,
        RMI4_F01_RMI_DEVICE_CONTROL);

    if (index == ControllerContext->FunctionCount)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Unexpected - RMI Function 01 missing");

        status = STATUS_INVALID_DEVICE_STATE;
        goto exit;
    }

    status = RmiChangePage(
        ControllerContext,
        SpbContext,
        ControllerContext->FunctionOnPage[index]);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not change register page");

        goto exit;
    }

    //
    // Read interrupt status registers
    //
    status = SpbReadDataSynchronously(
        SpbContext,
        ControllerContext->Descriptors[index].DataBase,
        &data,
        sizeof(data));

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INTERRUPT,
            "Error reading interrupt status - %!STATUS!",
            status);

        goto exit;
    }

    //
    // Check for catastrophic failures, simply store in context for
    // debugging should these errors occur.
    //
    switch (data.DeviceStatus.Status)
    {
        case RMI4_F01_DATA_STATUS_NO_ERROR:
        {
            break;
        }
        case RMI4_F01_DATA_STATUS_RESET_OCCURRED:
        {
            ControllerContext->ResetOccurred = TRUE;
            break;
        }
        case RMI4_F01_DATA_STATUS_INVALID_CONFIG:
        {
            ControllerContext->InvalidConfiguration = TRUE;

            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Received status code 2 - invalid configuration");

            break;
        }
        case RMI4_F01_DATA_STATUS_DEVICE_FAILURE:
        {
            ControllerContext->DeviceFailure = TRUE;

            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Received status code 4 - device failure");

            break;
        }
        default:
        {
            ControllerContext->UnknownStatus = TRUE;
            ControllerContext->UnknownStatusMessage = data.DeviceStatus.Status;

            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Received unknown status code - %d",
                ControllerContext->UnknownStatusMessage);

            break;
        }
    }

    //
    // If we're in flash programming mode, report an error
    //
    if (data.DeviceStatus.FlashProg)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INTERRUPT,
            "Error, device status indicates chip in programming mode");

        goto exit;
    }

    //
    // If the chip has lost it's configuration, reconfigure
    //
    if (data.DeviceStatus.Unconfigured)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INTERRUPT,
            "Error, device status indicates chip is unconfigured");

        status = RmiConfigureFunctions(
            ControllerContext,
            SpbContext);

        if (!NT_SUCCESS(status))
        {
            Trace(
                TRACE_LEVEL_ERROR,
                TRACE_FLAG_INTERRUPT,
                "Could not reconfigure chip - %!STATUS!",
                status);

            goto exit;
        }

    }

    if (data.InterruptStatus[0])
    {
        *InterruptStatus = data.InterruptStatus[0] & 0xFF;
    }
    else
    {
        Trace(
            TRACE_LEVEL_VERBOSE,
            TRACE_FLAG_INTERRUPT,
            "Unexpected -- no interrupt status bit set");
    }

exit:
    return status;
}

NTSTATUS 
TchStartDevice(
    IN VOID *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++
 
  Routine Description:

    This routine is called in response to the KMDF prepare hardware call
    to initialize the touch controller for use.

  Arguments:

    ControllerContext - A pointer to the current touch controller
    context
    
    SpbContext - A pointer to the current i2c context

  Return Value:

    NTSTATUS indicating success or failure

--*/
{
    RMI4_CONTROLLER_CONTEXT* controller;
    ULONG interruptStatus;
    NTSTATUS status;

    controller = (RMI4_CONTROLLER_CONTEXT*) ControllerContext;
    interruptStatus = 0;
    status = STATUS_SUCCESS;

    //
    // Populate context with RMI function descriptors
    //
    status = RmiBuildFunctionsTable(
        ControllerContext,
        SpbContext);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not build table of RMI functions - %!STATUS!",
            status);
        goto exit;
    }

    //
    // Initialize RMI function control registers
    //
    status = RmiConfigureFunctions(
        ControllerContext,
        SpbContext);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not configure RMI functions - %!STATUS!",
            status);
        goto exit;
    }

    //
    // Read and store the firmware version
    //
    status = RmiGetFirmwareVersion(
        ControllerContext,
        SpbContext);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not get RMI firmware version - %!STATUS!",
            status);
        goto exit;
    }

    //
    // Clear any pending interrupts
    //
    status = RmiCheckInterrupts(
        ControllerContext,
        SpbContext,
        &interruptStatus
        );

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not get interrupt status - %!STATUS!%",
            status);
    }

exit:
    
    return status;
}

NTSTATUS 
TchStopDevice(
    IN VOID *ControllerContext,
    IN SPB_CONTEXT *SpbContext
    )
/*++

Routine Description:

    This routine cleans up the device that is stopped.

Argument:

    ControllerContext - Touch controller context
    
    SpbContext - A pointer to the current i2c context

Return Value:

    NTSTATUS indicating sucess or failure
--*/
{
    RMI4_CONTROLLER_CONTEXT* controller;

    UNREFERENCED_PARAMETER(SpbContext);

    controller = (RMI4_CONTROLLER_CONTEXT*) ControllerContext;

    return STATUS_SUCCESS;
}

NTSTATUS 
TchAllocateContext(
    OUT VOID **ControllerContext,
    IN WDFDEVICE FxDevice
    )
/*++

Routine Description:

    This routine allocates a controller context.

Argument:

    ControllerContext - Touch controller context
    FxDevice - Framework device object

Return Value:

    NTSTATUS indicating sucess or failure
--*/
{
    RMI4_CONTROLLER_CONTEXT* context;
    NTSTATUS status;

    context = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        sizeof(RMI4_CONTROLLER_CONTEXT),
        TOUCH_POOL_TAG);

    if (NULL == context)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not allocate controller context!");

        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    RtlZeroMemory(context, sizeof(RMI4_CONTROLLER_CONTEXT));
    context->FxDevice = FxDevice;

    //
    // Get screen properties and populate context
    //
    TchGetScreenProperties(&context->Props);

    //
    // Allocate a WDFWAITLOCK for guarding access to the
    // controller HW and driver controller context
    //
    status = WdfWaitLockCreate(
        WDF_NO_OBJECT_ATTRIBUTES,
        &context->ControllerLock);

    if (!NT_SUCCESS(status))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_FLAG_INIT,
            "Could not create lock - %!STATUS!",
            status);

        TchFreeContext(context);
        goto exit;

    }

    *ControllerContext = context;

exit:

    return status;
}

NTSTATUS 
TchFreeContext(
    IN VOID *ControllerContext
    )
/*++

Routine Description:

    This routine frees a controller context.

Argument:

    ControllerContext - Touch controller context

Return Value:

    NTSTATUS indicating sucess or failure
--*/
{
    RMI4_CONTROLLER_CONTEXT* controller;

    controller = (RMI4_CONTROLLER_CONTEXT*) ControllerContext;

    if (controller != NULL)
    {

        if (controller->ControllerLock != NULL)
        {
            WdfObjectDelete(controller->ControllerLock);
        }

        ExFreePoolWithTag(controller, TOUCH_POOL_TAG);
    }
    
    return STATUS_SUCCESS;
}

#pragma warning(pop)
