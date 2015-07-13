import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2

import "."
import "jdenticon/jdenticon-1.0.1.min.js" as Jdenticon

Rectangle {
   anchors.fill: parent

   Component.onCompleted: console.log("Made a transfer form")
   Component.onDestruction: console.log("Destroyed a transfer form")

   Column {
      anchors.centerIn: parent

      RowLayout {
         Canvas {
            id: identicon
            width: Scaling.cm(2)
            height: Scaling.cm(2)
            contextType: "2d"

            onPaint: {
               if (nameField.text)
                  Jdenticon.draw(identicon, nameField.text)
               else {
                  var context = identicon.context
                  context.reset()
                  var draw_circle = function(context, x, y, radius) {
                     context.beginPath()
                     context.arc(x, y, radius, 0, 2 * Math.PI, false)
                     context.fillStyle = "rgba(0, 0, 0, 0.1)"
                     context.fill()
                  }
                  var size = Math.min(identicon.height, identicon.width)
                  var centerX = size / 2
                  var centerY = size / 2
                  var radius = size/15
                  draw_circle(context, centerX, centerY, radius)
                  draw_circle(context, 2*radius, 2*radius, radius)
                  draw_circle(context, centerX, 2*radius, radius)
                  draw_circle(context, size - 2*radius, 2*radius, radius)
                  draw_circle(context, size - 2*radius, centerY, radius)
                  draw_circle(context, size - 2*radius, size - 2*radius, radius)
                  draw_circle(context, centerX, size - 2*radius, radius)
                  draw_circle(context, 2*radius, size - 2*radius, radius)
                  draw_circle(context, 2*radius, centerY, radius)
               }
            }
         }
         TextField {
            id: nameField
            Layout.fillWidth: true
            onTextChanged: identicon.requestPaint()
         }
      }
   }
}
