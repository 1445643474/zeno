
#ifndef IMAGEAREA_H
#define IMAGEAREA_H

#include <QtWidgets>

class QUndoStack;
class UndoCommand;
class AbstractInstrument;

class ImageArea : public QWidget
{
    Q_OBJECT

public:
    explicit ImageArea(const QString &filePath, QWidget *parent);
    ~ImageArea();

    void setZoom(int zoom);
    bool save();
    bool saveAs();

    inline QString getFileName() { return (m_filePath.isEmpty() ? m_filePath :
                                           m_filePath.split('/').last()); }
    inline QString getFilePath(){ return m_filePath; }
    inline QImage* getImage() { return m_image; }
    inline void setImage(const QImage &image) { *m_image = image; }

    inline void setEdited(bool flag) { if (m_bIsEdited == flag) return; m_bIsEdited = flag; emit editChanged(m_bIsEdited); }

    inline bool getEdited() { return m_bIsEdited; }

    void restoreCursor();
    inline qreal getZoomFactor() { return m_dbZoomFactor; }
    inline QUndoStack* getUndoStack() { return m_pUndoStack; }
    inline void setIsPaint(bool isPaint) { m_bIsPaint = isPaint; }
    inline bool isPaint() { return m_bIsPaint; }


    void pushUndoCommand(UndoCommand *command);

    void clearImage();

signals:
    void sendNewImageSize(const QSize&);
    void sendCursorPos(const QPoint&);
    void sendColor(const QColor&);
    void editChanged(bool bEdited);

protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void paintEvent(QPaintEvent *event);
    
private:
    void initializeImage();
    bool open(const QString& filePath);
    void drawCursor();
    void makeFormatsFilters();
    void adjustPos(QPointF& pos);
private:
    QImage* m_image;
    QString m_filePath; /**< Path where located image. */
    QString m_openFilter; /**< Supported open formats filter. */
    QString m_saveFilter; /**< Supported save formats filter. */
    bool m_bIsEdited, m_bIsPaint, m_rightButtonPressed;
    QPixmap* m_pPixmap;
    qreal m_dbZoomFactor;
    QUndoStack* m_pUndoStack;
    QVector<AbstractInstrument*> m_instrumentsHandlers;
    AbstractInstrument* m_pInstrumentHandler;

};

#endif // IMAGEAREA_H
