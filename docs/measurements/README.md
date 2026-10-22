# Measurements

Raw captures taken during bring-up and verification (2026-10). Images only; numbers are read off the captures.

## Output signals

Port output on the oscilloscope (Keysight Infiniium), per port, frequency and attenuator setting. File name:
`PORT<n>_<frequency>_<attenuation>.jpeg` (`-31-75dB` = the attenuator at its maximum, 31.75 dB). At 50 MHz the
output is far from a sine: the lowest LPF cuts off at 947 MHz, so the synthesizer's harmonics pass; from 500 MHz on
the LPF bank turns it into a sine.

| Frequency | Port 1 | Port 2 |
|---|---|---|
| 50 MHz, 0dB | [image](Output%20Signals/PORT1_50M_0dB.jpeg) | [image](Output%20Signals/PORT2_50M_0dB.jpeg) |
| 500 MHz, 0dB | [image](Output%20Signals/PORT1_500M_0dB.jpeg) | [image](Output%20Signals/PORT2_500M_0dB.jpeg) |
| 600 MHz, 0dB | [image](Output%20Signals/PORT1_600M_0dB.jpeg) | [image](Output%20Signals/PORT2_600M_0dB.jpeg) |
| 2000 MHz, 0dB | [image](Output%20Signals/PORT1_2000M_0dB.jpeg) | [image](Output%20Signals/PORT2_2000M_0dB.jpeg) |
| 2000 MHz, −31.75 dB | [image](Output%20Signals/PORT1_2000M_-31-75dB.jpeg) | [image](Output%20Signals/PORT2_2000M_-31-75dB.jpeg) |

![Port 1, 500 MHz, 0 dB](Output%20Signals/PORT1_500M_0dB.jpeg)

## Comparison with the LibreVNA

The same DUTs measured with the reference LibreVNA (`LibreVNA *.jpeg`, LibreVNA-GUI) and with the SNA
(`SNA COLD/WARM *.png`, SNA client). COLD = right after power-up, WARM = after the board has warmed up.

| DUT | LibreVNA | SNA cold | SNA warm |
|---|---|---|---|
| BPF 175-237M | [image](LibreVNA%20Comparison/LibreVNA%20BPF%20175-237M.jpeg) | [image](LibreVNA%20Comparison/SNA%20COLD%20BPF%20175-237M.png) | [image](LibreVNA%20Comparison/SNA%20WARM%20BPF%20175-237M.png) |
| LPF 1G | [image](LibreVNA%20Comparison/LibreVNA%20LPF%201G.jpeg) | [image](LibreVNA%20Comparison/SNA%20COLD%20LPF%201G.png) | [image](LibreVNA%20Comparison/SNA%20WARM%20LPF%201G.png) |
| THRU | [image](LibreVNA%20Comparison/LibreVNA%20THRU.jpeg) | [image](LibreVNA%20Comparison/SNA%20COLD%20THRU.png) | [image](LibreVNA%20Comparison/SNA%20WARM%20THRU.png) |
| THRU (NON Reference cable) | [image](LibreVNA%20Comparison/LibreVNA%20THRU%20%28NON%20Reference%20cable%29.jpeg) | [image](LibreVNA%20Comparison/SNA%20COLD%20THRU%20%28NON%20Reference%20cable%29.png) | [image](LibreVNA%20Comparison/SNA%20WARM%20THRU%20%28NON%20Reference%20cable%29.png) |
| OPEN | — | [image](LibreVNA%20Comparison/SNA%20COLD%20OPEN.png) | [image](LibreVNA%20Comparison/SNA%20WARM%20OPEN.png) |

`BPF 175-237M` = band-pass 175–237 MHz, `LPF 1G` = 1 GHz low-pass, `THRU` = the calibration thru cable,
`THRU (NON Reference cable)` = a different cable than the one used for calibration, `OPEN` = open ports.
