#include "ott_plugin.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// PLUGIN INITIALIZATION
// ============================================================================

void OTT_Initialize(OTTPlugin* plugin, float sampleRate)
{
    // Clear all memory to start with clean state
    memset(plugin, 0, sizeof(OTTPlugin));
    
    // ========================================================================
    // BASIC PLUGIN SETUP
    // ========================================================================
    
    plugin->inputChannels = 2;
    plugin->outputChannels = 2;
    plugin->inputChannelIndex = 0;
    plugin->outputChannelIndex = 0;
    plugin->bypass = false;
    plugin->advancedMode = false;
    plugin->needsUpdate = true;
    
    // Initialize envelopes
    plugin->peakEnvelopeLeft = 0.0f;
    plugin->peakEnvelopeRight = 0.0f;
    
    // ========================================================================
    // ALLOCATE AUDIO BUFFERS
    // ========================================================================
    
    // Allocate band processing buffers (6 bands for stereo 3-band processing)
    plugin->bandBuffers = (float**)malloc(6 * sizeof(float*));
    plugin->delayBuffers = (float**)malloc(8 * sizeof(float*)); // 6 bands + 2 original channels
    
    for (int band = 0; band < 6; band++) {
        plugin->bandBuffers[band] = (float*)calloc(DELAY_BUFFER_SIZE, sizeof(float));
    }
    
    for (int buffer = 0; buffer < 8; buffer++) {
        plugin->delayBuffers[buffer] = (float*)calloc(DELAY_BUFFER_SIZE, sizeof(float));
    }
    
    // ========================================================================
    // INITIALIZE FILTER SYSTEM
    // ========================================================================
    
    // Initialize all crossover filters
    for (int i = 0; i < 6; i++) {
        InitializeBiquadFilter(&plugin->crossoverFilters[i]);
    }
    
    // Setup crossover frequencies for 3-band system
    SetupOTTCrossoverFilters(plugin, sampleRate);
    
    // ========================================================================
    // INITIALIZE COMPRESSION SYSTEM
    // ========================================================================
    
    InitializeCompressor(&plugin->compressorLow);
    InitializeCompressor(&plugin->compressorMid);
    InitializeCompressor(&plugin->compressorHigh);
    
    // Setup compressor parameters for each band
    SetCompressorParameters(&plugin->compressorLow, -20.0, 2.0, 0.1, 0.01, 2.0);
    SetCompressorParameters(&plugin->compressorMid, -15.0, 3.0, 0.08, 0.015, 2.5);
    SetCompressorParameters(&plugin->compressorHigh, -10.0, 4.0, 0.05, 0.02, 3.0);
    
    // ========================================================================
    // INITIALIZE PARAMETER SMOOTHERS
    // ========================================================================
    
    // Allocate parameter smoothing filters (simple first-order lowpass)
    plugin->depthSmoother = malloc(2 * sizeof(float));
    plugin->upwardSmoother = malloc(2 * sizeof(float));
    plugin->outputSmoother = malloc(2 * sizeof(float));
    
    // Initialize smoother states [current_value, smoothing_coefficient]
    float* depthSmooth = (float*)plugin->depthSmoother;
    depthSmooth[0] = 0.0f;   // Current value
    depthSmooth[1] = 0.01f;  // Smoothing coefficient (1% per sample)
    
    float* upwardSmooth = (float*)plugin->upwardSmoother;
    upwardSmooth[0] = 0.0f;
    upwardSmooth[1] = 0.01f;
    
    float* outputSmooth = (float*)plugin->outputSmoother;
    outputSmooth[0] = 1.0f;  // Start at unity gain
    outputSmooth[1] = 0.005f; // Slower smoothing for output
    
    // ========================================================================
    // PRESET SYSTEM SETUP
    // ========================================================================
    
    plugin->currentPresetSlot = 0;
    plugin->presetData = malloc(0x1000); // 4KB for preset storage
    memset(plugin->presetData, 0, 0x1000);
    
    // ========================================================================
    // INITIALIZE PARAMETERS TO DEFAULTS
    // ========================================================================
    
    OTT_InitializeParametersToDefaults(plugin);
    
    // Set some better starting values
    plugin->depth = 0.5f;
    plugin->timeControl = 0.3f;
    plugin->upwardRatio = 0.6f;  // Slight upward compression
    plugin->downwardRatio = 0.7f; // Moderate downward compression
    plugin->lowBandGain = 0.5f;
    plugin->midBandGain = 0.5f;
    plugin->highBandGain = 0.5f;
    
    // ========================================================================
    // BUFFER MANAGEMENT SETUP
    // ========================================================================
    
    plugin->bufferIndex = 0;
    plugin->bufferOffset = 0;
    plugin->writeIndex = 0;
    
    // Clear compressor state storage
    memset(plugin->compressorStates, 0, sizeof(plugin->compressorStates));
}

// ============================================================================
// PLUGIN CLEANUP
// ============================================================================

void OTT_Cleanup(OTTPlugin* plugin)
{
    if (!plugin) return;
    
    // Free audio buffers
    if (plugin->bandBuffers) {
        for (int band = 0; band < 6; band++) {
            free(plugin->bandBuffers[band]);
        }
        free(plugin->bandBuffers);
    }
    
    if (plugin->delayBuffers) {
        for (int buffer = 0; buffer < 8; buffer++) {
            free(plugin->delayBuffers[buffer]);
        }
        free(plugin->delayBuffers);
    }
    
    // Free parameter smoothers
    free(plugin->depthSmoother);
    free(plugin->upwardSmoother);
    free(plugin->outputSmoother);
    
    // Free preset data
    free(plugin->presetData);
    
    // Clear the plugin structure
    memset(plugin, 0, sizeof(OTTPlugin));
}

// ============================================================================
// PLUGIN PROCESSING WRAPPER
// ============================================================================

void OTT_Process(OTTPlugin* plugin, float** inputs, float** outputs, int32_t sampleCount)
{
    // Validate inputs
    if (!plugin || !inputs || !outputs || sampleCount <= 0) {
        return;
    }
    
    // Ensure we have valid input/output pointers
    if (!inputs[0] || !outputs[0]) {
        return;
    }
    
    // Process audio
    OTT_ProcessAudio(plugin, inputs, outputs, sampleCount);
}

// ============================================================================
// VST INTEGRATION HELPERS
// ============================================================================

void OTT_SetSampleRate(OTTPlugin* plugin, float sampleRate)
{
    // Recalculate filter coefficients for new sample rate
    SetupOTTCrossoverFilters(plugin, sampleRate);
    
    // Update compressor timing for new sample rate
    SetCompressorTiming(&plugin->compressorLow, 10.0f, 100.0f, sampleRate);
    SetCompressorTiming(&plugin->compressorMid, 8.0f, 80.0f, sampleRate);
    SetCompressorTiming(&plugin->compressorHigh, 5.0f, 50.0f, sampleRate);
    
    plugin->needsUpdate = true;
}

void OTT_Reset(OTTPlugin* plugin)
{
    // Reset all filter states
    for (int i = 0; i < 6; i++) {
        InitializeBiquadFilter(&plugin->crossoverFilters[i]);
    }
    
    // Reset compressor states
    InitializeCompressor(&plugin->compressorLow);
    InitializeCompressor(&plugin->compressorMid);
    InitializeCompressor(&plugin->compressorHigh);
    
    // Clear envelopes
    plugin->peakEnvelopeLeft = 0.0f;
    plugin->peakEnvelopeRight = 0.0f;
    
    // Clear buffers
    if (plugin->bandBuffers) {
        for (int band = 0; band < 6; band++) {
            memset(plugin->bandBuffers[band], 0, DELAY_BUFFER_SIZE * sizeof(float));
        }
    }
    
    if (plugin->delayBuffers) {
        for (int buffer = 0; buffer < 8; buffer++) {
            memset(plugin->delayBuffers[buffer], 0, DELAY_BUFFER_SIZE * sizeof(float));
        }
    }
    
    // Reset buffer positions
    plugin->bufferIndex = 0;
    plugin->writeIndex = 0;
    
    plugin->needsUpdate = true;
}

// ============================================================================
// PRESET MANAGEMENT
// ============================================================================

void OTT_SavePreset(OTTPlugin* plugin, int presetSlot)
{
    if (presetSlot < 0 || presetSlot >= 32) return; // Support up to 32 presets
    
    // Calculate preset storage location
    float* presetLocation = (float*)((char*)plugin->presetData + presetSlot * 0x6c + 0x138);
    
    // Save all parameter values
    for (int i = 0; i < 20; i++) {
        presetLocation[i] = OTT_GetParameter(plugin, i);
    }
    
    plugin->currentPresetSlot = presetSlot;
}

void OTT_LoadPreset(OTTPlugin* plugin, int presetSlot)
{
    if (presetSlot < 0 || presetSlot >= 32) return;
    
    // Calculate preset storage location
    float* presetLocation = (float*)((char*)plugin->presetData + presetSlot * 0x6c + 0x138);
    
    // Load all parameter values
    for (int i = 0; i < 20; i++) {
        OTT_SetParameter(plugin, i, presetLocation[i]);
    }
    
    plugin->currentPresetSlot = presetSlot;
}

// ============================================================================
// PERFORMANCE MONITORING
// ============================================================================

float OTT_GetCPUUsage(OTTPlugin* plugin)
{
    // Simple CPU usage estimation based on processing complexity
    // In a real implementation, you'd measure actual processing time
    float usage = 0.0f;
    
    // Base processing cost
    usage += 5.0f; // 5% base overhead
    
    // Filter processing cost
    usage += 8.0f; // 8% for 6 biquad filters
    
    // Compression processing cost  
    if (plugin->advancedMode) {
        usage += 15.0f; // 15% for advanced compression
    } else {
        usage += 8.0f;  // 8% for simple mode
    }
    
    // Additional cost based on active compressors
    if (IsCompressorActive(&plugin->compressorLow)) usage += 2.0f;
    if (IsCompressorActive(&plugin->compressorMid)) usage += 2.0f;
    if (IsCompressorActive(&plugin->compressorHigh)) usage += 2.0f;
    
    return fminf(usage, 100.0f); // Cap at 100%
}

// ============================================================================
// EXAMPLE USAGE
// ============================================================================

/*
// Example of how to integrate OTT into a VST host:

int main() {
    // Initialize plugin
    OTTPlugin* ott = malloc(sizeof(OTTPlugin));
    OTT_Initialize(ott, 44100.0f);
    
    // Set some parameters
    OTT_SetParameter(ott, OTT_PARAM_DEPTH, 0.7f);        // 70% compression depth
    OTT_SetParameter(ott, OTT_PARAM_UPWARD_RATIO, 0.6f); // Upward compression
    OTT_SetParameter(ott, OTT_PARAM_ADVANCED_MODE, 1.0f); // Enable advanced mode
    
    // Prepare audio buffers
    float* inputL = malloc(512 * sizeof(float));
    float* inputR = malloc(512 * sizeof(float));
    float* outputL = malloc(512 * sizeof(float));  
    float* outputR = malloc(512 * sizeof(float));
    
    float* inputs[2] = {inputL, inputR};
    float* outputs[2] = {outputL, outputR};
    
    // Process audio (example processing loop)
    for (int block = 0; block < 1000; block++) {
        // Fill input buffers with audio data here...
        
        // Process one block of audio
        OTT_Process(ott, inputs, outputs, 512);
        
        // Use processed audio from outputs...
    }
    
    // Cleanup
    free(inputL);
    free(inputR);
    free(outputL);
    free(outputR);
    OTT_Cleanup(ott);
    free(ott);
    
    return 0;
}
