#ifndef DATASINGLETON_H
#define DATASINGLETON_H

#include <QColor>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QMap>
#include <QKeySequence>

enum InstrumentsEnum
{
    NONE_INSTRUMENT = 0,
    ERASER,
    PEN,
    // Don't use it. (Used to know count of current instrument)
    INSTRUMENTS_COUNT
};

class DataSingleton
{
public:
    static DataSingleton* Instance();

     QColor getPrimaryColor();
     void setPrimaryColor(const QColor& color);
     QColor getSecondaryColor();
     void setSecondaryColor(const QColor& color);
     int getPenSize();
     void setPenSize(const int& size);
     InstrumentsEnum getInstrument();
     void setInstrument(const InstrumentsEnum& instrument);
     InstrumentsEnum getPreviousInstrument();
     void setPreviousInstrument(const InstrumentsEnum& instrument);
     QSize getBaseSize();
     int getHistoryDepth();
     void setZoomFactor(int factor);
     int getZoomFactor();
     QKeySequence getFileShortcutByKey(const QString& key);
     QKeySequence getEditShortcutByKey(const QString& key);
     QKeySequence getInstrumentShortcutByKey(const QString& key);
     QKeySequence getToolShortcutByKey(const QString& key);

private:
    DataSingleton();

    static DataSingleton* m_pInstance;
    QColor m_primaryColor;
    QColor m_secondaryColor;
    int m_penSize;
    int m_eraserSize;
    InstrumentsEnum m_currentInstrument, m_previousInstrument;
    QSize m_baseSize;
    int  m_historyDepth;
    int m_zoomFactor;
    QMap<QString, QKeySequence> m_fileShortcuts, m_editShortcuts, m_instrumentsShortcuts, m_toolsShortcuts;

};

#endif // DATASINGLETON_H
