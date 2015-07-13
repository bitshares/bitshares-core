import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2

Rectangle {
   Component.onCompleted: console.log("Made a transfer form")
   Component.onDestruction: console.log("Destroyed a transfer form")
}
