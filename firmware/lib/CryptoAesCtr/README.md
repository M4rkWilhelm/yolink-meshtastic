Unmodified AES-128, AES-256 and CTR files from [rweather/Crypto](https://github.com/rweather/arduinolibs)
0.4.0, Copyright (C) Southern Storm Software, Pty Ltd, MIT license (see each file's header).

Only the files needed for AES-CTR are included. The library's `RNG.cpp` defines a
global `RNG` object that collides with the STM32 CMSIS `RNG` peripheral macro, so it is
left out.
