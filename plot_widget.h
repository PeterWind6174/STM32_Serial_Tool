#pragma once

#include <QWidget>
#include <QTimer>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QColor>
#include <QPointF>
#include <QString>

class QListWidget;
class QComboBox;
class QPushButton;
class QLabel;
class QCheckBox;
class QSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QScrollBar;

// Qt Charts forward declarations (Qt6: in global namespace)
class QChartView;
class QChart;
class QLineSeries;
class QScatterSeries;
class QValueAxis;

class PlotWidget final : public QWidget {
    Q_OBJECT
public:
    explicit PlotWidget(QWidget *tabRoot, QWidget *parent = nullptr);
    ~PlotWidget() override;

public slots:
    // from SerialTerminalWidget (shared serial): one full line (no trailing newline)
    void onSerialLineReceived(const QString &line);

private slots:
    void onAddCurve();
    void onRemoveCurve();
    void onActiveCurveChanged(int index);
    void onCurveListSelectionChanged();
    void onPickColor();

    void onRenderModeChanged(int);
    void onFitTypeChanged(int);
    void onShowRawPointsToggled(bool);
    void onFitWindowChanged(int);
    void onMaxPointsChanged(int);

    void onClearAll();

    void onMetaAdd();
    void onMetaRemove();

    void onScrollBarXChanged(int value);

    void onRenderTick();

private:
    enum class RenderMode { Points, Lines, Fit };
    enum class FitType { None, Sine, Triangle, Square };

    struct Curve {
        int channelId = -1;                // CH:n
        QString name;                      // display name
        QColor color;

        RenderMode renderMode = RenderMode::Lines;
        FitType fitType = FitType::None;
        bool showRawPointsInFit = true;

        int fitWindow = 200;               // last N points
        int maxPoints = 2000;              // rolling buffer

        QVector<QPointF> points;

        // series
        QScatterSeries *scatter = nullptr;
        QLineSeries *line = nullptr;
        QLineSeries *fitLine = nullptr;
    };

    void bindUi(QWidget *root);
    bool isUiComplete() const;

    // curves
    Curve* ensureCurveForChannel(int ch);
    Curve* activeCurve();
    const Curve* activeCurve() const;
    void rebuildCurveListUi();
    void syncUiFromCurve(const Curve &c);
    void applyUiToCurve(Curve &c);

    QColor defaultColorForIndex(int idx) const;

    // parsing
    struct ParsedLine {
        bool hasPoint = false;
        QPointF point;
        bool hasChannel = false;
        int channel = -1;
        QMap<QString, QString> kv; // key:value
    };
    static ParsedLine parseLine(const QString &line);

    // chart
    void initChartIfNeeded();
    void updateSeriesForAllCurves();
    void updateVisibilityForCurve(Curve &c);
    void updateStyleForCurve(Curve &c);
    void updateAxesAndScrollbar(bool keepRightIfPinned);

    // fitting
    QVector<QPointF> computeFitCurve(const Curve &c, double xMin, double xMax, int samples) const;
    QVector<QPointF> fitSine(const QVector<QPointF> &pts, double xMin, double xMax, int samples) const;
    QVector<QPointF> fitTriangle(const QVector<QPointF> &pts, double xMin, double xMax, int samples) const;
    QVector<QPointF> fitSquare(const QVector<QPointF> &pts, double xMin, double xMax, int samples) const;

    static QVector<QPointF> lastNPoints(const QVector<QPointF> &pts, int n);

    // meta
    void updateMetaDisplay();

private:
    // UI pointers
    QChartView *m_chartView = nullptr;
    QScrollBar *m_scrollBarX = nullptr;
    QLabel *m_labelRange = nullptr;

    QListWidget *m_curveList = nullptr;
    QComboBox *m_activeCurveCombo = nullptr;
    QPushButton *m_addCurveBtn = nullptr;
    QPushButton *m_removeCurveBtn = nullptr;

    QPushButton *m_pickColorBtn = nullptr;
    QLabel *m_colorPreview = nullptr;

    QComboBox *m_renderModeCombo = nullptr;
    QComboBox *m_fitTypeCombo = nullptr;
    QCheckBox *m_showRawPointsCheck = nullptr;
    QSpinBox *m_fitWindowSpin = nullptr;
    QSpinBox *m_maxPointsSpin = nullptr;
    QPushButton *m_clearBtn = nullptr;

    QLineEdit *m_metaKeyEdit = nullptr;
    QPushButton *m_metaAddBtn = nullptr;
    QListWidget *m_metaKeysList = nullptr;
    QPushButton *m_metaRemoveBtn = nullptr;
    QPlainTextEdit *m_metaDisplay = nullptr;

    // chart objects
    QChart *m_chart = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    // data
    QVector<Curve> m_curves;
    int m_activeCurveIndex = -1;

    // rendering control
    QTimer m_renderTimer;
    bool m_dirty = false;

    // scrollbar mapping
    bool m_pinnedToRight = true;  // user at right edge => auto follow latest
    double m_viewXStart = 0.0;
    double m_viewXEnd = 1.0;
    double m_windowSpan = 1.0;

    // meta selected keys and latest values (global)
    QSet<QString> m_selectedMetaKeys;
    QMap<QString, QString> m_latestMeta;
    QSet<QString> m_seenMetaKeys;   // NEW: all keys ever seen from serial
};
