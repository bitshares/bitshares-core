# BSIP-CT3: Ring confidential transfers (CLSAG)

Status: draft. Hardfork time is a **placeholder** and is not a mainnet activation. If this draft is too large to finish in one pass, BSIP-CT1 and BSIP-CT2 stand on their own; this document matches the code that is in the tree.

中文摘要：新增两种操作：环盲转账和环盲出账。每个输入是一个固定大小（16）的环，用 CLSAG 签名，并公布 key image。已花费的承诺**不从状态里删除**，继续作为同资产的诱饵。用伪输出承诺配平金额。硬分叉之后，旧的 `blind_transfer` 和 `transfer_from_blind` 不能再花盲余额，否则同一笔承诺可以被环签名和普通签名各花一次。

## 1. Motivation / 动机

Today's blind spend reveals which commitment is consumed: the owner authority signs that input, and the commitment is deleted. A third party learns the link between successive outputs. Amounts are hidden; the graph is not.

RingCT, as used by Monero with CLSAG (eprint 2019/654), hides which member of a ring was spent. The key image `I = x * Hp(P)` is unique to the spend key, so a second spend of the same key is rejected without revealing which ring member it was.

BitShares commitments stay on secp256k1 and are not deleted. Deleting them would shrink the decoy set and would also be incompatible with "hide which one was spent" if deletion selected a member. Keeping them is what makes the anonymity set grow.

## 2. New operations / 新操作

Appended to `graphene::protocol::operation`. Existing tags 0..77 do not move.

| Tag | Name | Role |
| --- | --- | --- |
| 78 | `ring_blind_transfer_operation` | Confidential to confidential |
| 79 | `ring_transfer_from_blind_operation` | Confidential to a public account, optional confidential change |

Both are invalid before `HARDFORK_BSIP_CT3_TIME` = `2030-01-01T02:00:00Z` (`1893463200`), including inside a proposal. Fee-schedule updates cannot set their fee parameters before that time.

### 2.1 Common rules / 共同规则

Constants (not chain parameters; changing them is a hardfork):

| Name | Value |
| --- | --- |
| `GRAPHENE_RING_SIZE` | 16 |
| `GRAPHENE_RING_MAX_INPUTS` | 8 |
| `GRAPHENE_RING_MAX_OUTPUTS` | 16 |
| memo bounds | BSIP-CT1 |

Each `ring_input`:

- `members`: exactly 16 commitments, strictly increasing.
- Every member exists in `blinded_balance_index`, has the same `asset_id` as `fee.asset_id`, and its `owner` is a single key of weight 1 (no account or address authorities). The spend key is read from that owner. It is not trusted from the transaction.
- `pseudo_commitment`: a Pedersen commitment to the same value as the real member, with a fresh blind.
- `key_image`: the compressed point `I`, and it must equal the image inside `clsag`.
- `clsag`: the serialization below. It must verify.

A key image that is already in `key_image_index` (object space `2.19.x`) rejects the transaction. Two inputs in one operation cannot share an image.

The CLSAG message is SHA-256 of:

```
ASCII "BTS-RING-MSG-v1"
|| pack(fee) || pack(public_amount) || pack(to) || pack(public_blind)
|| for each input: pack(members) || pack(pseudo_commitment) || pack(key_image)
|| pack(output commitments) || pack(range_proof)
```

`public_amount`, `to`, and `public_blind` are zero for operation 78. The signature bytes themselves are not in the message, so they can be attached after the message is hashed. The key image is in the message, and it does not depend on the message, so the signer computes `I` first.

CLSAG statement, for secret index `l` (not transmitted):

- `P[l] = p * G` where `P[i]` is the spend key of member `i`
- `C[i] = member_commitment[i] - pseudo_commitment`, and `C[l] = z * G`
- `I = p * Hp(P[l])`, `D = z * Hp(P[l])`
- `Hp` is try-and-increment hash-to-curve of `BTS-CLSAG-Hp-v1 || compressed(P)`

Aggregation hashes use domains `BTS-CLSAG-AGG-P-v1` and `BTS-CLSAG-AGG-C-v1`. The round hash uses `BTS-CLSAG-ROUND-v1`. Details and the Monero correspondence are in `libraries/ctcrypto/src/clsag.cpp` and `NOTICE.md`.

### 2.2 ring_blind_transfer (78) / 环内盲转

```
asset fee
vector<ring_input> inputs
vector<ring_output> outputs
vector<char> range_proof
```

`ring_output` is `commitment`, `owner` (single key), `stealth_memo` (BSIP-CT1).

Balance, using the existing tally: `sum(pseudo_commitment) - sum(output commitment) - fee * H = 0`.

`range_proof` is one BSIP-CT2 Bulletproofs+ over the outputs in order. Outputs are strictly sorted by commitment. `1 <= outputs.size() <= 16`.

Apply:

- credit `fee` to the temp account (the fee payer), then the normal fee machinery debits it;
- insert each key image; do **not** remove input commitments;
- create a `blinded_balance_object` per output;
- `confidential_supply -= fee`.

### 2.3 ring_transfer_from_blind (79) / 环盲出账

```
asset fee
asset amount          # same asset as fee, amount > 0
account_id_type to
blind_factor_type blinding_factor
vector<ring_input> inputs
vector<ring_output> changes
vector<char> range_proof
```

Let `public = fee.amount + amount.amount`. The value commitment `fc::ecc::blind(blinding_factor, public)` is appended to the outputs for the balance equation only. It is not range-proved; it is a public value. Change outputs, if any, are range-proved with BSIP-CT2. If there is no change, `range_proof` is empty.

`confidential_supply -= amount + fee`. `to` is credited `amount`. Input commitments stay.

### 2.4 Serialization of a CLSAG / CLSAG 序列化

```
ASCII "CL"
version = 0x01
I (33) D (33) c1 (32) n (1 byte) s[0] .. s[n-1] (32 each)
```

`n` must be 16. Scalars are canonical. `I` and `D` are not infinity. `c1` is not zero. The byte length must be exact.

### 2.5 Fees / 手续费

```
fee_params: fee, price_per_kbyte, price_per_input
required = fee + price_per_input * input_count + floor(packed_bytes * price_per_kbyte / 1024)
```

Defaults: `fee = 10 * precision`, `price_per_kbyte = 1 * precision`, `price_per_input = 20 * precision`. The per-input term prices CLSAG verification (16 key-prefixed hash-to-points and a linear number of scalar multiplications). The per-byte term prices the proof and the memos. `calculate_data_fee` rejects a product above `GRAPHENE_MAX_SHARE_SUPPLY`.

Because new operations are absent from old fee schedules, the defaults apply until the committee publishes parameters, and only after the hardfork.

### 2.6 Decoy selection / 诱饵选择（钱包）

`list_blinded_balances(asset, start, limit)` walks the `(asset, commitment)` index. Commitments are not removed when spent, so the list is the decoy pool, not the unspent set. A wallet must:

- only select members of the **same asset** (consensus rejects mixes);
- include the real output;
- sort the 16 commitments before signing;
- prefer members from the whole pool, not only recent ones, and not only the wallet's own outputs;
- refuse to build a ring until the asset has at least 16 commitments. A younger asset cannot ring-spend yet. That is a liveness constraint of a fixed ring size, not a soundness hole;
- track its own key images locally so it does not try to spend twice. Consensus will reject the second image anyway.

### 2.7 Migration / 从现有盲余额迁出

An existing `blinded_balance_object` can be a ring member when its owner is a single key and the wallet knows the spend scalar, the amount, and the blind (from the stealth memo or from an out-of-band backup).

At `HARDFORK_BSIP_CT3_TIME`, `blind_transfer` and `transfer_from_blind` stop being able to **spend**. `transfer_to_blind` still creates outputs, under BSIP-CT1 and BSIP-CT2. The closure is required: a ring spend does not delete the commitment, so a later legacy signature of the same owner would spend it again. After the hardfork the only spend path is a CLSAG, and the key image makes that path one-time.

Outputs whose owner is not a single key cannot be ring members. They have to be spent before the hardfork, or they become stuck. Wallets should warn on that shape. New outputs created by this implementation use a single key.

## 3. Backward compatibility / 向后兼容

- Tags 78 and 79 are appended. Old operations keep their tags and their byte layout.
- `blinded_balance_object` fields are unchanged. A secondary index `(asset_id, commitment)` is derived at startup.
- `key_image_object` is new (`2.19.x`). Old databases have an empty index.
- Pre-hardfork blocks do not contain these operations. Replaying them on this code follows the old spend rules.
- This is a hardfork: an old node cannot deserialize tags 78 and 79.

## 4. Security considerations / 安全考虑

- **Double spend.** Key image uniqueness is the spend rule. The chain test spends once, replays the block, and rejects a second operation carrying the same image. Legacy spends after the hardfork are rejected even when the commitment is still present.
- **Inflation.** Output values are Bulletproofs+-bounded. Input values were bounded when created (Borromean after the 2026 soft fork, Bulletproofs+ after BSIP-CT2). The pseudo-output matches one ring member's value (CLSAG) and the pseudos balance the outputs plus the public fee (Pedersen tally). A commitment to -1 does not verify as a new output.
- **Cross-asset rings.** The evaluator loads each member and requires `asset_id == fee.asset_id`. A member from another asset never reaches signature verification.
- **Forged or truncated signatures.** `clsag_parse` rejects wrong magic, wrong length, and non-canonical scalars. `clsag_verify` rejects a flipped response, a different message, a substituted commitment, and a pseudo-output of the wrong value. Tests cover these at the primitive layer; the chain test checks integration.
- **Identification of the real index.** The index is not a field. Members are sorted, so the sort order is not a signal.
- **Anonymity limits.** The ring is 16 out of the asset's pool. The pool is public. Amounts are hidden relative to the bulletproof, not relative to a global decoy of mixed assets. This is not Monero's transaction graph; assets are separate pools on purpose.
- **DoS.** CLSAG verification is linear in ring size, and the ring size is fixed. Input count is capped at 8. Proof and memo sizes are capped. Fee scales with inputs and bytes.
- **External audit.** The CLSAG and Bulletproofs+ code is a port. It must be reviewed by someone who did not write it before any mainnet hardfork time is chosen. `libraries/ctcrypto/doc/AUDIT.md` records what was and was not checked here.

## 5. Test vectors / 测试向量

Primitive tests (`libraries/ctcrypto/src/test_ct.cpp`):

- honest CLSAG, ring sizes 4 and 16, verifies;
- the same spend key produces the same key image across two signatures;
- a different message, a flipped `s[0]`, a pseudo-output of a different amount, and a substituted commitment all fail;
- truncated and empty byte strings fail `clsag_parse`.

Chain tests (`tests/tests/confidential_ct_tests.cpp`, `ring_transfer_double_spend_and_replay`):

- 16 outputs deposited under Bulletproofs+;
- one ring spend keeps the spent commitment, inserts the key image, and reduces `confidential_supply` by the fee;
- a second push of an operation with that key image throws;
- `pop_block` + `push_block` restores the key image (replay);
- `transfer_from_blind` after the hardfork throws;
- fee grows with input count; an absurd `price_per_kbyte` on a multi-megabyte blob throws;
- a ring that is not size 16 throws in `validate()`;
- `ring_transfer_from_blind` with mismatched asset ids throws in `validate()`.

## 6. Hardfork placeholder / 硬分叉时间占位

All three times live in `libraries/chain/hardfork.d/BSIP_CT.hf` and are in January 2030 so a private testnet can jump to them. They are not a proposal to mine them on mainnet. Replace the constants only after an external review, and publish the new times in a release note before the network upgrades.
