# Linear RLC AC response

A 1 V real phasor drives 10 Ω, 10 mH and 1 µF in series. The output is across
the capacitor. At `1 / (2π√(LC)) ≈ 1591.549431 Hz`, its gain is 10 V/V (20 dB)
and its phase is −90°. This is voltage magnification in the reactive circuit,
not power gain. The source's `dc: 0` belongs to bias/transient behavior; the AC
request explicitly selects a unit perturbation independently of that value.

```sh
embsim ac examples/ac-rlc/project.json \
  --request examples/ac-rlc/response.ac.json > /tmp/rlc-ac.json
python3 tools/validate.py /tmp/rlc-ac.json --strict
```

Open the circuit in the native workbench, choose **AC sweep**, select `V1`,
reference `net:GND` and output `net:OUT`. Sweep 10–100000 Hz or use equal
1591.549431 Hz endpoints with one point to inspect the resonance exactly.

The [AC contract](../../docs/engineering/ac-sweeps.md) states the linear model,
loading, numerical limits and unsupported effects. DC/transient startup,
parasitic losses not explicitly configured, nonlinear behavior, noise and
manufacturer/hardware conformance are not implied.
