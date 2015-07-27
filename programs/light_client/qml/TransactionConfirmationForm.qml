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
      console.log(JSON.stringify(arg))
      for (var op in arg)
         trx.appendOperation(arg[op])
      console.log(JSON.stringify(trx))
   }
}
