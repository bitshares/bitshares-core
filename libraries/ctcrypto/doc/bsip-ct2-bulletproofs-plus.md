# BSIP-CT2: Bulletproofs+ aggregated range proofs

Status: draft. Hardfork time is a **placeholder** and is not a mainnet activation.

中文摘要：硬分叉之后，新的盲输出不再使用 Borromean range proof，而改用一条覆盖本操作全部输出的 Bulletproofs+（64 位，按操作聚合）。证明写在第一笔输出的 `range_proof` 里，其余输出的 `range_proof` 必须为空。分叉前仍按 7.0.4-patch3 验证 Borromean（含 `verify_blind_output_range_proofs`）。

## 1. Motivation / 动机

Range proofs on confidential outputs must be fully verified. 7.0.4-patch3 (commits `3391f503`, `5eb091e`, tag `7.0.4-patch3`) calls `verify_blind_output_range_proofs` after soft-fork time `1795165200`, which runs `secp256k1_rangeproof_verify` (Borromean, secp256k1-zkp commit `bd067945`). This BSIP builds on that behavior.

This BSIP replaces Borromean for **new** outputs:

- one aggregated proof per operation instead of one multi-kilobyte proof per output;
- a 64-bit range, so a commitment cannot encode a value outside `[0, 2^64)`;
- batch verification of several proofs in one multiscalar multiplication for nodes that want it. Consensus verification of a single operation uses weight 1 and is deterministic.

The commitment equation is unchanged: `C = blind * G + value * H`, with the same `G` and `H` as `secp256k1_pedersen_commit`. Balance is still `fc::ecc::verify_sum`. The range proof only shows each new output value is a 64-bit non-negative integer consistent with that commitment.

## 2. Specification / 规范

Placeholder time: `HARDFORK_BSIP_CT2_TIME` = `2030-01-01T01:00:00Z` (`1893459600`). It is strictly after BSIP-CT1.

After the hardfork, for `transfer_to_blind` and for `blind_transfer` (until BSIP-CT3 disables the latter):

1. `1 <= outputs.size() <= 16`. Sixteen is `BP_PLUS_MAX_OUTPUTS`. Larger sets must be split across operations.
2. `outputs` are already sorted by commitment, as today.
3. `outputs[0].range_proof` is one Bulletproofs+ proof, as serialized below, of every output commitment in vector order.
4. `outputs[i].range_proof` for `i > 0` is empty.
5. Verification fails closed on: empty proof, truncated proof, extra trailing bytes, non-canonical scalar, point not on the curve, wrong number of inner-product rounds, or a proof that does not match the commitments.

Before the hardfork, the 7.0.4-patch3 Borromean check remains (full `verify_range` after `1795165200`, header check in `validate()` when `outputs.size() > 1`). A Bulletproofs+ byte string is not a valid Borromean proof, so it is rejected before this hardfork.

Ring operations (BSIP-CT3) carry the same proof in a dedicated `range_proof` field instead of inside the first output.

### Proof bytes / 证明序列化

All scalars are 32-byte big-endian, strictly less than the secp256k1 group order. All points are 33-byte compressed SEC1 (`0x02`/`0x03` || X), canonical (re-encoding must match).

```
offset 0   ASCII "BP+"
offset 3   version = 0x01
offset 4   A      (33)   vector commitment to the bit vectors
offset 37  A1     (33)   final inner-product announcement
offset 70  B      (33)
offset 103 r1     (32)
offset 135 s1     (32)
offset 167 d1     (32)
offset 199 rounds (1)    must equal 6 + log2(M)
offset 200 L[0] .. L[rounds-1]   (33 each)
then       R[0] .. R[rounds-1]   (33 each)
```

`M` is the smallest power of two greater than or equal to the number of commitments, and `M <= 16`. No bytes may follow `R`. A 1-commitment proof has 6 rounds and is 596 bytes. A 4-commitment proof (padded to `M = 4`) is 728 bytes.

The proof does **not** repeat the commitments. The verifier inserts the operation's commitments into the Fiat-Shamir transcript. Swapping or editing a commitment invalidates the proof.

### Cryptographic statement / 密码学陈述

The algorithm is Bulletproofs+ (eprint 2020/735) as structured in Monero 0.18.3.4 `bulletproofs_plus.cc`, retargeted to secp256k1. See `libraries/ctcrypto/NOTICE.md`.

- `G` is the SEC2 generator. `H` is secp256k1-zkp G2 (`group_impl.h`).
- Generators `Gi[i]`, `Hi[i]` are try-and-increment hash-to-curve of `BTS-BP+-Gi-v1 || uint32le(i)` and `BTS-BP+-Hi-v1 || uint32le(i)`.
- The transcript is SHA-256. Domain string for the initial state is `BTS-BP+-TRANSCRIPT-v1`. Each update hashes a typed encoding: `0x01 || scalar` or `0x02 || point`.
- `z` is SHA-256 of the 32-byte scalar `y` alone, matching the Monero step that hashes `y` by itself.
- There is no cofactor clearing. secp256k1's cofactor is 1.
- Consensus verification uses batch weight 1. `bulletproof_plus_verify_batch` may use other non-zero weights off the consensus path; a valid proof verifies for every non-zero weight, and an invalid proof fails the weight-1 check outright.

### Fee / 手续费

Operations 39 and 40 keep `fee + price_per_output * n`. The proof is smaller than Borromean, so the per-output price still covers verification with margin. Measured on this implementation (Release, OpenSSL secp256k1):

| Proof | Verify time | Size |
| --- | --- | --- |
| 1 output, 64 bits | about 5.6 ms | 596 bytes |
| 4 aggregated outputs | about 20 ms | 728 bytes |

A fee on the order of the existing `price_per_output` (default `5 * GRAPHENE_BLOCKCHAIN_PRECISION` per output) is far above the CPU cost of a few tens of milliseconds. See `libraries/ctcrypto/doc/AUDIT.md` for the DoS discussion. Ring operations add an explicit per-byte fee. `fee_params_t` for operations 39 and 40 is not extended: appending a field would change the serialized fee schedule already stored on chain.

### Transaction size / 交易大小

`maximum_transaction_size` stays 2048 for ordinary transactions. After this hardfork, a transaction whose operations are all confidential (`transfer_to_blind`, `blind_transfer`, `transfer_from_blind`, `ring_blind_transfer`, `ring_transfer_from_blind`) may be up to `GRAPHENE_CONFIDENTIAL_TX_SIZE_ALLOWANCE` (16384) bytes, and still no larger than `maximum_block_size`. A mixed transaction does not get the allowance. Sixteen blinded outputs with short memos fit; sixteen outputs with a 1024-byte memo each do not. The per-output fee still grows with the output count, which is what bounds the Bulletproofs+ work (at most 16 outputs, one proof).

### Backward compatibility / 向后兼容

- `blind_output` reflection is unchanged. Only the bytes inside `range_proof` change, and only after the hardfork.
- Historical Borromean outputs stay spendable until BSIP-CT3, which stops legacy spend operations. Their commitments remain valid inputs to a ring if the owner is a single key (BSIP-CT3).
- Nodes that verify a post-hardfork block with pre-upgrade software will reject the proof in `verify_range`, so this is a hardfork, not a softfork.

### Security considerations / 安全考虑

- A range proof binds the integer value to the commitment. It does not hide which asset is used; the asset id stays in the operation or in the balance object.
- Inflation by cancelling commitments (a positive output plus a commitment to a negative value) fails because the negative commitment has no accepting proof. The chain test pushes a proof built for a different value and expects rejection.
- The implementation is a curve port of audited source, not the audited binary. **An external cryptographic review is required before mainnet.**
- The prover turns each secret value bit into scalars with `aL = bit` and `aR = bit - 1`, without a branch on the bit. OpenSSL scalar multiplication and the CLSAG challenge walk are still observable to a local process. That is a wallet-host leak, not a consensus bug.
- Padding to the next power of two uses virtual zero outputs that are not commitments on chain. The verifier's `M` is derived only from the number of real commitments.

## 3. Test vectors / 测试向量

Deterministic commitments (big-endian scalar blind, value as `uint64`):

| blind | value | compressed commitment |
| --- | --- | --- |
| 3 | 1 | `0263ed256bcac0f48fabdd28184aaf0dc05c5d0019bf88ab78f842c437b5159830` |
| 9 | 2 | `03c80827b14ea6b15ba26b5ff36c46013201582440417539a1914abe20fb574de7` |

A proof of those two openings, produced by `bulletproof_plus_prove`, is locked in `libraries/ctcrypto/src/test_ct.cpp` (`test_locked_vector`). It begins with ASCII `BP+` (`42502b01`). The unit test checks that this exact byte string verifies, and that flipping one byte does not. Other proofs of the same openings are also valid; the prover's blinding randomness is not consensus.

Further negative cases live in `confidential_ct_tests`: Borromean rejected after the hardfork, empty proof, 8-byte truncation, a flipped proof byte, and a commitment that does not match the proof.

## 4. Hardfork placeholder / 硬分叉时间占位

`HARDFORK_BSIP_CT2_TIME` in `libraries/chain/hardfork.d/BSIP_CT.hf` is 2030-01-01T01:00:00Z. Replace it only in a release that has had an external review of `libraries/ctcrypto`.
