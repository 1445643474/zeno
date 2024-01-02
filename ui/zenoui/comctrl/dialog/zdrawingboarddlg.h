#ifndef __ZDRAWINGBOARDDLG_H__
#define __ZDRAWINGBOARDDLG_H__

#include <QtWidgets>
#include "zframelessdialog.h"
#include "drawingboard/datasingleton.h"

class ImageArea;

class ZDrawingBoardDlg : public ZFramelessDialog
{
    Q_OBJECT
public:
    explicit ZDrawingBoardDlg(QWidget* parent = nullptr, const QString &filePath = "");
    ~ZDrawingBoardDlg();
    static QString getImage(QWidget* parent = nullptr, const QString& filePath = "");
signals:
    void pathChanged(const QString& path);
protected:
    void closeEvent(QCloseEvent *event) override;
private slots:
    void instumentsAct(bool state);
    void saveAct();
    void saveAsAct();
    void setNewPosToPosLabel(const QPoint& pos);
    void penValueChanged(int val);
    void clearImage();
    void onEditStateChanged(bool bEdited);

private:
    void initMainWiget();

    void initializeToolBar(QBoxLayout* pLayout);
    void initializeImageArea(QBoxLayout* pLayout);
    void initializeStatusBar(QBoxLayout* pLayout);

    void setAllInstrumentsUnchecked(QAction* action);
    void clearImageSelection();
private:
    QString m_filePath;
    QMap<InstrumentsEnum, QAction*> m_instrumentsActMap;
    QUndoGroup* m_pUndoStackGroup;
    ImageArea* m_pImageArea;
    QLabel* m_SizeLabel;
    QLabel* m_pPosLabel;
    QSpinBox* m_pPenSizeSpin;
};
#endif

