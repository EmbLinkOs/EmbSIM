# A loaded native RC spectrum

A 1 kHz, 1 V-peak sine drives 10 kΩ and 10 nF. Capture 16–32 ms after warm-up with
256 intervals. Expected output peak is about 0.846733 V; cosine-referenced phase
is about −122.141908° because the source sine has −90° cosine phase.

```sh
embsim transient project.json --request response.transient.json > /tmp/rc-capture.json
embsim spectrum /tmp/rc-capture.json --target net:OUT --window hann > /tmp/rc-spectrum.json
```

In the schematic use Transient capture, enter the endpoints and select Use 256 FFT
intervals, then capture IN/OUT and open Frequency spectrum. The last endpoint is
retained in the raw capture but excluded from the transform. See
[scaling, loaded acceptance and limits](../../docs/engineering/frequency-spectrum.md).
