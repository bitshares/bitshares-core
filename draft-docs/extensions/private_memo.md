
Status
------

This spec is a suggestion for a future improvement.  It has not yet
been implemented and is not yet considered normative for wallets.

private_memo_data specification
-------------------------------

The `private_memo_data` is a `data_extension` with the following
semantics:

- Memo encrypted by ECDH shared secret used as AES key
- All wallets should support displaying and sending memos with accounts'
memo keys for `asset_issue_operation`, `transfer_operation`,
`override_transfer_operation` and `withdraw_permission_claim_operation`.

Data extension structure
------------------------

    /**
     *  @brief defines the keys used to derive the shared secret
     *
     *  Because account authorities and keys can change at any time, each memo must
     *  capture the specific keys used to derive the shared secret.  In order to read
     *  the cipher message you will need one of the two private keys.
     *
     *  If @ref from == @ref to and @ref from == 0 then no encryption is used, the memo is public.
     *  If @ref from == @ref to and @ref from != 0 then invalid memo data
     *
     *  The reason we include *both* keys is to allow the wallet to
     *  rebuild the data for any transfer, including the memo, from the
     *  user's private keys and the contents of the blockchain --
     *  regardless of whether the user is the sender or receiver.
     */
    struct memo_data
    {
       public_key_type from;
       public_key_type to;
       /**
        * 64 bit nonce format:
        * [  8 bits | 56 bits   ]
        * [ entropy | timestamp ]
        * Timestamp is number of microseconds since the epoch
        * Entropy is a byte taken from the hash of a new private key
        *
        * This format is not mandated or verified; it is chosen to ensure uniqueness of key-IV pairs only. This should
        * be unique with high probability as long as the generating host has a high-resolution clock OR a strong source
        * of entropy for generating private keys.
        */
       uint64_t nonce;
       /**
        * This field contains the AES encrypted packed @ref memo_message
        */
       vector<char> message;
    };

Encrypting/decrypting
---------------------

TODO: write in words

NB no public memo (this is separate extension)

    void memo_data::set_message(const fc::ecc::private_key& priv, const fc::ecc::public_key& pub,
       const string& msg, uint64_t nonce)
    {
       from = priv.get_public_key();
       to = pub;
       auto secret = priv.get_shared_secret(pub);
       auto nonce_plus_secret = fc::sha512::hash(fc::to_string(nonce) + secret.str());
       string text = memo_message(digest_type::hash(msg)._hash[0], msg).serialize();
       message = fc::aes_encrypt( nonce_plus_secret, vector<char>(text.begin(), text.end()) );
    }

    string memo_data::get_message(const fc::ecc::private_key& priv,
                                  const fc::ecc::public_key& pub)const
    {
       auto secret = priv.get_shared_secret(pub);
       auto nonce_plus_secret = fc::sha512::hash(fc::to_string(nonce) + secret.str());
       auto plain_text = fc::aes_decrypt( nonce_plus_secret, message );
       auto result = memo_message::deserialize(string(plain_text.begin(), plain_text.end()));
       FC_ASSERT( result.checksum == uint32_t(digest_type::hash(result.text)._hash[0]) );
       return result.text;
    }
