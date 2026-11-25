# BSIP-CT1: Mandatory encrypted stealth memo on blind outputs

Status: draft. Hardfork time is a **placeholder** and is not a mainnet activation.

中文摘要：在硬分叉之后，每一笔新的盲输出都必须携带 `stealth_memo`（`one_time_key` + 有长度上限的 `encrypted_memo`），并且禁止设置 `to`。这样钱包可以只靠链上数据扫描，而不把收款地址写进明文。分叉前的历史交易保持原样。

## 1. Motivation / 动机

BitShares blind outputs (`blind_output`) carry an optional `stealth_confirmation`. The official wallet often returns that confirmation out of band and may set `to` to the recipient public key. A scanner that does not already know the recipient cannot find the output, and a set `to` links the output to a key.

膜DEX already writes `one_time_key` and `encrypted_memo` on every blind output and scans the chain. This BSIP makes that shape mandatory for outputs created after the hardfork, and forbids the plaintext `to` field so the memo does not identify the recipient.

Operations affected: `transfer_to_blind` (39), `blind_transfer` (40) until BSIP-CT3 closes it, and the new ring outputs in BSIP-CT3.

## 2. Specification / 规范

Let `HARDFORK_BSIP_CT1_TIME` be the placeholder `2030-01-01T00:00:00Z` (`1893456000` seconds). Consensus code is `HARDFORK_BSIP_CT1_PASSED(head_block_time)`.

For every `blind_output` or `ring_output` **created** in a block at or after that time:

1. `stealth_memo` is present.
2. `stealth_memo.to` is absent. A transaction that sets it is invalid.
3. `encrypted_memo` length is in `[32, 1024]` bytes.
4. `one_time_key` is a canonical compressed secp256k1 point (33 bytes, prefix `0x02` or `0x03`, on the curve, not infinity).

The protocol does not decrypt the memo. The ciphertext is authenticated only by the recipient, who derives the shared secret from `one_time_key` and a private spend key, then checks `memo_data.check` (the first 4 bytes of the shared-secret hash), exactly as today's `stealth_confirmation::memo_data`.

`transfer_from_blind` has no outputs. This BSIP does not add a memo to it.

### Serialization / 序列化

No new fields. `stealth_confirmation` stays

```
public_key_type one_time_key
optional<public_key_type> to
vector<char> encrypted_memo
```

`to` is the existing `fc::optional` encoding: one byte `0` when absent, or `1` followed by the key when present. After the hardfork the `0` form is the only valid one. Historical transactions that used `1` remain valid because the check is gated on `head_block_time` inside the evaluator, not inside unconditional `validate()` of operations 39 and 40.

### Fee / 手续费

Unchanged for operations 39 and 40: `fee + price_per_output * output_count`. The memo is bounded by 1024 bytes, so it cannot be used to inflate the transaction without bound beyond the existing transaction-size limit. Ring operations use the size fee in BSIP-CT3, which already charges for the memo bytes.

### Backward compatibility / 向后兼容

- Blocks before the hardfork validate exactly as they do on 7.0.4-patch3, including outputs with no memo and outputs with `to` set.
- Nodes that have not upgraded cannot parse blocks that contain BSIP-CT3 operations, but BSIP-CT1 alone does not change operation tags.
- Wallets should keep a local copy of `to` in the receipt they show the sender. The on-chain memo omits it. `cli_wallet` `receive_blind_transfer` scans local keys when `to` is absent.

### Security considerations / 安全考虑

- Forcing a memo does not by itself hide the amount. Amount privacy remains the range proof and the commitment.
- A missing or unbounded memo is a scanning and DoS problem, not an inflation problem. The size cap is the DoS control.
- `one_time_key` is not proven to be a Diffie-Hellman contribution of the sender. A sender can put an unreadable memo. The recipient simply does not accept the funds. Consensus does not need to know the plaintext.
- Setting `to` is rejected because it is a permanent public link. The check is not retroactive.

## 3. Test vectors / 测试向量

Consensus constants:

| Name | Value |
| --- | --- |
| minimum ciphertext | 32 bytes |
| maximum ciphertext | 1024 bytes |
| `to` | must be absent after the hardfork |

Chain tests in `tests/tests/confidential_ct_tests.cpp` (`stealth_memo_and_bulletproof_hardfork_boundary`) cover: acceptance before the hardfork with no memo; rejection after the hardfork with no memo; rejection when `to` is set; rejection of a 1025-byte ciphertext; acceptance of a 48-byte ciphertext with a valid `one_time_key` and no `to`.

## 4. Hardfork placeholder / 硬分叉时间占位

`libraries/chain/hardfork.d/BSIP_CT.hf` defines `HARDFORK_BSIP_CT1_TIME` as 2030-01-01T00:00:00Z. Replace that constant in a separate, explicitly scheduled release. Do not ship this placeholder to BitShares mainnet.
