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
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include <graphene/chain/protocol/types.hpp>

#include <QObject>
#include <QList>
#include <QPair>
#include <QtQml>

using std::string;
using std::vector;
using std::set;
using fc::flat_set;
using std::map;
using graphene::chain::public_key_type;
using graphene::chain::digest_type;
using graphene::chain::signature_type;
using fc::optional;

class Transaction;

QString toQString(public_key_type k);

struct key_data
{
   string       label; /** unique label assigned to this key */
   /** encrypted as a packed std::string containing a wif private key */
   vector<char>              encrypted_private_key;
   int32_t                   brain_sequence = -1;
   optional<public_key_type> owner; /// if this key was derived from an owner key + sequence
};

struct wallet_file
{
   vector<char>                    encrypted_brain_key;
   fc::sha512                      brain_key_digest;
   vector<char>                    encrypted_master_key;
   fc::sha512                      master_key_digest;
   map<public_key_type, key_data>  encrypted_private_keys;
};


/**
 *  @class Wallet
 *  @brief Securely maintains a set of labeled public and private keys
 *
 *  @note this API is designed for use with QML which does not support exceptions so
 *  all errors are passed via return values.
 */
class Wallet : public QObject
{
   Q_OBJECT
   Q_PROPERTY(bool isOpen READ isOpen NOTIFY isOpenChanged)
   Q_PROPERTY(bool isLocked READ isLocked NOTIFY isLockedChanged)
   public:
      Wallet( QObject* parent = nullptr );
      ~Wallet();

      Q_INVOKABLE bool open(QString file_path);
      Q_INVOKABLE bool close();
      bool isOpen()const;
      Q_INVOKABLE bool save();
      Q_INVOKABLE bool saveAs(QString file_path);
      Q_INVOKABLE bool create(QString file_path, QString password, QString brain_key = QString());

      /** required to generate new owner keys */
      Q_INVOKABLE bool loadBrainKey(QString brain_key);

      /** removes brain key to secure owner keys */
      Q_INVOKABLE bool purgeBrainKey();
      Q_INVOKABLE bool hasBrainKey()const;

      /** @pre hasBrainKey() */
      Q_INVOKABLE QString getBrainKey();

      bool isLocked()const;
      Q_INVOKABLE bool unlock(QString password);
      Q_INVOKABLE bool lock();
      Q_INVOKABLE bool changePassword(QString new_password);

      /**
       * @pre !isLocked();
       * @post save()
       * @return WIF private key
       */
      Q_INVOKABLE QString getActivePrivateKey(QString owner_public_key, uint32_t sequence);
      Q_INVOKABLE QString getActivePublicKey(QString owner_public_key, uint32_t sequence);

      /**
       * @pre !isLocked();
       * @pre hasBrainKey();
       * @post save()
       * @return WIF private key
       */
      Q_INVOKABLE QString getOwnerPrivateKey(uint32_t sequence);
      Q_INVOKABLE QString getOwnerPublicKey(uint32_t sequence);
      Q_INVOKABLE QString getPublicKey(QString wif_private_key)const;

      Q_INVOKABLE QString getKeyLabel(QString pubkey);
      Q_INVOKABLE bool    setKeyLabel(QString pubkey, QString label);
      Q_INVOKABLE QString getPublicKey(QString label);
      Q_INVOKABLE QString getPrivateKey(QString pubkey);
      Q_INVOKABLE bool    hasPrivateKey(QString pubkey, bool include_with_brain_key = false);

      /** imports a public key and assigns it a label */
      Q_INVOKABLE bool    importPublicKey(QString pubkey, QString label = QString());

      /**
       * @param wifkey a private key in (WIF) Wallet Import Format
       * @pre !isLocked()
       **/
      Q_INVOKABLE bool    importPrivateKey(QString wifkey, QString label = QString());

      /** removes the key, its lablel and its private key */
      Q_INVOKABLE bool    removePublicKey(QString pubkey);

      /** removes only the private key, keeping the public key and label */
      Q_INVOKABLE bool    removePrivateKey(QString pubkey);

      /**
       *  @param only_if_private  filter any public keys for which the wallet lacks a private key
       *  @return a list of PUBLICKEY, LABEL for all known public keys
       */
      Q_INVOKABLE QList<QPair<QString,QString>> getAllPublicKeys(bool only_if_private)const;

      /**
       * @pre !isLocked()
       */
      vector<signature_type>           signDigest(const digest_type& d,
                                                  const set<public_key_type>& keys)const;

      const flat_set<public_key_type>& getAvailablePrivateKeys()const;

   Q_SIGNALS:
      void isLockedChanged(bool state);
      void isOpenChanged(bool state);

   private:
      fc::path                  _wallet_file_path;
      wallet_file               _data;
      /** used to decrypt all of the encrypted private keys */
      fc::sha512                _decrypted_master_key;
      flat_set<public_key_type> _available_private_keys;
      map<QString,QString>      _label_to_key;
      QString                   _brain_key;
};
QML_DECLARE_TYPE(Wallet)

FC_REFLECT( key_data, (label)(encrypted_private_key)(brain_sequence)(owner) )
FC_REFLECT( wallet_file,
            (encrypted_brain_key)
            (brain_key_digest)
            (encrypted_master_key)
            (master_key_digest)
            (encrypted_private_keys)
          )
