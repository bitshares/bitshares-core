import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2
import QtGraphicalEffects 1.0

import Graphene.Client 0.1

import "."

/**
 * This is the form for previewing and approving a transaction prior to broadcasting it
 *
 * The arguments property should be populated with an Array of operations. These operations will be used to create a
 * Transaction, display it, and get confirmation to sign and broadcast it. This form will populate the transaction with
 * the operations and expiration details, sign it, and pass the signed transaction through the completed signal.
 */
FormBase {
   id: base

   readonly property alias trx: __trx

   Component.onCompleted: console.log("Made a transaction confirmation form")
   Component.onDestruction: console.log("Destroyed a transaction confirmation form")

   onDisplay: {
      trx.clearOperations()
      for (var op in arg)
         trx.appendOperation(arg[op])
   }

   Transaction {
      id: __trx
   }

   Rectangle {
      id: background
      Layout.preferredHeight: childrenRect.height + Scaling.mm(4)
      Layout.preferredWidth: childrenRect.width + Scaling.mm(4)
      layer.enabled: true
      layer.effect: DropShadow {
         radius: 8.0
         samples: 16
         horizontalOffset: Scaling.mm(.5)
         verticalOffset: Scaling.mm(.5)
         source: background
         color: "#80000000"
         transparentBorder: true
      }

      // Debugging shim; disable before release
      Loader {
         id: delegateLoader
         x: Scaling.mm(2)
         y: Scaling.mm(2)
         Layout.preferredWidth: Scaling.cm(10)
         Component.onCompleted: setSource("TransactionDelegate.qml", {"trx": __trx, "app": base.app})

         MouseArea {
            anchors.fill: parent
            onClicked: {
               console.log("Reloading transaction")
               delegateLoader.source = ""
               delegateLoader.setSource("TransactionDelegate.qml", {"trx": __trx, "app": base.app})
            }
         }
      }
   }
   RowLayout {
      Label {
         text: qsTr("Transaction expires in")
      }
      ComboBox {
         id: expirationSelector
         model: [qsTr("five seconds"), qsTr("thirty seconds"), qsTr("a minute"), qsTr("an hour"), qsTr("a month"), qsTr("a year")]

         function getExpiration() {
            switch(expirationSelector.currentIndex) {
               case 0: return new Date(app.model.chainTime.getTime() + 1000*5)
               case 1: return new Date(app.model.chainTime.getTime() + 1000*30)
               case 2: return new Date(app.model.chainTime.getTime() + 1000*60)
               case 3: return new Date(app.model.chainTime.getTime() + 1000*60*60)
               case 4: return new Date(app.model.chainTime.getTime() + 1000*60*60*24*30)
               case 5: return new Date(app.model.chainTime.getTime() + 1000*60*60*24*365)
            }
         }
      }
   }
   UnlockingFinishButtons {
      app: base.app
      Layout.fillWidth: true
      rightButtonText: qsTr("Commit")
      onLeftButtonClicked: canceled({})
      onRightButtonClicked: {
         if (app.wallet.isLocked)
            app.wallet.unlock(passwordField.text)
         else {
            trx.setExpiration(expirationSelector.getExpiration())
            app.signTransaction(trx)
            app.model.broadcast(trx)
            completed(trx)
         }
      }
   }
}
