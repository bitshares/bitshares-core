# Provenance and licenses

This directory is a secp256k1 implementation of two existing protocols. It is not a new cryptographic construction.

## Bulletproofs+

- Paper: Chung, Han, Kim, Seo, "Bulletproofs+: Shorter Proofs for Privacy-Enhanced Distributed Ledger", https://eprint.iacr.org/2020/735 (17 Jun 2020 version, as cited by Monero).
- Source structure: Monero `src/ringct/bulletproofs_plus.cc` from tag `v0.18.3.4`. Copyright (c) 2017-2022 The Monero Project. BSD-3-Clause. See `LICENSE.monero`.
- Differences from that file, all intentional:
  - Group is secp256k1 (cofactor 1). Monero's multiply-by-8 / divide-by-8 cofactor clearing is omitted.
  - Commitments are BitShares Pedersen commitments `blind*G + value*H`, with `H` equal to secp256k1-zkp's generator G2 (MIT, `libraries/fc/vendor/secp256k1-zkp` commit `bd067945`).
  - Fiat-Shamir transcript is SHA-256 with the domain strings in `bulletproofs_plus.cpp`, not Keccak. Proofs are not byte-compatible with Monero.
  - Commitments are passed in by the caller. They are not embedded in the proof bytes.

## CLSAG

- Paper: Goodell, Noether, et al., "Concise Linkable Ring Signatures and Forgery Against Adversarial Keys", https://eprint.iacr.org/2019/654.
- Source structure: Monero `src/ringct/rctSigs.cpp` `CLSAG_Gen` / `verRctCLSAGSimple`, same tag and license.
- Same curve, cofactor, and hash differences as above. Signatures are not byte-compatible with Monero.

## Group arithmetic

OpenSSL libcrypto (Apache-2.0) provides secp256k1 point and scalar operations, including `EC_POINTs_mul`. That call is deprecated in OpenSSL 3 but still implements the same group law. The library is compiled with `-Wno-deprecated-declarations` for that reason.

## What this is not

This code has not had an independent cryptographic audit. Consensus behavior of a randomized prover is tested by round-trip, negative tests, and one locked proof vector. See `libraries/ctcrypto/doc/AUDIT.md`. Do not activate the hardfork times on BitShares mainnet.
