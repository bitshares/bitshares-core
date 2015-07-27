import QtQuick 2.5
import QtQuick.Controls 1.4
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2

import Graphene.Client 0.1

import "."

/**
 * Base for all forms
 *
 * This base contains all of the properties, slots, and signals which all forms are expected to expose. It also
 * automatically lays its children out in a ColumnLayout
 */
Rectangle {
   anchors.fill: parent

   /// Reference to the GrapheneApplication object
   property GrapheneApplication app
   /// Parent should trigger this signal to notify the form that it is about to be displayed
   /// See specific form for the argument semantics
   signal display(var arg)
   /// Emitted when the form is canceled -- see specific form for the argument semantics
   signal canceled(var arg)
   /// Emitted when the form is completed -- see specific form for the argument semantics
   signal completed(var arg)

   default property alias childItems: childLayout.data

   ColumnLayout {
      id: childLayout
      anchors.centerIn: parent
      width: parent.width - Scaling.cm(2)
      spacing: Scaling.mm(5)
   }
}
