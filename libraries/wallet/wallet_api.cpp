#include <graphene/wallet/wallet.hpp>
#include <fc/rpc/api_connection.hpp>

// explicit instantiation for later use
namespace fc {
	template class api<graphene::wallet::wallet_api, identity_member_with_optionals>;
}