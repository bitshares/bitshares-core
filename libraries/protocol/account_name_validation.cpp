#include <graphene/protocol/account_name_validation.hpp>
#include <fc/log/logger.hpp>
#include <algorithm>
#include <cctype>
#include <string>

namespace graphene { namespace protocol {

bool account_name_validator::_patterns_initialized = false;
std::vector<std::pair<std::regex, account_name_validator::pattern_category>> account_name_validator::_forbidden_patterns;

bool account_name_validator::initialize_patterns()
{
    if(_patterns_initialized) return true;
    
    try {
        // ======================
        // ETHEREUM ECOSYSTEM
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^0x[a-fA-F0-9]{40}$"), ETHEREUM_ECOSYSTEM); // Ethereum, BSC, Polygon, etc.
        _forbidden_patterns.emplace_back(std::regex("^0x[a-fA-F0-9]{64}$"), ETHEREUM_ECOSYSTEM); // Transaction hashes sometimes used as addresses
        
        // ======================
        // BITCOIN ECOSYSTEM
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^[13][a-km-zA-HJ-NP-Z1-9]{25,34}$"), BITCOIN_ECOSYSTEM); // Legacy P2PKH/P2SH
        _forbidden_patterns.emplace_back(std::regex("^bc1[a-z0-9]{25,39}$"), BITCOIN_ECOSYSTEM); // Bech32
        _forbidden_patterns.emplace_back(std::regex("^tb1[a-z0-9]{25,39}$"), BITCOIN_ECOSYSTEM); // Testnet Bech32
        _forbidden_patterns.emplace_back(std::regex("^bcrt1[a-z0-9]{25,39}$"), BITCOIN_ECOSYSTEM); // Regtest Bech32
        
        // ======================
        // LITECOIN
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^[LM3][a-km-zA-HJ-NP-Z1-9]{25,34}$"), LITECOIN); // Legacy L/M/3 prefix
        _forbidden_patterns.emplace_back(std::regex("^ltc1[a-z0-9]{25,39}$"), LITECOIN); // Bech32
        
        // ======================
        // DOGECOIN
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^D{1}[5-9A-HJ-NP-U]{1}[1-9A-HJ-NP-Za-km-z]{32}$"), DOGECOIN);
        
        // ======================
        // XRP (RIPPLE)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^r[a-zA-Z0-9]{24,34}$"), XRP); // Standard XRP address
        _forbidden_patterns.emplace_back(std::regex("^X[a-zA-Z0-9]{49}$"), XRP); // X-address format
        
        // ======================
        // COSMOS ECOSYSTEM (Bech32 with prefixes)
        // ======================
        // Cosmos Hub
        _forbidden_patterns.emplace_back(std::regex("^cosmos1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Osmosis
        _forbidden_patterns.emplace_back(std::regex("^osmo1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Juno
        _forbidden_patterns.emplace_back(std::regex("^juno1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Secret Network
        _forbidden_patterns.emplace_back(std::regex("^secret1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Kava
        _forbidden_patterns.emplace_back(std::regex("^kava1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Persistence
        _forbidden_patterns.emplace_back(std::regex("^persistence1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Sifchain
        _forbidden_patterns.emplace_back(std::regex("^sif1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Akash
        _forbidden_patterns.emplace_back(std::regex("^akash1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Regen
        _forbidden_patterns.emplace_back(std::regex("^regen1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Terra Classic/Luna 2.0
        _forbidden_patterns.emplace_back(std::regex("^terra1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        _forbidden_patterns.emplace_back(std::regex("^luna1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Thorchain
        _forbidden_patterns.emplace_back(std::regex("^thor1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Injective
        _forbidden_patterns.emplace_back(std::regex("^inj1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Band Protocol
        _forbidden_patterns.emplace_back(std::regex("^band1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Chihuahua
        _forbidden_patterns.emplace_back(std::regex("^chihuahua1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Bitsong
        _forbidden_patterns.emplace_back(std::regex("^bitsong1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Stargaze
        _forbidden_patterns.emplace_back(std::regex("^stars1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Umee
        _forbidden_patterns.emplace_back(std::regex("^umee1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Cudos
        _forbidden_patterns.emplace_back(std::regex("^cudos1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Desmos
        _forbidden_patterns.emplace_back(std::regex("^desmos1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Comdex
        _forbidden_patterns.emplace_back(std::regex("^comdex1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Vidulum
        _forbidden_patterns.emplace_back(std::regex("^vidulum1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Iris Network
        _forbidden_patterns.emplace_back(std::regex("^iaa1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Sentinel
        _forbidden_patterns.emplace_back(std::regex("^sent1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // AssetMantle
        _forbidden_patterns.emplace_back(std::regex("^mantle1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        // Decentr
        _forbidden_patterns.emplace_back(std::regex("^decentr1[a-z0-9]{38}$"), COSMOS_ECOSYSTEM);
        
        // ======================
        // SOLANA
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^[1-9A-HJ-NP-Za-km-z]{32,44}$"), SOLANA); // Base58, 32-44 chars
        _forbidden_patterns.emplace_back(std::regex("^[a-z0-9]{32,44}$"), SOLANA); // Sometimes lowercase hex-like
        
        // ======================
        // POLKADOT/KUSAMA ECOSYSTEM
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^1[a-z0-9]{46}$"), POLKADOT_ECOSYSTEM); // Polkadot SS58
        _forbidden_patterns.emplace_back(std::regex("^k[a-z0-9]{46}$"), POLKADOT_ECOSYSTEM); // Kusama SS58
        _forbidden_patterns.emplace_back(std::regex("^5[a-z0-9]{46}$"), POLKADOT_ECOSYSTEM); // Generic Substrate
        _forbidden_patterns.emplace_back(std::regex("^2[a-z0-9]{46}$"), POLKADOT_ECOSYSTEM); // Westend
        
        // ======================
        // CARDANO
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^addr1[02-9a-z]{58}$"), CARDANO); // Shelley mainnet
        _forbidden_patterns.emplace_back(std::regex("^addr_test1[02-9a-z]{58}$"), CARDANO); // Testnet
        
        // ======================
        // MONERO (XMR)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^4[0-9AB][1-9A-HJ-NP-Za-km-z]{93}$"), MONERO); // Standard
        _forbidden_patterns.emplace_back(std::regex("^8[0-9AB][1-9A-HJ-NP-Za-km-z]{93}$"), MONERO); // Integrated
        _forbidden_patterns.emplace_back(std::regex("^4[0-9AB][1-9A-HJ-NP-Za-km-z]{113}$"), MONERO); // Subaddress
        
        // ======================
        // TRON (TRX)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^T[a-zA-Z0-9]{32,33}$"), TRON);
        
        // ======================
        // NEAR PROTOCOL
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^[a-z0-9_\-\\.]{2,64}([\\.]near)?$", std::regex_constants::icase), NEAR);
        
        // ======================
        // ALGORAND
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^[A-Z2-7]{58}$"), ALGORAND); // Mainnet
        _forbidden_patterns.emplace_back(std::regex("^[A-Z2-7]{58}$"), ALGORAND); // Testnet (same format)
        
        // ======================
        // TEZOS (XTZ)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^tz[123][a-zA-Z0-9]{33}$"), TEZOS); // tz1/tz2/tz3
        _forbidden_patterns.emplace_back(std::regex("^kt1[a-zA-Z0-9]{33}$"), TEZOS); // Smart contract
        
        // ======================
        // ZCASH (ZEC)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^t[13][a-km-zA-HJ-NP-Z1-9]{33}$"), ZCASH); // Transparent (t1/t3)
        _forbidden_patterns.emplace_back(std::regex("^zs[a-z0-9]{74}$"), ZCASH); // Shielded Sapling
        _forbidden_patterns.emplace_back(std::regex("^z[a-z0-9]{94}$"), ZCASH); // Shielded Sprout
        
        // ======================
        // FILECOIN (FIL)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^f[0-4][a-z0-9]{45}$"), FILECOIN);
        
        // ======================
        // HARMONY (ONE)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^one1[a-z0-9]{38}$"), HARMONY);
        
        // ======================
        // ELROND (EGLD)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^erd1[a-z0-9]{47}$"), ELROND);
        
        // ======================
        // FLOW
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^0x[a-fA-F0-9]{16}$"), FLOW); // Flow addresses are 8 bytes
        
        // ======================
        // HEDERA HASHGRAPH (HBAR)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^[0-9]{1,3}\\.[0-9]{1,4}\\.[0-9]{1,10}$"), HEDERA); // 0.0.12345 format
        
        // ======================
        // STELLAR (XLM)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^G[a-zA-Z0-9]{55}$"), STELLAR); // Public key
        _forbidden_patterns.emplace_back(std::regex("^M[a-zA-Z0-9]{55}$"), STELLAR); // Federated address
        
        // ======================
        // WAVES
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^3[a-zA-Z0-9]{34}$"), WAVES); // Mainnet
        _forbidden_patterns.emplace_back(std::regex("^2[a-zA-Z0-9]{34}$"), WAVES); // Testnet
        
        // ======================
        // NANO
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^nano_[a-z0-9]{60}$"), NANO);
        _forbidden_patterns.emplace_back(std::regex("^xrb_[a-z0-9]{60}$"), NANO); // Legacy
        
        // ======================
        // IOTA
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^[a-zA-Z0-9]{90}$"), IOTA); // Ed25519 address
        _forbidden_patterns.emplace_back(std::regex("^iota1[a-z0-9]{59}$"), IOTA); // Bech32
        
        // ======================
        // AVALANCHE (AVAX)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^X-[a-zA-Z0-9]{32,35}$"), AVALANCHE); // X-Chain
        _forbidden_patterns.emplace_back(std::regex("^P-[a-zA-Z0-9]{32,35}$"), AVALANCHE); // P-Chain
        _forbidden_patterns.emplace_back(std::regex("^C-0x[a-fA-F0-9]{40}$"), AVALANCHE); // C-Chain (Ethereum compatible)
        
        // ======================
        // FANTOM (FTM)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^0x[a-fA-F0-9]{40}$"), FANTOM); // Same as Ethereum
        
        // ======================
        // CELO
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^0x[a-fA-F0-9]{40}$"), CELO); // Same as Ethereum
        
        // ======================
        // EOS
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^[a-z0-9]{12}$"), EOS); // 12 character account names
        _forbidden_patterns.emplace_back(std::regex("^[a-z]{1,11}[\\.]{1}[a-z0-9]{1,10}$"), EOS); // EOSIO resource names
        
        // ======================
        // CHAINLINK (LINK)
        // ======================
        _forbidden_patterns.emplace_back(std::regex("^0x[a-fA-F0-9]{40}$"), CHAINLINK); // ERC-20 token on Ethereum
        
        // ======================
        // GENERIC PATTERNS
        // ======================
        // Generic Bech32 pattern (catches most modern address formats)
        _forbidden_patterns.emplace_back(std::regex("^[a-z]{1,12}1[a-z0-9]{25,50}$"), GENERIC);
        
        // Generic Base58Check pattern (covers Bitcoin, Litecoin, DOGE, etc.)
        _forbidden_patterns.emplace_back(std::regex("^[123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz]{26,44}$"), GENERIC);
        
        // Generic hex patterns (catches many address formats)
        _forbidden_patterns.emplace_back(std::regex("^[a-fA-F0-9]{42}$"), GENERIC); // 42 chars (ETH with 0x)
        _forbidden_patterns.emplace_back(std::regex("^[a-fA-F0-9]{64}$"), GENERIC); // 64 chars (SHA256 hash size)
        _forbidden_patterns.emplace_back(std::regex("^[a-fA-F0-9]{34,40}$"), GENERIC); // Common address lengths
        
        // Transaction hash patterns (sometimes used as addresses in scams)
        _forbidden_patterns.emplace_back(std::regex("^[a-fA-F0-9]{64}$"), GENERIC);
                
        _patterns_initialized = true;
        return true;
    } catch (const std::regex_error& e) {
        elog("Regex initialization failed: ${error}", ("error", e.what()));
        return false;
    }
}

bool account_name_validator::is_valid_account_name(const std::string& name)
{
    if(!_patterns_initialized) initialize_patterns();
    
    
    // Must contain at least one non-hex character (most foreign addresses are pure hex)
    bool has_non_hex = false;
    for (char c : name) {
        if (!std::isxdigit(c) && c != 'x' && c != 'X') {
            has_non_hex = true;
            break;
        }
    }
    if (!has_non_hex && name.size() >= 40) {
        return false;
    }
    
    // Prevent accounts that are just transaction hashes
    if (name.size() == 64 && std::all_of(name.begin(), name.end(), [](char c) { 
        return std::isxdigit(c); 
    })) {
        return false;
    }
    
    // Prevent accounts that are just block hashes
    if (name.size() == 64 && std::all_of(name.begin(), name.end(), [](char c) { 
        return std::isxdigit(c); 
    })) {
        return false;
    }
    
    // Check against all forbidden patterns
    for (const auto& pattern_pair : _forbidden_patterns) {
        if (std::regex_match(name, pattern_pair.first)) {
            return false;
        }
    }
    
    return true;
}

std::string account_name_validator::get_validation_error(const std::string& name)
{
    if(!_patterns_initialized) initialize_patterns();
        
    // Must contain at least one non-hex character for long names
    bool has_non_hex = false;
    for (char c : name) {
        if (!std::isxdigit(c) && c != 'x' && c != 'X') {
            has_non_hex = true;
            break;
        }
    }
    if (!has_non_hex && name.size() >= 40) {
        return "Account name must contain non-hexadecimal characters to prevent blockchain address confusion";
    }
        
    // Check against all forbidden patterns
    for (const auto& pattern_pair : _forbidden_patterns) {
        if (std::regex_match(name, pattern_pair.first)) {
            switch(pattern_pair.second) {
                case ETHEREUM_ECOSYSTEM: return "Account name matches Ethereum ecosystem address format (ETH, BSC, Polygon, Avalanche C-Chain, etc.)";
                case BITCOIN_ECOSYSTEM: return "Account name matches Bitcoin address format";
                case LITECOIN: return "Account name matches Litecoin address format";
                case DOGECOIN: return "Account name matches Dogecoin address format";
                case XRP: return "Account name matches XRP (Ripple) address format";
                case COSMOS_ECOSYSTEM: return "Account name matches Cosmos ecosystem address format (Cosmos, Osmosis, Juno, Secret, Thorchain, etc.)";
                case SOLANA: return "Account name matches Solana address format";
                case POLKADOT_ECOSYSTEM: return "Account name matches Polkadot/Kusama ecosystem address format";
                case CARDANO: return "Account name matches Cardano address format";
                case MONERO: return "Account name matches Monero address format";
                case TRON: return "Account name matches TRON address format";
                case NEAR: return "Account name matches NEAR Protocol address format";
                case ALGORAND: return "Account name matches Algorand address format";
                case TEZOS: return "Account name matches Tezos address format";
                case ZCASH: return "Account name matches Zcash address format";
                case FILECOIN: return "Account name matches Filecoin address format";
                case HARMONY: return "Account name matches Harmony address format";
                case ELROND: return "Account name matches Elrond address format";
                case FLOW: return "Account name matches Flow address format";
                case HEDERA: return "Account name matches Hedera Hashgraph address format";
                case STELLAR: return "Account name matches Stellar address format";
                case WAVES: return "Account name matches Waves address format";
                case NANO: return "Account name matches Nano address format";
                case IOTA: return "Account name matches IOTA address format";
                case AVALANCHE: return "Account name matches Avalanche address format";
                case FANTOM: return "Account name matches Fantom address format";
                case CELO: return "Account name matches Celo address format";
                case EOS: return "Account name matches EOS address format";
                case CHAINLINK: return "Account name matches Chainlink address format";
                case GENERIC: return "Account name matches generic blockchain address pattern";
                case EXCHANGE_PATTERNS: return "Account name matches cryptocurrency exchange deposit pattern";
                case SCAM_PATTERNS: return "Account name matches known scam pattern";
                default: return "Account name matches forbidden blockchain address pattern";
            }
        }
    }
    
    return "";
}

std::vector<std::string> account_name_validator::get_matching_patterns(const std::string& name)
{
    std::vector<std::string> matches;
    if(!_patterns_initialized) initialize_patterns();
    
    for (const auto& pattern_pair : _forbidden_patterns) {
        if (std::regex_match(name, pattern_pair.first)) {
            switch(pattern_pair.second) {
                case ETHEREUM_ECOSYSTEM: matches.push_back("Ethereum ecosystem"); break;
                case BITCOIN_ECOSYSTEM: matches.push_back("Bitcoin ecosystem"); break;
                case LITECOIN: matches.push_back("Litecoin"); break;
                case DOGECOIN: matches.push_back("Dogecoin"); break;
                case XRP: matches.push_back("XRP"); break;
                case COSMOS_ECOSYSTEM: matches.push_back("Cosmos ecosystem"); break;
                case SOLANA: matches.push_back("Solana"); break;
                case POLKADOT_ECOSYSTEM: matches.push_back("Polkadot ecosystem"); break;
                case CARDANO: matches.push_back("Cardano"); break;
                case MONERO: matches.push_back("Monero"); break;
                case TRON: matches.push_back("TRON"); break;
                case NEAR: matches.push_back("NEAR"); break;
                case ALGORAND: matches.push_back("Algorand"); break;
                case TEZOS: matches.push_back("Tezos"); break;
                case ZCASH: matches.push_back("Zcash"); break;
                case FILECOIN: matches.push_back("Filecoin"); break;
                case HARMONY: matches.push_back("Harmony"); break;
                case ELROND: matches.push_back("Elrond"); break;
                case FLOW: matches.push_back("Flow"); break;
                case HEDERA: matches.push_back("Hedera"); break;
                case STELLAR: matches.push_back("Stellar"); break;
                case WAVES: matches.push_back("Waves"); break;
                case NANO: matches.push_back("Nano"); break;
                case IOTA: matches.push_back("IOTA"); break;
                case AVALANCHE: matches.push_back("Avalanche"); break;
                case FANTOM: matches.push_back("Fantom"); break;
                case CELO: matches.push_back("Celo"); break;
                case EOS: matches.push_back("EOS"); break;
                case CHAINLINK: matches.push_back("Chainlink"); break;
                case GENERIC: matches.push_back("Generic blockchain pattern"); break;
                case EXCHANGE_PATTERNS: matches.push_back("Exchange deposit pattern"); break;
                case SCAM_PATTERNS: matches.push_back("Known scam pattern"); break;
                default: matches.push_back("Unknown pattern"); break;
            }
        }
    }
    return matches;
}

}} // namespace graphene::protocol
