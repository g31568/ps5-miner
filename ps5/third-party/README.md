# Native dependencies

`nlohmann/json.hpp` and `nlohmann/LICENSE.MIT` are from nlohmann/json v3.11.3,
commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`. The native pool adapter uses
its nonthrowing parser behind a 16-KiB message and 16-level depth limit.

Native TLS builds Mbed TLS 3.6.5, commit
`e185d7fd85499c8ce5ca2a54f5cf8fe7dbe3f8df`, in the ignored build tree. Desktop
TLS uses installed OpenSSL. Both follow MOM: `tls_verify: true` enables chain
and hostname verification; the default accepts self-signed certificates.
Mbed TLS uses its parent-pinned framework submodule and Apache-2.0 license;
its source and license remain in `build/MbedTLS`.
SOCKS5 uses the no-authentication method and proxy-side DNS for the pool host.
