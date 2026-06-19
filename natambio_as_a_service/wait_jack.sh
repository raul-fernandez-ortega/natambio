#!/bin/bash
#
# ExecStartPre for natambio.service: waits until jackd is serving ports
# (jack_lsp returns at least one entry) before starting natambio. Avoids the
# race after a jackd restart. Does not block indefinitely: if no ports appear
# within ~10 s, exits 0 and natambio will retry via Restart=always.

for _ in $(seq 1 100); do
    jack_lsp 2>/dev/null | grep -q . && exit 0
    sleep 0.1
done
exit 0
