import QtQuick 2.5
import QtQuick.Layouts 1.2

import Graphene.Client 0.1

import "."

ColumnLayout {
   id: base
   spacing: Scaling.mm(2)

   property Transaction trx
   property GrapheneApplication app

   Text {
      font.bold: true
      font.pointSize: Scaling.cm(.4)
      text: qsTr("Transaction Status: %1").arg(trx.statusString)
   }
   Repeater {
      model: trx.operations
      Loader {
         function load() {
            var source
            switch (modelData.operationType) {
            case OperationBase.TransferOperationType: source = "TransferOperationDelegate.qml"
            }
            setSource(source, {"op": modelData, "app": base.app, "width": base.width})
         }
         Component.onCompleted: load()
         // Debugging shim; disable before release
         MouseArea {
            anchors.fill: item
            onClicked: {
               console.log("Reloading " + parent.source)
               parent.source = ""
               parent.load()
            }
         }
      }
   }
}
