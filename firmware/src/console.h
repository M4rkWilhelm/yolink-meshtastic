// Setup console on the header's serial pins (pad 2 = TX, pad 3 = RX, pad 4 =
// GND; 115200 8N1, 3.3 V). Entered by holding the button at power-up. Commands
// follow the Meshtastic CLI naming; type `help` once connected.
#pragma once

namespace console {

// Runs until `exit` or the idle timeout, then reboots (which hands PA13/PA14
// back to SWD). Never returns.
[[noreturn]] void run();

}  // namespace console
