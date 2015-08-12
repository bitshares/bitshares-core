import QtQuick 2.5
import QtQuick.Window 2.2
import QtWebEngine 1.0

Window {
   id: window
   width: Screen.width / 2
   height: Screen.height / 2

   Component.onCompleted: blockChain.start()

   Rectangle { anchors.fill: parent; color: "#1F1F1F" }
   WebEngineView {
      anchors.fill: parent
      url: "http://localhost:8080"
      onLoadProgressChanged: if (loadProgress === 100) window.visible = true
   }
}
