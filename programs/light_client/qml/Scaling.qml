pragma Singleton
import QtQuick 2.5
import QtQuick.Window 2.2

Item {
   function mm(millimeters) {
      return Screen.pixelDensity * millimeters
   }
   function cm(centimeters) {
      return mm(centimeters * 10)
   }
}
