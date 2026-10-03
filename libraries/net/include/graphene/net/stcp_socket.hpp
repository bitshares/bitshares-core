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
#pragma once
#include <fc/network/tcp_socket.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/pqc.hpp>

namespace graphene { namespace net {

/**
 *  Uses ECDH + ML-KEM (FIPS 203, hybrid) to negotiate an aes key for
 *  communicating with other nodes on the network:
 *
 *    shared_secret = sha512( ECDH_secret || ML-KEM_shared_secret )
 *
 *  The ML-KEM part protects the channel against quantum attackers, while the
 *  retained ECDH part keeps the classic forward-secrecy properties.
 *
 *  The ML-KEM exchange is DISABLED by default, because it is not
 *  wire-compatible with peers that do not perform it: the extra key and
 *  ciphertext bytes are read by such a peer as encrypted payload, and since
 *  the two sides then derive different AES keys, every subsequent byte
 *  decrypts to noise. In practice the peer reads a nonsense message length
 *  and drops the connection, so an upgraded node is silently partitioned from
 *  the rest of the network the moment it starts.
 *
 *  Enabling it therefore requires every peer to have it enabled too, which is
 *  why it is an explicit operator decision (--enable-pq-p2p) rather than
 *  something that switches on with the PQ hardfork. It deliberately cannot be
 *  keyed off chain state: a node syncing from the genesis block would evaluate
 *  "is PQ active" as false while its already-synced peers evaluated it as
 *  true, so new nodes could never join an activated chain.
 */
class stcp_socket : public virtual fc::iostream
{
  public:
    stcp_socket();
    ~stcp_socket();

    /// Enable the hybrid ML-KEM handshake process-wide. Off by default; only
    /// turn it on when every peer this node talks to also has it on.
    static void set_pq_handshake_enabled( bool enabled );
    static bool pq_handshake_enabled();
    fc::tcp_socket&  get_socket() { return _sock; }
    void             accept();

    void             connect_to( const fc::ip::endpoint& remote_endpoint );
    void             bind( const fc::ip::endpoint& local_endpoint );

    virtual size_t   readsome( char* buffer, size_t max );
    virtual size_t   readsome( const std::shared_ptr<char>& buf, size_t len, size_t offset );
    virtual bool     eof()const;

    virtual size_t   writesome( const char* buffer, size_t len );
    virtual size_t   writesome( const std::shared_ptr<const char>& buf, size_t len, size_t offset );

    virtual void     flush();
    virtual void     close();

    using istream::get;
    void             get( char& c ) { read( &c, 1 ); }
    fc::sha512       get_shared_secret() const { return _shared_secret; }
  private:
    void do_key_exchange();
    void do_ecdh_key_exchange();
    void do_mlkem_key_exchange();

    fc::sha512           _shared_secret;
    fc::sha512           _ecdh_secret;
    fc::ecc::private_key _priv_key;
    fc::tcp_socket       _sock;
    fc::aes_encoder      _send_aes;
    fc::aes_decoder      _recv_aes;
    std::shared_ptr<char> _read_buffer;
    std::shared_ptr<char> _write_buffer;
#ifndef NDEBUG
    bool _read_buffer_in_use;
    bool _write_buffer_in_use;
#endif
};

typedef std::shared_ptr<stcp_socket> stcp_socket_ptr;

} } // graphene::net
