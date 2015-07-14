import QtQuick 2.5

import "jdenticon/jdenticon-1.0.1.min.js" as Jdenticon

Canvas {
   id: identicon
   contextType: "2d"

   property var name
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
}
