# Diagnostic codes

Stable identifiers produced by the core (SPEC-001 §7). Each has a severity
default and a subject.

| Code | Default severity | Subject | Meaning |
|---|---|---|---|
| `schema.unknown_field` | error | file, node | a description contains a field its schema does not define |
| `schema.missing_field` | error | file, node | a required field is absent |
| `schema.type` | error | file, node | a field has the wrong type or an out-of-range value |
| `schema.version` | error | file | unsupported `schema_version` |
| `catalog.not_found` | error | id | a referenced component, board or MCU is not in any library |
| `catalog.pin_unknown` | error | instance, pin | a net references a pin the component does not have |
| `catalog.pin_twice` | error | instance, pin | a pin appears in two nets |
| `fidelity.tier_missing` | error | id | the session policy needs a fidelity tier the model does not declare |
| `analog.param_out_of_range` | error | instance, param | a parameter is outside its declared range |
| `net.contention` | warning | net | two drivers of equal strength disagree |
| `net.floating_input` | warning | pin | an input sampled a net with no driver and no pull |
| `net.unknown_input` | warning | pin | an input sampled X |
| `net.power_short` | error | net | two supplies with different nominal voltage on one net |
| `net.unconnected_required_pin` | warning | pin | a required pin is unconnected |
| `sched.delta_overflow` | fatal | targets | zero-delay propagation exceeded `max_delta_cycles` |
| `sched.queue_full` | fatal | | the event queue capacity was exceeded |
| `analog.no_convergence` | error (fatal in strict) | nodes | the operating point or a step did not converge |
| `analog.step_failure` | fatal | island | consecutive step rejections exceeded the limit |
| `analog.timestep_too_small` | fatal | device | the step fell below `h_min` |
| `analog.singular_topology` | error | node | a floating node or inconsistent sources |
| `analog.implicit_supply` | info | pin | a pull or bridge used the family's nominal supply |
| `analog.overvoltage`, `analog.overcurrent` | warning | device | a declared absolute maximum exceeded |
| `island.requires_electrical` | error | instance | a component has no functional model under functional policy |
| `mcu.unsupported_access` | per policy | board, register, pc | an access to an unmodelled register or field |
| `mcu.bus_error` | per policy | board, address, pc | nothing answered at an address |
| `ext.budget_exceeded` | error | instance | a Lua evaluation exceeded its instruction budget |
| `limit.exceeded` | fatal | limit | a configured capacity was exceeded |
