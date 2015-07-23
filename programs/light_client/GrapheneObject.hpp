#pragma once
#pragma GCC diagnostic ignored "-Wunknown-pragmas"

#include <QObject>

#include <functional>

QString toQString( const std::string& s );

using ObjectId = qint64;
Q_DECLARE_METATYPE(ObjectId)

Q_DECLARE_METATYPE(std::function<void()>)

class GrapheneObject : public QObject
{
   Q_OBJECT
   Q_PROPERTY(ObjectId id MEMBER m_id READ id NOTIFY idChanged)

   ObjectId m_id;

public:
   GrapheneObject(ObjectId id = -1, QObject* parent = nullptr)
      : QObject(parent), m_id(id)
   {}

   ObjectId id() const {
      return m_id;
   }

Q_SIGNALS:
   void idChanged();
};
