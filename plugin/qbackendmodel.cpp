#include "qbackendmodel.h"
#include "qbackendobject.h"
#include "qbackendmodel_p.h"
#include <QQmlEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcModel, "backend.model")

/* Models are QBackendObjects with QAbstractListModel behavior client-side, using an internal object API.
 * A QBackendModel is a fully functional object and can have user-defined properties, methods, and signals.
 *
 * Objects with a '_qb_model' property of type 'object' will construct a QBackendModel. See below for
 * details on this object.
 */

QBackendModel::QBackendModel(QBackendConnection *connection, QByteArray identifier, QMetaObject *metaObject, QObject *parent)
    : QAbstractListModel(parent)
    , m_metaObject(metaObject)
{
    d = new BackendModelPrivate(this, connection, identifier);
}

QBackendModel::QBackendModel(QBackendConnection *connection, QMetaObject *type)
    : m_metaObject(type)
{
    d = new BackendModelPrivate(type->className(), this, connection);
}

QBackendModel::~QBackendModel()
{
    delete d;
}

const QMetaObject *QBackendModel::metaObject() const
{
    Q_ASSERT(m_metaObject);
    return m_metaObject;
}

int QBackendModel::qt_metacall(QMetaObject::Call c, int id, void **argv)
{
    id = QAbstractListModel::qt_metacall(c, id, argv);
    if (id < 0)
        return id;
    return d->metacall(c, id, argv);
}

void *QBackendModel::qt_metacast(const char *clname)
{
    if (!clname)
        return nullptr;
    if (!strcmp(clname, m_metaObject->className()))
        return static_cast<void*>(this);
    return QAbstractListModel::qt_metacast(clname);
}

void QBackendModel::classBegin()
{
    d->classBegin();
}

void QBackendModel::componentComplete()
{
    d->componentComplete();
}

/* The _qb_model object must implement:
 *
 * {
 *   "properties": {
 *     "roleNames": "array", // string list
 *     "batchSize": "int" // writable, max number of rows with data in a change/reset signal
 *   },
 *   "methods": {
 *     "reset": []
 *   },
 *   "signals": {
 *     "modelReset": [ "array rowData", "int moreRows" ],
 *     "modelInsert": [ "int start", "array rowData", "int moreRows" ],
 *     "modelRemove": [ "int start", "int end" ],
 *     "modelMove": [ "int start", "int end", "int destination" ],
 *     "modelUpdate": [ "int row", "var rowData" ],
 *     "modelRowData": [ "int start", "var rowData" ]
 *   }
 * }
 *
 */

void BackendModelPrivate::ensureModel()
{
    if (m_modelData)
        return;

    m_modelData = m_object->property("_qb_model").value<QObject*>();
    if (!m_modelData) {
        qCWarning(lcModel) << "Missing _qb_model object on model type" << m_object->metaObject()->className();
        return;
    }
    m_modelData->setParent(this);

    m_roleNames = m_modelData->property("roleNames").value<QStringList>();
    if (m_roleNames.isEmpty()) {
        qCWarning(lcModel) << "Model type" << m_object->metaObject()->className() << "has no role names";
        return;
    }

    connect(m_modelData, SIGNAL(modelReset(QJSValue,int)), this, SLOT(doReset(QJSValue,int)));
    connect(m_modelData, SIGNAL(modelInsert(int,QJSValue,int)), this, SLOT(doInsert(int,QJSValue,int)));
    connect(m_modelData, SIGNAL(modelRemove(int,int)), this, SLOT(doRemove(int,int)));
    connect(m_modelData, SIGNAL(modelMove(int,int,int)), this, SLOT(doMove(int,int,int)));
    connect(m_modelData, SIGNAL(modelUpdate(int,QJSValue)), this, SLOT(doUpdate(int,QJSValue)));
    connect(m_modelData, SIGNAL(modelRowData(int,QJSValue)), this, SLOT(doRowData(int,QJSValue)));

    if (m_batchSize > 0) {
        m_modelData->setProperty("batchSize", m_batchSize);
    }
    QMetaObject::invokeMethod(m_modelData, "reset");
}

QHash<int, QByteArray> QBackendModel::roleNames() const
{
    const_cast<QBackendModel*>(this)->d->ensureModel();
    QHash<int,QByteArray> roles;
    for (const QString &name : d->m_roleNames)
        roles[Qt::UserRole + roles.size()] = name.toUtf8();
    return roles;
}

int QBackendModel::rowCount(const QModelIndex&) const
{
    const_cast<QBackendModel*>(this)->d->ensureModel();
    return d->m_rowCount;
}

QVariant QBackendModel::data(const QModelIndex &index, int role) const
{
    const_cast<QBackendModel*>(this)->d->ensureModel();

    if (index.row() < 0 || index.row() >= d->m_rowCount || role < Qt::UserRole)
        return QVariant();

    const QJSValue &row = d->fetchRow(index.row());
    QJSValue data = row.property(role - Qt::UserRole);

    if (data.isQObject())
        return QVariant::fromValue(data.toQObject());
    else if ((data.isArray() || data.isObject()) && !data.isVariant())
        return QVariant::fromValue(data);
    else
        return data.toVariant();
}

QJSValue BackendModelPrivate::fetchRow(int row)
{
    QJSValue data = m_rowData.value(row);
    if (!data.isUndefined()) {
        // rowData can grow in various ways other than by fetch requests, so
        // check if it needs cleaning here too.
        cleanRowCache(row);
        return data;
    }

    // Find the nearest populated rows before and after this row
    int start = 0, end = m_rowCount-1;
    auto it = m_rowData.lowerBound(row);
    if (it != m_rowData.end() && it.key() == row) {
        // There should not be undefined values in the map, but..
        it = it++;
    }
    if (it != m_rowData.end()) {
        end = it.key()-1;
    }
    if (it != m_rowData.begin()) {
        it--;
        start = it.key()+1;
    }

    if (m_batchSize > 0) {
        const int half = m_batchSize/2;
        if (start > row-half) {
            end = qMin(end, start+m_batchSize);
        } else if (end < row+half) {
            start = qMax(start, end-m_batchSize);
        } else {
            start = qMax(start, row-half);
            end = qMin(end, row+half);
        }
    }

    qCDebug(lcModel) << "blocking to fetch rows" << start << "to" << end << "to get data for row" << row;

    QMetaObject::invokeMethod(m_modelData, "requestRows", Q_ARG(int, start), Q_ARG(int, end-start+1));
    m_connection->waitForMessage("model_emit",
        [&](const QJsonObject &msg) {
            return msg.value("command").toString() == "EMIT" &&
                   msg.value("method").toString() == "modelRowData" &&
                   msg.value("identifier").toString() == m_modelData->property("_qb_identifier").toString();
        }
    );

    // This should have been filled in by the doRowData slot
    data = m_rowData.value(row);
    if (data.isUndefined()) {
        qCWarning(lcModel) << "row has no data after synchronous fetch";
    }
    return data;
}

void BackendModelPrivate::cleanRowCache(int rowHint)
{
    if (m_cacheSize < 2)
        return;

    // Remove the rows furthest from rowHint until under m_cacheSize
    int removed = 0;
    while (m_rowData.size() > m_cacheSize) {
        auto first = m_rowData.begin();
        auto last = m_rowData.end()-1;

        if (std::abs(rowHint-first.key()) >= std::abs(last.key()-rowHint)) {
            m_rowData.erase(first);
        } else {
            m_rowData.erase(last);
        }
        removed++;
    }

    if (removed > 0) {
        qCDebug(lcModel) << "cleaned" << removed << "rows from cache based on hint" << rowHint;
    }
}

void BackendModelPrivate::doReset(const QJSValue &data, int moreRows)
{
    model()->beginResetModel();
    m_rowData.clear();

    int size = data.property("length").toNumber();
    for (int i = 0; i < size; i++) {
        QJSValue rowData = data.property(i);
        if (!rowData.isArray()) {
            qCWarning(lcModel) << "Model row" << i << "data is not an array";
            continue;
        }
        m_rowData.insert(i, rowData);
    }
    m_rowCount = size + moreRows;

    model()->endResetModel();
}

void BackendModelPrivate::doInsert(int start, const QJSValue &data, int moreRows)
{
    int dataSize = data.property("length").toNumber();
    int size = dataSize + moreRows;
    if (size < 1)
        return;

    model()->beginInsertRows(QModelIndex(), start, start + size - 1);

    // Increment keys >= start by size
    QMap<int,QJSValue> tmp;
    for (auto it = m_rowData.lowerBound(start); it != m_rowData.end(); ) {
        tmp.insert(it.key()+size, it.value());
        it = m_rowData.erase(it);
    }
    m_rowData.unite(tmp);

    // Insert new row data
    for (int i = 0; i < dataSize; i++) {
        m_rowData.insert(start+i, data.property(i));
    }

    m_rowCount += size;
    model()->endInsertRows();
}

void BackendModelPrivate::doRemove(int start, int end)
{
    model()->beginRemoveRows(QModelIndex(), start, end);

    // Remove keys between start and end, and decrement all keys after
    int size = end-start+1;
    for (auto it = m_rowData.lowerBound(start); it != m_rowData.end(); ) {
        if (it.key() > end) {
            // Copy to the new row number. This loop runs from start to the end of
            // the map, keys are only decremented evenly, and no key is decremented below
            // start. Under those constraints, the new key will always be just before
            // 'it'. That's useful as an insert hint, and to keep the loop position.
            it = m_rowData.insert(it, it.key()-size, it.value());
            // Increment back to the old index for the row
            it++;
        }
        it = m_rowData.erase(it);
    }
    m_rowCount -= size;
    model()->endRemoveRows();
}

void BackendModelPrivate::doMove(int start, int end, int destination)
{
    model()->beginMoveRows(QModelIndex(), start, end, QModelIndex(), destination);

    int size = end-start+1;
    QMap<int,QJSValue> moveData;
    for (auto it = m_rowData.begin(); it != m_rowData.end(); ) {
        auto i = it.key();
        if (i >= start && i <= end) {
            // Copy moved rows to moveData and remove from rowData
            if (destination < start) {
                // When moving up, row 'start' becomes row 'destination'
                moveData.insert(i-start+destination, it.value());
            } else {
                // When moving down, row 'end' becomes row 'destination-1'
                moveData.insert(destination-end-1+i, it.value());
            }
            it = m_rowData.erase(it);
        } else if (i < start && i < destination) {
            // Rows before start or destination do not change
            it++;
        } else if (destination < start) {
            if (i > end) {
                // Rows moved up, but the source was above too. Nothing to do.
                break;
            } else {
                // Rows moved up, and i is between destination and start. Shift down.
                // This shift is not safe to do in-place, because it could overwrite
                // an index that hasn't been updated yet. Use moveData for these.
                moveData.insert(i+size, it.value());
                it = m_rowData.erase(it);
            }
        } else {
            if (i >= destination) {
                // Rows moved down, with the last new row being destination-1 and the
                // source rows above that. Remaining rows are unchanged.
                break;
            } else {
                // Rows moved down, from before i to after i. Shift up.
                // Unlike above, this is safe to do in-place. Like the loop in doRemove,
                // it's safe to assume that the new row is immediately before this row.
                it = m_rowData.insert(it, i-size, it.value());
                it++;
                it = m_rowData.erase(it);
            }
        }
    }
    m_rowData.unite(moveData);

    model()->endMoveRows();
}

void BackendModelPrivate::doUpdate(int row, const QJSValue &data)
{
    if (row < 0 || row >= m_rowCount) {
        qCWarning(lcModel) << "invalid row" << row << "in model update";
        return;
    }

    m_rowData[row] = data;
    emit model()->dataChanged(model()->index(row), model()->index(row));
}

void BackendModelPrivate::doRowData(int start, const QJSValue &data)
{
    int size = data.property("length").toNumber();
    if (start < 0 || size < 1 || start+size > m_rowCount) {
        qCWarning(lcModel) << "invalid rowData for" << size << "rows starting from" << start;
        return;
    }

    for (int i = 0; i < size; i++) {
        QJSValue rowData = data.property(i);
        if (!rowData.isArray()) {
            qCWarning(lcModel) << "Model row" << start+i << "data is not an array in rowData";
            continue;
        }
        m_rowData.insert(start+i, rowData);
    }

    qCDebug(lcModel) << "populated rows" << start << "to" << start+size-1;
    cleanRowCache(start+(size/2));
}
