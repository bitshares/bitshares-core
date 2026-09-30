/*
 * Copyright (c) 2026 BitShares contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <graphene/chain/oracle_evaluator.hpp>
#include <graphene/chain/oracle_object.hpp>

#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>

namespace graphene { namespace chain {

namespace {

/// Every producer must exist, and the assets an oracle quotes must exist. Checked here rather
/// than left to first use: an oracle referencing a deleted account or a nonexistent asset
/// would look configured while being unable to ever report a value.
void check_producers_exist( const database& d, const oracle_options& options )
{
   for( const auto& p : options.producers )
      FC_ASSERT( nullptr != d.find( p.first ),
                 "Producer account ${a} does not exist", ("a", p.first) );
}

/**
 * Pushes a new value out to every market-issued asset fed by this oracle.
 *
 * Walking the oracle's own subscriber set rather than every bitasset on the chain: publishing
 * is designed to happen every few seconds, and work proportional to the chain's total asset
 * count does not belong in it. The set is capped at GRAPHENE_ORACLE_MAX_SUBSCRIBERS.
 */
void refresh_subscribers( database& d, const oracle_object& o )
{
   for( const auto& asset_id : o.subscribers )
   {
      const asset_object* a = d.find( asset_id );
      if( nullptr == a || !a->is_market_issued() )
         continue;   // defensive: an asset cannot stop being an MPA, but never assume it
      d.update_bitasset_current_feed( a->bitasset_data( d ) );
   }
}

} // namespace

void_result oracle_create_evaluator::do_evaluate( const oracle_create_operation& op ) const
{ try {
   const database& d = db();

   FC_ASSERT( HARDFORK_ORACLE_PASSED( d.head_block_time() ),
              "Not allowed until the oracle hardfork" );

   op.base_asset(d);    // throws if the asset does not exist
   op.quote_asset(d);
   check_producers_exist( d, op.options );

   // Names are how humans address an oracle, so a duplicate is not a harmless collision --
   // it is the way to get a consumer to reference the wrong series.
   const auto& idx = d.get_index_type<oracle_index>().indices().get<by_oracle_name>();
   FC_ASSERT( idx.find( op.name ) == idx.end(),
              "An oracle named '${n}' already exists", ("n", op.name) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

object_id_type oracle_create_evaluator::do_apply( const oracle_create_operation& op ) const
{ try {
   const auto& new_oracle = db().create<oracle_object>( [&op]( oracle_object& o ) {
      o.owner       = op.owner;
      o.name        = op.name;
      o.description = op.description;
      o.base_asset  = op.base_asset;
      o.quote_asset = op.quote_asset;
      o.options     = op.options;
      // current_value stays absent: a new oracle reports nothing until its producers have
      // published enough values to meet quorum.
   } );
   return new_oracle.id;
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result oracle_update_evaluator::do_evaluate( const oracle_update_operation& op )
{ try {
   const database& d = db();

   FC_ASSERT( HARDFORK_ORACLE_PASSED( d.head_block_time() ),
              "Not allowed until the oracle hardfork" );

   _oracle = &op.oracle_id(d);
   FC_ASSERT( _oracle->owner == op.owner, "Only the oracle's owner may update it" );

   if( op.new_options.valid() )
      check_producers_exist( d, *op.new_options );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result oracle_update_evaluator::do_apply( const oracle_update_operation& op ) const
{ try {
   database& d = db();
   const auto now = d.head_block_time();

   d.modify( *_oracle, [&op]( oracle_object& o ) {
      if( op.new_description.valid() )
         o.description = *op.new_description;
      if( op.new_options.valid() )
         o.options = *op.new_options;
   } );

   // Policy changes take effect immediately rather than at the next publish. Removing a
   // producer, tightening quorum or shortening the lifetime are the things an owner does in
   // response to a problem, and leaving the old aggregate standing until someone happens to
   // publish would defeat the point.
   if( op.new_options.valid() )
   {
      d.modify( *_oracle, [now]( oracle_object& o ) { o.update_current_value( now ); } );
      // Tightening quorum or dropping a producer can change or remove the value, and any
      // asset fed by this oracle has to see that immediately, not at the next publish.
      refresh_subscribers( d, *_oracle );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result oracle_delete_evaluator::do_evaluate( const oracle_delete_operation& op )
{ try {
   const database& d = db();

   FC_ASSERT( HARDFORK_ORACLE_PASSED( d.head_block_time() ),
              "Not allowed until the oracle hardfork" );

   _oracle = &op.oracle_id(d);
   FC_ASSERT( _oracle->owner == op.owner, "Only the oracle's owner may delete it" );

   // Deleting an oracle a smartcoin depends on would leave that asset with no price source,
   // unable to margin call or force-settle. The issuer must unbind first.
   FC_ASSERT( _oracle->subscribers.empty(),
              "Oracle '${n}' is still the price source for ${c} asset(s); clear their "
              "price_oracle_id first", ("n", _oracle->name)("c", _oracle->subscribers.size()) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result oracle_delete_evaluator::do_apply( const oracle_delete_operation& op ) const
{ try {
   db().remove( *_oracle );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result oracle_publish_evaluator::do_evaluate( const oracle_publish_operation& op )
{ try {
   const database& d = db();

   FC_ASSERT( HARDFORK_ORACLE_PASSED( d.head_block_time() ),
              "Not allowed until the oracle hardfork" );

   _oracle = &op.oracle_id(d);

   FC_ASSERT( _oracle->options.producers.count( op.producer ) > 0,
              "Account ${a} is not a producer of oracle '${n}'",
              ("a", op.producer)("n", _oracle->name) );

   // The orientation is fixed by the oracle, not chosen per submission: a consumer must never
   // have to work out whether a given number was inverted.
   FC_ASSERT( op.value.base.asset_id == _oracle->base_asset
              && op.value.quote.asset_id == _oracle->quote_asset,
              "Published value must be quoted as ${b}/${q} to match oracle '${n}'",
              ("b", _oracle->base_asset)("q", _oracle->quote_asset)("n", _oracle->name) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result oracle_publish_evaluator::do_apply( const oracle_publish_operation& op ) const
{ try {
   database& d = db();
   const auto now = d.head_block_time();

   d.modify( *_oracle, [&op, now]( oracle_object& o ) {
      o.submissions[op.producer] = std::make_pair( now, op.value );
      o.update_current_value( now );
   } );

   refresh_subscribers( d, *_oracle );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

} } // graphene::chain
