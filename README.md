<img width="2500" height="1000" alt="LOGO" src="https://github.com/user-attachments/assets/fa075a60-3d34-49a5-b111-088fc0b894fd" />

This is a reverse-engineered implementation of Xfer Records' OTT multiband compressor.


**Note:** This is still a work in progress. Building does not work.

## What’s Inside

A basic **C implementation** of OTT’s DSP core:
- 3-band crossover filtering
- Upward + downward compression
- Parameter mapping similar to the VST
- Peak detection and envelope following

## File Layout

```
ott_plugin.h           - Main header with structures/constants
ott_processing.c       - Core audio engine
ott_filters.c          - Biquad filter code  
ott_compression.c      - Compression logic
ott_parameters.c       - Parameter mapping and control
ott_main.c             - Plugin init/integration
README.md              - You’re here
```

### Architecture:
1. **Dual-output biquads** – lowpass and highpass simultaneously.
2. **Ratio math** – `(x-0.5)*16+1` scaling.
3. **Upward + downward compression** – expands quiet parts and clamps loud parts.
4. **Log-based gain math** – uses log domain for smoother behavior.
5. **Advanced mode** – toggles between different internal behaviors.

- `ENVELOPE_DECAY_RATE`: `2.49999994e-05f`
- `COMPRESSION_SCALING`: `0.519999981f`
- `LOG_SCALE_FACTOR`: `0x40215f2ced384f29`
- `DELAY_BUFFER_SIZE`: `0x8000` (32768)

## Building

**Warning:** Building is iffy right now. It compiles, but integration and some features still need work.

### Example Build Command
```bash
gcc -c ott_processing.c ott_filters.c ott_compression.c ott_parameters.c ott_main.c
ar rcs libott.a *.o
```

## Parameters

| Parameter        | Index | Range  | Description          |
|------------------|-------|--------|----------------------|
| Depth            | 0     | 0.0-1.0| Compression amount   |
| Time             | 1     | 0.0-1.0| Attack/release time  |
| Upward Ratio     | 2     | 0.0-1.0| Upward compression   |
| Downward Ratio   | 3     | 0.0-1.0| Downward compression |
| Advanced Mode    | 4     | 0/1    | Switch advanced algo |
| Low/Mid/High Band| 5-7   | 0.0-1.0| Band controls        |
| Low/Mid/High Gain| 8-10  | 0.0-1.0| Band gains           |
| Switches 1-6     | 11-16 | 0/1    | Misc toggles         |
| Controls 1-2     | 17-18 | 0.0-1.0| Extra parameters     |
| Bypass           | 19    | 0/1    | Master bypass        |

## TODO
- GUI remake
- Fix building issues
- Organize and clean up code
- Port to other plugin formats (e.g., AU, LV2)
