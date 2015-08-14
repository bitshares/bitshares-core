import QtQuick 2.5
import QtQuick.Window 2.2
import QtWebEngine 1.0

import Graphene.FullNode 1.0

Window {
   id: window
   width: Screen.width / 2
   height: Screen.height / 2
   visible: true

   BlockChain {
      id: blockChain
      onStarted: webView.url = "qrc:/index.html"
   }
   Component.onCompleted: blockChain.start()

   Rectangle { anchors.fill: parent; color: "#1F1F1F" }
   Text {
      font.pointSize: 20
      anchors.centerIn: parent
      text: qsTr("Loading...")
      color: "white"
   }
   WebEngineView {
      id: webView
      visible: false
      anchors.fill: parent
      onLoadProgressChanged: if (loadProgress === 100) visible = true
      onJavaScriptConsoleMessage: console.log(JSON.stringify(arguments))
   }
}
