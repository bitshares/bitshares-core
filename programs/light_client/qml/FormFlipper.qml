import QtQuick 2.5

import Graphene.Client 0.1

Flipable {
   id: flipable
   anchors.fill: parent

   property Component frontComponent
   property Component backComponent
   property GrapheneApplication app
   signal canceled
   signal completed

   property bool flipped: false

   Component.onCompleted: {
      back = backComponent.createObject(flipable, {app: app, enabled: Qt.binding(function(){return flipped})})
      front = frontComponent.createObject(flipable, {app: app, enabled: Qt.binding(function(){return !flipped})})
      front.canceled.connect(function() { canceled.apply(this, arguments) })
      front.completed.connect(function() {
         if (back.hasOwnProperty("arguments"))
            back.arguments = arguments
         flipped = true
      })
      back.canceled.connect(function() {
         if (front.hasOwnProperty("arguments"))
            front.arguments = arguments
         flipped = false
      })
      back.completed.connect(function() { completed.apply(this, arguments) })
   }

   transform: Rotation {
       id: rotation
       origin.x: flipable.width/2
       origin.y: flipable.height/2
       axis.x: 0; axis.y: 1; axis.z: 0     // set axis.y to 1 to rotate around y-axis
       angle: 0    // the default angle
   }

   states: State {
       name: "back"
       PropertyChanges { target: rotation; angle: 180 }
       when: flipable.flipped
   }

   transitions: Transition {
       NumberAnimation { target: rotation; property: "angle"; duration: 500 }
   }
}
