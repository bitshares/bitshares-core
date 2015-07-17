import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2

import Graphene.Client 0.1

import "."

Rectangle {
   anchors.fill: parent

   property alias senderAccount: senderPicker.account
   property alias receiverAccount: recipientPicker.account

   property GrapheneApplication app
   signal finished

   Component.onCompleted: console.log("Made a transfer form")
   Component.onDestruction: console.log("Destroyed a transfer form")

   ColumnLayout {
      anchors.centerIn: parent
      width: parent.width - Scaling.cm(2)
      spacing: Scaling.mm(5)

      AccountPicker {
         id: senderPicker
         Layout.fillWidth: true
         Layout.minimumWidth: Scaling.cm(5)
         Component.onCompleted: setFocus()
         placeholderText: qsTr("Sender")
         showBalance: balances? balances.reduce(function(foundIndex, balance, index) {
                                                   if (foundIndex >= 0) return foundIndex
                                                   return balance.type.symbol === assetField.currentText? index : -1
                                                }, -1) : -1
      }
      AccountPicker {
         id: recipientPicker
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
         }
         ComboBox {
            id: assetField
            Layout.minimumWidth: Scaling.cm(3)
            enabled: senderPicker.balances instanceof Array && senderPicker.balances.length > 0
            model: enabled? senderPicker.balances.filter(function(balance) { return balance.amount > 0 })
                                                 .map(function(balance) { return balance.type.symbol })
                          : ["Asset Type"]
         }
         Item { Layout.fillWidth: true }
         Button {
            text: qsTr("Cancel")
            onClicked: finished()
         }
         Button {
            text: qsTr("Transfer")
            enabled: senderPicker.account && recipientPicker.account && senderPicker.account !== recipientPicker.account && amountField.value
            onClicked: console.log(amountField.value)
         }
      }
   }
}
