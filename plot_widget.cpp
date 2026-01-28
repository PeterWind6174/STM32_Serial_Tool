#include "plot_widget.h"

#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QColorDialog>
#include <QRegularExpression>
#include <QtMath>

// Qt Charts (Qt6: global classes, module Qt::Charts)
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>

static QString normKey(const QString &k) { return k.trimmed(); }

PlotWidget::PlotWidget(QWidget *tabRoot, QWidget *parent)
    : QWidget(parent) {

    bindUi(tabRoot);
    initChartIfNeeded();

    // defaults for controls (if user didn't prefill)
    if (m_renderModeCombo && m_renderModeCombo->count() == 0) {
        m_renderModeCombo->addItems({"Points","Lines","Fit"});
        m_renderModeCombo->setCurrentText("Lines");
    }
    if (m_fitTypeCombo && m_fitTypeCombo->count() == 0) {
        m_fitTypeCombo->addItems({"None","Sine","Triangle","Square"});
        m_fitTypeCombo->setCurrentText("None");
    }
    if (m_fitWindowSpin) {
        m_fitWindowSpin->setRange(20, 200000);
        if (m_fitWindowSpin->value() == 0) m_fitWindowSpin->setValue(200);
    }
    if (m_maxPointsSpin) {
        m_maxPointsSpin->setRange(100, 2000000);
        if (m_maxPointsSpin->value() == 0) m_maxPointsSpin->setValue(2000);
    }
    if (m_metaDisplay) m_metaDisplay->setReadOnly(true);
    if (m_scrollBarX) {
        m_scrollBarX->setOrientation(Qt::Horizontal);
        m_scrollBarX->setRange(0, 0);
        m_scrollBarX->setValue(0);
    }

    // wire UI
    if (m_addCurveBtn) connect(m_addCurveBtn, &QPushButton::clicked, this, &PlotWidget::onAddCurve);
    if (m_removeCurveBtn) connect(m_removeCurveBtn, &QPushButton::clicked, this, &PlotWidget::onRemoveCurve);

    if (m_activeCurveCombo) connect(m_activeCurveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &PlotWidget::onActiveCurveChanged);

    if (m_curveList) connect(m_curveList, &QListWidget::itemSelectionChanged,
                this, &PlotWidget::onCurveListSelectionChanged);

    if (m_pickColorBtn) connect(m_pickColorBtn, &QPushButton::clicked, this, &PlotWidget::onPickColor);

    if (m_renderModeCombo) connect(m_renderModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &PlotWidget::onRenderModeChanged);

    if (m_fitTypeCombo) connect(m_fitTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &PlotWidget::onFitTypeChanged);

    if (m_showRawPointsCheck) connect(m_showRawPointsCheck, &QCheckBox::toggled,
                this, &PlotWidget::onShowRawPointsToggled);

    if (m_fitWindowSpin) connect(m_fitWindowSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &PlotWidget::onFitWindowChanged);

    if (m_maxPointsSpin) connect(m_maxPointsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &PlotWidget::onMaxPointsChanged);

    if (m_clearBtn) connect(m_clearBtn, &QPushButton::clicked, this, &PlotWidget::onClearAll);

    if (m_metaAddBtn) connect(m_metaAddBtn, &QPushButton::clicked, this, &PlotWidget::onMetaAdd);
    if (m_metaRemoveBtn) connect(m_metaRemoveBtn, &QPushButton::clicked, this, &PlotWidget::onMetaRemove);

    if (m_scrollBarX) connect(m_scrollBarX, &QScrollBar::valueChanged,
                this, &PlotWidget::onScrollBarXChanged);

    // render timer (UI throttling)
    m_renderTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_renderTimer, &QTimer::timeout, this, &PlotWidget::onRenderTick);
    m_renderTimer.start(33); // ~30 FPS

    // create a default curve (CH:0) for convenience
    ensureCurveForChannel(0);
    m_activeCurveIndex = 0;
    rebuildCurveListUi();
    if (m_activeCurveCombo) m_activeCurveCombo->setCurrentIndex(0);
}

PlotWidget::~PlotWidget() = default;

void PlotWidget::bindUi(QWidget *root) {
    if (!root) return;

    m_chartView = root->findChild<QChartView*>("chartViewPlot");
    m_scrollBarX = root->findChild<QScrollBar*>("scrollBarPlotX");
    m_labelRange = root->findChild<QLabel*>("labelPlotRange");

    m_curveList = root->findChild<QListWidget*>("listWidgetPlotCurves");
    m_activeCurveCombo = root->findChild<QComboBox*>("comboBoxPlotActiveCurve");
    m_addCurveBtn = root->findChild<QPushButton*>("pushButtonPlotAddCurve");
    m_removeCurveBtn = root->findChild<QPushButton*>("pushButtonPlotRemoveCurve");

    m_pickColorBtn = root->findChild<QPushButton*>("pushButtonPlotPickColor");
    m_colorPreview = root->findChild<QLabel*>("labelPlotColorPreview");

    m_renderModeCombo = root->findChild<QComboBox*>("comboBoxPlotRenderMode");
    m_fitTypeCombo = root->findChild<QComboBox*>("comboBoxPlotFitType");
    m_showRawPointsCheck = root->findChild<QCheckBox*>("checkBoxPlotShowRawPoints");
    m_fitWindowSpin = root->findChild<QSpinBox*>("spinBoxPlotFitWindow");
    m_maxPointsSpin = root->findChild<QSpinBox*>("spinBoxPlotMaxPoints");
    m_clearBtn = root->findChild<QPushButton*>("pushButtonPlotClear");

    m_metaKeyEdit = root->findChild<QLineEdit*>("lineEditPlotMetaKey");
    m_metaAddBtn = root->findChild<QPushButton*>("pushButtonPlotMetaAdd");
    m_metaKeysList = root->findChild<QListWidget*>("listWidgetPlotMetaKeys");
    m_metaRemoveBtn = root->findChild<QPushButton*>("pushButtonPlotMetaRemove");
    m_metaDisplay = root->findChild<QPlainTextEdit*>("plainTextEditPlotMetaDisplay");
}

bool PlotWidget::isUiComplete() const {
    return m_chartView && m_scrollBarX &&
           m_curveList && m_activeCurveCombo &&
           m_addCurveBtn && m_removeCurveBtn &&
           m_pickColorBtn && m_colorPreview &&
           m_renderModeCombo && m_fitTypeCombo &&
           m_showRawPointsCheck && m_fitWindowSpin && m_maxPointsSpin &&
           m_clearBtn && m_metaAddBtn && m_metaKeysList && m_metaRemoveBtn && m_metaDisplay;
}

void PlotWidget::initChartIfNeeded() {
    if (!m_chartView) return;

    // ✅ 如果 Designer 已经给了 chart()，就复用它；否则自己 new
    m_chart = m_chartView->chart();
    if (!m_chart) {
        m_chart = new QChart();
        m_chartView->setChart(m_chart);
    }

    m_chartView->setRenderHint(QPainter::Antialiasing, true);
    m_chart->legend()->setVisible(true);
    if (m_chart->title().isEmpty()) {
        m_chart->setTitle("Waveform Plot");
    }

    // ✅ 确保有 ValueAxis；如果已有别的轴，移除后换成 ValueAxis
    auto ensureValueAxis = [&](Qt::Alignment align, Qt::Orientation ori) -> QValueAxis* {
        // 查找已有 QValueAxis
        const auto axes = m_chart->axes(ori);
        for (auto *ax : axes) {
            if (auto *v = qobject_cast<QValueAxis*>(ax)) return v;
        }
        // 没有 ValueAxis：移除现有同方向轴
        for (auto *ax : axes) {
            m_chart->removeAxis(ax);
            ax->deleteLater();
        }
        // 新建 ValueAxis
        auto *v = new QValueAxis();
        m_chart->addAxis(v, align);
        return v;
    };

    m_axisX = ensureValueAxis(Qt::AlignBottom, Qt::Horizontal);
    m_axisY = ensureValueAxis(Qt::AlignLeft,   Qt::Vertical);

    m_axisX->setTitleText("X");
    m_axisY->setTitleText("Y");
    m_axisX->setLabelFormat("%.6g");
    m_axisY->setLabelFormat("%.6g");
}

QColor PlotWidget::defaultColorForIndex(int idx) const {
    const int h = (idx * 47) % 360;
    return QColor::fromHsv(h, 200, 220);
}

PlotWidget::Curve* PlotWidget::ensureCurveForChannel(int ch) {
    for (auto &c : m_curves) {
        if (c.channelId == ch) return &c;
    }

    Curve c;
    c.channelId = ch;
    c.name = QString("CH:%1").arg(ch);
    c.color = defaultColorForIndex(m_curves.size());

    // initial from UI defaults
    if (m_renderModeCombo) {
        const QString t = m_renderModeCombo->currentText().trimmed();
        if (t.compare("Points", Qt::CaseInsensitive) == 0) c.renderMode = RenderMode::Points;
        else if (t.compare("Fit", Qt::CaseInsensitive) == 0) c.renderMode = RenderMode::Fit;
        else c.renderMode = RenderMode::Lines;
    }
    if (m_fitTypeCombo) {
        const QString t = m_fitTypeCombo->currentText().trimmed();
        if (t.compare("Sine", Qt::CaseInsensitive) == 0) c.fitType = FitType::Sine;
        else if (t.compare("Triangle", Qt::CaseInsensitive) == 0) c.fitType = FitType::Triangle;
        else if (t.compare("Square", Qt::CaseInsensitive) == 0) c.fitType = FitType::Square;
        else c.fitType = FitType::None;
    }
    if (m_showRawPointsCheck) c.showRawPointsInFit = m_showRawPointsCheck->isChecked();
    if (m_fitWindowSpin) c.fitWindow = m_fitWindowSpin->value();
    if (m_maxPointsSpin) c.maxPoints = m_maxPointsSpin->value();

    // create series
    if (m_chart && m_axisX && m_axisY) {
        c.scatter = new QScatterSeries();
        c.scatter->setName(c.name + " (pts)");
        c.scatter->setMarkerSize(6.0);

        c.line = new QLineSeries();
        c.line->setName(c.name);

        c.fitLine = new QLineSeries();
        c.fitLine->setName(c.name + " (fit)");

        m_chart->addSeries(c.scatter);
        m_chart->addSeries(c.line);
        m_chart->addSeries(c.fitLine);

        c.scatter->attachAxis(m_axisX);
        c.scatter->attachAxis(m_axisY);
        c.line->attachAxis(m_axisX);
        c.line->attachAxis(m_axisY);
        c.fitLine->attachAxis(m_axisX);
        c.fitLine->attachAxis(m_axisY);

        updateStyleForCurve(c);
        updateVisibilityForCurve(c);
        //qDebug() << "SERIES CREATED for" << c.name;
    }

    m_curves.push_back(c);
    return &m_curves.back();
}

PlotWidget::Curve* PlotWidget::activeCurve() {
    if (m_activeCurveIndex < 0 || m_activeCurveIndex >= m_curves.size()) return nullptr;
    return &m_curves[m_activeCurveIndex];
}
const PlotWidget::Curve* PlotWidget::activeCurve() const {
    if (m_activeCurveIndex < 0 || m_activeCurveIndex >= m_curves.size()) return nullptr;
    return &m_curves[m_activeCurveIndex];
}

void PlotWidget::rebuildCurveListUi() {
    if (!m_curveList || !m_activeCurveCombo) return;

    m_curveList->clear();
    m_activeCurveCombo->clear();

    for (int i = 0; i < m_curves.size(); ++i) {
        const auto &c = m_curves[i];

        auto *item = new QListWidgetItem(c.name);
        item->setData(Qt::UserRole, i);
        item->setForeground(c.color);
        m_curveList->addItem(item);

        m_activeCurveCombo->addItem(c.name, i);
    }

    if (m_activeCurveIndex >= 0 && m_activeCurveIndex < m_curves.size()) {
        for (int i = 0; i < m_activeCurveCombo->count(); ++i) {
            if (m_activeCurveCombo->itemData(i).toInt() == m_activeCurveIndex) {
                m_activeCurveCombo->setCurrentIndex(i);
                break;
            }
        }
        if (m_activeCurveIndex < m_curveList->count()) {
            m_curveList->setCurrentRow(m_activeCurveIndex);
        }
        syncUiFromCurve(m_curves[m_activeCurveIndex]);
    } else if (!m_curves.isEmpty()) {
        m_activeCurveIndex = 0;
        m_activeCurveCombo->setCurrentIndex(0);
        m_curveList->setCurrentRow(0);
        syncUiFromCurve(m_curves[0]);
    }
}

void PlotWidget::syncUiFromCurve(const Curve &c) {
    if (!isUiComplete()) return;

    if (m_colorPreview) {
        m_colorPreview->setAutoFillBackground(true);
        QPalette pal = m_colorPreview->palette();
        pal.setColor(QPalette::Window, c.color);
        m_colorPreview->setPalette(pal);
    }

    if (m_renderModeCombo) {
        switch (c.renderMode) {
        case RenderMode::Points: m_renderModeCombo->setCurrentText("Points"); break;
        case RenderMode::Lines:  m_renderModeCombo->setCurrentText("Lines"); break;
        case RenderMode::Fit:    m_renderModeCombo->setCurrentText("Fit"); break;
        }
    }
    if (m_fitTypeCombo) {
        switch (c.fitType) {
        case FitType::None:     m_fitTypeCombo->setCurrentText("None"); break;
        case FitType::Sine:     m_fitTypeCombo->setCurrentText("Sine"); break;
        case FitType::Triangle: m_fitTypeCombo->setCurrentText("Triangle"); break;
        case FitType::Square:   m_fitTypeCombo->setCurrentText("Square"); break;
        }
    }

    if (m_showRawPointsCheck) m_showRawPointsCheck->setChecked(c.showRawPointsInFit);
    if (m_fitWindowSpin) m_fitWindowSpin->setValue(c.fitWindow);
    if (m_maxPointsSpin) m_maxPointsSpin->setValue(c.maxPoints);
}

void PlotWidget::applyUiToCurve(Curve &c) {
    if (!isUiComplete()) return;

    if (m_renderModeCombo) {
        const QString t = m_renderModeCombo->currentText().trimmed();
        if (t.compare("Points", Qt::CaseInsensitive) == 0) c.renderMode = RenderMode::Points;
        else if (t.compare("Fit", Qt::CaseInsensitive) == 0) c.renderMode = RenderMode::Fit;
        else c.renderMode = RenderMode::Lines;
    }

    if (m_fitTypeCombo) {
        const QString t = m_fitTypeCombo->currentText().trimmed();
        if (t.compare("Sine", Qt::CaseInsensitive) == 0) c.fitType = FitType::Sine;
        else if (t.compare("Triangle", Qt::CaseInsensitive) == 0) c.fitType = FitType::Triangle;
        else if (t.compare("Square", Qt::CaseInsensitive) == 0) c.fitType = FitType::Square;
        else c.fitType = FitType::None;
    }

    if (m_showRawPointsCheck) c.showRawPointsInFit = m_showRawPointsCheck->isChecked();
    if (m_fitWindowSpin) c.fitWindow = m_fitWindowSpin->value();
    if (m_maxPointsSpin) c.maxPoints = m_maxPointsSpin->value();

    updateStyleForCurve(c);
    updateVisibilityForCurve(c);
}

void PlotWidget::updateStyleForCurve(Curve &c) {
    if (!c.scatter || !c.line || !c.fitLine) return;

    c.scatter->setColor(c.color);
    c.scatter->setBorderColor(c.color);

    QPen pLine(c.color);
    pLine.setWidthF(1.6);
    c.line->setPen(pLine);

    QPen pFit(c.color);
    pFit.setWidthF(2.2);
    c.fitLine->setPen(pFit);
}

void PlotWidget::updateVisibilityForCurve(Curve &c) {
    if (!c.scatter || !c.line || !c.fitLine) return;

    if (c.renderMode == RenderMode::Points) {
        c.scatter->setVisible(true);
        c.line->setVisible(false);
        c.fitLine->setVisible(false);
    } else if (c.renderMode == RenderMode::Lines) {
        c.scatter->setVisible(false);
        c.line->setVisible(true);
        c.fitLine->setVisible(false);
    } else { // Fit
        c.fitLine->setVisible(c.fitType != FitType::None);
        c.scatter->setVisible(c.showRawPointsInFit);
        c.line->setVisible(false);
    }
}

void PlotWidget::onAddCurve() {
    int ch = 0;
    while (true) {
        bool used = false;
        for (const auto &c : m_curves) {
            if (c.channelId == ch) { used = true; break; }
        }
        if (!used) break;
        ++ch;
    }

    ensureCurveForChannel(ch);
    m_activeCurveIndex = m_curves.size() - 1;
    rebuildCurveListUi();
    m_dirty = true;
}

void PlotWidget::onRemoveCurve() {
    if (m_activeCurveIndex < 0 || m_activeCurveIndex >= m_curves.size()) return;
    if (m_curves.size() == 1) return;

    Curve &c = m_curves[m_activeCurveIndex];
    if (m_chart) {
        if (c.scatter) m_chart->removeSeries(c.scatter);
        if (c.line) m_chart->removeSeries(c.line);
        if (c.fitLine) m_chart->removeSeries(c.fitLine);
    }
    delete c.scatter;
    delete c.line;
    delete c.fitLine;

    m_curves.removeAt(m_activeCurveIndex);
    if (m_activeCurveIndex >= m_curves.size()) m_activeCurveIndex = m_curves.size() - 1;

    rebuildCurveListUi();
    m_dirty = true;
}

void PlotWidget::onActiveCurveChanged(int index) {
    if (!m_activeCurveCombo) return;
    const int curveIdx = m_activeCurveCombo->itemData(index).toInt();
    if (curveIdx < 0 || curveIdx >= m_curves.size()) return;

    m_activeCurveIndex = curveIdx;

    if (m_curveList) m_curveList->setCurrentRow(curveIdx);
    syncUiFromCurve(m_curves[curveIdx]);
}

void PlotWidget::onCurveListSelectionChanged() {
    if (!m_curveList || !m_activeCurveCombo) return;
    const int row = m_curveList->currentRow();
    if (row < 0 || row >= m_curves.size()) return;

    m_activeCurveIndex = row;

    for (int i = 0; i < m_activeCurveCombo->count(); ++i) {
        if (m_activeCurveCombo->itemData(i).toInt() == row) {
            m_activeCurveCombo->setCurrentIndex(i);
            break;
        }
    }
    syncUiFromCurve(m_curves[row]);
}

void PlotWidget::onPickColor() {
    Curve *c = activeCurve();
    if (!c) return;

    const QColor chosen = QColorDialog::getColor(c->color, this, "Choose curve color");
    if (!chosen.isValid()) return;

    c->color = chosen;
    updateStyleForCurve(*c);
    rebuildCurveListUi();
    m_dirty = true;
}

void PlotWidget::onRenderModeChanged(int) {
    Curve *c = activeCurve();
    if (!c) return;
    applyUiToCurve(*c);
    m_dirty = true;
}

void PlotWidget::onFitTypeChanged(int) {
    Curve *c = activeCurve();
    if (!c) return;
    applyUiToCurve(*c);
    m_dirty = true;
}

void PlotWidget::onShowRawPointsToggled(bool) {
    Curve *c = activeCurve();
    if (!c) return;
    applyUiToCurve(*c);
    m_dirty = true;
}

void PlotWidget::onFitWindowChanged(int) {
    Curve *c = activeCurve();
    if (!c) return;
    applyUiToCurve(*c);
    m_dirty = true;
}

void PlotWidget::onMaxPointsChanged(int) {
    Curve *c = activeCurve();
    if (!c) return;
    applyUiToCurve(*c);
    m_dirty = true;
}

void PlotWidget::onClearAll() {
    for (auto &c : m_curves) {
        c.points.clear();
        if (c.scatter) c.scatter->clear();
        if (c.line) c.line->clear();
        if (c.fitLine) c.fitLine->clear();
    }

    m_latestMeta.clear();
    updateMetaDisplay();

    m_pinnedToRight = true;
    if (m_scrollBarX) {
        m_scrollBarX->setRange(0, 0);
        m_scrollBarX->setValue(0);
    }
    if (m_axisX) m_axisX->setRange(0, 1);
    if (m_axisY) m_axisY->setRange(0, 1);

    m_dirty = true;
    m_selectedMetaKeys.clear();
    m_seenMetaKeys.clear();
    if (m_metaKeysList) m_metaKeysList->clear();
    updateMetaDisplay();
}

void PlotWidget::onMetaAdd() {
    if (!m_metaKeysList) return;

    const auto items = m_metaKeysList->selectedItems();
    if (items.isEmpty()) return;

    for (auto *it : items) {
        const QString k = it->text().trimmed();
        if (k.isEmpty()) continue;

        m_selectedMetaKeys.insert(k);

        // UI: mark as selected (checked)
        it->setCheckState(Qt::Checked);
    }
    updateMetaDisplay();
}

void PlotWidget::onMetaRemove() {
    if (!m_metaKeysList) return;

    const auto items = m_metaKeysList->selectedItems();
    if (items.isEmpty()) return;

    for (auto *it : items) {
        const QString k = it->text().trimmed();
        if (k.isEmpty()) continue;

        m_selectedMetaKeys.remove(k);

        // UI: mark as unselected
        it->setCheckState(Qt::Unchecked);
    }
    updateMetaDisplay();
}

void PlotWidget::updateMetaDisplay() {
    if (!m_metaDisplay) return;

    QStringList lines;
    // 按字母序输出（可选）
    QStringList keys = QStringList(m_selectedMetaKeys.begin(), m_selectedMetaKeys.end());
    keys.sort(Qt::CaseInsensitive);

    for (const QString &k : keys) {
        const QString v = m_latestMeta.value(k, "");
        lines << QString("%1=%2").arg(k, v);
    }
    m_metaDisplay->setPlainText(lines.join("\n"));
}

void PlotWidget::onScrollBarXChanged(int value) {
    if (!m_scrollBarX) return;
    const int maxv = m_scrollBarX->maximum();
    m_pinnedToRight = (maxv <= 0) ? true : (value >= maxv);

    updateAxesAndScrollbar(false);
    m_dirty = true;
}

static bool parseDouble(const QString &s, double *out) {
    bool ok = false;
    const double v = s.trimmed().toDouble(&ok);
    if (!ok) return false;
    if (out) *out = v;
    return true;
}

PlotWidget::ParsedLine PlotWidget::parseLine(const QString &line) {
    ParsedLine r;
    QString s = line.trimmed();
    if (s.isEmpty()) return r;

    // CH:n anywhere
    {
        QRegularExpression re(R"((?:^|,)\s*CH\s*:\s*([+-]?\d+)\s*(?=,|$))",
                              QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch m = re.match(s);
        if (m.hasMatch()) {
            r.hasChannel = true;
            r.channel = m.captured(1).toInt();
        }
    }

    // point: [x,y] anywhere
    int pointStart = -1, pointLen = 0;
    {
        QRegularExpression re(R"(\[\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*\])");
        auto m = re.match(s);
        if (m.hasMatch()) {
            double x=0,y=0;
            if (parseDouble(m.captured(1), &x) && parseDouble(m.captured(2), &y)) {
                r.hasPoint = true;
                r.point = QPointF(x,y);
                pointStart = m.capturedStart(0);
                pointLen = m.capturedLength(0);
            }
        }
    }

    // fallback: x,y at beginning
    if (!r.hasPoint) {
        QRegularExpression re(R"(^\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*,\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*)");
        auto m = re.match(s);
        if (m.hasMatch()) {
            double x=0,y=0;
            if (parseDouble(m.captured(1), &x) && parseDouble(m.captured(2), &y)) {
                r.hasPoint = true;
                r.point = QPointF(x,y);
                pointStart = m.capturedStart(0);
                pointLen = m.capturedLength(0);
            }
        }
    }

    // remove point substring to parse remaining key:value safely
    QString rest = s;
    if (pointStart >= 0 && pointLen > 0) {
        rest.remove(pointStart, pointLen);
    }

    rest = rest.trimmed();
    while (rest.startsWith(',')) rest.remove(0,1);
    rest = rest.trimmed();

    if (!rest.isEmpty()) {
        const QStringList parts = rest.split(',', Qt::SkipEmptyParts);
        for (const QString &p0 : parts) {
            const QString p = p0.trimmed();
            const int colon = p.indexOf(':');
            if (colon <= 0) continue;
            const QString key = p.left(colon).trimmed();
            const QString val = p.mid(colon+1).trimmed();
            if (key.isEmpty()) continue;
            r.kv.insert(key, val);
        }
    }

    return r;
}

void PlotWidget::onSerialLineReceived(const QString &line) {
    const ParsedLine pl = parseLine(line);

    // meta update (global)
    for (auto it = pl.kv.constBegin(); it != pl.kv.constEnd(); ++it) {
        const QString k = it.key().trimmed();
        const QString v = it.value().trimmed();

        if (k.isEmpty()) continue;
        if (k.compare("CH", Qt::CaseInsensitive) == 0) continue;

        m_latestMeta[k] = v;

        // NEW: first time seen -> add into listWidgetPlotMetaKeys
        if (m_metaKeysList && !m_seenMetaKeys.contains(k)) {
            m_seenMetaKeys.insert(k);

            auto *item = new QListWidgetItem(k);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            item->setCheckState(Qt::Unchecked);
            m_metaKeysList->addItem(item);
        }
    }
    updateMetaDisplay();

    if (!pl.hasPoint) return;

    Curve *curve = nullptr;
    if (pl.hasChannel && pl.channel >= 0) {
        curve = ensureCurveForChannel(pl.channel);
    } else {
        curve = activeCurve();
        if (!curve) curve = ensureCurveForChannel(0);
    }
    if (!curve) return;

    curve->points.push_back(pl.point);

    const int maxPts = qMax(100, curve->maxPoints);
    if (curve->points.size() > maxPts) {
        const int drop = curve->points.size() - maxPts;
        curve->points.erase(curve->points.begin(), curve->points.begin() + drop);
    }

    if (m_activeCurveCombo && m_activeCurveCombo->count() != m_curves.size()) {
        rebuildCurveListUi();
    }

    m_dirty = true;
    //qDebug() << "PLOT LINE" << line;
}

void PlotWidget::onRenderTick() {
    if (!m_dirty) return;
    m_dirty = false;

    updateSeriesForAllCurves();
    updateAxesAndScrollbar(true);
}

static QList<QPointF> toList(const QVector<QPointF> &v) {
    QList<QPointF> out;
    out.reserve(v.size());
    for (const auto &p : v) out.push_back(p);
    return out;
}

void PlotWidget::updateSeriesForAllCurves() {
    for (auto &c : m_curves) {
        if (!c.scatter || !c.line || !c.fitLine) continue;

        if (c.renderMode == RenderMode::Points) {
            c.scatter->replace(toList(c.points));
        } else if (c.renderMode == RenderMode::Lines) {
            c.line->replace(toList(c.points));
        } else {
            if (c.showRawPointsInFit) c.scatter->replace(toList(c.points));
            else c.scatter->clear();
            // fit computed in updateAxesAndScrollbar (needs x-range)
        }

        updateVisibilityForCurve(c);
        updateStyleForCurve(c);
    }
}

static void globalMinMaxX_fromPoints(const QVector<QVector<QPointF>> &allPts, double *xmin, double *xmax) {
    double mn = 0, mx = 0;
    bool init = false;
    for (const auto &pts : allPts) {
        for (const auto &p : pts) {
            if (!init) { mn = mx = p.x(); init = true; }
            else { mn = qMin(mn, p.x()); mx = qMax(mx, p.x()); }
        }
    }
    if (!init) { mn = 0; mx = 1; }
    if (xmin) *xmin = mn;
    if (xmax) *xmax = mx;
}

static void minMaxYInXRange_fromPoints(const QVector<QVector<QPointF>> &allPts, double x0, double x1, double *ymin, double *ymax) {
    double mn = 0, mx = 0;
    bool init = false;
    for (const auto &pts : allPts) {
        for (const auto &p : pts) {
            if (p.x() < x0 || p.x() > x1) continue;
            if (!init) { mn = mx = p.y(); init = true; }
            else { mn = qMin(mn, p.y()); mx = qMax(mx, p.y()); }
        }
    }
    if (!init) { mn = 0; mx = 1; }
    if (ymin) *ymin = mn;
    if (ymax) *ymax = mx;
}

void PlotWidget::updateAxesAndScrollbar(bool keepRightIfPinned) {
    if (!m_axisX || !m_axisY || !m_scrollBarX) return;

    QVector<QVector<QPointF>> allPts;
    allPts.reserve(m_curves.size());
    bool any = false;
    for (const auto &c : m_curves) {
        allPts.push_back(c.points);
        if (!c.points.isEmpty()) any = true;
    }

    if (!any) {
        m_axisX->setRange(0, 1);
        m_axisY->setRange(0, 1);
        m_scrollBarX->setRange(0, 0);
        m_scrollBarX->setValue(0);
        if (m_labelRange) m_labelRange->setText("No data");
        return;
    }

    double gx0=0, gx1=1;
    globalMinMaxX_fromPoints(allPts, &gx0, &gx1);
    double gSpan = gx1 - gx0;
    if (gSpan <= 0) gSpan = 1.0;

    const double spanAll = gSpan;
    const double spanWin = (spanAll <= 1e-9) ? 1.0 : qMax(spanAll * 0.20, spanAll / 50.0);
    m_windowSpan = qMin(spanAll, spanWin);

    const double maxStart = gx1 - m_windowSpan;
    const bool canScroll = (maxStart > gx0 + 1e-12);

    const int sliderMax = canScroll ? 1000 : 0;
    if (m_scrollBarX->maximum() != sliderMax) {
        m_scrollBarX->setRange(0, sliderMax);
    }

    if (keepRightIfPinned && m_pinnedToRight && sliderMax > 0) {
        m_scrollBarX->setValue(sliderMax);
    }

    const int v = m_scrollBarX->value();
    double start = gx0;
    if (sliderMax > 0) {
        const double t = double(v) / double(sliderMax);
        start = gx0 + t * (maxStart - gx0);
    } else {
        start = gx0;
    }
    double end = start + m_windowSpan;
    if (end < start + 1e-9) end = start + 1.0;

    m_viewXStart = start;
    m_viewXEnd = end;

    double y0=0,y1=1;
    minMaxYInXRange_fromPoints(allPts, m_viewXStart, m_viewXEnd, &y0, &y1);
    double ySpan = y1 - y0;
    if (ySpan <= 1e-12) ySpan = 1.0;
    const double pad = ySpan * 0.08;

    m_axisX->setRange(m_viewXStart, m_viewXEnd);
    m_axisY->setRange(y0 - pad, y1 + pad);

    // Update fit curves with visible range
    for (auto &c : m_curves) {
        if (!c.fitLine) continue;
        if (c.renderMode != RenderMode::Fit || c.fitType == FitType::None) {
            c.fitLine->clear();
            continue;
        }
        const QVector<QPointF> fitPts = computeFitCurve(c, m_viewXStart, m_viewXEnd, 400);
        c.fitLine->replace(toList(fitPts));
    }

    if (m_labelRange) {
        m_labelRange->setText(QString("X:[%1, %2]")
                                  .arg(m_viewXStart, 0, 'g', 6)
                                  .arg(m_viewXEnd,   0, 'g', 6));
    }
}

QVector<QPointF> PlotWidget::lastNPoints(const QVector<QPointF> &pts, int n) {
    if (n <= 0 || pts.isEmpty()) return {};
    if (pts.size() <= n) return pts;
    return pts.mid(pts.size() - n);
}

QVector<QPointF> PlotWidget::computeFitCurve(const Curve &c, double xMin, double xMax, int samples) const {
    QVector<QPointF> window = lastNPoints(c.points, qMax(20, c.fitWindow));
    if (window.size() < 20) return {};

    QVector<QPointF> inRange;
    inRange.reserve(window.size());
    for (const auto &p : window) {
        if (p.x() >= xMin && p.x() <= xMax) inRange.push_back(p);
    }
    if (inRange.size() >= 20) window = inRange;

    switch (c.fitType) {
    case FitType::Sine:     return fitSine(window, xMin, xMax, samples);
    case FitType::Triangle: return fitTriangle(window, xMin, xMax, samples);
    case FitType::Square:   return fitSquare(window, xMin, xMax, samples);
    case FitType::None:     break;
    }
    return {};
}

// 3x3 solver
static bool solve3(double A[3][3], double b[3], double x[3]) {
    double M[3][4] = {
                      {A[0][0], A[0][1], A[0][2], b[0]},
                      {A[1][0], A[1][1], A[1][2], b[1]},
                      {A[2][0], A[2][1], A[2][2], b[2]},
                      };

    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r) {
            if (qFabs(M[r][col]) > qFabs(M[piv][col])) piv = r;
        }
        if (qFabs(M[piv][col]) < 1e-15) return false;
        if (piv != col) {
            for (int k = col; k < 4; ++k) std::swap(M[piv][k], M[col][k]);
        }
        const double div = M[col][col];
        for (int k = col; k < 4; ++k) M[col][k] /= div;
        for (int r = 0; r < 3; ++r) {
            if (r == col) continue;
            const double f = M[r][col];
            for (int k = col; k < 4; ++k) M[r][k] -= f * M[col][k];
        }
    }
    x[0] = M[0][3];
    x[1] = M[1][3];
    x[2] = M[2][3];
    return true;
}

static double estimatePeriodFromMaxima(const QVector<QPointF> &pts) {
    if (pts.size() < 10) return 0.0;
    QVector<double> peakXs;
    peakXs.reserve(32);
    for (int i = 1; i + 1 < pts.size(); ++i) {
        const double y0 = pts[i-1].y();
        const double y1 = pts[i].y();
        const double y2 = pts[i+1].y();
        if (y1 > y0 && y1 > y2) peakXs.push_back(pts[i].x());
    }
    if (peakXs.size() < 2) return 0.0;
    double sum=0; int cnt=0;
    for (int i = 1; i < peakXs.size(); ++i) {
        const double d = peakXs[i] - peakXs[i-1];
        if (d > 0) { sum += d; ++cnt; }
    }
    return (cnt>0) ? (sum / double(cnt)) : 0.0;
}

static bool fitSineLinearLS(const QVector<QPointF> &pts, double omega,
                            double *outA, double *outB, double *outC, double *outSSE) {
    // y = A*sin(wx) + B*cos(wx) + C
    double ss=0, cc=0, sc=0, s1=0, c1=0;
    double ys=0, yc=0, y1=0;
    const int n = pts.size();

    for (const auto &p : pts) {
        const double x = p.x();
        const double y = p.y();
        const double s = qSin(omega * x);
        const double c = qCos(omega * x);
        ss += s*s; cc += c*c; sc += s*c;
        s1 += s;   c1 += c;
        ys += y*s; yc += y*c; y1 += y;
    }

    double M[3][3] = {
                      {ss, sc, s1},
                      {sc, cc, c1},
                      {s1, c1, double(n)},
                      };
    double b[3] = {ys, yc, y1};
    double sol[3] = {0,0,0};
    if (!solve3(M, b, sol)) return false;

    const double A = sol[0], B = sol[1], C = sol[2];

    double sse = 0.0;
    for (const auto &p : pts) {
        const double yhat = A*qSin(omega*p.x()) + B*qCos(omega*p.x()) + C;
        const double e = p.y() - yhat;
        sse += e*e;
    }

    if (outA) *outA = A;
    if (outB) *outB = B;
    if (outC) *outC = C;
    if (outSSE) *outSSE = sse;
    return true;
}

QVector<QPointF> PlotWidget::fitSine(const QVector<QPointF> &pts, double xMin, double xMax, int samples) const {
    if (pts.size() < 20) return {};
    const double span = xMax - xMin;
    if (span <= 1e-12) return {};

    double period = estimatePeriodFromMaxima(pts);
    if (period <= 1e-12) period = span;
    const double omega0 = 2.0 * M_PI / period;

    double bestOmega = omega0;
    double bestA=0, bestB=0, bestC=0, bestSSE=1e300;

    for (int i = -10; i <= 10; ++i) {
        const double omega = omega0 * (1.0 + 0.02 * double(i));
        if (omega <= 0) continue;
        double A=0,B=0,C=0,SSE=0;
        if (!fitSineLinearLS(pts, omega, &A, &B, &C, &SSE)) continue;
        if (SSE < bestSSE) {
            bestSSE = SSE;
            bestOmega = omega;
            bestA = A; bestB = B; bestC = C;
        }
    }

    QVector<QPointF> out;
    out.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        const double t = double(i) / double(samples - 1);
        const double x = xMin + t * (xMax - xMin);
        const double y = bestA*qSin(bestOmega*x) + bestB*qCos(bestOmega*x) + bestC;
        out.push_back(QPointF(x,y));
    }
    return out;
}

QVector<QPointF> PlotWidget::fitTriangle(const QVector<QPointF> &pts, double xMin, double xMax, int samples) const {
    if (pts.size() < 20) return {};

    double ymin = pts[0].y(), ymax = pts[0].y();
    for (const auto &p : pts) { ymin = qMin(ymin, p.y()); ymax = qMax(ymax, p.y()); }
    const double amp = 0.5 * (ymax - ymin);
    const double offset = 0.5 * (ymax + ymin);

    double period = estimatePeriodFromMaxima(pts);
    const double span = xMax - xMin;
    if (period <= 1e-12) period = span;
    if (period <= 1e-12) return {};

    auto tri = [&](double x)->double {
        double ph = (x - xMin) / period;
        ph = ph - qFloor(ph);
        double v = 0.0;
        if (ph < 0.25) v = ph * 4.0;
        else if (ph < 0.75) v = 2.0 - ph * 4.0;
        else v = ph * 4.0 - 4.0;
        return offset + amp * v;
    };

    QVector<QPointF> out;
    out.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        const double t = double(i) / double(samples - 1);
        const double x = xMin + t * (xMax - xMin);
        out.push_back(QPointF(x, tri(x)));
    }
    return out;
}

QVector<QPointF> PlotWidget::fitSquare(const QVector<QPointF> &pts, double xMin, double xMax, int samples) const {
    if (pts.size() < 20) return {};

    double ymin = pts[0].y(), ymax = pts[0].y();
    for (const auto &p : pts) { ymin = qMin(ymin, p.y()); ymax = qMax(ymax, p.y()); }
    const double thr = 0.5 * (ymax + ymin);

    double sumHi=0, sumLo=0;
    int cntHi=0, cntLo=0;
    QVector<double> risingXs;
    risingXs.reserve(32);

    bool prevHigh = (pts[0].y() >= thr);
    for (int i = 0; i < pts.size(); ++i) {
        const bool high = (pts[i].y() >= thr);
        if (high) { sumHi += pts[i].y(); cntHi++; }
        else { sumLo += pts[i].y(); cntLo++; }

        if (!prevHigh && high) risingXs.push_back(pts[i].x());
        prevHigh = high;
    }

    const double hi = (cntHi > 0) ? (sumHi / cntHi) : ymax;
    const double lo = (cntLo > 0) ? (sumLo / cntLo) : ymin;

    double period = 0.0;
    if (risingXs.size() >= 2) {
        double sum=0; int cnt=0;
        for (int i = 1; i < risingXs.size(); ++i) {
            const double d = risingXs[i] - risingXs[i-1];
            if (d > 0) { sum += d; cnt++; }
        }
        if (cnt > 0) period = sum / cnt;
    }
    const double span = xMax - xMin;
    if (period <= 1e-12) period = span;
    if (period <= 1e-12) return {};

    const double duty = (pts.size() > 0) ? (double(cntHi) / double(pts.size())) : 0.5;
    const double dutyClamped = qBound(0.05, duty, 0.95);

    auto sq = [&](double x)->double {
        double ph = (x - xMin) / period;
        ph = ph - qFloor(ph);
        return (ph < dutyClamped) ? hi : lo;
    };

    QVector<QPointF> out;
    out.reserve(samples);
    for (int i = 0; i < samples; ++i) {
        const double t = double(i) / double(samples - 1);
        const double x = xMin + t * (xMax - xMin);
        out.push_back(QPointF(x, sq(x)));
    }
    return out;
}
