/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <iomanip>
#include <boost/algorithm/string/join.hpp>
#include <graphene/wallet/wallet.hpp>
#include <graphene/wallet/api_documentation.hpp>

namespace graphene { namespace wallet {
   namespace detail {
      namespace
      {
         template <typename... Args>
         struct types_to_string_list_helper;

         template <typename First, typename... Args>
         struct types_to_string_list_helper<First, Args...>
         {
            std::list<std::string> operator()() const
            {
               std::list<std::string> argsList = types_to_string_list_helper<Args...>()();
               argsList.push_front(fc::get_typename<typename std::decay<First>::type>::name());
               return argsList;
            }
         };

         template <>
         struct types_to_string_list_helper<>
         {
            std::list<std::string> operator()() const
            {
               return std::list<std::string>();
            }
         };

         template <typename... Args>
         std::list<std::string> types_to_string_list()
         {
            return types_to_string_list_helper<Args...>()();
         }
      } // end anonymous namespace

      struct help_visitor
      {
         std::vector<method_description> method_descriptions;

         template<typename R, typename... Args>
         void operator()( const char* name, std::function<R(Args...)>& memb )
         {
            method_description this_method;
            this_method.method_name = name;
            std::ostringstream ss;
            ss << std::setw(40) << std::left << fc::get_typename<R>::name() << " " << name << "(";
            ss << boost::algorithm::join(types_to_string_list<Args...>(), ", ");
            ss << ")\n";
            this_method.brief_description = ss.str();
            method_descriptions.push_back(this_method);
         }
      };
   } // end namespace detail

   api_documentation::api_documentation()
   {
      fc::api<wallet_api> tmp;
      detail::help_visitor visitor;
      tmp->visit(visitor);
      std::copy(visitor.method_descriptions.begin(), visitor.method_descriptions.end(),
                std::inserter(method_descriptions, method_descriptions.end()));
   }

} } // end namespace graphene::wallet
