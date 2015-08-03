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
      id: trxBackground
      anchors.fill: trxColumn
      anchors.margins: Scaling.mm(-2)
      layer.enabled: true
      layer.effect: DropShadow {
         radius: 8.0
         samples: 16
         horizontalOffset: Scaling.mm(.5)
         verticalOffset: Scaling.mm(.5)
         source: trxBackground
         color: "#80000000"
         transparentBorder: true
      }
   }
   Column {
      id: trxColumn
      Layout.preferredWidth: Scaling.cm(10)
      spacing: Scaling.mm(2)

      Repeater {
         Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
         model: trx.operations
         Label {
            property Asset transferAsset: app.model.getAsset(modelData.amountType)
            property Asset feeAsset: app.model.getAsset(modelData.feeType)
            text: {
               return qsTr("Transfer %1 %2 from %3 to %4\nFee: %5 %6").arg(transferAsset.formatAmount(modelData.amount))
               .arg(transferAsset.symbol)
               .arg(app.model.getAccount(modelData.sender).name)
               .arg(app.model.getAccount(modelData.receiver).name)
               .arg(feeAsset.formatAmount(modelData.fee))
               .arg(feeAsset.symbol)
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
