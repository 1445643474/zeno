/*
 * This source file is part of EasyPaint.
 *
 * Copyright (c) 2012 EasyPaint <https://github.com/Gr1N/EasyPaint>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
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
