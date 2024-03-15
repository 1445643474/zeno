#include "calculationmgr.h"
#include <zeno/core/Session.h>
#include <zeno/core/Graph.h>
#include <zeno/extra/GraphException.h>
#include "viewport/displaywidget.h"
#include "zassert.h"
#include "util/uihelper.h"
#include "zenoapplication.h"
#include "zenomainwindow.h"


CalcWorker::CalcWorker(QObject* parent) {
    zeno::getSession().registerRunTrigger([=]() {
        run();
    });
}

void CalcWorker::run() {
    auto& sess = zeno::getSession();

    zeno::GraphException::catched([&] {
        sess.run();
    }, *sess.globalError);
    sess.globalState->set_working(false);
    if (sess.globalError->failed()) {
        QStringList namePath = UiHelper::stdlistToQStringList(sess.globalError->getNode());
        QString errMsg = QString::fromStdString(sess.globalError->getErrorMsg());
        emit calcFinished(false, namePath, errMsg);
    }
    else {
        emit calcFinished(true, {}, "");
    }
}


CalculationMgr::CalculationMgr(QObject* parent)
    : QObject(parent)
    , m_bMultiThread(true)
    , m_worker(nullptr)
{
    m_worker = new CalcWorker(this);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &CalcWorker::run);
    connect(m_worker, &CalcWorker::calcFinished, this, &CalculationMgr::onCalcFinished);
}

void CalculationMgr::onCalcFinished(bool bSucceed, QStringList namePath, QString msg)
{
    //ȷ����ʱ�����̲߳������߼���������ʱ�Ǵ�����Լ����Ҳ����CalcWorker::run()����ͷ��źš�
    m_thread.quit();
    m_thread.wait();
    emit calcFinished(bSucceed, namePath, msg);
}

void CalculationMgr::run()
{
    if (m_bMultiThread) {
        m_thread.start();
    }
}

void CalculationMgr::kill()
{
    zeno::getSession().globalState->set_working(false);
}

void CalculationMgr::registerRenderWid(DisplayWidget* pDisp)
{
    m_registerRenders.insert(pDisp);
    connect(this, &CalculationMgr::calcFinished, pDisp, &DisplayWidget::onCalcFinished);
    connect(pDisp, &DisplayWidget::render_objects_loaded, this, &CalculationMgr::on_render_objects_loaded);
}

void CalculationMgr::unRegisterRenderWid(DisplayWidget* pDisp) {
    m_loadedRender.remove(pDisp);
}

void CalculationMgr::on_render_objects_loaded()
{
    DisplayWidget* pWid = qobject_cast<DisplayWidget*>(sender());
    ZASSERT_EXIT(pWid);
    m_loadedRender.insert(pWid);
    if (m_loadedRender.size() == m_registerRenders.size())
    {
        //todo: notify calc to continue, if still have something to calculate.
    }
}
