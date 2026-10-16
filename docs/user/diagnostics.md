# Diagnostic codes

Stable identifiers produced by the core (SPEC-001 §7). Each has a severity
default and a subject.

| Code | Default severity | Subject | Meaning |
|---|---|---|---|
| `schema.parse` | error | file | a description is not valid JSON |
| `schema.unknown_field` | error | file, node | a description contains a field its schema does not define |
| `schema.missing_field` | error | file, node | a required field is absent |
| `schema.type` | error | file, node | a field has the wrong type or an out-of-range value |
| `schema.version` | error | file | unsupported `schema_version` |
| `catalog.not_found` | error | id | a referenced component, board or MCU is not in any library |
| `catalog.pin_unknown` | error | instance, pin | a net references a pin the component does not have |
| `catalog.pin_twice` | error | instance, pin | a pin appears in two nets |
| `catalog.composite_depth` | error | id | composites nest deeper than 8 levels |
| `catalog.model_unsupported` | error | id | a composite is used where only a primitive can be |
| `catalog.port_missing` | warning | id, pin | a composite has no port for one of its pins |
| `catalog.instantiate` | error | id | a component could not be instantiated |
| `fidelity.tier_missing` | error | id | the session policy needs a fidelity tier the model does not declare |
| `analog.param_out_of_range` | error | instance, param | a parameter is outside its declared range |
| `net.contention` | warning | net | two drivers of equal strength disagree (two opposing weak pulls are a divider and are not reported) |
| `net.floating_input` | warning | pin | an input sampled a net with no driver and no pull |
| `net.unknown_input` | warning | pin | an input sampled X |
| `net.power_short` | error | net | two supplies with different nominal voltage on one net |
| `net.unconnected_required_pin` | warning | pin | a required pin is unconnected |
| `sched.delta_overflow` | fatal | targets | zero-delay propagation exceeded `max_delta_cycles` |
| `sched.time_overflow` | error | component | a delay overflows the time horizon |
| `sched.queue_full` | fatal | | the event queue capacity was exceeded |
| `analog.no_convergence` | error (fatal in strict) | nodes | the operating point or a step did not converge |
| `analog.step_failure` | fatal | island | consecutive step rejections exceeded the limit |
| `analog.timestep_too_small` | fatal | device | the step fell below `h_min` |
| `analog.singular_topology` | error | node | a floating node or inconsistent sources |
| `analog.implicit_supply` | info | pin | a pull or bridge used the family's nominal supply |
| `analog.implicit_ground` | info | node | an island with no ground reference was given one |
| `analog.supply_driven` | info | net | a power pin's net is driven by a component, so its ideal supply is not added |
| `analog.overvoltage`, `analog.overcurrent` | warning | device | a declared absolute maximum exceeded |
| `island.requires_electrical` | error | instance | a component has no functional model under functional policy |
| `mcu.no_firmware` | info (warning for an input) | board | a board has no firmware: its MCU's pins stay inputs; an input to it has nowhere to go |
| `mcu.no_image` | warning | board | the firmware's image file is missing: the core is held in reset until one is loaded |
| `mcu.image` | error | board | the image could not be loaded (not ELF or HEX, wrong machine, outside memory) |
| `mcu.register_file` | error | board | the MCU's register description is missing or invalid |
| `mcu.model_missing` | info | board, peripheral | a peripheral is bound to a model this build does not have: its registers still hold values |
| `mcu.unsupported_access` | warning | board, register, pc | the first access to a peripheral with registers and no model |
| `mcu.clock_gated` | warning | board, register, pc | an access to a peripheral whose clock is off: reads 0, writes ignored |
| `mcu.bus_error` | warning | board, address, pc | nothing answered an access: the core takes a bus fault |
| `mcu.held_in_reset` | warning | board, register, pc | an access to a peripheral its reset controller holds in reset (the RP2040's RESETS): reads its reset values, writes ignored |
| `mcu.no_boot2` | warning | board | the RP2040's bootrom finds no boot2 with a good CRC in flash: the part waits in its USB boot |
| `mcu.watchdog_reset` | info | board | the watchdog reset the chip (its counter reached 0, or CTRL.TRIGGER) |
| `mcu.easydma_range` | warning | board, peripheral | an nRF52840 EasyDMA buffer outside Data RAM: nothing is moved |
| `mcu.boot_mode` | warning | board | BOOT0 selects system memory, which is not modelled |
| `mcu.exit` | info | board | the firmware exited through semihosting, with its status |
| `mcu.lockup` | error | board | the core locked up (a fault with nowhere to go) |
| `input.rejected` | warning | component | a component refused an input |
| `probe.invalid` | warning | probe | a probe could not be created |
| `io.read` | error | file | a file could not be read |
| `ext.budget_exceeded` | error | instance | a Lua evaluation exceeded its instruction budget |
| `limit.exceeded` | fatal | limit | a configured capacity was exceeded |
