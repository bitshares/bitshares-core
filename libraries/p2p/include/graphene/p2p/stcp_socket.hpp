/*
 * Copyright (c) 2015, Cryptonomex, Inc. All rights reserved.
 */
#pragma once
#include <fc/network/tcp_socket.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/crypto/elliptic.hpp>

namespace graphene { namespace p2p {

/**
 *  Uses ECDH to negotiate a aes key for communicating
 *  with other nodes on the network.
 */
class stcp_socket : public virtual fc::iostream
{
  public:
    stcp_socket();
    ~stcp_socket();
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

    fc::sha512           _shared_secret;
    fc::ecc::private_key _priv_key;
    fc::array<char,8>    _buf;
    //uint32_t             _buf_len;
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

} } // graphene::p2p
