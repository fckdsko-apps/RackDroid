//----------------------------------------------------------------------------
//	/^M^\ Count Modula Plugin for VCV Rack - Event timer
//	A count down timer to generate events at a specific moment
//	Copyright (C) 2022  Adam Verspaget
//----------------------------------------------------------------------------
#include "../CountModula.hpp"
#include "../components/CountModulaLEDDisplay.hpp"
#include "../inc/FrequencyDivider.hpp"
#include "../inc/GateProcessor.hpp"
#include "../inc/Utility.hpp"

// set the module name for the theme selection functions
#define THEME_MODULE_NAME EventTimer

// module specifics names
#define STRUCT_NAME EventTimer
#define WIDGET_NAME EventTimerWidget
#define MODULE_NAME "EventTimer"
#define PANEL_FILE "EventTimer.svg"
#define MODEL_NAME	modelEventTimer

#define NUM_DIGITS 3

#include "EventTimer.hpp"
