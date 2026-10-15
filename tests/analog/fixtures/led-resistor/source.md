# led-resistor — analytic bounds (ngspice golden pending)

Red LED (vf ≈ 1.8 V at 20 mA, n = 2) with 100 Ω from 3.3 V, and a green LED
(vf ≈ 2.1 V) with 220 Ω from 5 V:

    i_red   ≈ (3.3 − 1.8) / 100 = 15.0 mA  (the forward drop is slightly lower
              at 15 mA than at 20 mA, so the current is a little higher)
    i_green ≈ (5.0 − 2.1) / 220 = 13.2 mA
    luminance = i / 20 mA, saturating at 1

Bounds are ±10 % on the current and the matching luminance window. An
ngspice golden (LED as `D(IS=... N=2)`) is **pending**.
