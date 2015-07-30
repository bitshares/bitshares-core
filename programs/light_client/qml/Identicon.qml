import QtQuick 2.5

import "jdenticon/jdenticon-1.0.1.min.js" as Jdenticon

Canvas {
   id: identicon
   contextType: "2d"

   property var name
   property bool showOwnership: false
   property bool fullOwnership: false

   onNameChanged: requestPaint()

   onPaint: {
      if (name)
         Jdenticon.draw(identicon, name)
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

   Rectangle {
      anchors {
         bottom: parent.bottom
         left: parent.left
         right: parent.right
      }
      height: parent.height / 4
      color: fullOwnership? "green" : "blue"
      opacity: .6
      visible: showOwnership

      Text {
         anchors.centerIn: parent
         color: "white"
         font.pixelSize: parent.height * 2/3
         font.bold: true
         text: fullOwnership? qsTr("FULL") : qsTr("PARTIAL")
      }
      TooltipArea {
         text: fullOwnership? qsTr("You have full control of this account, and can use it to perform actions directly.")
                            : qsTr("You have partial control of this account, and can vote for it to take an action.")
      }
   }
}
