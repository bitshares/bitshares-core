/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
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
#include <fc/io/sstream.hpp>
#include "wallet_api_impl.hpp"
#include "operation_printer.hpp"

namespace graphene { namespace wallet { namespace detail {

std::map<string,std::function<string(fc::variant,const fc::variants&)>> wallet_api_impl::get_result_formatters() const
   {
      std::map<string,std::function<string(fc::variant,const fc::variants&)> > m;

      m["help"] = [](variant result, const fc::variants&)
      {
         return result.get_string();
      };

      m["gethelp"] = [](variant result, const fc::variants&)
      {
         return result.get_string();
      };

      auto format_account_history = [this](variant result, const fc::variants&)
      {
         auto r = result.as<vector<operation_detail>>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;

         for( operation_detail& d : r )
         {
            operation_history_object& i = d.op;
            auto b = _remote_db->get_block_header(i.block_num);
            FC_ASSERT(b);
            ss << i.block_num << " ";
            ss << b->timestamp.to_iso_string() << " ";
            i.op.visit(operation_printer(ss, *this, i));
            ss << " \n";
         }

         return ss.str();
      };

      m["get_account_history"] = format_account_history;
      m["get_relative_account_history"] = format_account_history;

      m["get_account_history_by_operations"] = [this](variant result, const fc::variants&) {
          auto r = result.as<account_history_operation_detail>( GRAPHENE_MAX_NESTED_OBJECTS );
          std::stringstream ss;
          ss << "total_count : " << r.total_count << " \n";
          ss << "result_count : " << r.result_count << " \n";
          for (operation_detail_ex& d : r.details) {
              operation_history_object& i = d.op;
              auto b = _remote_db->get_block_header(i.block_num);
              FC_ASSERT(b);
              ss << i.block_num << " ";
              ss << b->timestamp.to_iso_string() << " ";
              i.op.visit(operation_printer(ss, *this, i));
              ss << " transaction_id : ";
              ss << d.transaction_id.str();
              ss << " \n";
          }

          return ss.str();
      };

      auto format_balances = [this](variant result, const fc::variants&)
      {
         auto r = result.as<vector<asset>>( GRAPHENE_MAX_NESTED_OBJECTS );
         vector<asset_object> asset_recs;
         std::transform(r.begin(), r.end(), std::back_inserter(asset_recs), [this](const asset& a) {
            return get_asset(a.asset_id);
         });

         std::stringstream ss;
         for( unsigned i = 0; i < asset_recs.size(); ++i )
            ss << asset_recs[i].amount_to_pretty_string(r[i]) << "\n";

         return ss.str();
      };

      m["list_account_balances"] = format_balances;
      m["get_blind_balances"] = format_balances;

      auto format_blind_transfers  = [this](variant result, const fc::variants&)
      {
         auto r = result.as<blind_confirmation>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;
         r.trx.operations[0].visit( operation_printer( ss, *this, operation_history_object() ) );
         ss << "\n";
         for( const auto& out : r.outputs )
         {
            auto a = get_asset( out.decrypted_memo.amount.asset_id );
            ss << a.amount_to_pretty_string( out.decrypted_memo.amount ) << " to  " << out.label
               << "\n\t  receipt: " << out.confirmation_receipt << "\n\n";
         }
         return ss.str();
      };

      m["transfer_to_blind"] = format_blind_transfers;
      m["blind_transfer"] = format_blind_transfers;

      m["receive_blind_transfer"] = [this](variant result, const fc::variants&)
      {
         auto r = result.as<blind_receipt>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;
         auto as = get_asset( r.amount.asset_id );
         ss << as.amount_to_pretty_string( r.amount ) << "  " << r.from_label << "  =>  "
            << r.to_label  << "  " << r.memo <<"\n";
         return ss.str();
      };

      m["blind_history"] = [this](variant result, const fc::variants&)
      {
         auto records = result.as<vector<blind_receipt>>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;
         ss << "WHEN         "
            << "  " << "AMOUNT"  << "  " << "FROM" << "  =>  " << "TO" << "  " << "MEMO" <<"\n";
         ss << "====================================================================================\n";
         for( auto& r : records )
         {
            auto as = get_asset( r.amount.asset_id );
            ss << fc::get_approximate_relative_time_string( r.date )
               << "  " << as.amount_to_pretty_string( r.amount ) << "  " << r.from_label << "  =>  " << r.to_label
               << "  " << r.memo <<"\n";
         }
         return ss.str();
      };

      m["get_order_book"] = [](variant result, const fc::variants&)
      {
         auto orders = result.as<order_book>( GRAPHENE_MAX_NESTED_OBJECTS );
         auto bids = orders.bids;
         auto asks = orders.asks;
         std::stringstream ss;
         std::stringstream sum_stream;
         sum_stream << "Sum(" << orders.base << ')';
         double bid_sum = 0;
         double ask_sum = 0;
         const int spacing = 20;

         auto prettify_num = [&ss]( double n )
         {
            if (abs( round( n ) - n ) < 0.00000000001 )
            {
               ss << (int) n;
            }
            else if (n - floor(n) < 0.000001)
            {
               ss << setiosflags( ios::fixed ) << setprecision(10) << n;
            }
            else
            {
               ss << setiosflags( ios::fixed ) << setprecision(6) << n;
            }
         };
         auto prettify_num_string = [&]( string& num_string )
         {
            double n = fc::to_double( num_string );
            prettify_num( n );
         };

         ss << setprecision( 8 ) << setiosflags( ios::fixed ) << setiosflags( ios::left );

         ss << ' ' << setw( (spacing * 4) + 6 ) << "BUY ORDERS" << "SELL ORDERS\n"
            << ' ' << setw( spacing + 1 ) << "Price" << setw( spacing ) << orders.quote << ' ' << setw( spacing )
            << orders.base << ' ' << setw( spacing ) << sum_stream.str()
            << "   " << setw( spacing + 1 ) << "Price" << setw( spacing ) << orders.quote << ' ' << setw( spacing )
            << orders.base << ' ' << setw( spacing ) << sum_stream.str()
            << "\n====================================================================================="
            << "|=====================================================================================\n";

         for (unsigned int i = 0; i < bids.size() || i < asks.size() ; i++)
         {
            if ( i < bids.size() )
            {
                bid_sum += fc::to_double( bids[i].base );
                ss << ' ' << setw( spacing );
                prettify_num_string( bids[i].price );
                ss << ' ' << setw( spacing );
                prettify_num_string( bids[i].quote );
                ss << ' ' << setw( spacing );
                prettify_num_string( bids[i].base );
                ss << ' ' << setw( spacing );
                prettify_num( bid_sum );
                ss << ' ';
            }
            else
            {
                ss << setw( (spacing * 4) + 5 ) << ' ';
            }

            ss << '|';

            if ( i < asks.size() )
            {
               ask_sum += fc::to_double( asks[i].base );
               ss << ' ' << setw( spacing );
               prettify_num_string( asks[i].price );
               ss << ' ' << setw( spacing );
               prettify_num_string( asks[i].quote );
               ss << ' ' << setw( spacing );
               prettify_num_string( asks[i].base );
               ss << ' ' << setw( spacing );
               prettify_num( ask_sum );
            }

            ss << '\n';
         }

         ss << endl
            << "Buy Total:  " << bid_sum << ' ' << orders.base << endl
            << "Sell Total: " << ask_sum << ' ' << orders.base << endl;

         return ss.str();
      };

      m["sign_message"] = [](variant result, const fc::variants&)
      {
         auto r = result.as<signed_message>( GRAPHENE_MAX_NESTED_OBJECTS );

         fc::stringstream encapsulated;
         encapsulated << ENC_HEADER;
         encapsulated << r.message << '\n';
         encapsulated << ENC_META;
         encapsulated << "account=" << r.meta.account << '\n';
         encapsulated << "memokey=" << std::string( r.meta.memo_key ) << '\n';
         encapsulated << "block=" << r.meta.block << '\n';
         encapsulated << "timestamp=" << r.meta.time << '\n';
         encapsulated << ENC_SIG;
         encapsulated << fc::to_hex( (const char*)r.signature->data(), r.signature->size() ) << '\n';
         encapsulated << ENC_FOOTER;

         return encapsulated.str();
      };

      return m;
   }

}}} // namespace graphene::wallet::detail
