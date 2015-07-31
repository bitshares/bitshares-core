
Status
------

This spec is a suggestion for a future improvement.  It has not yet
been implemented and is not yet considered normative for wallets.

data_extension specification
----------------------------

The `data_extension` is an extension (in `future_extensions`) with
the following semantics:

- All `data_extension` are semantically no-ops as far as validation
and core consensus is concerned (of course they increase data tx fees
as dictated by their size).
- The `data_extension` semantics is determined by a GUID account
(described below)

Data extension structure
------------------------

    struct data_extension
    {
        account_id_type semantics_uid;
        vector<char> data;
    };

UID accounts
------------

Many different `data_extension` will be created by different
applications.  Each data extension will have its own semantics.  The
semantics dictate how `data` is parsed:  Imagine a (fairly typical)
application which only understands `data` formatted by other instances
of itself.  The way it can *unambiguously tell* that the `data` was
created by an interoperable application is because `semantics_uid`
says it is compatible.

Specifically, `semantics_uid` refers to an account whose name is
`ripemd160(sha256( spec ))`, where `spec` is the contents of
the specification in some format (usually English text).

The hash of the specification can be viewed as a "magic number"
which is "assigned" in a decentralized way (because 160 bits of entropy
are included in the hash, two specifications will almost certainly be
"assigned" separate magic numbers).  The `account_id_type` is simply a
shorthand way to refer to a magic number, and anyone with access to
the blockchain can convert back and forth between them.

Note, it does not matter who registered or controls the account, or
what activity they do!  All that matters is that the name-to-ID mapping
is bijective, immutable, and known to all chain participants.

Genesis UID's
-------------

UID accounts (for example, for the memo extension) may already exist,
or may even be created at genesis.  Wallets *may* hard-code these ID's,
however for maximum compatibility with all Graphene-powered chains,
wallets are encouraged to look up the name-to-ID mapping to resolve
the UID instead.
