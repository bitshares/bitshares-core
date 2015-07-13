import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2

Rectangle {
   id: greySheet
   state: "HIDDEN"
   color: Qt.rgba(0, 0, 0, showOpacity)

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

   function showForm(formType, params, closedCallback) {
      if (formType.status === Component.Error)
         console.log(formType.errorString())

      formContainer.data = [formType.createObject(formContainer, params)]
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
      anchors.fill: formContainer
      acceptedButtons: Qt.AllButtons
      onClicked: mouse.accepted = true
   }
   Item {
      id: formContainer
      anchors.centerIn: parent
      width: parent.width / 2
      height: parent.height / 2
   }

   states: [
      State {
         name: "HIDDEN"
         PropertyChanges {
            target: greySheet
            opacity: 0
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
               console.log("Post")
               greySheet.closed()
               formContainer.data = []
               if (internal.callback instanceof Function)
                  internal.callback()
               internal.callback = undefined
            }
         }
      },
      State {
         name: "SHOWN"
         PropertyChanges {
            target: greySheet
            opacity: 1
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
         SequentialAnimation {
            ScriptAction { scriptName: "preShown" }
            PropertyAnimation {
               target: greySheet
               property: "opacity"
               duration: animationTime
            }
            ScriptAction { scriptName: "postShown" }
         }
      },
      Transition {
         from: "SHOWN"
         to: "HIDDEN"
         SequentialAnimation {
            ScriptAction { scriptName: "preHidden" }
            PropertyAnimation {
               target: greySheet
               property: "opacity"
               duration: animationTime
            }
            ScriptAction { scriptName: "postHidden" }
         }
      }
   ]

   QtObject {
      id: internal
      property var callback
   }
}
