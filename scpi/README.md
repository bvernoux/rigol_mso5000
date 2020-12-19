# Rigol MSO5000 SCPI tests for waveform
This C source code is multi-platform portable code intended to test Rigol MSO5000 SCPI commands over Ethernet especially to retrieve as fast(and as reliable) as possible waveform data.

How to Build GNU/Linux
* cd to current directory
* make clean all

How to Build Windows with MSYS2 mingw64
* Launch C:\msys64\mingw64.exe
* cd to current directory
* mingw32-make clean all

Usage:
* `MSO5000_SCPI <hostname> <port> [-n<nb_waveform>] [-f<waveform_rx_raw_data.bin>]`

Example:
* `MSO5000_SCPI 10.0.0.1 5555 -n10`
* `MSO5000_SCPI 10.0.0.1 5555 -n10 -fwaveform_rx_raw_data.bin`
