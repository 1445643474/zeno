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
#include "imagearea.h"
#include "datasingleton.h"
#include "undocommand.h"
#include "instruments/abstractinstrument.h"
#include "instruments/pencilinstrument.h"
#include "instruments/eraserinstrument.h"
#include <zenomodel/include/graphsmanagment.h>

ImageArea::ImageArea(const QString &filePath, QWidget *parent) :
    QWidget(parent), m_bIsEdited(false), m_bIsPaint(false)
{
    setMouseTracking(true);

    m_rightButtonPressed = false;
    m_filePath = QString();
    makeFormatsFilters();
    initializeImage();
    m_dbZoomFactor = 1;
    m_pUndoStack = new QUndoStack(this);
    m_pUndoStack->setUndoLimit(DataSingleton::Instance()->getHistoryDepth());

    if (filePath.isEmpty() || !open(filePath))
    {
        resize(m_image->rect().right() + 6,
            m_image->rect().bottom() + 6);

        QPainter* painter = new QPainter(m_image);
        painter->fillRect(0, 0, m_image->width(), m_image->height(), Qt::black);
        painter->end();

        m_filePath = QString("");
    }

    // Instruments handlers
    m_instrumentsHandlers.fill(0, (int)INSTRUMENTS_COUNT);
    m_instrumentsHandlers[PEN] = new PencilInstrument(this);
    m_instrumentsHandlers[ERASER] = new EraserInstrument(this);
}

ImageArea::~ImageArea()
{

}

bool ImageArea::open(const QString& filePath)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QImage image;
    if (image.load(filePath))
    {
        *m_image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        m_filePath = filePath;
        resize(m_image->rect().right() + 6,
            m_image->rect().bottom() + 6);
        QApplication::restoreOverrideCursor();
        return true;
    }
    else
    {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, tr("Error opening file"), tr("Can't open file \"%1\".").arg(filePath));
        return false;
    }
}

void ImageArea::initializeImage()
{
    m_image = new QImage(DataSingleton::Instance()->getBaseSize(),
                        QImage::Format_ARGB32_Premultiplied);
}

void ImageArea::setZoom(int zoom)
{
    DataSingleton::Instance()->setZoomFactor(zoom);
    qreal factor = zoom / 100.0;
    resize(m_image->size() * factor);
}

bool ImageArea::save()
{
    if (m_filePath.isEmpty())
    {
        return saveAs();
    }
    if (!m_image->save(m_filePath))
    {
        QMessageBox::warning(this, tr("Error saving file"), tr("Can't save file \"%1\".").arg(m_filePath));
        return false;
    }
    setEdited(false);
    return true;
}

bool ImageArea::saveAs()
{
    bool result = true;
    QString filter;
    QString fileName(m_filePath);
    if (fileName.isEmpty())
    {
        QString dir = GraphsManagment::instance().zsgDir();
        if (dir.isEmpty())
            dir = QDir::homePath();
        fileName = dir + "/" + tr("Untitled_image") + ".png";
    }
    QString filePath = QFileDialog::getSaveFileName(this, tr("Save image..."), fileName, m_saveFilter,&filter);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    //parse file extension
    if (!filePath.isEmpty())
    {
        QString extension;
        //we should test it on windows, because of different slashes
        QString temp = filePath.split("/").last();
        //if user entered some extension
        if (temp.contains('.'))
        {
            temp = temp.split('.').last();
            if (QImageWriter::supportedImageFormats().contains(temp.toLatin1()))
                extension = temp;
            else
                extension = "png"; //if format is unknown, save it as png format, but with user extension
        }
        else
        {
            extension = filter.split('.').last().remove(')');
            filePath += '.' + extension;
        }

        if (m_image->save(filePath, extension.toLatin1().data()))
        {
            m_filePath = filePath;
            setEdited(false);
        }
        else
        {
            QMessageBox::warning(this, tr("Error saving file"), tr("Can't save file \"%1\".").arg(filePath));
            result = false;
        }
    }
    QApplication::restoreOverrideCursor();
    return result;
}


void ImageArea::mousePressEvent(QMouseEvent *event)
{
    if(DataSingleton::Instance()->getInstrument() != NONE_INSTRUMENT)
    {
        auto pos = event->localPos();
        adjustPos(pos);
        event->setLocalPos(pos);
        m_pInstrumentHandler = m_instrumentsHandlers.at(DataSingleton::Instance()->getInstrument());
        m_pInstrumentHandler->mousePressEvent(event, *this);
        m_rightButtonPressed = event->button() == Qt::RightButton;
    }
}

void ImageArea::mouseMoveEvent(QMouseEvent *event)
{
    InstrumentsEnum instrument = DataSingleton::Instance()->getInstrument();
    m_pInstrumentHandler = m_instrumentsHandlers.at(DataSingleton::Instance()->getInstrument());

    auto pos = event->localPos();
    adjustPos(pos);
    event->setLocalPos(pos);
    if(event->pos().x() < m_image->width() &&
            event->pos().y() < m_image->height())
    {
        emit sendCursorPos(event->pos());
    }

    restoreCursor();

    if(instrument != NONE_INSTRUMENT)
    {
        m_pInstrumentHandler->mouseMoveEvent(event, *this);
    }
}

void ImageArea::mouseReleaseEvent(QMouseEvent *event)
{
    if(DataSingleton::Instance()->getInstrument() != NONE_INSTRUMENT)
    {
        auto pos = event->localPos();
        adjustPos(pos);
        event->setLocalPos(pos);
        m_pInstrumentHandler = m_instrumentsHandlers.at(DataSingleton::Instance()->getInstrument());
        m_pInstrumentHandler->mouseReleaseEvent(event, *this);
    }
}

void ImageArea::paintEvent(QPaintEvent *event)
{
    QPainter *painter = new QPainter(this);
    //QRect *rect = new QRect(event->rect());

    //painter->setBrush(QBrush(QPixmap(":media/textures/transparent.jpg")));
    painter->drawRect(0, 0,
                      m_image->rect().right() - 1,
                      m_image->rect().bottom() - 1);

    qreal scaleX = size().width() / qreal(m_image->size().width());
    qreal scaleY = size().height() / qreal(m_image->size().height());
    qreal scale = qMin(scaleX, scaleY);
    auto eventRect = event->rect();
    auto imageRect = QRectF(eventRect.topLeft() / scale, eventRect.bottomRight() / scale);
    painter->drawImage(eventRect, *m_image, imageRect);
    //painter->drawImage(event->rect(), *m_image, event->rect());

    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(Qt::black));
    painter->drawRect(QRect(m_image->rect().right(),
                            m_image->rect().bottom(), 6, 6));

    painter->end();
}

void ImageArea::restoreCursor()
{
    ImageArea::drawCursor();
    QCursor cursor(*m_pPixmap);
    setCursor(cursor);
}

void ImageArea::drawCursor()
{
    QPainter painter;
    qreal factor = DataSingleton::Instance()->getZoomFactor() / 100.0;
    QSize size = QSize(50, 50) * factor;
    m_pPixmap = new QPixmap(size);
    QPoint center(m_pPixmap->rect().center());
    InstrumentsEnum inst = DataSingleton::Instance()->getInstrument();
    m_pPixmap->fill(QColor(0, 0, 0, 0));
    painter.begin(m_pPixmap);
    int radius = (DataSingleton::Instance()->getPenSize() / 2) * factor;
    switch(inst)
    {    
    case PEN:
        if(m_rightButtonPressed)
        {
            painter.setPen(QPen(DataSingleton::Instance()->getSecondaryColor()));
            painter.setBrush(QBrush(DataSingleton::Instance()->getSecondaryColor()));
        }
        else
        {
            painter.setPen(QPen(DataSingleton::Instance()->getPrimaryColor()));
            painter.setBrush(QBrush(DataSingleton::Instance()->getPrimaryColor()));
        }
        painter.drawEllipse(center, radius, radius);
        break;
    case ERASER:
        painter.setBrush(QBrush(Qt::darkGray));
        painter.drawEllipse(center, radius, radius);
        break;
    }
    int x = center.x();
    int y = center.y();
    painter.setPen(Qt::black);
    painter.drawPoint(center);
    painter.drawPoint(x, y - 5);
    painter.drawPoint(x, y - 10);
    painter.drawPoint(x, y + 5);
    painter.drawPoint(x, y + 10);
    painter.drawPoint(x - 5, y);
    painter.drawPoint(x - 10, y);
    painter.drawPoint(x + 5, y);
    painter.drawPoint(x + 10, y);
    painter.setPen(Qt::white);
    painter.drawPoint(x, y - 2);
    painter.drawPoint(x, y + 2);
    painter.drawPoint(x - 2, y);
    painter.drawPoint(x + 2, y);
    painter.drawPoint(x, y - 9);
    painter.drawPoint(x, y - 7);
    painter.drawPoint(x, y + 7);
    painter.drawPoint(x, y + 9);
    painter.drawPoint(x - 9, y);
    painter.drawPoint(x - 7, y);
    painter.drawPoint(x + 7, y);
    painter.drawPoint(x + 9, y);
    painter.end();
}

void ImageArea::makeFormatsFilters()
{
    QList<QByteArray> ba = QImageReader::supportedImageFormats();
    //make "all supported" part
    m_openFilter = "All supported (";
    foreach (QByteArray temp, ba)
        m_openFilter += "*." + temp + " ";
    m_openFilter[m_openFilter.length() - 1] = ')'; //delete last space
    m_openFilter += ";;";

    //using ";;" as separator instead of "\n", because Qt's docs recomended it :)
    if(ba.contains("png"))
        m_openFilter += "Portable Network Graphics(*.png);;";
    if(ba.contains("bmp"))
        m_openFilter += "Windows Bitmap(*.bmp);;";
    if(ba.contains("gif"))
        m_openFilter += "Graphic Interchange Format(*.gif);;";
    if(ba.contains("jpg") || ba.contains("jpeg"))
        m_openFilter += "Joint Photographic Experts Group(*.jpg *.jpeg);;";
    if(ba.contains("mng"))
        m_openFilter += "Multiple-image Network Graphics(*.mng);;";
    if(ba.contains("pbm"))
        m_openFilter += "Portable Bitmap(*.pbm);;";
    if(ba.contains("pgm"))
        m_openFilter += "Portable Graymap(*.pgm);;";
    if(ba.contains("ppm"))
        m_openFilter += "Portable Pixmap(*.ppm);;";
    if(ba.contains("tiff") || ba.contains("tif"))
        m_openFilter += "Tagged Image File Format(*.tiff, *tif);;";
    if(ba.contains("xbm"))
        m_openFilter += "X11 Bitmap(*.xbm);;";
    if(ba.contains("xpm"))
        m_openFilter += "X11 Pixmap(*.xpm);;";
    if(ba.contains("svg"))
        m_openFilter += "Scalable Vector Graphics(*.svg);;";

    m_openFilter += "All Files(*.*)";

    //make saveFilter
    ba = QImageWriter::supportedImageFormats();
    if(ba.contains("png"))
        m_saveFilter += "Portable Network Graphics(*.png)";
    if(ba.contains("bmp"))
        m_saveFilter += ";;Windows Bitmap(*.bmp)";
    if(ba.contains("jpg") || ba.contains("jpeg"))
        m_saveFilter += ";;Joint Photographic Experts Group(*.jpg)";
    if(ba.contains("ppm"))
        m_saveFilter += ";;Portable Pixmap(*.ppm)";
    if(ba.contains("tiff") || ba.contains("tif"))
        m_saveFilter += ";;Tagged Image File Format(*.tiff)";
    if(ba.contains("xbm"))
        m_saveFilter += ";;X11 Bitmap(*.xbm)";
    if(ba.contains("xpm"))
        m_saveFilter += ";;X11 Pixmap(*.xpm)";
}

void ImageArea::adjustPos(QPointF& pos)
{
    qreal factor = 100.0 / DataSingleton::Instance()->getZoomFactor();
    pos *= factor;
}

void ImageArea::pushUndoCommand(UndoCommand *command)
{
    if(command != 0)
        m_pUndoStack->push(command);
}

void ImageArea::clearImage()
{
    pushUndoCommand(new UndoCommand(getImage(), *this));
    QPainter* painter = new QPainter(m_image);
    painter->fillRect(0, 0, m_image->width(), m_image->height(), Qt::black);
    painter->end();
}
