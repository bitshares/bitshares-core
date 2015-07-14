import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2

import Graphene.Client 0.1

import "."

RowLayout {
   property Account account

   property alias placeholderText: accountNameField.placeholderText

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
      Label {
         id: accountDetails
         function update(name) {
            if (!name)
            {
               text = ""
               return
            }

            account = app.model.getAccount(name)
            if (account == null)
               text = qsTr("Error fetching account.")
            else
               text = Qt.binding(function() {
                  if (account == null)
                     return qsTr("Account does not exist.")
                  return qsTr("Account ID: %1").arg(account.id < 0? qsTr("Loading...")
                                                                  : account.id)
               })
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
