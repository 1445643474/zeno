#ifndef __Z_EXPANDABLE_SECTION_H__
#define __Z_EXPANDABLE_SECTION_H__

#include <QtWidgets>

class ZIconLabel;

class ZContentWidget : public QWidget
{
	Q_OBJECT
public:
    ZContentWidget(QWidget* parent = nullptr);
	virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;
};


class ZExpandableSection : public QWidget
{
	Q_OBJECT
public:
	explicit ZExpandableSection(const QString& title, QWidget* parent = nullptr);
	void setContentLayout(QLayout* layout);
	QLayout* contentLayout() const;
	virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

public slots:
	void toggle(bool collasped);

private:
	QGridLayout* m_mainLayout;
	ZIconLabel* m_collaspBtn;
	QScrollArea* m_contentArea;
	QWidget* m_contentWidget;
};

#endif