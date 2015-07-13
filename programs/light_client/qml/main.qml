import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2

import Qt.labs.settings 1.0

import Graphene.Client 0.1

ApplicationWindow {
   id: window
   visible: true
   width: 640
   height: 480
   title: qsTr("Hello World")

   Component.onCompleted: {
      app.start("ws://localhost:8090", "user", "pass")
   }

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

   GrapheneApplication {
      id: app
   }
   Settings {
      id: appSettings
      category: "appSettings"
   }

   Button {
      text: "Transfer"
      anchors.centerIn: parent
      onClicked: formBox.showForm(Qt.createComponent("TransferForm.qml"), function() {
         console.log("Closed form")
      })
   }

   FormBox {
      id: formBox
      anchors.fill: parent
      z: 10
   }

   // This Settings is only for geometry -- it doesn't get an id. See appSettings for normal settings
   Settings {
      category: "windowGeometry"
      property alias x: window.x
      property alias y: window.y
      property alias width: window.width
      property alias height: window.height
   }
}
