import QtQuick 2.5
import QtQuick.Layouts 1.2

import Graphene.Client 0.1

import "."

ColumnLayout {
   id: base

   property TransferOperation op
   property GrapheneApplication app

   property Asset transferAsset: app.model.getAsset(op.amountType)
   property Asset feeAsset: app.model.getAsset(op.feeType)
   property Account sender: app.model.getAccount(op.sender)
   property Account receiver: app.model.getAccount(op.receiver)

   RowLayout {
      Layout.fillWidth: true
      Identicon {
         height: Scaling.cm(1)
         width: Scaling.cm(1)
         name: sender.name
      }
      Text {
         Layout.alignment: Qt.AlignVCenter
         text: "-><br/>%1 %2".arg(transferAsset.formatAmount(op.amount)).arg(transferAsset.symbol)
         horizontalAlignment: Text.Center
      }
      Identicon {
         height: Scaling.cm(1)
         width: Scaling.cm(1)
         name: receiver.name
      }
      ColumnLayout {
         Layout.fillWidth: true
         Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            text: qsTr("Transfer from %1 to %2").arg(sender.name).arg(receiver.name)
         }
         Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            font.pointSize: 9
            text: qsTr("Fee: %1 %2").arg(feeAsset.formatAmount(op.fee)).arg(feeAsset.symbol)
         }
      }
   }

   Text {
      text: op.memo? qsTr("Memo: %1").arg(op.memoIsEncrypted && !app.wallet.isLocked? op.decryptedMemo(app.wallet, app.model)
                                                                                    : op.memo)
                   : qsTr("No memo")
      color: op.memo? "black" : "grey"
      font.italic: !op.memo
   }
}
