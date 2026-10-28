# Assembly variants: independent loaded sheets

A 10 V source feeds two repeated sheets through 1 MΩ resistors. Their meters have
1 MΩ and 10 MΩ input resistance. Base outputs are 5 V and 100/11 V. Production
leaves A.R unfitted and changes B.R to 2 MΩ: outputs become 0 V and 25/3 V.

```sh
embsim run project.json --scenario scenario.json
embsim variant list project.json --json
embsim variant render project.json --name production > production.json
embsim run production.json --scenario scenario.production.json
```

The `loaded` alternative changes actual meter loading; `reference` leaves the
base unchanged. Open the base in the workbench and use Assembly variants for
native review, exports, activation and return-to-base. Keep exports beside this
project when using relative inputs. See the [contract](../../docs/engineering/assembly-variants.md).
