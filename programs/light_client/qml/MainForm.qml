import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Layouts 1.2

Item {
   id: item1
   width: 640
   height: 480

   property alias button1: button1
   property alias button2: button2

   ColumnLayout {
      id: columnLayout1
      width: parent.width / 2
      anchors.verticalCenter: parent.verticalCenter
      anchors.horizontalCenter: parent.horizontalCenter

      RowLayout {
         id: buttonRow
         Layout.fillWidth: true

         Button {
            id: button1
            text: qsTr("Press Me 1")
         }
         Item { Layout.fillWidth: true }
         Button {
            id: button2
            text: qsTr("Press Me 2")
         }
      }

      Slider {
         id: slider
         Layout.fillWidth: true
      }
      ProgressBar {
         id: progressBar1
         value: Math.sin(Math.PI * slider.value)
         Layout.fillWidth: true
      }
   }
}

