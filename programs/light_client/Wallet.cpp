#include "Wallet.hpp"
#include <fc/crypto/aes.hpp>
#include <fc/io/json.hpp>

Wallet::Wallet()
{
}

Wallet::~Wallet()
{
   close();
}

bool Wallet::open( QString file_path )
{
   fc::path p( file_path.toStdString() );
   if( !fc::exists( p ) )
   {
      ilog( "Unable to open wallet file '${f}', it does not exist", ("f",p) );
      return false;
   }
   _wallet_file_path = p;
   return true;
}

bool Wallet::isOpen()const
{
   return _wallet_file_path != fc::path();
}

bool Wallet::close()
{
   if( !isOpen() ) return false;
   save();
   _wallet_file_path = fc::path();
   return false;
}

bool Wallet::save()
{
   if( !isOpen() ) return false;
   /// TODO: backup existing wallet file first.
   fc::json::save_to_file( _data, _wallet_file_path );
   return false;
}

bool Wallet::saveAs( QString file_path )
{
   if( !isOpen() ) return false;
   return false;
}

bool Wallet::create( QString file_path, QString password, QString brain_key )
{
   if( isOpen() ) return false;
   if( password == QString() ) return false;

   fc::path p( file_path.toStdString() );
   if( fc::exists( p ) )
   {
      ilog( "Unable to create wallet file '${f}' because a file with that name already exists.", ("f",p) );
      return false;
   }

   if( brain_key == QString() )
   {
      auto key = fc::ecc::private_key::generate().get_secret();
      brain_key.fromStdString( fc::variant(key).as_string() );
   }
   auto brainkey = brain_key.toStdString();
   auto pw_str = password.toStdString();

   auto password_hash                = fc::sha512::hash( pw_str.c_str(), pw_str.size() );
   _decrypted_master_key             = fc::sha512::hash( fc::ecc::private_key::generate().get_secret() );
   _data.master_key_digest           = fc::sha512::hash( _decrypted_master_key );
   _data.encrypted_master_key        = fc::aes_encrypt( password_hash, fc::raw::pack(_decrypted_master_key) );

   _data.brain_key_digest            = fc::sha512::hash( brainkey.c_str(), brainkey.size() );
   _data.encrypted_brain_key         = fc::aes_encrypt( _decrypted_master_key, fc::raw::pack( brainkey ) );

   fc::json::save_to_file( _data, p );
   _wallet_file_path = p;

   return false;
}

bool Wallet::loadBrainKey( QString brain_key )
{
   if( !isOpen() ) return false;

   if( brain_key == QString() ) return false;
   auto brainkey = brain_key.toStdString();

   if( _data.brain_key_digest  != fc::sha512::hash( brainkey.c_str(), brainkey.size() ) )
      return false;

   _brain_key = brain_key;
   return true;
}

bool Wallet::purgeBrainKey()
{
   if( !isOpen() ) return false;
   _data.encrypted_brain_key.resize(0);
   _brain_key = QString();
   return save();
}

bool Wallet::hasBrainKey()const
{
   if( !isOpen() ) return false;
   return _brain_key != QString() || _data.encrypted_brain_key.size();
}

QString Wallet::getBrainKey()
{
   if( !isOpen() ) return QString();
   if( isLocked() ) return QString();

   if( _brain_key != QString() )
      return _brain_key;

   auto dec_brain_key = fc::aes_decrypt( _decrypted_master_key, _data.encrypted_brain_key );
   auto dec_brain_key_str = fc::raw::unpack<string>(dec_brain_key);
   _brain_key.fromStdString( dec_brain_key_str );
   return _brain_key;
}

bool Wallet::isLocked()const
{
   if( !isOpen() ) return true;
   return false;
}
QString Wallet::unlock( QString password )
{
   return QString();
}

bool Wallet::lock()
{
   if( !isOpen() ) return false;
   _brain_key            = QString();
   _decrypted_master_key = fc::sha512();
   return true;
}
bool  Wallet::changePassword( QString new_password )
{
   if( !isOpen() ) return false;
   return false;
}

QString Wallet::getActivePrivateKey( QString owner_pub_key, uint32_t seq )
{
   if( !isOpen() ) return QString();
   return QString();
}

QString Wallet::getOwnerPrivateKey( uint32_t seq )
{
   if( !isOpen() ) return QString();
   return QString();
}


QString Wallet::getKeyLabel( QString pubkey )
{
   if( !isOpen() ) return QString();
   return QString();
}

QString Wallet::getPublicKey( QString label )
{
   if( !isOpen() ) return QString();
   return QString();
}

/** imports a public key and assigns it a label */
bool    Wallet::importPublicKey( QString pubkey, QString label)
{
   if( !isOpen() ) return false;
   return false;
}

/** 
 * @param wifkey a private key in (WIF) Wallet Import Format
 * @pre !isLocked() 
 **/
bool    Wallet::importPrivateKey( QString wifkey, QString label  )
{
   if( !isOpen() ) return false;
   return false;
}

/** removes the key, its lablel and its private key */
bool    Wallet::removePublicKey( QString pubkey )
{
   if( !isOpen() ) return false;
   return false;
}

/** removes only the private key, keeping the public key and label */
bool    Wallet::removePrivateKey( QString pubkey )
{
   if( !isOpen() ) return false;
   return false;
}

/**
 * @pre !isLocked()
 */
vector<signature_type>           Wallet::signDigest( const digest_type& d, 
                                             const set<public_key_type>& keys )const
{
   vector<signature_type> result;
   return result;
}

const flat_set<public_key_type>& Wallet::getAvailablePrivateKeys()const
{
   return _available_private_keys;
}


