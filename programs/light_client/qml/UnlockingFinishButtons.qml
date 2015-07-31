import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.2

import "."

import Graphene.Client 0.1

RowLayout {
   property string leftButtonText: qsTr("Cancel")
   property string unlockButtonText: qsTr("Unlock")
   property string rightButtonText: qsTr("Finish")
   property bool leftButtonEnabled: true
   property bool rightButtonEnabled: true
   property bool requiresUnlocking: true
   property GrapheneApplication app

   signal leftButtonClicked
   signal rightButtonClicked

   Item { Layout.fillWidth: true }
   Button {
      text: leftButtonText
      onClicked: leftButtonClicked()
   }
   TextField {
      id: passwordField
      property bool shown: requiresUnlocking && app.wallet.isLocked
      property real desiredWidth: shown? Scaling.cm(4) : 0
      Layout.preferredWidth: desiredWidth
      echoMode: TextInput.Password
      placeholderText: qsTr("Wallet password")
      visible: desiredWidth > 0
      onAccepted: rightButton.clicked()

      Behavior on desiredWidth { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
   }
   Button {
      id: rightButton
      text: passwordField.shown? unlockButtonText : rightButtonText
      enabled: rightButtonEnabled
      onClicked: {
         if (passwordField.visible)
            return app.wallet.unlock(passwordField.text)

         rightButtonClicked()
      }
      Behavior on implicitWidth { NumberAnimation {} }
   }
}
