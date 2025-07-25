/**
 * OTT Compression Engine
 * Extracted from the complex compression algorithm in main processing
 */

#include "ott_plugin.h"

// ============================================================================
// COMPRESSOR INITIALIZATION
// ============================================================================

void InitializeCompressor(CompressorState* comp)
{
    // Initialize all states to neutral values
    comp->rms_smoother = 0.0;
    comp->rms_smoothing_coeff = 0.1;     // 10% smoothing per sample
    comp->log_envelope = 0.0;
    comp->threshold = -20.0;             // -20dB threshold (in log domain)
    comp->ratio_state = 0.0;
    comp->gain_reduction = 0.0;
    comp->attack_coeff = 0.1;            // Fast attack
    comp->release_coeff = 0.01;          // Slow release  
    comp->release_time = 1.0;
    comp->upward_ratio = 2.0;            // 2:1 upward ratio
    comp->envelope_output = 1.0;         // Unity gain
    comp->processed_envelope = 1.0;
    comp->linear_coeff = 1.0;
    comp->knee_coeff = 0.5;
}

// ============================================================================
// MAIN COMPRESSION PROCESSING FUNCTION
// ============================================================================

double ProcessCompressorBand(CompressorState* comp, double inputPower, double outputLevel, 
                            double bandGain, double timeConstant)
{
    // ========================================================================
    // RMS DETECTION & SMOOTHING
    // ========================================================================
    
    // Apply RMS smoothing filter (first-order lowpass)
    double rms_difference = comp->rms_smoother - inputPower;
    rms_difference = rms_difference * comp->rms_smoothing_coeff;
    comp->rms_smoother = rms_difference + inputPower;
    
    // ========================================================================
    // LOGARITHMIC ENVELOPE PROCESSING  
    // ========================================================================
    
    // Calculate exponential of log envelope for processing
    double envelope_exp = exp(comp->log_envelope * timeConstant);
    double envelope_sqrt = sqrt(comp->rms_smoother);
    
    // Apply absolute value operation (handle negative values)
    if (envelope_sqrt < 0.0) {
        envelope_sqrt = -envelope_sqrt;
    }
    
    // ========================================================================
    // COMPRESSION ALGORITHM BRANCHING
    // ========================================================================
    
    double final_gain_reduction;
    
    if (comp->ratio_state <= NEGATIVE_THRESHOLD) {
        // ====================================================================
        // MAIN COMPRESSION PATH (Above Threshold Processing)
        // ====================================================================
        
        double threshold_value = comp->threshold;
        
        // Convert to logarithmic domain for compression processing
        double log_input = log(envelope_sqrt + 1e-30) * LOG_SCALE_FACTOR;
        double over_threshold = log_input - threshold_value;
        double max_reduction = fmax(0.0, over_threshold);
        
        double current_ratio = comp->gain_reduction;
        double ratio_difference = current_ratio - max_reduction;
        
        // Determine compression curve based on signal level
        double compression_coeff;
        if (max_reduction <= current_ratio) {
            // Signal decreasing - use release coefficient (slower)
            compression_coeff = comp->release_coeff;
        } else {
            // Signal increasing - use attack coefficient (faster)
            compression_coeff = comp->attack_coeff;
        }
        
        // Apply compression curve
        double compressed_level = ratio_difference * compression_coeff + max_reduction;
        comp->gain_reduction = compressed_level;
        
        // Calculate final gain reduction based on threshold comparison
        if (compressed_level <= threshold_value) {
            // ================================================================
            // BELOW THRESHOLD - UPWARD COMPRESSION/EXPANSION
            // ================================================================
            
            double release_factor = comp->release_time - UNITY_GAIN;
            double upward_gain = release_factor * compressed_level * timeConstant;
            final_gain_reduction = exp(upward_gain);
            
            // Apply minimum gain limiting
            if (final_gain_reduction <= MIN_GAIN_THRESHOLD) {
                final_gain_reduction = MIN_GAIN_THRESHOLD;
            }
            
        } else {
            // ================================================================
            // ABOVE THRESHOLD - DOWNWARD COMPRESSION  
            // ================================================================
            
            double attack_factor = compressed_level - threshold_value;
            double downward_gain = attack_factor * timeConstant;
            double gain_multiplier = exp(downward_gain);
            
            // Apply upward ratio processing for musical compression
            double upward_factor = gain_multiplier * comp->upward_ratio;
            comp->processed_envelope = exp(downward_gain);
            
            // Limit maximum compression ratio to prevent over-compression
            if (upward_factor <= MAX_COMPRESSION_RATIO) {
                final_gain_reduction = upward_factor * timeConstant;
            } else {
                final_gain_reduction = MAX_COMPRESSION_RATIO * timeConstant;
            }
            
            final_gain_reduction = exp(final_gain_reduction);
        }
        
    } else {
        // ====================================================================
        // ALTERNATIVE PROCESSING PATH (Linear/Expander Mode)
        // ====================================================================
        
        double linear_threshold = envelope_exp;
        double processed_input;
        
        if (envelope_sqrt <= linear_threshold) {
            // Below linear threshold - use knee coefficient for smooth transition
            processed_input = comp->knee_coeff * linear_threshold;
        } else {
            // Above linear threshold - use linear coefficient for expansion  
            double above_threshold = envelope_sqrt - linear_threshold;
            processed_input = comp->linear_coeff * above_threshold;
            processed_input = processed_input + linear_threshold;
        }
        
        // Ensure minimum processing level to prevent numerical issues
        double min_processing_level = 1e-30; // Very small threshold
        double final_level = (processed_input >= min_processing_level) ? processed_input : min_processing_level;
        
        // Convert to logarithmic domain for gain calculation
        double log_processed = log(processed_input + 1e-30) * LOG_SCALE_FACTOR;
        double log_final = log(final_level + 1e-30) * LOG_SCALE_FACTOR;
        
        // Store envelope state for next iteration
        comp->log_envelope = log_final;
        
        // Calculate gain reduction based on threshold comparison
        double threshold_diff = log_processed - comp->threshold;
        
        if (threshold_diff <= 0.0) {
            // ================================================================
            // BELOW THRESHOLD - EXPANSION/UPWARD COMPRESSION
            // ================================================================
            
            double expansion_gain = threshold_diff * timeConstant;
            comp->processed_envelope = exp(expansion_gain);
            
            double release_gain = comp->release_time - UNITY_GAIN;
            double final_expansion = release_gain * threshold_diff * timeConstant;
            final_gain_reduction = exp(final_expansion);
            
            // Apply minimum gain threshold
            if (final_gain_reduction <= MIN_GAIN_THRESHOLD) {
                final_gain_reduction = MIN_GAIN_THRESHOLD;
            }
            
        } else {
            // ================================================================
            // ABOVE THRESHOLD - STANDARD COMPRESSION  
            // ================================================================
            
            if (threshold_diff <= -NEGATIVE_THRESHOLD) {
                double standard_compression = threshold_diff * timeConstant;
                final_gain_reduction = exp(standard_compression);
            } else {
                // Maximum compression limiting to prevent distortion
                final_gain_reduction = MIN_GAIN_THRESHOLD;
            }
        }
    }
    
    // ========================================================================
    // FINAL OUTPUT PROCESSING
    // ========================================================================
    
    // Store final envelope state for UI display/metering
    comp->envelope_output = final_gain_reduction;
    
    // Apply to input signal with band-specific gain compensation
    double processed_output = final_gain_reduction * outputLevel * bandGain;
    
    return processed_output;
}

// ============================================================================
// COMPRESSOR PARAMETER CONTROL
// ============================================================================

void SetCompressorParameters(CompressorState* comp, double threshold, double ratio, 
                           double attack, double release, double upward_ratio)
{
    // Convert parameters to internal representation
    comp->threshold = threshold;
    comp->ratio_state = ratio;
    comp->attack_coeff = attack;
    comp->release_coeff = release;
    comp->upward_ratio = upward_ratio;
    
    // Calculate smoothing coefficient from attack time
    // Faster attack = higher coefficient (more responsive)
    comp->rms_smoothing_coeff = fmin(0.5, attack * 10.0);
}

void SetCompressorThreshold(CompressorState* comp, double threshold_db)
{
    // Convert dB to internal logarithmic representation
    comp->threshold = threshold_db * 0.11512925; // ln(10)/20 for dB conversion
}

void SetCompressorRatio(CompressorState* comp, double ratio)
{
    // Ratio of 1.0 = no compression, higher values = more compression
    comp->ratio_state = ratio;
    
    // Update related coefficients based on ratio
    if (ratio > 1.0) {
        // Compression mode
        comp->attack_coeff = 0.1 / ratio;    // Slower attack for higher ratios
        comp->release_coeff = 0.01 / ratio;  // Slower release for higher ratios
    } else {
        // Expansion mode
        comp->attack_coeff = 0.1 * ratio;    // Faster attack for expansion
        comp->release_coeff = 0.01 * ratio;  // Faster release for expansion
    }
}

void SetCompressorTiming(CompressorState* comp, double attack_ms, double release_ms, double sample_rate)
{
    // Convert milliseconds to per-sample coefficients
    double attack_samples = attack_ms * sample_rate / 1000.0;
    double release_samples = release_ms * sample_rate / 1000.0;
    
    // Calculate exponential coefficients for smooth envelopes
    comp->attack_coeff = 1.0 - exp(-1.0 / attack_samples);
    comp->release_coeff = 1.0 - exp(-1.0 / release_samples);
    
    // Update RMS smoothing based on attack time
    comp->rms_smoothing_coeff = comp->attack_coeff * 0.5;
}

// ============================================================================
// COMPRESSOR ANALYSIS & METERING
// ============================================================================

double GetCompressorGainReduction(CompressorState* comp)
{
    // Return current gain reduction in dB
    return 20.0 * log10(comp->envelope_output + 1e-30);
}

double GetCompressorRMSLevel(CompressorState* comp)
{
    // Return current RMS level in dB
    return 20.0 * log10(sqrt(comp->rms_smoother) + 1e-30);
}

bool IsCompressorActive(CompressorState* comp)
{
    // Return true if compressor is currently applying gain reduction
    return (comp->envelope_output < 0.95); // 5% threshold for "active"
}

/*
NOTES on OTT's Compression Algorithm:

This compression engine implements OTT's distinctive dual-mode processing:

1. **Dual Processing Paths**: The algorithm branches based on ratio_state vs 
   NEGATIVE_THRESHOLD, creating two completely different compression behaviors.

2. **Logarithmic Domain Processing**: All gain calculations happen in log domain
   for smooth, musical compression curves that avoid harsh artifacts.

3. **Upward AND Downward Compression**: Unlike typical compressors that only
   reduce gain above threshold, OTT simultaneously:
   - Expands quiet signals (upward compression)
   - Compresses loud signals (downward compression)

4. **Complex Envelope Following**: Multiple envelope followers with different
   time constants create the characteristic "pumping" effect.

5. **Ratio-Dependent Behavior**: The processing characteristics change 
   dramatically based on the compression ratio setting.

Key Features:
- **Musical Compression Curves**: Exponential gain changes sound natural
- **Dual-Slope Processing**: Different behavior above/below threshold
- **Advanced Envelope Control**: Separate attack/release for each mode
- **Numerical Stability**: Careful handling of edge cases and zero values

This algorithm is what gives OTT its distinctive sound - the combination of
upward and downward compression creates the characteristic "pumping" effect
that makes quiet parts louder and loud parts more controlled simultaneously.
*/
