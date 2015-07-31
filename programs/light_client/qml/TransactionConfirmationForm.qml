import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2

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

   property Transaction trx

   Component.onCompleted: console.log("Made a transaction confirmation form")
   Component.onDestruction: console.log("Destroyed a transaction confirmation form")

   onDisplay: {
      trx = app.createTransaction()
      for (var op in arg)
         trx.appendOperation(arg[op])
   }

   Component {
      id: transactionDelegate

      Rectangle {
         width: Scaling.cm(10)
         height: childrenRect.height + Scaling.cm(1)
         radius: Scaling.mm(3)
         color: "#EEEEEE"
         border.width: Scaling.mm(.25)
         border.color: "black"

         Column {
            y: Scaling.cm(.5)
            x: y
            width: parent.width - Scaling.cm(1)
            Repeater {
               model: trx.operations
               Label {
                  property Asset transferAsset: app.model.getAsset(modelData.amountType)
                  property Asset feeAsset: app.model.getAsset(modelData.feeType)
                  text: qsTr("Transfer %1 %2 from %3 to %4\nFee: %5 %6").arg(transferAsset.formatAmount(modelData.amount))
                  .arg(transferAsset.symbol)
                  .arg(app.model.getAccount(modelData.sender).name)
                  .arg(app.model.getAccount(modelData.receiver).name)
                  .arg(feeAsset.formatAmount(modelData.fee))
                  .arg(feeAsset.symbol)
               }
            }
         }
      }
   }
   Loader {
      sourceComponent: trx && Array.prototype.slice.call(trx.operations).length > 0? transactionDelegate : undefined
   }
   UnlockingFinishButtons {
      app: base.app
      Layout.fillWidth: true
      rightButtonText: qsTr("Commit")
      onLeftButtonClicked: {
         canceled({})
         trx = null
      }
      onRightButtonClicked: {
         if (app.wallet.isLocked)
            app.wallet.unlock(passwordField.text)
         else {
            app.signTransaction(trx)
            app.model.broadcast(trx)
            completed(trx)
         }
      }
   }
}
