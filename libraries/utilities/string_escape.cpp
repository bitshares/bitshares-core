/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <graphene/utilities/string_escape.hpp>
#include <sstream>

namespace graphene { namespace utilities {

  std::string escape_string_for_c_source_code(const std::string& input)
  {
    std::ostringstream escaped_string;
    escaped_string << "\"";
    for (unsigned i = 0; i < input.size(); ++i)
    {
      switch (input[i])
      {
      case '\a': 
        escaped_string << "\\a";
        break;
      case '\b': 
        escaped_string << "\\b";
        break;
      case '\t': 
        escaped_string << "\\t";
        break;
      case '\n': 
        escaped_string << "\\n";
        break;
      case '\v': 
        escaped_string << "\\v";
        break;
      case '\f': 
        escaped_string << "\\f";
        break;
      case '\r': 
        escaped_string << "\\r";
        break;
      case '\\': 
        escaped_string << "\\\\";
        break;
      case '\"': 
        escaped_string << "\\\"";
        break;
      default:
        escaped_string << input[i];
      }
    }
    escaped_string << "\"";
    return escaped_string.str();
  }

} } // end namespace graphene::utilities

