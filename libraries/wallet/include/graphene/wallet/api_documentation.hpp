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
#pragma once

#include <string>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

#include <fc/exception/exception.hpp>

namespace graphene { namespace wallet {

   struct method_description
   {
      std::string method_name;
      std::string brief_description;
      std::string detailed_description;
   };

   class api_documentation
   {
      typedef boost::multi_index::multi_index_container<method_description,
         boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<
               boost::multi_index::member<method_description, std::string, &method_description::method_name> > > > method_description_set;
      method_description_set method_descriptions;
   public:
      api_documentation();
      std::string get_brief_description(const std::string& method_name) const
      {
         auto iter = method_descriptions.find(method_name);
         if (iter != method_descriptions.end())
            return iter->brief_description;
         else
            FC_THROW_EXCEPTION(fc::key_not_found_exception, "No entry for method ${name}", ("name", method_name));
      }
      std::string get_detailed_description(const std::string& method_name) const
      {
         auto iter = method_descriptions.find(method_name);
         if (iter != method_descriptions.end())
            return iter->detailed_description;
         else
            FC_THROW_EXCEPTION(fc::key_not_found_exception, "No entry for method ${name}", ("name", method_name));
      }
      std::vector<std::string> get_method_names() const
      {
         std::vector<std::string> method_names;
         for (const method_description& method_description: method_descriptions)
            method_names.emplace_back(method_description.method_name);
         return method_names;
      }
   };

} } // end namespace graphene::wallet
