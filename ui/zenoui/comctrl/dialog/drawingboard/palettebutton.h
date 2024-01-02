#ifndef QPALETTEBUTTON_H
#define QPALETTEBUTTON_H

#include <QToolButton>

class PaletteButton : public QToolButton
{
    Q_OBJECT

public:
    PaletteButton(const QColor &color, QWidget *parent = nullptr);
public slots:
    void updateState();
signals:
    void colorPicked();
private:
    QColor mColor;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent* evt) override;
};

#endif // QPALETTEBUTTON_H
