# BatteryMonitor initial validation plan

## Scope

The first validation has two runs:

1. one **15-minute commissioning rehearsal** that deliberately exercises the
   end-to-end firmware and persistence path, then is erased;
2. one **full unattended calibration** using the same hardware arrangement.

The rehearsal is long enough to be meaningful without becoming a second
battery-capacity test. With the current configuration it covers approximately
9,000 sensor samples and 15 separately flushed journal intervals. At the
expected roughly 0.5 A load it also accumulates well over 100 mAh, making
scaling, clock, and integration errors visible above integer rounding.

No periodic `/battery status`, `/battery calibrate status`, `/metrics`, or
`/monitor` polling is part of either run.

## Run A: 15-minute commissioning rehearsal

### Start and electrical cross-check (about two minutes)

1. Fully charge the pack. Power the ESP32 and INA logic independently so it
   remains alive when the battery path opens.
2. Connect the intended 50 ohm load through the same shunt and wiring that will
   be used for the full calibration.
3. Start one continuous capture:

   ```sh
   bin/monitor-multi scratch --timestamps --strip-ansi \
       --no-suppress-repeats --log-file /tmp/battery-rehearsal.log
   ```

4. Run `/battery status` once and `/battery calibrate start` once.
5. Check the first measurement against the load itself:

   ```text
   expected current [A] = battery voltage [V] / actual load resistance [ohm]
   expected power [W]   = battery voltage [V] * measured current [A]
   ```

   Current and power should agree within 5% when only the nominal 50 ohm value
   is known, or within the combined component tolerances when the hot load
   resistance has been measured. If a multimeter is available, a single pack
   voltage comparison should agree within 1% or 100 mV, whichever is larger.

Stop here if the command does not enter `Discharging`, the current sign or
scale is wrong, or a sampling/storage error appears.

### Let the rehearsal run (15 minutes, no interaction)

The firmware must emit one persisted-interval report per minute. After 15
reports, the capture must show:

- one unchanged session ID and contiguous interval numbers;
- elapsed time increasing by approximately 60 seconds per interval;
- roughly 600 samples per full interval at the configured 100 ms rate;
- `maximumGapMs` below the 2,000 ms invalidation limit;
- monotonically increasing cumulative mAh and mWh;
- no `Invalid`, `Aborted`, sampling, or storage failure.

The host validator performs the exact record, cadence, and independent
integration checks; no manual minute-by-minute arithmetic is required.

### Exercise cutoff, finalization, and restore (about three minutes)

1. After the fifteenth report, simulate the BMS opening by disconnecting the
   battery upstream of both the INA bus-voltage sense and the load. Keep the
   ESP32/INA logic powered.
2. The measured bus must become `Absent` (at most 1 V and 10 mA), remain there
   for the configured five-second dwell, then transition through `Finalizing`
   to `Complete`. Allow up to two minutes for the journal scan and 101 profile
   point writes.
3. Download LittleFS once with `bin/littlefs-download scratch`, then run the
   standalone validator on the extracted `battery.bin` and captured log:

   ```sh
   bin/validate-battery-calibration path/to/battery.bin \
       --log /tmp/battery-rehearsal.log
   ```
4. Reboot once and run `/battery profiles` once. The rehearsal profile must be
   restored as the active complete profile.
5. Erase the deliberately short profile with `/rm /battery.bin`, reboot, and
   run `/battery profiles` once to confirm that no profile remains.

The rehearsal passes only if the validator passes, the profile survives the
first reboot, and cleanup survives the second reboot. Recharge the small amount
used by the rehearsal before the full calibration.

## Run B: full unattended calibration

1. Reconnect the fully charged pack and the same fixed load.
2. Start a timestamped capture and run `/battery status` once followed by
   `/battery calibrate start` once.
3. Leave the otherwise-idle system alone until automatic cutoff and
   finalization. The serial capture is evidence, not a control dependency; the
   CRC-protected journal remains authoritative if the host capture stops.
4. After the automatic `Complete` report, download `battery.bin`, run the same
   validator, reboot once, and confirm the learned profile is active with one
   `/battery profiles` command.

The full run passes when:

- every journal record has a valid CRC and contiguous per-session sequence;
- interval elapsed time and cumulative charge/energy are monotonic;
- integration reconstructed from interval averages agrees with the footer
  within 2% or 2 mAh/20 mWh, whichever absolute allowance is larger;
- the footer checksum, interval count, and 101-point curve are internally
  consistent;
- no sampling gap or terminal invalidation occurred; and
- the completed profile is restored after reboot.

The 10 Ah nameplate value is only a plausibility reference. It is not the
validity oracle when no calibrated battery tester is available.

## Required automation

- BatteryMonitor emits timestamped progress automatically after each successful
  minute-record flush and emits a complete final summary.
- Existing `bin/monitor-multi` provides the timestamped host recording; a
  second recorder is unnecessary.
- A standard-library-only host validator checks `battery.bin` and optionally
  cross-checks the progress log. It is commissioning tooling, not a runtime
  dependency and not part of routine future calibrations.

After these two initial runs pass, future calibrations require only one start
command and one final result check.
