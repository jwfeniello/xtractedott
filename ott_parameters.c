/**
 * OTT Parameter Management
 * Extracted from OTT_SetParameter function and related parameter handling
 */

#include "ott_plugin.h"

// ============================================================================
// PARAMETER INFORMATION DATABASE
// ============================================================================

typedef struct {
    const char* name;
    const char* units;
    float minValue;
    float maxValue;
    float defaultValue;
    bool isBoolean;
    bool usesComplexScaling;
} OTTParameterInfo;

static const OTTParameterInfo OTT_PARAMETERS[20] = {
    // Main compression controls
    {"Depth",           "%",    0.0f, 1.0f, 0.5f,  false, false},
    {"Time",            "ms",   0.0f, 1.0f, 0.5f,  false, false},
    {"Upward Ratio",    ":1",   0.0f, 1.0f, 0.5f,  false, true},
    {"Downward Ratio",  ":1",   0.0f, 1.0f, 0.5f,  false, true},
    {"Advanced Mode",   "",     0.0f, 1.0f, 0.0f,  true,  false},
    
    // Band controls
    {"Low Band",        "dB",   0.0f, 1.0f, 0.5f,  false, false},
    {"Mid Band",        "dB",   0.0f, 1.0f, 0.5f,  false, false},
    {"High Band",       "dB",   0.0f, 1.0f, 0.5f,  false, false},
    
    // Gain controls  
    {"Low Gain",        "dB",   0.0f, 1.0f, 0.5f,  false, false},
    {"Mid Gain",        "dB",   0.0f, 1.0f, 0.5f,  false, false},
    {"High Gain",       "dB",   0.0f, 1.0f, 0.5f,  false, false},
    
    // Boolean switches
    {"Switch 1",        "",     0.0f, 1.0f, 0.0f,  true,  false},
    {"Switch 2",        "",     0.0f, 1.0f, 0.0f,  true,  false},
    {"Switch 3",        "",     0.0f, 1.0f, 0.0f,  true,  false},
    {"Switch 4",        "",     0.0f, 1.0f, 0.0f,  true,  false},
    {"Switch 5",        "",     0.0f, 1.0f, 0.0f,  true,  false},
    {"Switch 6",        "",     0.0f, 1.0f, 0.0f,  true,  false},
    
    // Additional controls
    {"Control 1",       "",     0.0f, 1.0f, 0.5f,  false, false},
    {"Control 2",       "",     0.0f, 1.0f, 0.5f,  false, false},
    {"Bypass",          "",     0.0f, 1.0f, 0.0f,  true,  false},
};

// ============================================================================
// COMPLEX PARAMETER SCALING FUNCTIONS
// ============================================================================

float CalculateCompressionRatio(float vstValue)
{
    /*
    OTT's complex ratio scaling extracted from the original algorithm:
    - Below 0.5: Expansion mode (value * 2)
    - Above 0.5: Compression mode ((value - 0.5) * 16 + 1)
    - At 0.5: Unity/neutral point
    */
    
    if (vstValue > 0.5f) {
        // Above center: Compression ratios from 1:1 to 9:1
        // Formula: (value - 0.5) * 16 + 1
        // Range: 0.5-1.0 VST maps to 1.0-9.0 compression ratio
        return (vstValue - 0.5f) * 16.0f + 1.0f;
    } else {
        // Below center: Expansion ratios  
        // Formula: value * 2
        // Range: 0.0-0.5 VST maps to 0.0-1.0 (expansion)
        return vstValue * 2.0f;
    }
}

bool ConvertBooleanParameter(float vstValue)
{
    // Convert floating point parameter to boolean
    // 0.0 = false, anything else = true
    return (vstValue != 0.0f);
}

float ConvertRatioToVSTValue(float internalRatio)
{
    // Convert internal compression ratio back to VST parameter value
    if (internalRatio >= 1.0f) {
        // Compression range: 1.0-9.0 maps to 0.5-1.0
        return (internalRatio - 1.0f) / 16.0f + 0.5f;
    } else {
        // Expansion range: 0.0-1.0 maps to 0.0-0.5  
        return internalRatio / 2.0f;
    }
}

// ============================================================================
// MAIN PARAMETER SETTING FUNCTION
// ============================================================================

void OTT_SetParameter(OTTPlugin* plugin, int32_t parameterIndex, float value)
{
    // Bounds checking
    if (parameterIndex < 0 || parameterIndex > 19) {
        return; // Invalid parameter index
    }
    
    // Clamp value to valid range
    value = fmaxf(0.0f, fminf(1.0f, value));
    
    // Calculate preset storage location (for automation/preset saving)
    int64_t presetIndex = plugin->currentPresetSlot;
    float* presetStorage = (float*)((char*)plugin->presetData + presetIndex * 0x6c + 0x138);
    
    switch (parameterIndex) {
        
        // ====================================================================
        // MAIN COMPRESSION CONTROLS
        // ====================================================================
        
        case OTT_PARAM_DEPTH:
            plugin->depth = value;
            presetStorage[0] = value;
            plugin->needsUpdate = true;
            break;
            
        case OTT_PARAM_TIME:
            plugin->timeControl = value;
            presetStorage[1] = value;
            plugin->needsUpdate = true;
            break;
            
        case OTT_PARAM_UPWARD_RATIO:
            plugin->upwardRatioRaw = value;
            presetStorage[2] = value;
            // Apply complex scaling for internal use
            plugin->upwardRatio = CalculateCompressionRatio(value);
            plugin->needsUpdate = true;
            break;
            
        case OTT_PARAM_DOWNWARD_RATIO:
            plugin->downwardRatioRaw = value;
            presetStorage[3] = value;
            // Apply complex scaling for internal use
            plugin->downwardRatio = CalculateCompressionRatio(value);
            plugin->needsUpdate = true;
            break;
            
        case OTT_PARAM_ADVANCED_MODE:
            plugin->advancedMode = ConvertBooleanParameter(value);
            presetStorage[4] = value;
            plugin->needsUpdate = true;
            break;
            
        // ====================================================================
        // BAND CONTROLS (5-7)
        // ====================================================================
        
        case OTT_PARAM_LOW_BAND:
        case OTT_PARAM_MID_BAND:
        case OTT_PARAM_HIGH_BAND:
        {
            int bandIndex = parameterIndex - OTT_PARAM_LOW_BAND;
            plugin->bandControls[bandIndex] = value;
            presetStorage[parameterIndex] = value;
            plugin->needsUpdate = true;
            break;
        }
        
        // ====================================================================
        // GAIN CONTROLS (8-10) - Values are doubled internally
        // ====================================================================
        
        case OTT_PARAM_LOW_GAIN:
        case OTT_PARAM_MID_GAIN:
        case OTT_PARAM_HIGH_GAIN:
        {
            int gainIndex = parameterIndex - OTT_PARAM_LOW_GAIN;
            plugin->bandGains[gainIndex] = value;
            plugin->bandGainsDoubled[gainIndex] = value * 2.0f; // Internal processing uses doubled values
            presetStorage[parameterIndex] = value;
            plugin->needsUpdate = true;
            
            // Update band gain outputs
            switch (gainIndex) {
                case 0: plugin->lowBandGain = value; break;
                case 1: plugin->midBandGain = value; break;
                case 2: plugin->highBandGain = value; break;
            }
            break;
        }
        
        // ====================================================================
        // BOOLEAN SWITCHES (11-16)
        // ====================================================================
        
        case OTT_PARAM_SWITCH_1:
        case OTT_PARAM_SWITCH_2:
        case OTT_PARAM_SWITCH_3:
        case OTT_PARAM_SWITCH_4:
        case OTT_PARAM_SWITCH_5:
        case OTT_PARAM_SWITCH_6:
        {
            int switchIndex = parameterIndex - OTT_PARAM_SWITCH_1;
            plugin->switches[switchIndex] = ConvertBooleanParameter(value);
            presetStorage[parameterIndex] = value;
            plugin->needsUpdate = true;
            break;
        }
        
        // ====================================================================
        // ADDITIONAL CONTROLS
        // ====================================================================
        
        case OTT_PARAM_CONTROL_1:
            plugin->additionalControl1 = value;
            presetStorage[17] = value;
            plugin->needsUpdate = true;
            break;
            
        case OTT_PARAM_CONTROL_2:
            plugin->additionalControl2 = value;
            presetStorage[18] = value;
            plugin->needsUpdate = true;
            break;
            
        case OTT_PARAM_BYPASS:
            plugin->bypass = ConvertBooleanParameter(value);
            presetStorage[25] = value;  // Note: preset index 25, not 19
            // Bypass doesn't need processing update
            break;
    }
}

// ============================================================================
// PARAMETER QUERY FUNCTIONS
// ============================================================================

const char* OTT_GetParameterName(int index)
{
    if (index >= 0 && index < 20) {
        return OTT_PARAMETERS[index].name;
    }
    return "Unknown";
}

const char* OTT_GetParameterUnits(int index)
{
    if (index >= 0 && index < 20) {
        return OTT_PARAMETERS[index].units;
    }
    return "";
}

float OTT_GetParameterDefault(int index)
{
    if (index >= 0 && index < 20) {
        return OTT_PARAMETERS[index].defaultValue;
    }
    return 0.0f;
}

float OTT_GetParameterMin(int index)
{
    if (index >= 0 && index < 20) {
        return OTT_PARAMETERS[index].minValue;
    }
    return 0.0f;
}

float OTT_GetParameterMax(int index)
{
    if (index >= 0 && index < 20) {
        return OTT_PARAMETERS[index].maxValue;
    }
    return 1.0f;
}

bool OTT_IsParameterBoolean(int index)
{
    if (index >= 0 && index < 20) {
        return OTT_PARAMETERS[index].isBoolean;
    }
    return false;
}

// ============================================================================
// PARAMETER AUTOMATION AND PRESET SUPPORT
// ============================================================================

void OTT_InitializeParametersToDefaults(OTTPlugin* plugin)
{
    // Set all parameters to their default values
    for (int i = 0; i < 20; i++) {
        OTT_SetParameter(plugin, i, OTT_GetParameterDefault(i));
    }
}

float OTT_GetParameter(OTTPlugin* plugin, int32_t parameterIndex)
{
    // Return current parameter value in VST format (0.0-1.0)
    switch (parameterIndex) {
        case OTT_PARAM_DEPTH: return plugin->depth;
        case OTT_PARAM_TIME: return plugin->timeControl;
        case OTT_PARAM_UPWARD_RATIO: return plugin->upwardRatioRaw;
        case OTT_PARAM_DOWNWARD_RATIO: return plugin->downwardRatioRaw;
        case OTT_PARAM_ADVANCED_MODE: return plugin->advancedMode ? 1.0f : 0.0f;
        
        case OTT_PARAM_LOW_BAND:
        case OTT_PARAM_MID_BAND:
        case OTT_PARAM_HIGH_BAND:
            return plugin->bandControls[parameterIndex - OTT_PARAM_LOW_BAND];
            
        case OTT_PARAM_LOW_GAIN:
        case OTT_PARAM_MID_GAIN:
        case OTT_PARAM_HIGH_GAIN:
            return plugin->bandGains[parameterIndex - OTT_PARAM_LOW_GAIN];
            
        case OTT_PARAM_SWITCH_1:
        case OTT_PARAM_SWITCH_2:
        case OTT_PARAM_SWITCH_3:
        case OTT_PARAM_SWITCH_4:
        case OTT_PARAM_SWITCH_5:
        case OTT_PARAM_SWITCH_6:
            return plugin->switches[parameterIndex - OTT_PARAM_SWITCH_1] ? 1.0f : 0.0f;
            
        case OTT_PARAM_CONTROL_1: return plugin->additionalControl1;
        case OTT_PARAM_CONTROL_2: return plugin->additionalControl2;
        case OTT_PARAM_BYPASS: return plugin->bypass ? 1.0f : 0.0f;
        
        default: return 0.0f;
    }
}

void OTT_GetParameterDisplay(int parameterIndex, float value, char* display, int maxLen)
{
    // Convert parameter value to display string
    if (parameterIndex < 0 || parameterIndex >= 20 || !display || maxLen < 1) {
        return;
    }
    
    const OTTParameterInfo* param = &OTT_PARAMETERS[parameterIndex];
    
    if (param->isBoolean) {
        // Boolean parameters
        snprintf(display, maxLen, "%s", value > 0.5f ? "On" : "Off");
    } else if (param->usesComplexScaling) {
        // Compression ratio parameters with complex scaling
        float ratio = CalculateCompressionRatio(value);
        if (ratio >= 1.0f) {
            snprintf(display, maxLen, "%.1f:1", ratio);
        } else {
            snprintf(display, maxLen, "%.2f", ratio);
        }
    } else {
        // Standard parameters (0-100%)
        float percentage = value * 100.0f;
        snprintf(display, maxLen, "%.1f%s", percentage, param->units);
    }
}

/*
NOTES on OTT's Parameter System:

1. **Complex Ratio Scaling**: Parameters 2&3 (upward/downward ratio) use 
   sophisticated scaling where 0.5 is the "unity" point:
   - Below 0.5: Expansion mode (0.0-1.0 range)
   - Above 0.5: Compression mode (1.0-9.0 range)

2. **Doubled Gain Values**: Gain parameters (8-10) are stored both as normal
   and doubled values, with the processing engine using the doubled values.

3. **Boolean Processing**: Several parameters are treated as boolean switches
   with special conversion logic (0.0 = false, anything else = true).

4. **Preset Integration**: The parameter system includes preset storage with
   specific memory offsets for automation and patch saving.

5. **Update Triggering**: Most parameters set a needsUpdate flag to trigger
   recalculation of processing coefficients.

This parameter mapping system is crucial for recreating OTT's exact behavior,
as it determines how user interface controls translate to internal DSP values.
*/
