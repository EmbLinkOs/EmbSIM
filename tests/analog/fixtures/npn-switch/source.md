# npn-switch — analytic bounds (and ngspice)

Common-emitter switch: a 1 kΩ base resistor from a 0/3.3 V control, a 220 Ω
collector load from 3.3 V, emitter to ground. Ebers-Moll with bf = 100,
is = 1e-14, vaf = 100 V.

Cutoff (ctl = 0 V): no base current, the collector sits at 3.3 V.

Saturation (ctl = 3.3 V): ib ≈ (3.3 − 0.75) / 1 kΩ ≈ 2.5 mA; the collector
current demanded by the load, (3.3 − Vce) / 220 Ω ≈ 14 mA, is far below
bf · ib = 250 mA, so the transistor saturates with Vce(sat) under 0.3 V
(Ebers-Moll gives ≈ 0.05 to 0.15 V at this overdrive). The bounds above
express that; ngspice solves the same circuit:

    Q1 c b 0 QN
    .model QN NPN(IS=1e-14 BF=100 BR=1 VAF=100)
    RB ctl b 1k
    RC vcc c 220
    Vcc vcc 0 3.3
    V1 ctl 0 PULSE(0 3.3 0 10n 10n 1m 2m)
    .tran 1u 2m

The ngspice run of this circuit is `ngspice.json` (tools/analog/ngspice_fixture.py).
