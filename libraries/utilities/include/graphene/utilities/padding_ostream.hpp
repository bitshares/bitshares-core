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
#pragma once

namespace graphene { namespace utilities {

template<size_t BlockSize=16, char PaddingChar=' '>
class padding_ostream : public fc::buffered_ostream {
public:
   padding_ostream( fc::ostream_ptr o, size_t bufsize = 4096 ) : buffered_ostream(o, bufsize) {}
   virtual ~padding_ostream() {}

   virtual size_t writesome( const char* buffer, size_t len ) {
      auto out = buffered_ostream::writesome(buffer, len);
      bytes_out += out;
      bytes_out %= BlockSize;
      return out;
   }
   virtual size_t writesome( const std::shared_ptr<const char>& buf, size_t len, size_t offset ) {
      auto out = buffered_ostream::writesome(buf, len, offset);
      bytes_out += out;
      bytes_out %= BlockSize;
      return out;
   }
   virtual void flush() {
      static const char pad = PaddingChar;
      while( bytes_out % BlockSize )
         writesome(&pad, 1);
      buffered_ostream::flush();
   }

private:
   size_t bytes_out = 0;
};

} } //graphene::utilities

