# Stocks

MOEX requests use `iss.moex.com`. Stocks 0.2.15 packages Russian Trusted Root CA,
the same root used by Rates and Metals, for the chain observed on 2026-11-24.
Its SHA-256 fingerprint is
`D26D2D0231B7C39F92CC738512BA54103519E4405D68B5BD703E9788CA8ECF31`.

This CA update alone does not restore MOEX on Perinkpheral OS 0.1.96.
ESP-IDF 5.5.5's mbedTLS rejects NumericString subject attributes in the served
leaf certificate with -0x23E2 (invalid X.509 name, unexpected ASN.1 tag), before
checking its trust chain. A firmware parser correction or a compatible server
certificate is also required. RSA/SHA-256 support is already enabled.

The firmware repository's study `docs/decisions/0008-moex-tls.md` records the
certificate, native reproduction and earlier-run evidence. Simulator and
application checks use mocks and do not validate a live TLS handshake.
