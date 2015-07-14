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
      spacing: Scaling.cm(1)

      AccountPicker {
         id: senderPicker
         width: parent.width
         Component.onCompleted: setFocus()
         placeholderText: qsTr("Sender")
      }
      AccountPicker {
         id: recipientPicker
         width: parent.width
         placeholderText: qsTr("Recipient")
         layoutDirection: Qt.RightToLeft
      }
      RowLayout {
         width: parent.width
         SpinBox {
            Layout.preferredWidth: Scaling.cm(4)
            Layout.minimumWidth: Scaling.cm(1.5)
            enabled: senderPicker.account
            minimumValue: 0
            maximumValue: Number.POSITIVE_INFINITY
         }
         ComboBox {
            Layout.minimumWidth: Scaling.cm(3)
            enabled: senderPicker.account
            model: ["CORE", "USD", "GOLD"]
         }
         Item { Layout.fillWidth: true }
         Button {
            text: qsTr("Cancel")
            onClicked: finished()
         }
         Button {
            text: qsTr("Transfer")
            enabled: senderPicker.account
         }
      }
   }
}
