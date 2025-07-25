#ifndef OTT_PLUGIN_H
#define OTT_PLUGIN_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// ============================================================================
// CORE CONSTANTS
// ============================================================================

#define ENVELOPE_DECAY_RATE     2.49999994e-05f    // Peak envelope decay per sample
#define COMPRESSION_SCALING     0.519999981f       // Compression amount scaling
#define UPWARD_MULT_1          2.27304697f        // Upward compression multiplier 1  
#define UPWARD_MULT_2          0.927524984f       // Upward compression multiplier 2
#define DELAY_BUFFER_SIZE      0x8000             // 32768 samples delay buffer
#define NUM_FREQUENCY_BANDS    3                  // Low, Mid, High bands
#define NOISE_FLOOR            1e-25              // Prevents division by zero

// Compression algorithm constants
#define LOG_SCALE_FACTOR        0x40215f2ced384f29    // Logarithmic scaling constant
#define UNITY_GAIN              0x3ff0000000000000    // 1.0 in double precision
#define MIN_GAIN_THRESHOLD      0x3f847ae147ae147b    // Minimum gain threshold  
#define MAX_COMPRESSION_RATIO   0x4042000000000000    // Maximum compression ratio (36.0)
#define NEGATIVE_THRESHOLD      -0x3f81000000000000   // Negative threshold limit
#define ENVELOPE_TIME_CONSTANT  0x3fbd791c5f888822    // Time constant for envelopes

// ============================================================================
// BIQUAD FILTER STRUCTURE
// ============================================================================

typedef struct {
    // Filter coefficients (offsets match OTT's memory layout)
    float b0;                   // +0x0:  Feedforward coefficient b0
    float b1;                   // +0x4:  Feedforward coefficient b1  
    float state1;               // +0x8:  Previous intermediate value (w[n-1])
    float state2;               // +0xc:  Previous intermediate value (w[n-2])
    float coeff_a1;             // +0x10: Coefficient a1  
    float coeff_a2;             // +0x14: Coefficient a2
    float coeff_b2;             // +0x18: Coefficient b2
    float input_store;          // +0x1c: Current input sample storage
    float intermediate;         // +0x20: Intermediate calculation result
    float output;               // +0x24: Filter output (lowpass)
    float processed_input;      // +0x28: Processed input value
} BiquadFilter;

// ============================================================================
// COMPRESSOR STATE STRUCTURE
// ============================================================================

typedef struct {
    // RMS detection and smoothing
    double rms_smoother;           // +0xb8: RMS level smoother
    double rms_smoothing_coeff;    // +0xb0: RMS smoothing coefficient
    
    // Envelope followers  
    double log_envelope;           // +0x60: Logarithmic envelope value
    double threshold;              // +0x70: Compression threshold
    double ratio_state;            // +0x80: Current ratio state
    double gain_reduction;         // +0x90: Current gain reduction
    
    // Attack/Release parameters
    double attack_coeff;           // +0x30: Attack coefficient  
    double release_coeff;          // +0x50: Release coefficient
    double release_time;           // +0x78: Release time constant
    double upward_ratio;           // +0x88: Upward compression ratio
    
    // Internal processing states
    double envelope_output;        // +0x58: Envelope follower output
    double processed_envelope;     // +0x68: Processed envelope value
    
    // Additional coefficients for advanced processing
    float linear_coeff;            // +0x8:  Linear processing coefficient
    float knee_coeff;              // +0xc:  Knee/curve coefficient
    
} CompressorState;

// ============================================================================
// MAIN PLUGIN STRUCTURE
// ============================================================================

typedef struct {
    // Plugin state
    bool bypass;                    // +0x326: Bypass on/off
    bool advancedMode;             // +0x248: Advanced processing mode
    bool needsUpdate;              // Processing update flag
    
    // Channel configuration
    uint32_t inputChannels;        // +0x60: Number of input channels
    uint32_t outputChannels;       // +0x64: Number of output channels  
    uint32_t inputChannelIndex;    // +0x30c: Current input channel
    uint32_t outputChannelIndex;   // +0x310: Current output channel
    
    // Peak detection envelopes (stereo)
    float peakEnvelopeLeft;        // +0xf4: Left channel peak envelope
    float peakEnvelopeRight;       // +0xf8: Right channel peak envelope
    
    // Main compression parameters
    float depth;                   // +0x2dc: Compression depth/amount
    float timeControl;             // +0x2d8: Attack/release time control
    float upwardRatioRaw;          // +0x2e4: Raw upward ratio parameter (0-1)
    float upwardRatio;             // +0x2ec: Processed upward compression ratio
    float downwardRatioRaw;        // +0x2e8: Raw downward ratio parameter (0-1)  
    float downwardRatio;           // +0x2f0: Processed downward compression ratio
    float currentGain;             // +0x2f4: Current gain value
    float finalGain;               // +0x2f8: Final processed gain
    
    // Band controls
    float bandControls[3];         // +0x20c: Low/Mid/High band controls
    float bandGains[3];            // Band gain controls
    float bandGainsDoubled[3];     // +0x218: Doubled gain values (internal use)
    
    // Boolean switches
    bool switches[6];              // +0x315: Various on/off switches
    
    // Additional controls
    float additionalControl1;      // +0x2fc: Additional parameter 1
    float additionalControl2;      // +0x300: Additional parameter 2
    
    // Smoothing filters for parameters
    void* depthSmoother;          // +0x288: Depth parameter smoother
    void* upwardSmoother;         // +0x298: Upward ratio smoother  
    void* outputSmoother;         // +0x2a0: Output gain smoother
    
    // Multiband filter objects (6 filters for 3-band stereo crossover)
    BiquadFilter crossoverFilters[6];  // +0x140-0x178: Crossover filter objects
    
    // Band processing buffers
    float** bandBuffers;          // +0x250: Individual band audio buffers
    float** delayBuffers;         // +0x258: Delay line buffers
    
    // Compressor objects (3 bands)  
    CompressorState compressorLow;    // +0x200: Low band compressor
    CompressorState compressorMid;    // +0x208: Mid band compressor  
    CompressorState compressorHigh;   // +0x210: High band compressor
    
    // Buffer management
    uint32_t bufferIndex;         // +0x2a8: Current buffer position
    uint32_t bufferOffset;        // +0x2ac: Buffer offset
    uint32_t writeIndex;          // +0x2b0: Write position
    
    // Output gain controls (3 bands)
    float lowBandGain;            // +0x238: Low band output gain
    float midBandGain;            // +0x23c: Mid band output gain  
    float highBandGain;           // +0x240: High band output gain
    
    // Compressor state storage (for UI display)
    float compressorStates[12];   // +0x104-0x118: Various compressor states
    
    // Preset system
    uint32_t currentPresetSlot;   // +0x28: Current preset slot
    void* presetData;             // +0x138: Preset storage area
    
} OTTPlugin;

// ============================================================================
// PARAMETER DEFINITIONS
// ============================================================================

typedef enum {
    OTT_PARAM_DEPTH         = 0,    // Compression depth/amount
    OTT_PARAM_TIME          = 1,    // Attack/release time
    OTT_PARAM_UPWARD_RATIO  = 2,    // Upward compression ratio (complex scaling)
    OTT_PARAM_DOWNWARD_RATIO = 3,   // Downward compression ratio (complex scaling)
    OTT_PARAM_ADVANCED_MODE = 4,    // Advanced processing mode (boolean)
    OTT_PARAM_LOW_BAND      = 5,    // Low band control
    OTT_PARAM_MID_BAND      = 6,    // Mid band control  
    OTT_PARAM_HIGH_BAND     = 7,    // High band control
    OTT_PARAM_LOW_GAIN      = 8,    // Low band gain
    OTT_PARAM_MID_GAIN      = 9,    // Mid band gain
    OTT_PARAM_HIGH_GAIN     = 10,   // High band gain
    OTT_PARAM_SWITCH_1      = 11,   // Various on/off switches
    OTT_PARAM_SWITCH_2      = 12,
    OTT_PARAM_SWITCH_3      = 13,
    OTT_PARAM_SWITCH_4      = 14,
    OTT_PARAM_SWITCH_5      = 15,
    OTT_PARAM_SWITCH_6      = 16,
    OTT_PARAM_CONTROL_1     = 17,   // Additional parameter 1
    OTT_PARAM_CONTROL_2     = 18,   // Additional parameter 2
    OTT_PARAM_BYPASS        = 19,   // Master bypass (boolean)
} OTTParameterIndex;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Core processing
void OTT_ProcessAudio(OTTPlugin* plugin, float** inputs, float** outputs, int32_t sampleCount);

// Filter functions
void InitializeBiquadFilter(BiquadFilter* filter);
float ProcessBiquadFilter(BiquadFilter* filter, float input);
float GetBiquadLowpass(void* filterObj);
float GetBiquadHighpass(void* filterObj);
void CalculateBiquadCoefficients(BiquadFilter* filter, float frequency, float sampleRate);

// Compression functions  
void InitializeCompressor(CompressorState* comp);
double ProcessCompressorBand(CompressorState* comp, double inputPower, double outputLevel, 
                            double bandGain, double timeConstant);

// Parameter functions
void OTT_SetParameter(OTTPlugin* plugin, int32_t parameterIndex, float value);
float CalculateCompressionRatio(float vstValue);
const char* OTT_GetParameterName(int index);

// Plugin management
void OTT_Initialize(OTTPlugin* plugin, float sampleRate);
void OTT_Cleanup(OTTPlugin* plugin);

#endif // OTT_PLUGIN_H
