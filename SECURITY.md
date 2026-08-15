# Security boundary

## Current scope

This is an educational and portfolio firmware project. Do not deploy it as a
safety function or expose it directly to an untrusted network.

- ESP8266 MQTT and OTA HTTP currently use raw TCP without TLS.
- OTA CRC32 and SHA-256 detect accidental corruption, but there is no digital
  signature or trusted-key verification, so package authenticity is not proven.
- Wi-Fi and broker settings are compile-time development placeholders. Never
  commit real credentials; provision them through a protected production path.
- Board-level electrical protection, independent watchdog timing, brownout
  behavior, flash endurance and rollback remain Board Unverified.

## Reporting

Open a private security advisory in the GitHub repository for vulnerabilities.
Do not include real credentials, private keys or production firmware images in
public issues.
