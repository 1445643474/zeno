#include "datasingleton.h"

#include <QtCore/QSettings>

DataSingleton* DataSingleton::m_pInstance = 0;

DataSingleton::DataSingleton()
{
    m_primaryColor = Qt::red;
    m_secondaryColor = Qt::blue;
    m_penSize = 2;
    m_eraserSize = 10;
    m_currentInstrument = PEN;
    m_previousInstrument = NONE_INSTRUMENT;
    m_baseSize = QSize(1024, 1024);
    m_historyDepth = 40;
    m_zoomFactor = 100;
    m_fileShortcuts.insert("Save", QKeySequence(QKeySequence::Save));
    m_fileShortcuts.insert("SaveAs", QKeySequence("Ctrl+Shift+S"));
    m_editShortcuts.insert("Undo", QKeySequence(QKeySequence::Undo));
    m_editShortcuts.insert("Redo", QKeySequence(QKeySequence::Redo));
    m_instrumentsShortcuts.insert("Lastic", QKeySequence("Ctrl+1"));
    m_instrumentsShortcuts.insert("Pen", QKeySequence("Ctrl+2"));
}

DataSingleton* DataSingleton::Instance()
{
    if(!m_pInstance)
        m_pInstance = new DataSingleton;

    return m_pInstance;
}

 QColor DataSingleton::getPrimaryColor()
{
    return m_primaryColor; 
}

 void DataSingleton::setPrimaryColor(const QColor& color)
{
    m_primaryColor = color;
}

 QColor DataSingleton::getSecondaryColor()
{
    return m_secondaryColor;
}

 void DataSingleton::setSecondaryColor(const QColor& color)
{
    m_secondaryColor = color;
}

 int DataSingleton::getPenSize()
{
     if (m_currentInstrument == ERASER)
        return m_eraserSize;
     else
         return m_penSize;
}

 void DataSingleton::setPenSize(const int& size)
{
     if (m_currentInstrument == ERASER)
         m_eraserSize = size;
     else
        m_penSize = size;
}

 InstrumentsEnum DataSingleton::getInstrument()
{
    return m_currentInstrument;
}

 void DataSingleton::setInstrument(const InstrumentsEnum& instrument)
{
    m_currentInstrument = instrument;
}

 InstrumentsEnum DataSingleton::getPreviousInstrument()
{
    return m_previousInstrument;
}

 void DataSingleton::setPreviousInstrument(const InstrumentsEnum& instrument)
{
    m_previousInstrument = instrument;
}

 QSize DataSingleton::getBaseSize()
{
    return m_baseSize;
}

 int DataSingleton::getHistoryDepth()
{
    return m_historyDepth;
}

 void DataSingleton::setZoomFactor(int factor)
 {
     m_zoomFactor = factor;
 }

 int DataSingleton::getZoomFactor()
 {
     return m_zoomFactor;
 }

 QKeySequence DataSingleton::getFileShortcutByKey(const QString& key)
{
    if (m_fileShortcuts.contains(key))
        return m_fileShortcuts[key];
    return QKeySequence();
}

 QKeySequence DataSingleton::getEditShortcutByKey(const QString& key)
{
    if (m_editShortcuts.contains(key))
        return m_editShortcuts[key];
    return QKeySequence();
}

 QKeySequence DataSingleton::getInstrumentShortcutByKey(const QString& key)
{
    if (m_instrumentsShortcuts.contains(key))
        return m_instrumentsShortcuts[key];
    return QKeySequence();
}

 QKeySequence DataSingleton::getToolShortcutByKey(const QString& key)
{
    if (m_toolsShortcuts.contains(key))
        return m_toolsShortcuts[key];
    return QKeySequence();
}
