#include <graphene/protocol/confidential.hpp>

#include <bts_ct/clsag.hpp>

#include <fc/crypto/sha256.hpp>
#include <fc/io/raw.hpp>

#include <cstring>
#include <set>
#include <utility>

namespace graphene { namespace protocol {

namespace {

share_type ring_fee( uint64_t base, uint32_t price_per_kbyte, uint32_t price_per_input,
                     const vector<ring_input>& inputs, uint64_t packed_bytes )
{
   auto data_fee = base_operation::calculate_data_fee( packed_bytes, price_per_kbyte );
   fc::uint128_t per_in( price_per_input );
   per_in *= inputs.size();
   FC_ASSERT( per_in <= GRAPHENE_MAX_SHARE_SUPPLY );
   fc::uint128_t total = base;
   total += data_fee;
   total += per_in;
   FC_ASSERT( total <= GRAPHENE_MAX_SHARE_SUPPLY );
   return static_cast<int64_t>( total );
}

vector<char> append_pack( vector<char> bytes, const vector<char>& more )
{
   bytes.insert( bytes.end(), more.begin(), more.end() );
   return bytes;
}

} // namespace

void validate_ring_inputs( const vector<ring_input>& inputs )
{
   FC_ASSERT( !inputs.empty(), "a ring operation needs an input" );
   FC_ASSERT( inputs.size() <= GRAPHENE_RING_MAX_INPUTS, "too many ring inputs" );
   std::set<commitment_type> images;
   for( const auto& in : inputs )
   {
      FC_ASSERT( in.members.size() == GRAPHENE_RING_SIZE, "ring size is fixed" );
      FC_ASSERT( images.insert( in.key_image ).second, "duplicate key image in operation" );
      for( uint32_t i = 0; i < in.members.size(); ++i )
      {
         if( i > 0 )
            FC_ASSERT( in.members[i - 1] < in.members[i], "ring members must be strictly sorted" );
      }
      FC_ASSERT( in.clsag.size() <= 4096, "CLSAG blob exceeds the size cap" );
      btsct::ClsagSignature sig;
      FC_ASSERT( btsct::clsag_parse( reinterpret_cast<const uint8_t*>( in.clsag.data() ),
                                     in.clsag.size(), sig ),
                 "CLSAG did not parse" );
      FC_ASSERT( sig.s.size() == GRAPHENE_RING_SIZE, "CLSAG ring size mismatch" );
      uint8_t image[33];
      sig.I.to_compressed( image );
      FC_ASSERT( memcmp( image, in.key_image.data(), 33 ) == 0,
                 "key_image field does not match the CLSAG key image" );
   }
}

fc::sha256 ring_signature_message( const asset& fee,
                                   const asset& public_amount,
                                   account_id_type to,
                                   const blind_factor_type& public_blind,
                                   const vector<ring_input>& inputs,
                                   const vector<commitment_type>& outputs,
                                   const vector<char>& range_proof )
{
   // Domain tag, then the consensus fields. Order is part of the BSIP.
   static const char domain[] = "BTS-RING-MSG-v1";
   vector<char> bytes( domain, domain + sizeof(domain) - 1 );
   bytes = append_pack( std::move( bytes ), fc::raw::pack( fee ) );
   bytes = append_pack( std::move( bytes ), fc::raw::pack( public_amount ) );
   bytes = append_pack( std::move( bytes ), fc::raw::pack( to ) );
   bytes = append_pack( std::move( bytes ), fc::raw::pack( public_blind ) );
   // Sign the ring contents, not the signature scalars. Key images are included
   // so a signature cannot be moved onto a different image.
   for( const auto& in : inputs )
   {
      bytes = append_pack( std::move( bytes ), fc::raw::pack( in.members ) );
      bytes = append_pack( std::move( bytes ), fc::raw::pack( in.pseudo_commitment ) );
      bytes = append_pack( std::move( bytes ), fc::raw::pack( in.key_image ) );
   }
   bytes = append_pack( std::move( bytes ), fc::raw::pack( outputs ) );
   bytes = append_pack( std::move( bytes ), fc::raw::pack( range_proof ) );
   return fc::sha256::hash( bytes.data(), static_cast<uint32_t>( bytes.size() ) );
}

void verify_ring_signatures( const fc::sha256& message,
                             const vector<ring_input>& inputs,
                             const vector<fc::ecc::public_key_data>& spend_keys )
{
   FC_ASSERT( spend_keys.size() == inputs.size() * GRAPHENE_RING_SIZE );
   uint8_t msg[32];
   memcpy( msg, message.data(), 32 );
   size_t key_i = 0;
   for( const auto& in : inputs )
   {
      btsct::ClsagSignature sig;
      FC_ASSERT( btsct::clsag_parse( reinterpret_cast<const uint8_t*>( in.clsag.data() ),
                                     in.clsag.size(), sig ) );
      vector<btsct::Point> pubs;
      vector<btsct::Point> commits;
      pubs.reserve( GRAPHENE_RING_SIZE );
      commits.reserve( GRAPHENE_RING_SIZE );
      for( uint32_t m = 0; m < GRAPHENE_RING_SIZE; ++m, ++key_i )
      {
         btsct::Point pub;
         btsct::Point commit;
         FC_ASSERT( btsct::Point::from_compressed( spend_keys[key_i].data(), spend_keys[key_i].size(), pub ),
                    "ring member spend key is not a point" );
         FC_ASSERT( btsct::Point::from_compressed( in.members[m].data(), in.members[m].size(), commit ),
                    "ring member commitment is not a point" );
         pubs.push_back( pub );
         commits.push_back( commit );
      }
      btsct::Point pseudo;
      FC_ASSERT( btsct::Point::from_compressed( in.pseudo_commitment.data(),
                                               in.pseudo_commitment.size(), pseudo ),
                 "pseudo commitment is not a point" );
      FC_ASSERT( btsct::clsag_verify( msg, sig, pubs, commits, pseudo ),
                 "CLSAG verification failed" );
   }
}

void ring_blind_transfer_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   validate_ring_inputs( inputs );
   FC_ASSERT( !outputs.empty(), "ring transfer needs an output" );
   FC_ASSERT( outputs.size() <= GRAPHENE_RING_MAX_OUTPUTS );
   vector<commitment_type> out_c;
   out_c.reserve( outputs.size() );
   for( uint32_t i = 0; i < outputs.size(); ++i )
   {
      if( i > 0 )
         FC_ASSERT( outputs[i - 1].commitment < outputs[i].commitment, "outputs must be sorted" );
      FC_ASSERT( !outputs[i].owner.is_impossible() );
      check_required_stealth_memo( outputs[i].stealth_memo );
      out_c.push_back( outputs[i].commitment );
   }
   vector<commitment_type> pseudos;
   for( const auto& in : inputs )
      pseudos.push_back( in.pseudo_commitment );
   FC_ASSERT( fc::ecc::verify_sum( pseudos, out_c, fee.amount.value ),
              "ring commitments do not balance the fee" );
   verify_bulletproof_plus_commitments( range_proof, out_c );
}

share_type ring_blind_transfer_operation::calculate_fee( const fee_params_t& k )const
{
   return ring_fee( k.fee, k.price_per_kbyte, k.price_per_input, inputs,
                    fc::raw::pack_size( *this ) );
}

void ring_transfer_from_blind_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT( amount.amount > 0 );
   FC_ASSERT( amount.asset_id == fee.asset_id );
   validate_ring_inputs( inputs );
   FC_ASSERT( changes.size() <= GRAPHENE_RING_MAX_OUTPUTS );
   vector<commitment_type> out_c;
   for( uint32_t i = 0; i < changes.size(); ++i )
   {
      if( i > 0 )
         FC_ASSERT( changes[i - 1].commitment < changes[i].commitment, "change outputs must be sorted" );
      FC_ASSERT( !changes[i].owner.is_impossible() );
      check_required_stealth_memo( changes[i].stealth_memo );
      out_c.push_back( changes[i].commitment );
   }
   const int64_t public_value = fee.amount.value + amount.amount.value;
   FC_ASSERT( public_value > 0 );
   out_c.push_back( fc::ecc::blind( blinding_factor, static_cast<uint64_t>( public_value ) ) );
   vector<commitment_type> pseudos;
   for( const auto& in : inputs )
      pseudos.push_back( in.pseudo_commitment );
   // The public commitment is not a range-proved output. Drop it from the proof set.
   vector<commitment_type> proved( out_c.begin(), out_c.end() - 1 );
   if( proved.empty() )
      FC_ASSERT( range_proof.empty(), "no change outputs means no range proof" );
   else
      verify_bulletproof_plus_commitments( range_proof, proved );
   FC_ASSERT( fc::ecc::verify_sum( pseudos, out_c, 0 ), "ring exit does not balance" );
}

share_type ring_transfer_from_blind_operation::calculate_fee( const fee_params_t& k )const
{
   return ring_fee( k.fee, k.price_per_kbyte, k.price_per_input, inputs,
                    fc::raw::pack_size( *this ) );
}

} } // graphene::protocol
