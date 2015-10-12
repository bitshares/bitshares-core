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
#include "Transaction.hpp"
#include "Wallet.hpp"

#include <graphene/utilities/key_conversion.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>

#include <fc/crypto/aes.hpp>
#include <fc/io/json.hpp>

QString toQString(public_key_type k) { return QString::fromStdString(fc::variant(k).as_string()); }

Wallet::Wallet(QObject* parent)
:QObject(parent)
{
}

Wallet::~Wallet()
{
   close();
}

bool Wallet::open(QString file_path)
{
   fc::path p(file_path.toStdString());
   if( !fc::exists(p) )
   {
      ilog("Unable to open wallet file '${f}'; it does not exist", ("f",p));
      return false;
   }

   _data = fc::json::from_file(p).as<wallet_file>();

   for( const auto& key : _data.encrypted_private_keys )
   {
      if( key.second.label != string() )
         _label_to_key[QString::fromStdString(key.second.label)] = toQString(key.first);
      if( key.second.encrypted_private_key.size() )
         _available_private_keys.insert(key.first);
   }
   _wallet_file_path = p;

   Q_EMIT isOpenChanged(true);
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
   Q_EMIT isOpenChanged(false);
   return false;
}

bool Wallet::save()
{
   if( !isOpen() ) return false;
   /// TODO: backup existing wallet file first.
   fc::json::save_to_file(_data, _wallet_file_path);
   return false;
}

bool Wallet::saveAs(QString file_path)
{
   if( !isOpen() ) return false;
   fc::path p(file_path.toStdString());
   if( fc::exists(p) ) return false;
   fc::json::save_to_file(_data, _wallet_file_path);
   return true;
}

bool Wallet::create(QString file_path, QString password, QString brain_key)
{
   if( isOpen() ) return false;
   if( password == QString() ) return false;

   fc::path p(file_path.toStdString());
   if( fc::exists(p) )
   {
      ilog("Unable to create wallet file '${f}' because a file with that name already exists.", ("f",p));
      return false;
   }

   if( brain_key == QString() )
   {
      auto key = fc::ecc::private_key::generate().get_secret();
      brain_key.fromStdString( fc::variant(key).as_string() );
   }
   auto brainkey = brain_key.toStdString();
   auto pw_str = password.toStdString();

   auto password_hash                = fc::sha512::hash(pw_str.c_str(), pw_str.size());
   _decrypted_master_key             = fc::sha512::hash(fc::ecc::private_key::generate().get_secret());
   _data.master_key_digest           = fc::sha512::hash(_decrypted_master_key);
   _data.encrypted_master_key        = fc::aes_encrypt(password_hash, fc::raw::pack(_decrypted_master_key));

   _data.brain_key_digest            = fc::sha512::hash(brainkey.c_str(), brainkey.size());
   _data.encrypted_brain_key         = fc::aes_encrypt(_decrypted_master_key, fc::raw::pack(brainkey));

   QFileInfo(file_path).absoluteDir().mkpath(".");
   fc::json::save_to_file(_data, p);
   _wallet_file_path = p;
   ilog("Created wallet file '${f}'", ("f", p));

   return true;
}

bool Wallet::loadBrainKey(QString brain_key)
{
   if( !isOpen() ) return false;

   if( brain_key == QString() ) return false;
   auto brainkey = brain_key.toStdString();

   if( _data.brain_key_digest != fc::sha512::hash(brainkey.c_str(), brainkey.size()) )
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

   auto dec_brain_key = fc::aes_decrypt(_decrypted_master_key, _data.encrypted_brain_key);
   auto dec_brain_key_str = fc::raw::unpack<string>(dec_brain_key);
   _brain_key.fromStdString(dec_brain_key_str);
   return _brain_key;
}

bool Wallet::isLocked()const
{
   if( !isOpen() ) return true;
   return _decrypted_master_key == fc::sha512();
}
bool Wallet::unlock(QString password)
{
   try {
      if( !isLocked() ) return true;
      auto pw_str                  = password.toStdString();
      auto password_hash           = fc::sha512::hash(pw_str.c_str(), pw_str.size());
      auto plain_txt = fc::aes_decrypt(password_hash, _data.encrypted_master_key);
      _decrypted_master_key = fc::raw::unpack<fc::sha512>(plain_txt);
      if( _data.master_key_digest != fc::sha512::hash(_decrypted_master_key) )
         _decrypted_master_key = fc::sha512();

      Q_EMIT isLockedChanged(isLocked());
      return !isLocked();
   } catch (const fc::exception& e) {
      elog(e.to_detail_string());
      return false;
   }
}

bool Wallet::lock()
{
   if( !isOpen() ) return false;
   _brain_key            = QString();
   _decrypted_master_key = fc::sha512();

   Q_EMIT isLockedChanged(isLocked());
   return true;
}
bool  Wallet::changePassword(QString new_password)
{
   if( !isOpen() ) return false;
   if( isLocked() ) return false;

   auto pw_str                  = new_password.toStdString();
   auto password_hash           = fc::sha512::hash(pw_str.c_str(), pw_str.size());
   _data.encrypted_master_key   = fc::aes_encrypt(password_hash, fc::raw::pack(_decrypted_master_key));

   save();

   return true;
}
bool    Wallet::hasPrivateKey(QString pubkey, bool include_with_brain_key)
{
   auto pub = fc::variant(pubkey.toStdString()).as<public_key_type>();
   auto itr = _data.encrypted_private_keys.find(pub);
   if( itr == _data.encrypted_private_keys.end() )
      return false;
   if( itr->second.encrypted_private_key.size() )
      return true;
   if( include_with_brain_key && itr->second.brain_sequence >= 0 )
   {
      if( !itr->second.owner )
         return true;
      return hasPrivateKey(toQString(*itr->second.owner), include_with_brain_key);
   }
   return false;
}

QString Wallet::getPrivateKey(QString pubkey)
{
   if( !isOpen() ) return QString();
   if( isLocked() ) return QString();

   auto pub = fc::variant(pubkey.toStdString()).as<public_key_type>();
   auto itr = _data.encrypted_private_keys.find(pub);
   if( itr == _data.encrypted_private_keys.end() )
      return QString();
   if( itr->second.encrypted_private_key.size() == 0 )
      return QString();
   auto plain = fc::aes_decrypt(_decrypted_master_key, itr->second.encrypted_private_key);
   return QString::fromStdString(fc::raw::unpack<std::string>(plain));
}

QString Wallet::getPublicKey(QString wif_private_key)const
{
   auto priv = graphene::utilities::wif_to_key(wif_private_key.toStdString());
   if( !priv ) return QString();

   auto pub = public_key_type(priv->get_public_key());

   return QString::fromStdString(fc::variant(pub).as_string());
}

QString Wallet::getActivePrivateKey(QString owner_pub_key, uint32_t seq)
{
   if( !isOpen() ) return QString();
   if( isLocked() ) return QString();

   auto owner_wif_private_key = getPrivateKey(owner_pub_key);
   if( owner_wif_private_key == QString() ) return QString();

   auto seed =  (owner_wif_private_key + " " + QString::number(seq)).toStdString();
   auto secret = fc::sha256::hash(fc::sha512::hash(seed.c_str(), seed.size()));

   auto wif        = graphene::utilities::key_to_wif(secret);
   auto priv_key   = graphene::utilities::wif_to_key(wif);
   if( !priv_key ) return QString();

   public_key_type active_pub_key(priv_key->get_public_key());
   _data.encrypted_private_keys[active_pub_key].encrypted_private_key = fc::aes_encrypt(_decrypted_master_key, fc::raw::pack(wif));
   _data.encrypted_private_keys[active_pub_key].owner = fc::variant(owner_pub_key.toStdString()).as<public_key_type>();
   _data.encrypted_private_keys[active_pub_key].brain_sequence = seq;
   _available_private_keys.insert( active_pub_key );

   return QString::fromStdString(wif);
}

QString Wallet::getOwnerPrivateKey(uint32_t seq)
{
   if( !isOpen() ) return QString();
   if( isLocked() ) return QString();
   if( !hasBrainKey() ) return QString();

   auto seed = (getBrainKey() + " " + QString::number(seq)).toStdString();

   auto secret = fc::sha256::hash(fc::sha512::hash(seed.c_str(), seed.size()));

   auto wif        = graphene::utilities::key_to_wif(secret);
   auto priv_key   = graphene::utilities::wif_to_key(wif);
   if( !priv_key ) return QString();

   public_key_type owner_pub_key(priv_key->get_public_key());
   _data.encrypted_private_keys[owner_pub_key].encrypted_private_key = fc::aes_encrypt(_decrypted_master_key, fc::raw::pack(wif));
   _data.encrypted_private_keys[owner_pub_key].brain_sequence = seq;
   _available_private_keys.insert(owner_pub_key);

   return QString::fromStdString(wif);
}

QString Wallet::getActivePublicKey(QString active_pub, uint32_t seq)
{
   return getPublicKey(getActivePrivateKey(active_pub, seq));
}

QString Wallet::getOwnerPublicKey(uint32_t seq)
{
   return getPublicKey(getOwnerPrivateKey(seq));
}


QString Wallet::getKeyLabel(QString pubkey)
{
   if( !isOpen() ) return QString();
   public_key_type key = fc::variant( pubkey.toStdString() ).as<public_key_type>();
   auto itr = _data.encrypted_private_keys.find(key);
   if( itr == _data.encrypted_private_keys.end() )
      return QString();
   return QString::fromStdString(itr->second.label);
}
/**
 *  The same label may not be assigned to more than one key, this method will
 *  fail if a key with the same label already exists.
 *
 *  @return true if the label was set
 */
bool    Wallet::setKeyLabel(QString pubkey, QString label)
{
   if( label == QString() ) // clear the label
   {
      auto pub = fc::variant(pubkey.toStdString()).as<public_key_type>();
      auto old_label = _data.encrypted_private_keys[pub].label;
      _data.encrypted_private_keys[pub].label = string();
      if( old_label.size() )
         _label_to_key.erase(QString::fromStdString(old_label));

      return true;
   }

   auto itr = _label_to_key.find(label);
   if( itr != _label_to_key.end() )
      return false;

   _label_to_key[label] = pubkey;

   auto pub = fc::variant(pubkey.toStdString()).as<public_key_type>();
   _data.encrypted_private_keys[pub].label = label.toStdString();

   return true;
}


QList<QPair<QString,QString>> Wallet::getAllPublicKeys(bool only_if_private)const
{
   QList< QPair<QString,QString>> result;
   if( !isOpen() ) return result;

   for( const auto& item : _data.encrypted_private_keys )
   {
      if( only_if_private && !item.second.encrypted_private_key.size() ) continue;
      result.push_back(qMakePair(toQString(item.first), QString::fromStdString(item.second.label)));
   }

   return result;
}

QString Wallet::getPublicKey(QString label)
{
   if( !isOpen() ) return QString::null;

   auto itr = _label_to_key.find(label);
   if( itr != _label_to_key.end() )
      return QString::null;

   return itr->second;
}

/** imports a public key and assigns it a label */
bool    Wallet::importPublicKey(QString pubkey, QString label)
{
   return setKeyLabel(pubkey, label);
}

/**
 * @param wifkey a private key in (WIF) Wallet Import Format
 * @pre !isLocked()
 **/
bool    Wallet::importPrivateKey(QString wifkey, QString label)
{
   if( !isOpen() ) return false;
   if( isLocked() ) return false;

   auto priv = graphene::utilities::wif_to_key(wifkey.toStdString());
   if (!priv) return false;

   auto p = priv->get_public_key();
   auto pub = toQString(p);
   importPublicKey(pub, label);

   _data.encrypted_private_keys[p].encrypted_private_key = fc::aes_encrypt(_decrypted_master_key, fc::raw::pack(wifkey.toStdString()));
   _available_private_keys.insert(p);

   return true;
}

/** removes the key, its lablel and its private key */
bool    Wallet::removePublicKey(QString pubkey)
{
   if( !isOpen() ) return false;
   auto pub = fc::variant( pubkey.toStdString() ).as<public_key_type>();
   _available_private_keys.erase(pub);

   auto itr = _data.encrypted_private_keys.find(pub);
   if( itr != _data.encrypted_private_keys.end() )
   {
      _label_to_key.erase(QString::fromStdString(itr->second.label));
      _data.encrypted_private_keys.erase(itr);
      return true;
   }
   return false;
}

/** removes only the private key, keeping the public key and label
 *
 * @pre isOpen() && !isLocked()
 **/
bool    Wallet::removePrivateKey(QString pubkey)
{
   if( !isOpen() ) return false;
   if( isLocked() ) return false;

   auto pub = fc::variant(pubkey.toStdString()).as<public_key_type>();
   _data.encrypted_private_keys[pub].encrypted_private_key.resize(0);
   _available_private_keys.erase(pub);

   return true;
}

/**
 * @pre !isLocked()
 */
vector<signature_type> Wallet::signDigest(const digest_type& d, const set<public_key_type>& keys)const
{
   vector<signature_type> result;
   if( !isOpen() )  return result;
   if( isLocked() ) return result;

   result.reserve( keys.size() );

   for( const auto& key : keys )
   {
      auto itr = _data.encrypted_private_keys.find(key);
      if( itr == _data.encrypted_private_keys.end() )
         return vector<signature_type>();
      if( itr->second.encrypted_private_key.size() == 0 )
         return vector<signature_type>();

      auto plain_wif  = fc::aes_decrypt(_decrypted_master_key, itr->second.encrypted_private_key);
      auto wif = fc::raw::unpack<std::string>(plain_wif);
      auto priv = graphene::utilities::wif_to_key(wif);
      if( !priv ) return vector<signature_type>();

      result.push_back(priv->sign_compact(d));
   }

   return result;
}

const flat_set<public_key_type>& Wallet::getAvailablePrivateKeys()const
{
   return _available_private_keys;
}
