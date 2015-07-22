#include "Wallet.hpp"

Wallet::Wallet()
{
}

Wallet::~Wallet()
{
   close();
}

bool Wallet::open( QString file_path )
{
   return false;
}

bool Wallet::close()
{
   save();
   return false;
}

bool Wallet::save()
{
   return false;
}

bool Wallet::saveAs( QString file_path )
{
   return false;
}

bool Wallet::create( QString file_path, QString brain_key )
{
   return false;
}

bool Wallet::loadBrainKey( QString brain_key )
{
   return false;
}

bool Wallet::purgeBrainKey()
{
   return false;
}

bool Wallet::hasBrainKey()const
{
   return false;
}

QString Wallet::getBrainKey()const
{
   return QString();
}

bool Wallet::isLocked()const
{
   return false;
}
QString Wallet::unlock( QString password )
{
   return QString();
}

bool Wallet::lock()
{
   return false;
}
bool  Wallet::changePassword( QString new_password )
{
   return false;
}

QString Wallet::getActivePrivateKey( QString owner_pub_key, uint32_t seq )
{
   return QString();
}

QString Wallet::getOwnerPrivateKey( uint32_t seq )
{
   return QString();
}


QString Wallet::getKeyLabel( QString pubkey )
{
   return QString();
}

QString Wallet::getPublicKey( QString label )
{
   return QString();
}

/** imports a public key and assigns it a label */
bool    Wallet::importPublicKey( QString pubkey, QString label)
{
   return false;
}

/** 
 * @param wifkey a private key in (WIF) Wallet Import Format
 * @pre !isLocked() 
 **/
bool    Wallet::importPrivateKey( QString wifkey, QString label  )
{
   return false;
}

/** removes the key, its lablel and its private key */
bool    Wallet::removePublicKey( QString pubkey )
{
   return false;
}

/** removes only the private key, keeping the public key and label */
bool    Wallet::removePrivateKey( QString pubkey )
{
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


