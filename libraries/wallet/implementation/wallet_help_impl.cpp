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

namespace graphene { namespace wallet {

namespace detail {

variant wallet_api_impl::info() const
{
   auto chain_props = get_chain_properties();
   auto global_props = get_global_properties();
   auto dynamic_props = get_dynamic_global_properties();
   fc::mutable_variant_object result;
   result["head_block_num"] = dynamic_props.head_block_number;
   result["head_block_id"] = fc::variant(dynamic_props.head_block_id, 1);
   result["head_block_age"] = fc::get_approximate_relative_time_string(dynamic_props.time,
                                                                        time_point_sec(time_point::now()),
                                                                        " old");
   result["next_maintenance_time"] = fc::get_approximate_relative_time_string(dynamic_props.next_maintenance_time);
   result["chain_id"] = chain_props.chain_id;
   result["participation"] = (100*dynamic_props.recent_slots_filled.popcount()) / 128.0;
   result["active_witnesses"] = fc::variant(global_props.active_witnesses, GRAPHENE_MAX_NESTED_OBJECTS);
   result["active_committee_members"] = fc::variant(global_props.active_committee_members, GRAPHENE_MAX_NESTED_OBJECTS);
   return result;
}

variant_object wallet_api_impl::about() const
{
   string client_version( graphene::utilities::git_revision_description );
   const size_t pos = client_version.find( '/' );
   if( pos != string::npos && client_version.size() > pos )
      client_version = client_version.substr( pos + 1 );

   fc::mutable_variant_object result;
   //result["blockchain_name"]        = BLOCKCHAIN_NAME;
   //result["blockchain_description"] = BTS_BLOCKCHAIN_DESCRIPTION;
   result["client_version"]           = client_version;
   result["graphene_revision"]        = graphene::utilities::git_revision_sha;
   result["graphene_revision_age"]    = fc::get_approximate_relative_time_string( fc::time_point_sec( graphene::utilities::git_revision_unix_timestamp ) );
   result["fc_revision"]              = fc::git_revision_sha;
   result["fc_revision_age"]          = fc::get_approximate_relative_time_string( fc::time_point_sec( fc::git_revision_unix_timestamp ) );
   result["compile_date"]             = "compiled on " __DATE__ " at " __TIME__;
   result["boost_version"]            = boost::replace_all_copy(std::string(BOOST_LIB_VERSION), "_", ".");
   result["openssl_version"]          = OPENSSL_VERSION_TEXT;

   std::string bitness = boost::lexical_cast<std::string>(8 * sizeof(int*)) + "-bit";
#if defined(__APPLE__)
   std::string os = "osx";
#elif defined(__linux__)
   std::string os = "linux";
#elif defined(_MSC_VER)
   std::string os = "win32";
#else
   std::string os = "other";
#endif
   result["build"] = os + " " + bitness;

   return result;
}

std::map<string,std::function<string(fc::variant,const fc::variants&)>> wallet_api_impl::get_result_formatters() const
{
   std::map<string,std::function<string(fc::variant,const fc::variants&)> > m;
   m["help"] = [](variant result, const fc::variants& a)
   {
      return result.get_string();
   };

   m["gethelp"] = [](variant result, const fc::variants& a)
   {
      return result.get_string();
   };

   m["get_account_history"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<vector<operation_detail>>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;

      for( operation_detail& d : r )
      {
         operation_history_object& i = d.op;
         auto b = _remote_db->get_block_header(i.block_num);
         FC_ASSERT(b);
         ss << b->timestamp.to_iso_string() << " ";
         i.op.visit(operation_printer(ss, *this, i.result));
         ss << " \n";
      }

      return ss.str();
   };
   m["get_relative_account_history"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<vector<operation_detail>>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;

      for( operation_detail& d : r )
      {
         operation_history_object& i = d.op;
         auto b = _remote_db->get_block_header(i.block_num);
         FC_ASSERT(b);
         ss << b->timestamp.to_iso_string() << " ";
         i.op.visit(operation_printer(ss, *this, i.result));
         ss << " \n";
      }

      return ss.str();
   };

   m["get_account_history_by_operations"] = [this](variant result, const fc::variants& a) {
         auto r = result.as<account_history_operation_detail>( GRAPHENE_MAX_NESTED_OBJECTS );
         std::stringstream ss;
         ss << "total_count : ";
         ss << r.total_count;
         ss << " \n";
         ss << "result_count : ";
         ss << r.result_count;
         ss << " \n";
         for (operation_detail_ex& d : r.details) {
            operation_history_object& i = d.op;
            auto b = _remote_db->get_block_header(i.block_num);
            FC_ASSERT(b);
            ss << b->timestamp.to_iso_string() << " ";
            i.op.visit(operation_printer(ss, *this, i.result));
            ss << " transaction_id : ";
            ss << d.transaction_id.str();
            ss << " \n";
         }

         return ss.str();
   };

   m["list_account_balances"] = [this](variant result, const fc::variants& a)
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

   m["get_blind_balances"] = [this](variant result, const fc::variants& a)
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
   m["transfer_to_blind"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<blind_confirmation>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;
      r.trx.operations[0].visit( operation_printer( ss, *this, operation_result() ) );
      ss << "\n";
      for( const auto& out : r.outputs )
      {
         asset_object a = get_asset( out.decrypted_memo.amount.asset_id );
         ss << a.amount_to_pretty_string( out.decrypted_memo.amount ) << " to  " << out.label << "\n\t  receipt: " << out.confirmation_receipt <<"\n\n";
      }
      return ss.str();
   };
   m["blind_transfer"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<blind_confirmation>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;
      r.trx.operations[0].visit( operation_printer( ss, *this, operation_result() ) );
      ss << "\n";
      for( const auto& out : r.outputs )
      {
         asset_object a = get_asset( out.decrypted_memo.amount.asset_id );
         ss << a.amount_to_pretty_string( out.decrypted_memo.amount ) << " to  " << out.label << "\n\t  receipt: " << out.confirmation_receipt <<"\n\n";
      }
      return ss.str();
   };
   m["receive_blind_transfer"] = [this](variant result, const fc::variants& a)
   {
      auto r = result.as<blind_receipt>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;
      asset_object as = get_asset( r.amount.asset_id );
      ss << as.amount_to_pretty_string( r.amount ) << "  " << r.from_label << "  =>  " << r.to_label  << "  " << r.memo <<"\n";
      return ss.str();
   };
   m["blind_history"] = [this](variant result, const fc::variants& a)
   {
      auto records = result.as<vector<blind_receipt>>( GRAPHENE_MAX_NESTED_OBJECTS );
      std::stringstream ss;
      ss << "WHEN         "
         << "  " << "AMOUNT"  << "  " << "FROM" << "  =>  " << "TO" << "  " << "MEMO" <<"\n";
      ss << "====================================================================================\n";
      for( auto& r : records )
      {
         asset_object as = get_asset( r.amount.asset_id );
         ss << fc::get_approximate_relative_time_string( r.date )
            << "  " << as.amount_to_pretty_string( r.amount ) << "  " << r.from_label << "  =>  " << r.to_label  << "  " << r.memo <<"\n";
      }
      return ss.str();
   };
   m["get_order_book"] = [](variant result, const fc::variants& a)
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

   return m;
}

}}} // graphene::wallet::detail
