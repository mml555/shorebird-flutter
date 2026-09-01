# Route B super — 2C candidate Flutter revision

Pins `bin/internal/engine.version` to the engine artifact this qualification
run actually built and measured, so the value is a lookup key derived from real
bytes rather than an invented routing token.

    engine source   mml555/shorebird-flutter
                    route-b-2c-candidate
                    dfa2b24ac38477f3705ff0357530f33fe09474b8
    built artifact  iOS device-slice Flutter binary
                    sha1   a5a8be5854c529268378ce16762a16d6e31763e9   <- engine.version
                    sha256 2fa8b808e863552f1ebf9ffaa8b460c299b16241d68cfb19689798534e555f58
                    size   19,104,576

The engine source differs from the certified `619fdad1` by exactly one
retained, non-executable provenance atom in `shell/common/shorebird/shorebird.cc`.
Runtime semantics are intended unchanged; artifact identity is deliberately
distinct.

CANDIDATE ONLY. This revision exists to qualify the Route B direct-super
compiler (analyzer v11 + patch 0017) end to end. It does not promote anything:
`compatibility.yaml` is untouched, the certified engine `619fdad1` and the
published cell `4792f0ec` are untouched.
