#include "zdrawingboarddlg.h"
#include <zenoui/style/zenostyle.h>
#include "drawingboard/imagearea.h"
#include "drawingboard/palettebutton.h"
#include "../ztoolbutton.h"
#include "drawingboard/zoomslider.h"

ZDrawingBoardDlg::ZDrawingBoardDlg(QWidget* parent, const QString& filePath) 
    : ZFramelessDialog(parent)
    , m_filePath(filePath)
{
    this->setTitleIcon(QIcon(":/icons/zeno-logo.png"));
    this->setTitleText(m_filePath.isEmpty() ? tr("Drawing Board") : m_filePath);
    this->layout()->setSpacing(0);
    initMainWiget();
}

ZDrawingBoardDlg::~ZDrawingBoardDlg()
{
}
void ZDrawingBoardDlg::initMainWiget()
{
    QWidget* mainWidget = new QWidget(this);
    QVBoxLayout* pLayout = new QVBoxLayout(mainWidget);
    //init toolbar
    initializeToolBar(pLayout);
    //init image area
    initializeImageArea(pLayout);
    //init statusbar
    initializeStatusBar(pLayout);
    setMainWidget(mainWidget);
    resize(1060, 1215);
}

void ZDrawingBoardDlg::initializeToolBar(QBoxLayout* pLayout)
{
    QToolBar *pToolbar = new QToolBar(this);
    QAction* pEraserAction = new QAction(tr("Eraser"), this);
    pEraserAction->setCheckable(true);
    pEraserAction->setIcon(QIcon(":/icons/eraser.svg"));
    connect(pEraserAction, SIGNAL(triggered(bool)), this, SLOT(instumentsAct(bool)));
    pToolbar->addAction(pEraserAction);
    m_instrumentsActMap.insert(ERASER, pEraserAction);

    QAction* pPenAction = new QAction(tr("Pen"), this);
    pPenAction->setCheckable(true);
    pPenAction->setIcon(QIcon(":/icons/pen.svg"));
    connect(pPenAction, SIGNAL(triggered(bool)), this, SLOT(instumentsAct(bool)));
    pToolbar->addAction(pPenAction);
    m_instrumentsActMap.insert(PEN, pPenAction);
    pPenAction->setChecked(true);

    pToolbar->addSeparator();

    PaletteButton* mRedColorButton = new PaletteButton(Qt::red, this);
    mRedColorButton->setChecked(true);
    pToolbar->addWidget(mRedColorButton);
    PaletteButton* mBlueColorButton = new PaletteButton(Qt::blue, this);
    mBlueColorButton->setChecked(false);
    pToolbar->addWidget(mBlueColorButton);
    connect(mRedColorButton, &PaletteButton::pressed, mBlueColorButton, &PaletteButton::updateState);
    connect(mBlueColorButton, &PaletteButton::pressed, mRedColorButton, &PaletteButton::updateState);

    m_pPenSizeSpin = new QSpinBox(this);
    m_pPenSizeSpin->setRange(1, 50);
    m_pPenSizeSpin->setValue(DataSingleton::Instance()->getPenSize());
    m_pPenSizeSpin->setStatusTip(tr("Pen size"));
    m_pPenSizeSpin->setToolTip(tr("Pen size"));
    m_pPenSizeSpin->setProperty("cssClass", "control");
    m_pPenSizeSpin->setAlignment(Qt::AlignCenter);
    m_pPenSizeSpin->setFixedWidth(ZenoStyle::dpiScaled(80));
    connect(m_pPenSizeSpin, SIGNAL(valueChanged(int)), this, SLOT(penValueChanged(int)));
    pToolbar->addWidget(m_pPenSizeSpin);

    pToolbar->addSeparator();

    QAction* pClearAction = pToolbar->addAction(QIcon(":/icons/clearup.svg"), tr("Clear up"));
    connect(pClearAction, SIGNAL(triggered(bool)), this, SLOT(clearImage()));

    m_pUndoStackGroup = new QUndoGroup(this);
    QAction* pUndoAction = m_pUndoStackGroup->createUndoAction(this, tr("&Undo"));
    pUndoAction->setIcon(QIcon(":/icons/undo.svg"));
    pUndoAction->setEnabled(false);
    pToolbar->addAction(pUndoAction);

    QAction* pRedoAction = m_pUndoStackGroup->createRedoAction(this, tr("&Redo"));
    pRedoAction->setIcon(QIcon(":/icons/redo.svg"));
    pRedoAction->setEnabled(false);
    pToolbar->addAction(pRedoAction);

    pToolbar->addSeparator();

    QAction* pSaveAction = new QAction(tr("&Save"), this);
    pSaveAction->setIcon(QIcon(":/icons/save.svg"));
    connect(pSaveAction, SIGNAL(triggered()), this, SLOT(saveAct()));
    pToolbar->addAction(pSaveAction);

    QAction* pSaveAsAction = new QAction(tr("Save as"), this);
    pSaveAsAction->setIcon(QIcon(":/icons/save_as.svg"));
    connect(pSaveAsAction, SIGNAL(triggered()), this, SLOT(saveAsAct()));
    pToolbar->addAction(pSaveAsAction);

    pToolbar->layout()->setSpacing(10);
    pLayout->addWidget(pToolbar);

    pPenAction->setShortcut(DataSingleton::Instance()->getInstrumentShortcutByKey("Pen"));
    pEraserAction->setShortcut(DataSingleton::Instance()->getInstrumentShortcutByKey("Lastic"));

    pUndoAction->setShortcut(DataSingleton::Instance()->getEditShortcutByKey("Undo"));
    pRedoAction->setShortcut(DataSingleton::Instance()->getEditShortcutByKey("Redo"));

    pSaveAction->setShortcut(DataSingleton::Instance()->getFileShortcutByKey("Save"));
    pSaveAsAction->setShortcut(DataSingleton::Instance()->getFileShortcutByKey("SaveAs"));
}
void ZDrawingBoardDlg::initializeImageArea(QBoxLayout* pLayout)
{
    QString fileName(tr("Untitled Image"));
    m_pImageArea = new ImageArea(m_filePath, this);
    if (!m_pImageArea->getFileName().isNull())
    {
        QScrollArea* scrollArea = new QScrollArea(this);
        scrollArea->setAttribute(Qt::WA_DeleteOnClose);
        scrollArea->setBackgroundRole(QPalette::Dark);
        scrollArea->setWidget(m_pImageArea);
        connect(m_pImageArea, &ImageArea::sendCursorPos, this, &ZDrawingBoardDlg::setNewPosToPosLabel);
        connect(m_pImageArea, &ImageArea::editChanged, this, &ZDrawingBoardDlg::onEditStateChanged);

        pLayout->addWidget(scrollArea);
        QUndoStack* undoStack = m_pImageArea->getUndoStack();
        m_pUndoStackGroup->addStack(undoStack);
        m_pUndoStackGroup->setActiveStack(undoStack);
    }
    else
    {
        delete m_pImageArea;
    }
}

void ZDrawingBoardDlg::initializeStatusBar(QBoxLayout* pLayout)
{
    QStatusBar *pStatusBar = new QStatusBar(this);
    pLayout->addWidget(pStatusBar);

    m_SizeLabel = new QLabel(pStatusBar);
    m_pPosLabel = new QLabel(pStatusBar);
    QSize size = m_pImageArea->getImage()->size();
    m_SizeLabel->setText(QString("%1 x %2").arg(size.width()).arg(size.height()));
    pStatusBar->addPermanentWidget(m_SizeLabel, -1);
    pStatusBar->addPermanentWidget(m_pPosLabel, 1);

    QWidget* pWidget = new QWidget(pStatusBar);
    QHBoxLayout* pHLayout = new QHBoxLayout(pWidget);
    pHLayout->setMargin(0);
    auto zoom = new ZoomSlider(pStatusBar);
    pHLayout->addStretch();
    pHLayout->addWidget(zoom);
    zoom->setFixedSize(ZenoStyle::dpiScaledSize(QSize(240, 24)));
    connect(zoom->slider(), &QSlider::valueChanged, m_pImageArea, &ImageArea::setZoom);
    pStatusBar->addPermanentWidget(pWidget);
}

void ZDrawingBoardDlg::instumentsAct(bool state)
{
    QAction* currentAction = static_cast<QAction*>(sender());
    if (state)
    {
        setAllInstrumentsUnchecked(currentAction);
        currentAction->setChecked(true);
        InstrumentsEnum inst = m_instrumentsActMap.key(currentAction);
        DataSingleton::Instance()->setInstrument(inst);
        m_pPenSizeSpin->setValue(DataSingleton::Instance()->getPenSize());
    }
    else
    {
        setAllInstrumentsUnchecked(NULL);
        DataSingleton::Instance()->setInstrument(NONE_INSTRUMENT);
    }
}

void ZDrawingBoardDlg::setAllInstrumentsUnchecked(QAction* action)
{
    clearImageSelection();
    foreach(QAction * temp, m_instrumentsActMap)
    {
        if (temp != action)
            temp->setChecked(false);
    }
}

void ZDrawingBoardDlg::clearImageSelection()
{
    if (m_pImageArea)
    {
        DataSingleton::Instance()->setPreviousInstrument(NONE_INSTRUMENT);
    }
}

void ZDrawingBoardDlg::saveAct()
{
    m_pImageArea->save();
}

void ZDrawingBoardDlg::saveAsAct()
{
    m_pImageArea->saveAs();
}

void ZDrawingBoardDlg::setNewPosToPosLabel(const QPoint& pos)
{
    m_pPosLabel->setText(QString("%1, %2").arg(pos.x()).arg(pos.y()));
}

void ZDrawingBoardDlg::penValueChanged(int val)
{
    DataSingleton::Instance()->setPenSize(val);
}

void ZDrawingBoardDlg::closeEvent(QCloseEvent* event)
{
    if (m_pImageArea->getFileName().isEmpty() && m_pImageArea->getEdited())
    {
        if (QMessageBox::question(this, tr("Save"), tr("Whether to save image?")) == QMessageBox::Yes)
            saveAct();
    }
    m_filePath = m_pImageArea->getFilePath();
    ZFramelessDialog::closeEvent(event);
}

void ZDrawingBoardDlg::clearImage()
{
    m_pImageArea->clearImage();
}

QString ZDrawingBoardDlg::getImage(QWidget* parent, const QString& filePath)
{
    ZDrawingBoardDlg dlg(parent, filePath);
    dlg.exec();
    return dlg.m_filePath;
}

void ZDrawingBoardDlg::onEditStateChanged(bool bEdited)
{
    QString text = this->titleText();
    if (bEdited)
    {
        text += " *";
    }
    else
    {
        text = m_pImageArea->getFilePath();
    }
    this->setTitleText(text);
}