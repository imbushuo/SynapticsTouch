/*++
    Copyright (c) Microsoft Corporation. All Rights Reserved. 
    Sample code. Dealpoint ID #843729.

    Module Name:

        trace.h

    Abstract:

        Contains prototypes and definitions related to debugging/tracing

    Environment:

        Kernel mode

    Revision History:

--*/

#pragma once

//
// Control GUID:
// {64BAF936-E94C-4747-91E3-BB4CB8328E5F}
//

#define WPP_CONTROL_GUIDS                        \
    WPP_DEFINE_CONTROL_GUID(                     \
        TouchDriverTraceGuid, (64BAF936,E94C,4747,91E3,BB4CB8328E5F),  \
																	   \
        WPP_DEFINE_BIT(TRACE_INIT)          \
        WPP_DEFINE_BIT(TRACE_REGISTRY)      \
        WPP_DEFINE_BIT(TRACE_HID)           \
        WPP_DEFINE_BIT(TRACE_PNP)           \
        WPP_DEFINE_BIT(TRACE_POWER)         \
        WPP_DEFINE_BIT(TRACE_SPB)           \
        WPP_DEFINE_BIT(TRACE_CONFIG)        \
        WPP_DEFINE_BIT(TRACE_REPORTING)     \
        WPP_DEFINE_BIT(TRACE_INTERRUPT)     \
        WPP_DEFINE_BIT(TRACE_SAMPLES)       \
        WPP_DEFINE_BIT(TRACE_OTHER)         \
        WPP_DEFINE_BIT(TRACE_IDLE)          \
        )

#define WPP_FLAG_LEVEL_LOGGER(flag, level)                                  \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level)                                 \
    (WPP_LEVEL_ENABLED(flag) &&                                             \
     WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) \
           WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
           (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

//           
// WPP orders static parameters before dynamic parameters. To support the Trace function
// defined below which sets FLAGS=MYDRIVER_ALL_INFO, a custom macro must be defined to
// reorder the arguments to what the .tpl configuration file expects.
//
#define WPP_RECORDER_FLAGS_LEVEL_ARGS(flags, lvl) WPP_RECORDER_LEVEL_FLAGS_ARGS(lvl, flags)
#define WPP_RECORDER_FLAGS_LEVEL_FILTER(flags, lvl) WPP_RECORDER_LEVEL_FLAGS_FILTER(lvl, flags)

//
// This comment block is scanned by the trace preprocessor to define our
// Trace function.
//
// begin_wpp config
// FUNC Trace(LEVEL, FLAGS, MSG, ...);
// end_wpp
//
