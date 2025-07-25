/**
 * OTT Multiband Compressor - Main Audio Processing
 * Extracted and cleaned from original OTT VST
 */

#include "ott_plugin.h"

// ============================================================================
// MAIN AUDIO PROCESSING FUNCTION
// ============================================================================

void OTT_ProcessAudio(OTTPlugin* plugin, float** inputs, float** outputs, int32_t sampleCount)
{
    int64_t numSamples = (int64_t)sampleCount;
    
    // Early exit if bypassed
    if (plugin->bypass) {
        // Copy input to output when bypassed
        for (int ch = 0; ch < 2; ch++) {
            if (inputs[ch] && outputs[ch]) {
                for (int i = 0; i < sampleCount; i++) {
                    outputs[ch][i] = inputs[ch][i];
                }
            }
        }
        return;
    }
    
    // ========================================================================
    // CHANNEL SETUP
    // ========================================================================
    
    // Auto-detect mono input (copy to second channel)
    if (plugin->inputChannels == 2 && !inputs[1]) {
        plugin->inputChannelIndex = 0;
    }
    
    // Auto-detect mono output 
    if (plugin->outputChannels == 2 && !outputs[1]) {
        plugin->outputChannelIndex = 0;
    }
    
    // ========================================================================
    // PEAK DETECTION & ENVELOPE FOLLOWING  
    // ========================================================================
    
    int64_t sampleIdx = 0;
    int64_t rightChannelIdx = plugin->inputChannelIndex;
    float leftEnvelope = plugin->peakEnvelopeLeft;
    float rightEnvelope = plugin->peakEnvelopeRight;
    
    // Choose optimized loop based on sample count
    if (numSamples < 4) {
        // Process samples one by one (small buffers)
        if (sampleCount > 0) {
            do {
                // Left channel peak detection
                float leftSample = inputs[0][sampleIdx];
                if (leftSample < leftEnvelope) {
                    leftEnvelope -= ENVELOPE_DECAY_RATE;
                    if (leftEnvelope < 0.0f) {
                        leftEnvelope = 0.0f;
                    }
                } else {
                    leftEnvelope = leftSample;
                }
                
                // Right channel peak detection  
                float rightSample = inputs[rightChannelIdx][sampleIdx];
                if (rightSample < rightEnvelope) {
                    rightEnvelope -= ENVELOPE_DECAY_RATE;
                    if (rightEnvelope < 0.0f) {
                        rightEnvelope = 0.0f;
                    }
                } else {
                    rightEnvelope = rightSample;
                }
                
                sampleIdx++;
            } while (sampleIdx < numSamples);
        }
    } else {
        // Process 4 samples at a time (SIMD optimized)
        do {
            // Process 4 samples in parallel
            for (int offset = 0; offset < 4; offset++) {
                // Left channel samples
                float leftSample = inputs[0][sampleIdx + offset];
                if (leftSample < leftEnvelope) {
                    leftEnvelope -= ENVELOPE_DECAY_RATE;
                    if (leftEnvelope < 0.0f) leftEnvelope = 0.0f;
                } else {
                    leftEnvelope = leftSample;
                }
                
                // Right channel samples
                float rightSample = inputs[rightChannelIdx][sampleIdx + offset];
                if (rightSample < rightEnvelope) {
                    rightEnvelope -= ENVELOPE_DECAY_RATE; 
                    if (rightEnvelope < 0.0f) rightEnvelope = 0.0f;
                } else {
                    rightEnvelope = rightSample;
                }
            }
            
            sampleIdx += 4;
        } while (sampleIdx < numSamples - 3);
        
        // Process remaining samples
        while (sampleIdx < numSamples) {
            float leftSample = inputs[0][sampleIdx];
            if (leftSample < leftEnvelope) {
                leftEnvelope -= ENVELOPE_DECAY_RATE;
                if (leftEnvelope < 0.0f) leftEnvelope = 0.0f;
            } else {
                leftEnvelope = leftSample;
            }
            
            float rightSample = inputs[rightChannelIdx][sampleIdx];
            if (rightSample < rightEnvelope) {
                rightEnvelope -= ENVELOPE_DECAY_RATE;
                if (rightEnvelope < 0.0f) rightEnvelope = 0.0f;
            } else {
                rightEnvelope = rightSample;
            }
            
            sampleIdx++;
        }
    }
    
    // Store updated envelopes
    plugin->peakEnvelopeLeft = leftEnvelope;
    plugin->peakEnvelopeRight = rightEnvelope;
    
    // ========================================================================
    // MAIN PROCESSING LOOP
    // ========================================================================
    
    if (sampleCount <= 0) return;
    
    if (!plugin->advancedMode) {
        // ====================================================================
        // SIMPLE MODE - Basic multiband processing
        // ====================================================================
        
        for (int64_t sampleIdx = 0; sampleIdx < numSamples; sampleIdx++) {
            // Smooth compression parameters
            float depthTarget = plugin->depth;
            float* depthSmoother = (float*)plugin->depthSmoother;
            float smoothedDepth = depthTarget - depthSmoother[0];
            smoothedDepth = smoothedDepth * depthSmoother[1] + depthSmoother[0];
            depthSmoother[0] = smoothedDepth;
            
            float upwardTarget = plugin->upwardRatio; 
            float* upwardSmoother = (float*)plugin->upwardSmoother;
            float smoothedUpward = upwardTarget - upwardSmoother[0];
            smoothedUpward = smoothedUpward * upwardSmoother[1] + upwardSmoother[0];
            upwardSmoother[0] = smoothedUpward;
            plugin->currentGain = smoothedUpward;
            
            // Scale compression amounts
            float processingGain = smoothedDepth * COMPRESSION_SCALING + 1.0f;
            float leftProcessingGain = smoothedUpward * inputs[0][sampleIdx];
            float rightProcessingGain = smoothedUpward * inputs[rightChannelIdx][sampleIdx];
            
            // Apply multiband filtering
            ProcessBiquadFilter(&plugin->crossoverFilters[0], leftProcessingGain);
            ProcessBiquadFilter(&plugin->crossoverFilters[1], rightProcessingGain);
            ProcessBiquadFilter(&plugin->crossoverFilters[2], GetBiquadLowpass(&plugin->crossoverFilters[0]));
            ProcessBiquadFilter(&plugin->crossoverFilters[3], GetBiquadLowpass(&plugin->crossoverFilters[1]));
            
            // Store band outputs  
            plugin->bandBuffers[0][sampleIdx] = GetBiquadLowpass(&plugin->crossoverFilters[2]) * processingGain;
            plugin->bandBuffers[1][sampleIdx] = GetBiquadLowpass(&plugin->crossoverFilters[3]) * processingGain;
            
            // Process additional filter stages
            ProcessBiquadFilter(&plugin->crossoverFilters[4], leftProcessingGain);
            ProcessBiquadFilter(&plugin->crossoverFilters[5], rightProcessingGain);
            
            // Store high frequency bands
            plugin->bandBuffers[4][sampleIdx] = GetBiquadHighpass(&plugin->crossoverFilters[4]) * processingGain;
            plugin->bandBuffers[5][sampleIdx] = GetBiquadHighpass(&plugin->crossoverFilters[5]) * processingGain;
            
            // Update delay buffers  
            int bufferPos = plugin->bufferIndex;
            for (int band = 0; band < 6; band++) {
                plugin->delayBuffers[band][bufferPos] = plugin->bandBuffers[band][sampleIdx];
            }
            
            // Store original input in delay buffer
            plugin->delayBuffers[6][bufferPos] = inputs[0][sampleIdx];
            plugin->delayBuffers[7][bufferPos] = inputs[rightChannelIdx][sampleIdx];
            
            // Advance buffer position
            plugin->bufferIndex++;
            if (plugin->bufferIndex >= DELAY_BUFFER_SIZE) {
                plugin->bufferIndex = 0;
            }
        }
        
    } else {
        // ====================================================================
        // ADVANCED MODE - Full multiband compression with upward/downward
        // ====================================================================
        
        for (int64_t sampleIdx = 0; sampleIdx < numSamples; sampleIdx++) {
            // Smooth all parameters
            float* depthSmoother = (float*)plugin->depthSmoother;
            float smoothedDepth = (plugin->depth - depthSmoother[0]) * depthSmoother[1] + depthSmoother[0];
            depthSmoother[0] = smoothedDepth;
            
            float* upwardSmoother = (float*)plugin->upwardSmoother;  
            float smoothedUpward = (plugin->upwardRatio - upwardSmoother[0]) * upwardSmoother[1] + upwardSmoother[0];
            upwardSmoother[0] = smoothedUpward;
            plugin->currentGain = smoothedUpward;
            
            // Calculate processing gains
            float processingGain = smoothedDepth * COMPRESSION_SCALING + 1.0f;
            float upwardGain1 = smoothedUpward * UPWARD_MULT_1 + 1.0f;  
            float upwardGain2 = smoothedUpward * UPWARD_MULT_2 + 1.0f;
            
            // Get input samples
            float leftInput = inputs[0][sampleIdx] * plugin->currentGain;
            float rightInput = inputs[rightChannelIdx][sampleIdx] * plugin->currentGain;
            
            // ================================================================
            // MULTIBAND CROSSOVER FILTERING
            // ================================================================
            
            // Apply all 6 crossover filters for 3-band separation
            for (int filterIdx = 0; filterIdx < 6; filterIdx++) {
                float inputSample = (filterIdx % 2 == 0) ? leftInput : rightInput;
                ProcessBiquadFilter(&plugin->crossoverFilters[filterIdx], inputSample);
            }
            
            // Extract band outputs (Low, Mid, High for L/R)
            float lowLeft = GetBiquadLowpass(&plugin->crossoverFilters[0]) * processingGain;
            float lowRight = GetBiquadLowpass(&plugin->crossoverFilters[1]) * processingGain;
            float midLeft = GetBiquadHighpass(&plugin->crossoverFilters[2]) * processingGain;
            float midRight = GetBiquadHighpass(&plugin->crossoverFilters[3]) * processingGain;
            float highLeft = GetBiquadHighpass(&plugin->crossoverFilters[4]) * processingGain;
            float highRight = GetBiquadHighpass(&plugin->crossoverFilters[5]) * processingGain;
            
            // Store in band buffers
            plugin->bandBuffers[0][sampleIdx] = lowLeft;
            plugin->bandBuffers[1][sampleIdx] = lowRight;
            plugin->bandBuffers[2][sampleIdx] = midLeft;
            plugin->bandBuffers[3][sampleIdx] = midRight;
            plugin->bandBuffers[4][sampleIdx] = highLeft;
            plugin->bandBuffers[5][sampleIdx] = highRight;
            
            // Copy to delay buffers with circular indexing
            int bufferPos = plugin->bufferIndex;
            for (int band = 0; band < 6; band++) {
                plugin->delayBuffers[band][bufferPos] = plugin->bandBuffers[band][sampleIdx];
            }
            
            plugin->delayBuffers[6][bufferPos] = inputs[0][sampleIdx];
            plugin->delayBuffers[7][bufferPos] = inputs[rightChannelIdx][sampleIdx];
            
            plugin->bufferIndex++;
            if (plugin->bufferIndex >= DELAY_BUFFER_SIZE) {
                plugin->bufferIndex = 0;
            }
        }
    }
    
    // ========================================================================
    // COMPRESSOR PROCESSING & OUTPUT GENERATION  
    // ========================================================================
    
    // Calculate buffer positions for delay compensation
    int currentBufferPos = plugin->bufferIndex;
    int readPos = currentBufferPos - numSamples;
    if (readPos < 0) readPos += DELAY_BUFFER_SIZE;
    
    plugin->writeIndex = readPos;
    
    for (int64_t sampleIdx = 0; sampleIdx < numSamples; sampleIdx++) {
        // ====================================================================
        // ADVANCED COMPRESSION ALGORITHM (3-BAND)
        // ====================================================================
        
        // Smooth output gain
        float* outputSmoother = (float*)plugin->outputSmoother;  
        float smoothedOutput = (plugin->finalGain - outputSmoother[0]) * outputSmoother[1] + outputSmoother[0];
        outputSmoother[0] = smoothedOutput;
        plugin->finalGain = smoothedOutput;
        
        int readIndex = plugin->writeIndex;
        
        // Get band samples from delay buffers
        float lowLeft = plugin->delayBuffers[0][readIndex];
        float lowRight = plugin->delayBuffers[1][readIndex];
        float midLeft = plugin->delayBuffers[2][readIndex];  
        float midRight = plugin->delayBuffers[3][readIndex];
        float highLeft = plugin->delayBuffers[4][readIndex];
        float highRight = plugin->delayBuffers[5][readIndex];
        
        // Calculate RMS power for each band
        float lowPower = lowLeft * lowLeft + lowRight * lowRight + NOISE_FLOOR;
        float midPower = midLeft * midLeft + midRight * midRight + NOISE_FLOOR;
        float highPower = highLeft * highLeft + highRight * highRight + NOISE_FLOOR;
        
        // ================================================================
        // COMPRESSION PROCESSING
        // ================================================================
        
        float lowGainReduction = (float)ProcessCompressorBand(
            &plugin->compressorLow,
            lowPower,
            smoothedOutput,
            plugin->lowBandGain,
            ENVELOPE_TIME_CONSTANT
        );
        
        float midGainReduction = (float)ProcessCompressorBand(
            &plugin->compressorMid,
            midPower, 
            smoothedOutput,
            plugin->midBandGain,
            ENVELOPE_TIME_CONSTANT
        );
        
        float highGainReduction = (float)ProcessCompressorBand(
            &plugin->compressorHigh,
            highPower,
            smoothedOutput, 
            plugin->highBandGain,
            ENVELOPE_TIME_CONSTANT
        );
        
        // ================================================================
        // OUTPUT MIXING & FINAL GAIN
        // ================================================================
        
        // Apply gain reduction to each band
        lowLeft *= lowGainReduction;
        lowRight *= lowGainReduction; 
        midLeft *= midGainReduction;
        midRight *= midGainReduction;
        highLeft *= highGainReduction;
        highRight *= highGainReduction;
        
        // Mix all bands together
        float finalLeft = (lowLeft + midLeft + highLeft) * plugin->finalGain;
        float finalRight = (lowRight + midRight + highRight) * plugin->finalGain;
        
        // Write to output buffers
        outputs[0][sampleIdx] = finalLeft;
        outputs[plugin->outputChannelIndex][sampleIdx] = finalRight;
        
        // Advance read position
        plugin->writeIndex++;
        if (plugin->writeIndex >= DELAY_BUFFER_SIZE) {
            plugin->writeIndex = 0;
        }
    }
    
    // ========================================================================
    // UPDATE COMPRESSOR STATES (for UI display)
    // ========================================================================
    
    // Store final compressor states for metering/display
    plugin->compressorStates[0] = (float)plugin->compressorLow.envelope_output * plugin->lowBandGain;
    plugin->compressorStates[1] = (float)plugin->compressorMid.envelope_output * plugin->midBandGain;
    plugin->compressorStates[2] = (float)plugin->compressorHigh.envelope_output * plugin->highBandGain;
    
    // Store envelope followers for UI meters
    plugin->compressorStates[3] = (float)plugin->compressorLow.rms_smoother;
    plugin->compressorStates[4] = (float)plugin->compressorMid.rms_smoother;
    plugin->compressorStates[5] = (float)plugin->compressorHigh.rms_smoother;
}
