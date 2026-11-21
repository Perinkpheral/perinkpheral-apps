# Cached ISS observation

The four `iss-error-<language>-<font>` scenarios seed the same version 2 snapshot,
then answer every poll with HTTP 503. The retained observation is at 09:05 UTC
on 3 December 2033; the scenario starts two hours later. The failure and that
observation time must remain readable in English and Russian with both fonts.

The scenario store writes UTF-8 text, including escaped zero bytes. This date
and these values let the binary snapshot fit that representation exactly.
Generate its `store.state` string with Python's standard library:

```python
import struct

fields = [2, 1, 0, 5123, 2560, 421123, 27500, 0, 0, 0, 0x02010101, 0, 0]
snapshot = (struct.pack("<13i", *fields) + bytes(90 * 8)).decode("utf-8")
```

The fields are version, has-data, initial status, latitude, longitude, altitude,
speed, two reserved values, second, minute, track count and track cursor.
The unused track points are zero. The first mocked answer replaces the initial
status with 503. Native `apps/iss/test/failure.c` covers failures after a real
successful answer, repeated failures, reload and recovery.
