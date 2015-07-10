import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2

import Graphene.Client 0.1

ApplicationWindow {
   visible: true
   width: 640
   height: 480
   title: qsTr("Hello World")

   menuBar: MenuBar {
      Menu {
         title: qsTr("File")
         MenuItem {
            text: qsTr("&Open")
            onTriggered: console.log("Open action triggered");
         }
         MenuItem {
            text: qsTr("Exit")
            onTriggered: Qt.quit();
         }
      }
   }

   DataModel {
      id: model
      objectName: "model"
   }

   MainForm {
      id: form
      anchors.fill: parent
      button1.onClicked: {
         console.log(JSON.stringify(model.getAccount(3)))
         messageDialog.show(qsTr("Account name is %1").arg(model.getAccount(3).name))
      }
      button2.onClicked: messageDialog.show(qsTr("Account name is %1").arg(model.getAccount("steve").name))
   }

   MessageDialog {
      id: messageDialog
      title: qsTr("May I have your attention, please?")

      function show(caption) {
         messageDialog.text = caption;
         messageDialog.open();
      }
   }
}
