import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2

Rectangle {
   id: greySheet
   state: "HIDDEN"

   property real showOpacity: .5
   property int animationTime: 300

   /// Emitted immediately when opening, before fade-in animation
   signal opening
   /// Emitted when opened, following fade-in animation
   signal opened
   /// Emitted immediately when closing, before fade-out animation
   signal closing
   /// Emitted when closed, following fade-out animation
   signal closed

   function showForm(formType, closedCallback) {
      formLoader.sourceComponent = formType
      if (closedCallback instanceof Function)
         internal.callback = closedCallback
      state = "SHOWN"
   }

   MouseArea {
      id: mouseTrap
      anchors.fill: parent
      onClicked: {
         mouse.accepted = true
         greySheet.state = "HIDDEN"
      }
      acceptedButtons: Qt.AllButtons
   }
   MouseArea {
      anchors.fill: formLoader
      acceptedButtons: Qt.AllButtons
      onClicked: mouse.accepted = true
   }
   Loader {
      id: formLoader
      anchors.centerIn: parent
      width: parent.width / 2
      height: parent.height / 2
   }

   states: [
      State {
         name: "HIDDEN"
         PropertyChanges {
            target: greySheet
            color: Qt.rgba(0, 0, 0, 0)
            enabled: false
         }
         StateChangeScript {
            name: "preHidden"
            script: {
               greySheet.closing()
            }
         }
         StateChangeScript {
            name: "postHidden"
            script: {
               greySheet.closed()
               formLoader.sourceComponent = undefined
               if (internal.callback instanceof Function)
                  internal.callback()
            }
         }
      },
      State {
         name: "SHOWN"
         PropertyChanges {
            target: greySheet
            color: Qt.rgba(0, 0, 0, showOpacity)
            enabled: true
         }
         StateChangeScript {
            name: "preShown"
            script: {
               greySheet.opening()
            }
         }
         StateChangeScript {
            name: "postShown"
            script: {
               greySheet.opened()
            }
         }
      }
   ]
   transitions: [
      Transition {
         from: "HIDDEN"
         to: "SHOWN"
         ScriptAction { scriptName: "preShown" }
         ColorAnimation {
            target: greySheet
            duration: animationTime
         }
         ScriptAction { scriptName: "postShown" }
      },
      Transition {
         from: "SHOWN"
         to: "HIDDEN"
         ScriptAction { scriptName: "preHidden" }
         ColorAnimation {
            target: greySheet
            duration: animationTime
         }
         ScriptAction { scriptName: "postHidden" }
      }
   ]

   QtObject {
      id: internal
      property var callback
   }
}
