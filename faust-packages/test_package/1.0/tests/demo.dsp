// Demo using the x package

import("test_package.lib");

vol_db   = hslider("[2] volume [unit:dB]", -96, -96, 0, 1);
freq_hz  = hslider("[1] freq [unit:Hz]", 1000, 10, 20000, 1);
dest_idx = hslider("[3] destination", 0, 0, 8, 1);

sine_on  = checkbox("sine wave");
white_on = checkbox("white noise");
pink_on  = checkbox("pink noise");

process = vgroup("Audio Tester",
    x_tester(freq_hz, vol_db, dest_idx, sine_on, white_on, pink_on)
);