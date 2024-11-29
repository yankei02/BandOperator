# Exchange Band Audio Processor

An audio plugin that exchanges frequency bands between a main input and a sidechain input using FFT-based processing.

## Introduction

The Exchange Band Audio Processor is a plugin designed for audio signal processing applications. It allows users to exchange specific frequency bands between a main audio signal and a sidechain signal. This can be used for creative sound design, mixing, or mastering purposes.

## Features

- **Frequency Band Exchange**: Swap or blend specific frequency bands between two audio inputs.
- **FFT Processing**: Utilizes Fast Fourier Transform for frequency domain manipulation.
- **Customizable Parameters**: Adjust cutoff frequencies, band lengths, and mix ratios.
- **Sidechain Support**: Processes sidechain input for advanced audio effects.

## Installation

1. **Clone the Repository**

   ```bash
   git clone https://github.com/yourusername/exchange-band-audio-processor.git
   ```

2. **Build the Plugin**

   - Ensure you have JUCE and a compatible C++ compiler installed.
   - Open the project in your preferred IDE (Visual Studio, Xcode, etc.).
   - Build the project to create the plugin binary.

## Usage

- **Load the Plugin**: Insert the plugin into your DAW (Digital Audio Workstation) as an effect.
- **Configure Inputs**:
  - **Main Input**: The primary audio signal you want to process.
  - **Sidechain Input**: The secondary audio signal for exchanging frequency bands.
- **Adjust Parameters**:
  - **Cutoff Frequencies**: Set `CutFrequencyFrom1` and `CutFrequencyFrom2` to define the center frequencies of the bands to exchange.
  - **Band Length**: Use `FrequencyBandLength` to adjust the width of the frequency bands.
  - **Mix Ratios**: Control `Band1Mix` and `Band2Mix` to blend the exchanged bands.
  - **Exchange Mode**: Toggle `ExchangeBandValue` to switch between swapping and blending modes.

## Parameters

- `CutFrequencyFrom1`: Center frequency for the first band (in Hz).
- `CutFrequencyFrom2`: Center frequency for the second band (in Hz).
- `FrequencyBandLength`: Ratio determining the width of the frequency bands.
- `ExchangeBandValue`: Toggle between exchange modes (0.0 for blending, 1.0 for swapping).
- `Band1Mix`: Mix ratio for the first band (0.0 to 1.0).
- `Band2Mix`: Mix ratio for the second band (0.0 to 1.0).

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Author

- **Enqi Lian**
- [enqilian619@gmail.com](mailto:enqilian619@gmail.com)

## Acknowledgments

- Built using [JUCE](https://juce.com), a widely used framework for audio application development.
