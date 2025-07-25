/**
 * OTT Biquad Filter Implementation
 * Extracted from original OTT crossover filtering system
 */

#include "ott_plugin.h"

// ============================================================================
// BIQUAD FILTER PROCESSING (Direct Form II)
// ============================================================================

float ProcessBiquadFilter(BiquadFilter* filter, float input)
{
    // Load previous delay line states
    float w_n_minus_2 = filter->state2;    // w[n-2] 
    float w_n_minus_1 = filter->state1;    // w[n-1]
    
    // Store current input for reference
    filter->input_store = input;
    
    // Calculate intermediate values using filter coefficients
    float temp1 = w_n_minus_1 * filter->coeff_a1;           // a1 * w[n-1]
    float processed_input = input - w_n_minus_2;            // x[n] - w[n-2] 
    filter->processed_input = processed_input;
    
    float temp2 = processed_input * filter->coeff_a2;       // a2 * (x[n] - w[n-2])
    float feedback_term = processed_input * filter->coeff_b2; // b2 * processed_input
    
    // Combine intermediate calculations
    float intermediate = temp1 + temp2;                     // a1*w[n-1] + a2*(x[n] - w[n-2])
    filter->intermediate = intermediate;
    
    // Calculate final output 
    float output = w_n_minus_1 * filter->coeff_a2 + w_n_minus_2;  // a2*w[n-1] + w[n-2]
    output = output + feedback_term;                              // + b2*processed_input
    filter->output = output;
    
    // Update delay line states for next sample (OTT's specific state update)
    filter->state1 = intermediate + intermediate - w_n_minus_1;   // 2*intermediate - w[n-1] 
    filter->state2 = output + output - w_n_minus_2;               // 2*output - w[n-2]
    
    return output;
}

// ============================================================================
// FILTER OUTPUT FUNCTIONS
// ============================================================================

// Get lowpass filter output (sub_180126030)
float GetBiquadLowpass(void* filterObj)
{
    BiquadFilter* filter = (BiquadFilter*)filterObj;
    return filter->output;  // offset 0x24
}

// Get highpass filter output (sub_180126010) 
float GetBiquadHighpass(void* filterObj)
{
    BiquadFilter* filter = (BiquadFilter*)filterObj;
    // Highpass = input - (intermediate * b1_coefficient) - lowpass_output
    return filter->input_store - (filter->intermediate * filter->b1) - filter->output;
}

// Generic output getter (defaults to lowpass)
float GetBiquadOutput(void* filterObj)
{
    return GetBiquadLowpass(filterObj);
}

// ============================================================================
// FILTER COEFFICIENT CALCULATION (from sub_180126040)
// ============================================================================

void CalculateBiquadCoefficients(BiquadFilter* filter, float frequency, float sampleRate)
{
    // Calculate normalized frequency (0 to π)
    double normalizedFreq = frequency * M_PI / sampleRate;
    
    // Calculate intermediate values using bilinear transform
    float tan_half_freq = tanf((float)normalizedFreq);
    float reciprocal = 1.0f / tan_half_freq;
    
    // Calculate denominator for coefficient normalization
    float denominator = 1.0f / ((reciprocal + tan_half_freq) * tan_half_freq + 1.0f);
    
    // Calculate filter coefficients (extracted from original algorithm)
    filter->coeff_a1 = reciprocal;                          // offset 0x10
    filter->coeff_a2 = denominator * tan_half_freq;         // offset 0x14  
    filter->coeff_b2 = filter->coeff_a2 * tan_half_freq;    // offset 0x18
    
    // Set feedforward coefficients
    filter->b0 = 1.0f;                                      // offset 0x0
    filter->b1 = denominator;                               // offset 0x4
    
    // Store the calculated values at their proper offsets
    // This matches the exact memory layout from the original function
}

// ============================================================================
// FILTER INITIALIZATION AND MANAGEMENT
// ============================================================================

void InitializeBiquadFilter(BiquadFilter* filter)
{
    // Clear all filter states
    filter->state1 = 0.0f;
    filter->state2 = 0.0f;
    filter->input_store = 0.0f;
    filter->intermediate = 0.0f;
    filter->output = 0.0f;
    filter->processed_input = 0.0f;
    
    // Initialize coefficients to pass-through (unity gain, no filtering)
    filter->b0 = 1.0f;
    filter->b1 = 0.0f;
    filter->coeff_a1 = 0.0f;
    filter->coeff_a2 = 0.0f; 
    filter->coeff_b2 = 0.0f;
}

void SetBiquadCoefficients(BiquadFilter* filter, float b0, float b1, float a1, float a2, float b2)
{
    filter->b0 = b0;
    filter->b1 = b1;
    filter->coeff_a1 = a1;
    filter->coeff_a2 = a2;
    filter->coeff_b2 = b2;
}

// ============================================================================
// CROSSOVER FILTER SETUP FOR OTT'S 3-BAND SYSTEM
// ============================================================================

void SetupOTTCrossoverFilters(OTTPlugin* plugin, float sampleRate)
{
    // OTT uses a 3-band crossover system:
    // - Low/Mid split around 200 Hz
    // - Mid/High split around 2 kHz
    
    float lowMidCrossover = 200.0f;   // Low to Mid crossover frequency
    float midHighCrossover = 2000.0f; // Mid to High crossover frequency
    
    // Initialize all crossover filters
    for (int i = 0; i < 6; i++) {
        InitializeBiquadFilter(&plugin->crossoverFilters[i]);
    }
    
    // Setup crossover filter coefficients
    // Filters 0,1: Low/Mid split (Left/Right channels)
    CalculateBiquadCoefficients(&plugin->crossoverFilters[0], lowMidCrossover, sampleRate);
    CalculateBiquadCoefficients(&plugin->crossoverFilters[1], lowMidCrossover, sampleRate);
    
    // Filters 2,3: Process the highpass output from filters 0,1 for Mid/High split
    CalculateBiquadCoefficients(&plugin->crossoverFilters[2], midHighCrossover, sampleRate);
    CalculateBiquadCoefficients(&plugin->crossoverFilters[3], midHighCrossover, sampleRate);
    
    // Filters 4,5: Additional processing stages
    CalculateBiquadCoefficients(&plugin->crossoverFilters[4], midHighCrossover, sampleRate);
    CalculateBiquadCoefficients(&plugin->crossoverFilters[5], midHighCrossover, sampleRate);
}

// ============================================================================
// HELPER FUNCTIONS FOR ANALYSIS
// ============================================================================

// Calculate the magnitude response of a biquad filter at a given frequency
float CalculateFilterResponse(BiquadFilter* filter, float frequency, float sampleRate)
{
    // Convert frequency to normalized frequency (0 to π)
    float omega = 2.0f * M_PI * frequency / sampleRate;
    
    // Calculate complex response using filter coefficients
    // This is useful for analyzing the crossover characteristics
    float cos_omega = cosf(omega);
    float cos_2omega = cosf(2.0f * omega);
    
    // Calculate numerator magnitude squared
    float num_real = filter->b0 + filter->b1 * cos_omega;
    float num_imag = -filter->b1 * sinf(omega);
    float num_mag_sq = num_real * num_real + num_imag * num_imag;
    
    // Calculate denominator magnitude squared
    float den_real = 1.0f + filter->coeff_a1 * cos_omega + filter->coeff_a2 * cos_2omega;
    float den_imag = -filter->coeff_a1 * sinf(omega) - filter->coeff_a2 * sinf(2.0f * omega);
    float den_mag_sq = den_real * den_real + den_imag * den_imag;
    
    // Return magnitude response
    return sqrtf(num_mag_sq / den_mag_sq);
}
