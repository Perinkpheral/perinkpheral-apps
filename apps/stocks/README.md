# Stocks

MOEX requests use `iss.moex.com`. Stocks packages Russian Trusted Root CA,
the same root used by Rates and Metals. Perinkpheral OS 0.2.0 includes the
NumericString certificate parser correction required by the observed MOEX chain.
The root's SHA-256 fingerprint is
`D26D2D0231B7C39F92CC738512BA54103519E4405D68B5BD703E9788CA8ECF31`.

A failed preference restore leaves saved preferences untouched until a later load
can read them. Failed quote or chart reads skip that update; they never replace an
unreadable record with an empty one. A later successful read permits updates again.

The firmware repository's study `docs/decisions/0008-moex-tls.md` records the
certificate, native reproduction and earlier-run evidence. Simulator and
application checks use mocks and do not validate a live TLS handshake.
