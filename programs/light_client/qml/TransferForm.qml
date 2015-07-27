import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2

import Graphene.Client 0.1

import "."

/**
 * This is the form for transferring some amount of asset from one account to another.
 */
FormBase {
   id: base

   /// The Account object for the sender
   property alias senderAccount: senderPicker.account
   /// The Account object for the receiver
   property alias receiverAccount: recipientPicker.account

   function operation() {
      if (!transferButton.enabled) return app.operationBuilder.transfer(0,0,0,0, memoField.text, 0)

      return app.operationBuilder.transfer(senderPicker.account.id, recipientPicker.account.id,
                                           amountField.value * amountField.precisionAdjustment,
                                           amountField.maxBalance.type.id, memoField.text, 0)
   }

   Component.onCompleted: console.log("Made a transfer form")
   Component.onDestruction: console.log("Destroyed a transfer form")

   AccountPicker {
      id: senderPicker
      // The senderPicker is really the heart of the form. Everything else in the form adjusts based on the account
      // selected here. The assetField below updates to contain all assets this account has a nonzero balance in.
      // The amountField updates based on the asset selected in the assetField to have the appropriate precision and
      // to have a maximum value equal to the account's balance in that asset. The transfer button enables only when
      // both accounts are set, and a nonzero amount is selected to be transferred.

      app: base.app
      Layout.fillWidth: true
      Layout.minimumWidth: Scaling.cm(5)
      Component.onCompleted: setFocus()
      placeholderText: qsTr("Sender")
      showBalance: balances? balances.reduce(function(foundIndex, balance, index) {
         if (foundIndex >= 0) return foundIndex
         return balance.type.symbol === assetField.currentText? index : -1
      }, -1) : -1
      onBalanceClicked: amountField.value = balance
   }
   AccountPicker {
      id: recipientPicker
      app: base.app
      Layout.fillWidth: true
      Layout.minimumWidth: Scaling.cm(5)
      placeholderText: qsTr("Recipient")
      layoutDirection: Qt.RightToLeft
   }
   TextField {
      id: memoField
      Layout.fillWidth: true
      placeholderText: qsTr("Memo")
   }
   RowLayout {
      Layout.fillWidth: true
      SpinBox {
         id: amountField
         Layout.preferredWidth: Scaling.cm(4)
         Layout.minimumWidth: Scaling.cm(1.5)
         enabled: maxBalance
         minimumValue: 0
         maximumValue: maxBalance? maxBalance.amountReal() : 0
         decimals: maxBalance? maxBalance.type.precision : 0

         property Balance maxBalance: assetField.enabled && senderPicker.showBalance >= 0?
                                         senderPicker.balances[senderPicker.showBalance] : null
         property int precisionAdjustment: maxBalance? Math.pow(10, maxBalance.type.precision) : 1

         // Workaround to preserve value in case form gets disabled then re-enabled
         onEnabledChanged: if (!enabled) __valueBackup = value
         onMaximumValueChanged: if (enabled && maximumValue > __valueBackup) value = __valueBackup
         property real __valueBackup
      }
      ComboBox {
         id: assetField
         Layout.minimumWidth: Scaling.cm(1)
         enabled: senderPicker.balances instanceof Array && senderPicker.balances.length > 0
         model: enabled? senderPicker.balances.filter(function(balance) { return balance.amount > 0 })
                         .map(function(balance) { return balance.type.symbol })
                       : ["Asset Type"]
      }
      Text {
         font.pixelSize: assetField.height / 2.5
         text: {
            if (!senderPicker.account)
               return ""
            return qsTr("Fee:<br/>") + operation().fee / amountField.precisionAdjustment + " CORE"
         }
      }
      Item { Layout.fillWidth: true }
      Button {
         text: qsTr("Cancel")
         onClicked: canceled()
      }
      Button {
         id: transferButton
         text: qsTr("Transfer")
         enabled: senderPicker.account && recipientPicker.account && senderPicker.account !== recipientPicker.account && amountField.value
         onClicked: completed([operation()])
      }
   }
}
