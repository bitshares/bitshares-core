/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <assert.h>

#include <algorithm>

#include <fc/crypto/hex.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/crypto/city.hpp>
#include <fc/log/logger.hpp>
#include <fc/network/ip.hpp>
#include <fc/exception/exception.hpp>

#include <graphene/net/stcp_socket.hpp>

namespace graphene { namespace net {

stcp_socket::stcp_socket()
//:_buf_len(0)
#ifndef NDEBUG
   : _read_buffer_in_use(false),
     _write_buffer_in_use(false)
#endif
{
}
stcp_socket::~stcp_socket()
{
}

namespace {
   // Off by default: performing the ML-KEM exchange against a peer that does not
   // expect it desynchronises the stream and partitions this node from the network.
   bool g_pq_handshake_enabled = false;
}

void stcp_socket::set_pq_handshake_enabled( bool enabled )
{
   g_pq_handshake_enabled = enabled;
}

bool stcp_socket::pq_handshake_enabled()
{
   return g_pq_handshake_enabled;
}

void stcp_socket::do_key_exchange()
{
  do_ecdh_key_exchange();
  if( g_pq_handshake_enabled )
  {
     do_mlkem_key_exchange();   // also derives _shared_secret and keys the ciphers
     return;
  }

  // Legacy path: byte-for-byte identical to a node without the PQ feature --
  // the ECDH secret is used directly, with no extra bytes on the wire.
  _shared_secret = _ecdh_secret;
  _send_aes.init( fc::sha256::hash( (char*)&_shared_secret, sizeof(_shared_secret) ),
                  fc::city_hash_crc_128( (char*)&_shared_secret, sizeof(_shared_secret) ) );
  _recv_aes.init( fc::sha256::hash( (char*)&_shared_secret, sizeof(_shared_secret) ),
                  fc::city_hash_crc_128( (char*)&_shared_secret, sizeof(_shared_secret) ) );
}

void stcp_socket::do_ecdh_key_exchange()
{
  _priv_key = fc::ecc::private_key::generate();
  fc::ecc::public_key pub = _priv_key.get_public_key();
  fc::ecc::public_key_data s = pub.serialize();
  std::shared_ptr<char> serialized_key_buffer(new char[sizeof(fc::ecc::public_key_data)], [](char* p){ delete[] p; });
  memcpy(serialized_key_buffer.get(), (char*)&s, sizeof(fc::ecc::public_key_data));
  _sock.write( serialized_key_buffer, sizeof(fc::ecc::public_key_data) );
  _sock.read( serialized_key_buffer, sizeof(fc::ecc::public_key_data) );
  fc::ecc::public_key_data rpub;
  memcpy((char*)&rpub, serialized_key_buffer.get(), sizeof(fc::ecc::public_key_data));

  _ecdh_secret = _priv_key.get_shared_secret( rpub );
}

void stcp_socket::do_mlkem_key_exchange()
{
  // Post-quantum (FIPS 203 ML-KEM-768) key exchange on top of the ECDH part.
  const fc::pq_algorithm alg = fc::pq_algorithm::ml_kem_768;
  const uint16_t pk_size = fc::pqc_sizes::public_key_size( alg );
  const uint16_t ct_size = fc::pqc_sizes::ciphertext_size( alg );

  auto kp = fc::pq_kem_generate( alg );

  std::shared_ptr<char> pk_buffer( new char[pk_size], [](char* p){ delete[] p; } );
  memcpy( pk_buffer.get(), kp.pk.data(), kp.pk.size() );
  _sock.write( pk_buffer, pk_size );
  _sock.read( pk_buffer, pk_size );

  std::vector<char> peer_pk( pk_buffer.get(), pk_buffer.get() + pk_size );

  auto result = fc::pq_kem_encapsulate( alg, peer_pk );
  FC_ASSERT( result.valid, "ML-KEM encapsulation failed" );

  std::shared_ptr<char> ct( new char[ct_size], [](char* p){ delete[] p; } );
  memcpy( ct.get(), result.ciphertext.data(), result.ciphertext.size() );
  _sock.write( ct, ct_size );
  _sock.read( ct, ct_size );

  std::vector<char> peer_ct( ct.get(), ct.get() + ct_size );
  std::vector<char> peer_ss = fc::pq_kem_decapsulate( alg, kp.sk, peer_ct );

  // hybrid: shared_secret = sha512( ECDH || S(min pk) || S(max pk) )
  //
  // Unlike ECDH, ML-KEM encapsulate/decapsulate is NOT commutative: `result.shared_secret`
  // is the secret *this* side derived by encapsulating to the *peer's* public key, while
  // `peer_ss` is the secret this side recovered by decapsulating a ciphertext the peer
  // encapsulated to *this side's own* public key -- two distinct values. do_key_exchange()
  // runs identically on both connect_to() (client) and accept() (server), so concatenating
  // "own-then-peer" in a fixed order made each side hash the same two secrets in opposite
  // order, so the two sides never derived the same _shared_secret (every hybrid-KEM
  // connection failed to decrypt). Fix: order the two secrets by comparing the two KEM
  // public keys, which are already exchanged in the clear -- both sides compare the same
  // bytes and therefore agree on the same order regardless of which side is client/server.
  const bool own_pk_is_smaller = std::lexicographical_compare(
        kp.pk.begin(), kp.pk.end(), peer_pk.begin(), peer_pk.end() );

  fc::sha512::encoder enc;
  enc.write( (const char*)&_ecdh_secret, sizeof(_ecdh_secret) );
  if( own_pk_is_smaller )
  {
     // Our own pk is the smaller of the two, so the secret targeting it -- the one the peer
     // encapsulated to us, which we decapsulated -- is canonically first.
     enc.write( peer_ss.data(), peer_ss.size() );
     enc.write( result.shared_secret.data(), result.shared_secret.size() );
  }
  else
  {
     // The peer's pk is the smaller one, so the secret we ourselves encapsulated to it is
     // canonically first.
     enc.write( result.shared_secret.data(), result.shared_secret.size() );
     enc.write( peer_ss.data(), peer_ss.size() );
  }
  _shared_secret = enc.result();

  _send_aes.init( fc::sha256::hash( (char*)&_shared_secret, sizeof(_shared_secret) ),
                  fc::city_hash_crc_128((char*)&_shared_secret,sizeof(_shared_secret) ) );
  _recv_aes.init( fc::sha256::hash( (char*)&_shared_secret, sizeof(_shared_secret) ),
                  fc::city_hash_crc_128((char*)&_shared_secret,sizeof(_shared_secret) ) );
}


void stcp_socket::connect_to( const fc::ip::endpoint& remote_endpoint )
{
  _sock.connect_to( remote_endpoint );
  do_key_exchange();
}

void stcp_socket::bind( const fc::ip::endpoint& local_endpoint )
{
  _sock.bind(local_endpoint);
}

/**
 *   This method must read at least 16 bytes at a time from
 *   the underlying TCP socket so that it can decrypt them. It
 *   will buffer any left-over.
 */
size_t stcp_socket::readsome( char* buffer, size_t len )
{ try {
    assert( len > 0 && (len % 16) == 0 );

#ifndef NDEBUG
    // This code was written with the assumption that you'd only be making one call to readsome 
    // at a time so it reuses _read_buffer.  If you really need to make concurrent calls to 
    // readsome(), you'll need to prevent reusing _read_buffer here
    struct check_buffer_in_use {
      bool& _buffer_in_use;
      check_buffer_in_use(bool& buffer_in_use) : _buffer_in_use(buffer_in_use) { assert(!_buffer_in_use); _buffer_in_use = true; }
      ~check_buffer_in_use() { assert(_buffer_in_use); _buffer_in_use = false; }
    } buffer_in_use_checker(_read_buffer_in_use);
#endif

    const size_t read_buffer_length = 4096;
    if (!_read_buffer)
      _read_buffer.reset(new char[read_buffer_length], [](char* p){ delete[] p; });

    len = std::min<size_t>(read_buffer_length, len);

    size_t s = _sock.readsome( _read_buffer, len, 0 );
    if( s % 16 ) 
    {
      _sock.read(_read_buffer, 16 - (s%16), s);
      s += 16-(s%16);
    }
    _recv_aes.decode( _read_buffer.get(), s, buffer );
    return s;
} FC_RETHROW_EXCEPTIONS( warn, "", ("len",len) ) }

size_t stcp_socket::readsome( const std::shared_ptr<char>& buf, size_t len, size_t offset ) 
{
  return readsome(buf.get() + offset, len);
}

bool stcp_socket::eof()const
{
  return _sock.eof();
}

size_t stcp_socket::writesome( const char* buffer, size_t len )
{ try {
    assert( len > 0 && (len % 16) == 0 );

#ifndef NDEBUG
    // This code was written with the assumption that you'd only be making one call to writesome
    // at a time so it reuses _write_buffer.  If you really need to make concurrent calls to 
    // writesome(), you'll need to prevent reusing _write_buffer here
    struct check_buffer_in_use {
      bool& _buffer_in_use;
      check_buffer_in_use(bool& buffer_in_use) : _buffer_in_use(buffer_in_use) { assert(!_buffer_in_use); _buffer_in_use = true; }
      ~check_buffer_in_use() { assert(_buffer_in_use); _buffer_in_use = false; }
    } buffer_in_use_checker(_write_buffer_in_use);
#endif

    const std::size_t write_buffer_length = 4096;
    if (!_write_buffer)
      _write_buffer.reset(new char[write_buffer_length], [](char* p){ delete[] p; });
    len = std::min<size_t>(write_buffer_length, len);
    memset(_write_buffer.get(), 0, len); // just in case aes.encode screws up
    /**
     * every sizeof(crypt_buf) bytes the aes channel
     * has an error and doesn't decrypt properly...  disable
     * for now because we are going to upgrade to something
     * better.
     */
    uint32_t ciphertext_len = _send_aes.encode( buffer, len, _write_buffer.get() );
    assert(ciphertext_len == len);
    _sock.write( _write_buffer, ciphertext_len );
    return ciphertext_len;
} FC_RETHROW_EXCEPTIONS( warn, "", ("len",len) ) }

size_t stcp_socket::writesome( const std::shared_ptr<const char>& buf, size_t len, size_t offset )
{
  return writesome(buf.get() + offset, len);
}

void stcp_socket::flush()
{
  _sock.flush();
}


void stcp_socket::close()
{
  try 
  {
    _sock.close();
  }FC_RETHROW_EXCEPTIONS( warn, "error closing stcp socket" );
}

void stcp_socket::accept()
{
  do_key_exchange();
}


}} // namespace graphene::net

