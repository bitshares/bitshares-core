import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2

import Graphene.Client 0.1

import "."

RowLayout {
   property Account account
   property var balances: account? Object.keys(account.balances).map(function(key){return account.balances[key]})
                                 : null

   property alias placeholderText: accountNameField.placeholderText
   property int showBalance: -1

   function setFocus() {
      accountNameField.forceActiveFocus()
   }

   Identicon {
      name: accountNameField.text
      width: Scaling.cm(2)
      height: Scaling.cm(2)
   }
   Column {
      Layout.fillWidth: true
      TextField {
         id: accountNameField

         width: parent.width
         onEditingFinished: accountDetails.update(text)
      }
      Text {
         id: accountDetails
         width: parent.width
         height: text? implicitHeight : 0

         Behavior on height { NumberAnimation{ easing.type: Easing.InOutQuad } }

         function update(name) {
            if (!name)
            {
               text = ""
               account = null
               return
            }

            account = app.model.getAccount(name)
            if (account == null) {
               text = qsTr("Error fetching account.")
            } else {
               text = Qt.binding(function() {
                  if (account == null)
                     return qsTr("Account does not exist.")
                  var text = qsTr("Account ID: %1").arg(account.id < 0? qsTr("Loading...")
                                                                      : account.id)
                  if (showBalance >= 0) {
                     text += "\n" + qsTr("Balance: %1 %2").arg(balances[showBalance].amountReal())
                                                          .arg(balances[showBalance].type.symbol)
                  }
                  return text
               })
            }
         }

         Behavior on text {
            SequentialAnimation {
               PropertyAnimation {
                  target: accountDetails
                  property: "opacity"
                  from: 1; to: 0
                  duration: 100
               }
               PropertyAction { target: accountDetails; property: "text" }
               PropertyAnimation {
                  target: accountDetails
                  property: "opacity"
                  from: 0; to: 1
                  duration: 100
               }
            }
         }
      }
   }
}
