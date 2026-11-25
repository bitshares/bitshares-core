# Local security audit: confidential transfer upgrade

This is a review of the code in this tree. It is **not** an external cryptographic audit. Do not treat a passing test run as permission to activate the placeholder hardfork times on BitShares mainnet.

The work is based on abitmore/bitshares-core tag `7.0.4-patch3` (`5eb091e`), which already calls `verify_blind_output_range_proofs` after soft-fork time `1795165200`. This upgrade keeps full range-proof verification: before BSIP-CT2 the Borromean verifier still runs, and after BSIP-CT2 a Bulletproofs+ proof is required.

## Methodology

- Read the blind evaluators, the Pedersen commitment helpers in secp256k1-zkp `bd067945`, and Monero 0.18.3.4 `bulletproofs_plus.cc` and `CLSAG_Gen` / `verRctCLSAGSimple`.
- Ported those algorithms onto secp256k1 with OpenSSL group operations. Differences (cofactor 1, SHA-256 transcript, BitShares generators) are in `libraries/ctcrypto/NOTICE.md`.
- Unit-tested the primitives: honest proofs for 0, 1, 2, 1000, 2^64-1, and aggregates; tamper, truncation, empty input, wrong commitment, commitment to -1, batch weights; CLSAG rings of 4 and 16; forged response, wrong message, wrong pseudo-output, substituted commitment; stable key image.
- Locked one Bulletproofs+ byte string for openings (blind=3, value=1) and (blind=9, value=2).
- Ran a 2000-iteration deterministic fuzzer (`ct_fuzz`) and a one-hour libFuzzer campaign per harness (`libraries/ctcrypto/src/fuzz_bp.cpp`, `fuzz_clsag.cpp`) with ASan and UBSan. Seeds and the grown corpus are in `tools/ct_reference/corpus`.
- Cross-checked Bulletproofs+ and CLSAG with `tools/ct_reference/reference.py`. Group law is coincurve (libsecp256k1), not OpenSSL. Challenge weights are the explicit bit product that matches the C++ cache. `tools/ct_reference/crosscheck.sh` proves in each implementation and verifies in the other, including an aggregate of 0, 1000, and 2^64-1. A flipped proof is rejected.
- Chain tests on a private in-process database (no P2P, no mainnet genesis): hardfork boundary, memo rules, Borromean rejected after CT2, Bulletproofs+ accepted, legacy spend rejected after CT3, 16-member ring spend, key image double-spend, commitment retained, confidential supply decreased only by the public fee, signed block produced and replayed with `pop_block` / `push_block`. The pre-existing `confidential_tests` suite still passes.
- Two-process private testnet (`tests/private_testnet/two_node_local.sh`) using `libraries/egenesis/genesis-dev.json`. Chain id `c3807ea4b7d28da76249c6be53dc6342a12888327aab9d66ac1d53e31e797ae3` (not the mainnet id). Both nodes bound p2p and RPC to `127.0.0.1` and were given an empty public seed list; the follower's only seed was `127.0.0.1:18001`. The producer generated blocks 1 through 6. The follower applied blocks through `#4` (`0000000400ced49a84ffcf9c3a513feb716efe58`). After a clean stop, `--revalidate-blockchain --force-validate` replayed that log and reported "Started BitShares node on a chain with 4 blocks" with the same chain id. Logs were checked for the public seed hostnames and none appeared. No confidential operation was sent to this network; those operations were tested in-process only.
- Release build: `g++`, `-O3`, unity build off. Debug build: `g++`, `-O0`, unity build off. Both produced `ctcrypto_tests`, `ct_fuzz`, `chain_test`, `witness_node`, and `cli_wallet`.

Nothing in this audit was broadcast. `witness_node` in the Release build embeds the public mainnet genesis (`4018d784…`). It was not started.

## Findings

### F1 — Critical, fixed: ring spend plus legacy spend would double-spend

Keeping a commitment as a decoy means `blind_transfer` / `transfer_from_blind` could still spend it with the owner key after a CLSAG spend. That is inflation.

Fix: after `HARDFORK_BSIP_CT3_TIME`, those two operations are rejected in `transfer_from_blind_evaluator::do_evaluate` and `blind_transfer_evaluator::do_evaluate` (`libraries/chain/confidential_evaluator.cpp`). The chain test pushes `transfer_from_blind` after the hardfork and expects a throw. New spends go through the key image index (`2.19.x`).

### F2 — High, fixed in the test harness path: `validate()` treated Bulletproofs+ as Borromean

`transfer_to_blind_operation::validate` and `blind_transfer_operation::validate` call `range_get_info` whenever there is more than one output. A `BP+` blob is not a Borromean header, so `range_get_info` throws before the evaluator can run the new verifier.

Fix: `libraries/protocol/confidential.cpp` skips that header check when the first proof begins with `BP+\x01`. Before CT2 the evaluator still calls `verify_range`, which rejects the new format. After CT2 the evaluator calls `verify_bulletproof_plus_outputs`.

### F3 — Medium, fixed: default max transaction size is 2048 bytes

`GRAPHENE_DEFAULT_MAX_TRANSACTION_SIZE` is 2048. A 16-output Bulletproofs+ deposit does not fit in that cap. After `HARDFORK_BSIP_CT2_TIME`, `database::_apply_transaction` (`libraries/chain/db_block.cpp`) allows a transaction whose operations are all confidential to be up to `GRAPHENE_CONFIDENTIAL_TX_SIZE_ALLOWANCE` (16384 bytes), and still no larger than `maximum_block_size`. A mixed transaction keeps the ordinary cap. The existing per-output fee is unchanged, because appending a field to `fee_params_t` would reserialize the on-chain fee schedule. `sixteen_output_deposit_and_bad_rings` pushes a 16-output deposit larger than 2048 and at most 16384, and rejects the same shape with 1024-byte memos.

### F4 — Medium, residual: the cryptographic port is not the audited binary

Bulletproofs+ and CLSAG follow Monero’s structure and the papers cited in `NOTICE.md`, on a different curve, with SHA-256 instead of Keccak, and without ed25519 cofactor clearing. Tests show the prover and verifier agree and that the listed forgeries fail. They do not prove the absence of a soundness bug.

**External cryptographic review is still required before mainnet.**

### F5 — Low, partially addressed: signing is not fully constant-time

Value bits are now turned into scalars with `aL = bit` and `aR = bit - 1` (`libraries/ctcrypto/src/bulletproofs_plus.cpp`), so that step does not branch on the bit. The CLSAG real response is written into every slot with a byte mask (`Scalar::cmov` in `libraries/ctcrypto/src/clsag.cpp`). OpenSSL `EC_POINT_mul` still varies with the scalar, and the CLSAG challenge walk still starts at `real_index + 1`. A local observer of the wallet process can still learn output amounts and the real ring index. Consensus verification only sees public data.

### F6 — Low: fixed ring size strands young asset pools

A ring must contain exactly 16 commitments of the same asset. An asset with fewer live blinded balances cannot be ring-spent. That is specified in BSIP-CT3. Outputs whose owner is not a single key also cannot be ring members; they must be spent with the legacy operations before CT3 or they become unspendable.

### F7 — Info: build fixes required on this image

`libraries/fc/include/fc/string.hpp` needed `<cstdint>` for GCC 13. `libraries/fc/src/crypto/openssl.cpp` called `FIPS_mode_set`, which OpenSSL 3 removed; the call is now limited to OpenSSL < 3. Neither change alters consensus hashing.

### F10 — Medium, fixed: receiving a CT1 memo crashed the wallet

`receive_blind_transfer` decrypted a memo with no `to` (required after CT1) and then evaluated `*conf.to` while filling `blind_balance` (`libraries/wallet/wallet.cpp`). The optional is empty, so that path could not record the output. The assignment now uses the key that decrypted the memo when `to` is unset. The sender's own receipt still carries `to` locally so the wallet can book the output; the on-chain memo does not.

### F9 — Info, fixed: `CMAKE_CXX_FLAGS` is overwritten

Passing `-DGRAPHENE_PRIVATE_CT_HF_IN_PAST` through `CMAKE_CXX_FLAGS` does not reach the compiler. `CMakeLists.txt` assigns `CMAKE_CXX_FLAGS` from `CMAKE_C_FLAGS`. The private-testnet switch is the CMake option `GRAPHENE_PRIVATE_CT_HF_IN_PAST`, which uses `add_compile_definitions`. The default is OFF. A binary built with it ON must not dial public seeds.

### F8 — Info: harness noise, not a consensus failure

Shutting down the test database logs `popping block would leave head block null` while rewinding to block 0. The same log appears after the pre-existing `confidential_tests` suite. `FBA fee … designated asset does not exist` is the test genesis, which does not configure the blind-transfer FBA; `pay_fba_fee` falls back to the normal fee path.

## DoS: verification time versus fee

Release build, this machine, OpenSSL secp256k1, single-threaded:

| Work | Time | Size |
| --- | --- | --- |
| Bulletproofs+ verify, 1×64-bit | 5.6 ms | 596 bytes |
| Bulletproofs+ verify, 4 aggregated | 19.6 ms | 728 bytes |
| CLSAG verify, ring 16 | included in the chain test; linear in 16 | about 600 bytes |

Default ring fee is `10 * precision` plus `20 * precision` per input plus `1 * precision` per KiB. At `precision = 100000` that is on the order of 3 BTS plus the byte fee for one input, against tens of milliseconds of CPU. That is enough for one ring. Eight inputs (the cap) are eight CLSAG verifications; the per-input term scales with them. A 16-output Bulletproofs+ is one proof, not sixteen, so the old per-output fee on operations 39 and 40 overpays relative to CPU and still bounds the work (`outputs.size() <= 16`).

Parsers reject wrong lengths before the multiscalar multiplication. Memo ciphertext is capped at 1024 bytes. Proof blobs are capped at 1024 bytes. CLSAG blobs are capped at 4096 bytes and must be exact for ring size 16.

## What was not verified

- The Python reference checks the same verification equation as the C++ batch verifier, with a different group library and an explicit challenge product. It is not a second derivation from the paper, and it is not a formal proof of the Fiat-Shamir transcript. The locked vector was still produced by this prover.
- OpenSSL scalar multiplication and the CLSAG walk order are not constant-time (F5).
- The one-hour fuzz runs did not grow coverage after the first minutes (`cov` 153 and 130). Most mutated inputs die in the parser. That is not a multi-day campaign.
- `witness_node` was not started on the embedded mainnet genesis.

## Result of this run

- `ctcrypto_tests`: passed (Release and Debug). Debug verify times on this machine were 5.7 ms for one 64-bit proof (596 bytes) and 19.9 ms for a 4-output aggregate (728 bytes). Those times sit in the system OpenSSL library, which is built optimized, so Debug and Release are close.
- `ct_fuzz`: passed the built-in 2000-step loop (Release and Debug).
- `chain_test --run_test=confidential_ct_tests`: passed (Release and Debug, "No errors detected").
- `chain_test --run_test=confidential_tests`: passed (Release).
- ASan + UBSan (`-O1 -g -fsanitize=address,undefined`) on `ctcrypto_tests`: passed, no sanitizer reports. That run measured 8.6 ms and 31 ms for the same two proofs.
- Debug `ninja` of `ctcrypto_tests`, `ct_fuzz`, `chain_test`, `witness_node`, and `cli_wallet`: exit 0.
- Loopback two-node replay (`tests/private_testnet/two_node_local.sh`): passed. Private chain id `c3807ea4…`, follower head block 4, revalidate reported the same head and chain id. No public seed appeared in the logs.
- libFuzzer + ASan + UBSan, one hour each: Bulletproofs+ harness 106,173,487 runs, CLSAG harness 103,523,900 runs, both exit 0, no crash files. Corpus saved under `tools/ct_reference/corpus` (85 files, about 352 KiB).
- `tools/ct_reference/crosscheck.sh`: passed in both directions.
- `chain_test --run_test=confidential_ct_tests` after the size-allowance and bad-ring cases: passed (Release). The new case pushes a signed CLSAG whose ring mixes a core commitment with a different-asset commitment, and rejects a short ring, a duplicated member, and a duplicated key image inside one operation.
- Three-node loopback run (`tests/private_testnet/confidential_nodes.sh`) on binaries built with `-DGRAPHENE_PRIVATE_CT_HF_IN_PAST=ON` and `genesis-dev.json`. Chain id `c3807ea4…`. `cli_wallet` on node A imported the dev balance, deposited 16 blinded outputs, did `ring_blind_transfer`, and did `ring_transfer_from_blind`. Node A produced block `#9` with 2 transactions and later blocks with one transaction each. Node B applied `#9` `000000090011c20e817f3c1fda3b2841873d7ce3` (2 transactions) and `#11` (1 transaction). Rebroadcasting the ring transaction through a second `cli_wallet` attached to node B was rejected as a duplicate (`4ca78bab…`). Node C, stopped and restarted with `--revalidate-blockchain --force-validate` and an empty seed list, came back on a chain with 15 blocks and the same chain id. No public seed hostname appeared in the logs.

**External cryptographic audit is still required before mainnet.**
