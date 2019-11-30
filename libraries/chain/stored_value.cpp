/*  (C) 2019 Peter Conrad
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

#include <graphene/chain/stored_value.hpp>

#include <fc/exception/exception.hpp>

namespace graphene { namespace chain {
   stored_debt::stored_debt( const graphene::protocol::asset_id_type asset )
      : _asset(asset), _amount(0) {}

   stored_debt::~stored_debt()
   {
      FC_ASSERT( _amount == 0, "Value leak detected: (${n],${a])!", ("n",_amount.value)("a",_asset) );
   }

   stored_debt::stored_debt( stored_debt&& move )
      : _asset(move._asset), _amount(move._amount)
   {
      move._amount = 0;
   }

   stored_debt& stored_debt::operator=( stored_value&& other )
   {
      if( &other == this ) return *this;
      FC_ASSERT( _amount.value == 0 && (_asset == asset_id_type() || _asset == other._asset),
                 "Can't overwrite (${n},${a}) with (${on},${oa})!",
                 ("on",other._amount.value)("oa",other._asset)("n",_amount.value)("a",_asset) );
      _asset = other._asset;
      _amount = other._amount;
      other._amount = 0;
      return *this;
   }

   stored_value stored_debt::issue( const graphene::protocol::share_type amount )
   {
      FC_ASSERT( amount >= 0, "Cannot issue a negative amount of ${a}", ("a",_asset) );
      _amount += amount;
      return stored_value::issue( _asset, amount );
   }

   void stored_debt::burn( stored_value&& amount )
   {
      FC_ASSERT( amount.get_amount() >= 0, "Cannot burn a negative amount of ${a}", ("a",_asset) );
      FC_ASSERT( _asset == amount.get_asset(), "Debt ${a} cannot burn ${n} of ${b}",
                 ("a",_asset)("n",amount.get_amount())("b",amount.get_asset()) );
      _amount -= amount.get_amount();
      amount.burn();
   }

   void stored_debt::restore( const graphene::protocol::asset& backup )
   {
      FC_ASSERT( _asset == backup.asset_id, "Corrupted backup: ${a} != ${b}",
                 ("a",_asset)("b",backup.asset_id) );
      _amount = backup.amount;
   }

   graphene::protocol::asset stored_debt::get_value()const
   {
      return graphene::protocol::asset( _amount, _asset );
   }

   void stored_debt::restore( const graphene::protocol::asset& backup )
   {
      FC_ASSERT( _asset == backup.asset_id, "Corrupted backup: ${a} != ${b}",
                 ("a",_asset)("b",backup.asset_id) );
      _amount = backup.amount;
   }


   stored_value::stored_value( const graphene::protocol::asset_id_type asset ) : stored_debt( asset ) {}

   stored_value stored_value::split( const graphene::protocol::share_type amount )
   {
      FC_ASSERT( amount <= _amount, "Invalid split: want ${w} but have only ${n} of ${a}",
                 ("w",amount)("n",_amount)("a",_asset) );
      stored_value result( _asset );
      result._amount = amount;
      _amount -= amount;
      return result;
   }

   stored_value& stored_value::operator+=( stored_value&& other )
   {
      FC_ASSERT( other._asset == _asset, "Can't merge (${on},${oa}) with (${n},${a})!",
                 ("on",other._amount.value)("oa",other._asset)("n",_amount.value)("a",_asset) );
      FC_ASSERT( &other != this, "Can't merge (${n},${a}) with itself!",
		 ("n",_amount.value)("a",_asset) );
      _amount += other._amount;
      other._amount = 0;
      return *this;
   }

   stored_value stored_value::issue( const graphene::protocol::asset_id_type asset,
                                     const graphene::protocol::share_type amount )
   {
      stored_value result( asset );
      result._amount = amount;
      return result;
   }

   void stored_value::burn()
   {
      _amount = 0;
   }

} } // graphene::chain

