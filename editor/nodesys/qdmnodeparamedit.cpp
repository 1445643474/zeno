#include "qdmnodeparamedit.h"
#include "qdmgraphicsscene.h"
#include "qdmgraphicsnodesubnet.h"
#include <zeno/ztd/variant.h>
#include <zeno/ztd/algorithm.h>
#include <zeno/dop/Descriptor.h>
#include <QLineEdit>
#include <QIntValidator>
#include <QDoubleValidator>

ZENO_NAMESPACE_BEGIN

QDMNodeParamEdit::QDMNodeParamEdit(QWidget *parent)
    : QWidget(parent)
    , layout(new QFormLayout(this))
{
}

QWidget *QDMNodeParamEdit::makeEditForType(
    QDMGraphicsNode *node, int sockid, std::string const &type)
{
    auto *sockIn = node->socketInAt(sockid);

    static constexpr std::array edit_type_table = {
            "string",
            "int",
            "float",
    };

    switch (ztd::try_find_index(edit_type_table, type)) {

    case 0: {
        auto edit = new QLineEdit;

        if (auto expr = sockIn->value.value_cast<std::string>()) {
            auto const &value = *expr;
            edit->setText(QString::fromStdString(value));
        }

        connect(edit, &QLineEdit::editingFinished, this, [=, this] {
            auto expr = edit->text().toStdString();
            auto const &value = expr;
            sockIn->value = ztd::make_any(value);
            invalidateNode(node);
        });
        return edit;
    } break;

    case 1: {
        auto edit = new QLineEdit;
        edit->setValidator(new QIntValidator);

        if (auto expr = sockIn->value.value_cast<int>()) {
            auto value = std::to_string(*expr);
            edit->setText(QString::fromStdString(value));
        }

        connect(edit, &QLineEdit::editingFinished, this, [=, this] {
            auto expr = edit->text().toStdString();
            auto value = std::stoi(expr);
            sockIn->value = ztd::make_any(value);
            invalidateNode(node);
        });
        return edit;
    } break;

    case 2: {
        auto edit = new QLineEdit;
        edit->setValidator(new QDoubleValidator);

        if (auto expr = sockIn->value.value_cast<float>()) {
            auto value = std::to_string(*expr);
            edit->setText(QString::fromStdString(value));
        }

        connect(edit, &QLineEdit::editingFinished, this, [=, this] {
            auto expr = edit->text().toStdString();
            auto value = std::stof(expr);
            sockIn->value = ztd::make_any(value);
            invalidateNode(node);
        });
        return edit;
    } break;

    default: {
        return nullptr;
    } break;

    }
}

void QDMNodeParamEdit::setCurrentNode(QDMGraphicsNode *node)
{
    while (layout->rowCount())
        layout->removeRow(0);

    currNode = node;
    if (!node)
        return;

    node->setupParamEdit(this);
}

void QDMNodeParamEdit::invalidateNode(QDMGraphicsNode *node) const
{
    auto scene = static_cast<QDMGraphicsScene *>(node->scene());
    emit scene->sceneUpdated();
}

void QDMNodeParamEdit::addRow(const QString &name, QWidget *row)
{
    layout->addRow(name, row);
}

QDMNodeParamEdit::~QDMNodeParamEdit() = default;

ZENO_NAMESPACE_END
