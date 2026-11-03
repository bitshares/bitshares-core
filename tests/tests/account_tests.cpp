#include <boost/test/unit_test.hpp>
#include <graphene/protocol/account_name_validation.hpp>
#include <graphene/protocol/types.hpp>
#include <iostream>
#include <vector>
#include <string>

using namespace graphene::protocol;
using namespace std;

BOOST_AUTO_TEST_SUITE(account_name_validation_tests)

BOOST_AUTO_TEST_CASE( basic_validation_tests )
{
    // Test initialization
    BOOST_CHECK(account_name_validator::initialize_patterns());
    
    // Test valid names
    BOOST_CHECK(account_name_validator::is_valid_account_name("alice"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("bob-test"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("bitshares-user"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("test123"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("a-b-c"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("init0"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("committee-account"));
}

BOOST_AUTO_TEST_CASE( ethereum_address_tests )
{
    // Standard Ethereum addresses
    BOOST_CHECK(!account_name_validator::is_valid_account_name("0x742d35cc6634c0532925a3b844bc454e4438f44e"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("0x742D35Cc6634C0532925a3b844Bc454e4438f44e"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("0x1234567890abcdef1234567890abcdef12345678"));
    
    // Transaction hash format
    BOOST_CHECK(!account_name_validator::is_valid_account_name("0x742d35cc6634c0532925a3b844bc454e4438f44e742d35cc6634c0532925a3b8"));
    
    // Valid BitShares names that might look similar but are safe
    BOOST_CHECK(account_name_validator::is_valid_account_name("0x-test-account"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("ethereum-test"));
}

BOOST_AUTO_TEST_CASE( bitcoin_address_tests )
{
    // Legacy Bitcoin addresses
    BOOST_CHECK(!account_name_validator::is_valid_account_name("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy"));
    
    // Bech32 Bitcoin addresses
    BOOST_CHECK(!account_name_validator::is_valid_account_name("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("tb1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"));
    
    // Valid names that might be confused
    BOOST_CHECK(account_name_validator::is_valid_account_name("bitcoin-user"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("btc-test-123"));
}

BOOST_AUTO_TEST_CASE( cosmos_ecosystem_tests )
{
    // Cosmos Hub
    BOOST_CHECK(!account_name_validator::is_valid_account_name("cosmos1hjct6q7npsspsg3dgvzk3s77033h33p584x6k"));
    
    // Osmosis
    BOOST_CHECK(!account_name_validator::is_valid_account_name("osmo1hjct6q7npsspsg3dgvzk3s77033h33p584x6k"));
    
    // Juno
    BOOST_CHECK(!account_name_validator::is_valid_account_name("juno1hjct6q7npsspsg3dgvzk3s77033h33p584x6k"));
    
    // Secret Network
    BOOST_CHECK(!account_name_validator::is_valid_account_name("secret1nl7z7ha5kec4camsqm4yel0tsyz8zgmjqg6myp"));
    
    // Thorchain
    BOOST_CHECK(!account_name_validator::is_valid_account_name("thor1ypgjtk59f0yunfespykuwmd4l6ct7dnarlm2a9"));
    
    // Persistence
    BOOST_CHECK(!account_name_validator::is_valid_account_name("persistence1hjct6q7npsspsg3dgvzk3s77033h33p584x6k"));
    
    // Kava
    BOOST_CHECK(!account_name_validator::is_valid_account_name("kava1hjct6q7npsspsg3dgvzk3s77033h33p584x6k"));
    
    // Real BitShares accounts that should be allowed
    BOOST_CHECK(account_name_validator::is_valid_account_name("cosmos-test"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("secret-test-account"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("thor-test-123"));
}

BOOST_AUTO_TEST_CASE( solana_address_tests )
{
    // Solana addresses (base58 format)
    BOOST_CHECK(!account_name_validator::is_valid_account_name("11111111111111111111111111111112"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("So111111111111111111111111111111111111112"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("9WzDXwBbmkg8ZTbNMqUxvQRAyrZzDsGYdLVL9zYtAWWM"));
    
    // Valid names
    BOOST_CHECK(account_name_validator::is_valid_account_name("solana-test"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("sol-test-123"));
}

BOOST_AUTO_TEST_CASE( polkadot_kusama_tests )
{
    // Polkadot SS58 format
    BOOST_CHECK(!account_name_validator::is_valid_account_name("15oF4uVJwmo4TdGW7VfQxNLavjCXviqxT9S1MgbjMNHr6Sp5"));
    
    // Kusama SS58 format
    BOOST_CHECK(!account_name_validator::is_valid_account_name("kagc64k5zj9z6q6z6q6z6q6z6q6z6q6z6q6z6q6z6q6z6q6"));
    
    // Substrate generic
    BOOST_CHECK(!account_name_validator::is_valid_account_name("5GrwvaEF5zXb26Fz9rcQpDWS57CtERHpNehXCPcNoHGKutQY"));
    
    // Valid names
    BOOST_CHECK(account_name_validator::is_valid_account_name("polkadot-test"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("kusama-user-123"));
}


BOOST_AUTO_TEST_CASE( generic_pattern_tests )
{
    // Generic Bech32 pattern
    BOOST_CHECK(!account_name_validator::is_valid_account_name("chain1abcdefghijklmnopqrstuvwxyz0123456789"));
    
    // Generic Base58Check pattern
    BOOST_CHECK(!account_name_validator::is_valid_account_name("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa"));
    
    // Generic hex patterns
    BOOST_CHECK(!account_name_validator::is_valid_account_name("a1b2c3d4e5f67890a1b2c3d4e5f67890a1b2c3d4"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("a1b2c3d4e5f67890a1b2c3d4e5f67890a1b2c3d4e5f67890a1b2c3d4e5f67890"));
    
    // Valid names with hex characters
    BOOST_CHECK(account_name_validator::is_valid_account_name("test-account-1a2b3c"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("hex-test-123"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("account-742d35"));
}

BOOST_AUTO_TEST_CASE( error_message_tests )
{
    // Test error messages for different categories
    auto error = account_name_validator::get_validation_error("0x742d35cc6634c0532925a3b844bc454e4438f44e");
    BOOST_CHECK(!error.empty());
    BOOST_CHECK(error.find("Ethereum") != string::npos);
    
    error = account_name_validator::get_validation_error("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq");
    BOOST_CHECK(!error.empty());
    BOOST_CHECK(error.find("Bitcoin") != string::npos);
    
    error = account_name_validator::get_validation_error("cosmos1hjct6q7npsspsg3dgvzk3s77033h33p584x6k");
    BOOST_CHECK(!error.empty());
    BOOST_CHECK(error.find("Cosmos") != string::npos);
}

BOOST_AUTO_TEST_CASE( edge_case_tests )
{
    // Case sensitivity tests
    BOOST_CHECK(!account_name_validator::is_valid_account_name("COSMOS1HJCT6Q7NPSSPSG3DGVZK3S77033H33P584X6K"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("Cosmos1hjct6q7npsspsg3dgvzk3s77033h33p584x6k"));
    
    // Mixed case with special characters
    BOOST_CHECK(!account_name_validator::is_valid_account_name("0x742d35cc6634c0532925a3b844bc454e4438f44e-test"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq-test"));
    
    // Names with dots and hyphens
    BOOST_CHECK(!account_name_validator::is_valid_account_name("secret1.nl7z7ha5kec4camsqm4yel0tsyz8zgmjqg6myp"));
    BOOST_CHECK(!account_name_validator::is_valid_account_name("thor1-ypgjtk59f0yunfespykuwmd4l6ct7dnarlm2a9"));
    
    // Names with underscores
    BOOST_CHECK(account_name_validator::is_valid_account_name("test_account"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("user_name_123"));
    
    // Very short valid names
    BOOST_CHECK(account_name_validator::is_valid_account_name("ab"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("a-b"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("a_b"));
    
    // Names with numbers but also letters
    BOOST_CHECK(account_name_validator::is_valid_account_name("user123"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("test-456"));
    BOOST_CHECK(account_name_validator::is_valid_account_name("account_789"));
}

BOOST_AUTO_TEST_CASE( matching_patterns_tests )
{
    auto patterns = account_name_validator::get_matching_patterns("0x742d35cc6634c0532925a3b844bc454e4438f44e");
    BOOST_CHECK(!patterns.empty());
    BOOST_CHECK(find(patterns.begin(), patterns.end(), "Ethereum ecosystem") != patterns.end());
    
    patterns = account_name_validator::get_matching_patterns("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq");
    BOOST_CHECK(!patterns.empty());
    BOOST_CHECK(find(patterns.begin(), patterns.end(), "Bitcoin ecosystem") != patterns.end());
    
    patterns = account_name_validator::get_matching_patterns("cosmos1hjct6q7npsspsg3dgvzk3s77033h33p584x6k");
    BOOST_CHECK(!patterns.empty());
    BOOST_CHECK(find(patterns.begin(), patterns.end(), "Cosmos ecosystem") != patterns.end());    
    
    // Test name that matches multiple patterns
    patterns = account_name_validator::get_matching_patterns("secret1nl7z7ha5kec4camsqm4yel0tsyz8zgmjqg6myp");
    BOOST_CHECK(!patterns.empty());
    BOOST_CHECK(find(patterns.begin(), patterns.end(), "Cosmos ecosystem") != patterns.end());
    BOOST_CHECK(patterns.size() >= 1); // Should match at least Cosmos ecosystem
    
    // Test valid name that shouldn't match any patterns
    patterns = account_name_validator::get_matching_patterns("valid-bitshares-account");
    BOOST_CHECK(patterns.empty());
}

BOOST_AUTO_TEST_SUITE_END()
