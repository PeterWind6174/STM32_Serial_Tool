#pragma once

#include <QWidget>
#include <QSerialPort>
#include <QTimer>
#include <QElapsedTimer>

class QComboBox;
class QPushButton;
class QTextEdit;
class QLineEdit;
class QRadioButton;
class QCheckBox;
class QSpinBox;
class QLabel;

class SerialTerminalWidget final : public QWidget {
    Q_OBJECT

public:
    explicit SerialTerminalWidget(QWidget *tabRoot, QWidget *parent = nullptr);
    ~SerialTerminalWidget() override;
    void closeIfOpen();

private slots:
    void onRefreshPorts();
    void onOpenPort();
    void onClosePort();

    void onReadyRead();
    void onSendOnce();
    void onClearTerminal();

    void onTimedSendToggle();
    void onTimedSendTick();

private:
    enum class DisplayMode { ASCII, HEX };

    void bindUi(QWidget *root);
    bool isUiComplete() const;

    void setConnectedUi(bool connected);
    void logSystem(const QString &msg);
    void appendDividerLine();
    void appendMessage(const QByteArray &bytes, bool isRx);

    // rendering helpers
    DisplayMode recvMode() const;
    DisplayMode sendMode() const;
    bool showEscapes() const;

    QByteArray buildTxBytesFromInput(QString *outDisplayAscii) const; // always ASCII bytes
    void appendAlignedText(const QString &text, bool isRx, const QColor &color);
    void appendAlignedSegments(const QVector<QPair<QString,bool>> &segments, bool isRx,
                               const QColor &normalColor, const QColor &escapeColor);

    QVector<QPair<QString,bool>> renderAsciiSegments(const QByteArray &bytes, bool escapesEnabled) const;
    QString renderHexString(const QByteArray &bytes) const;

    // auto wrap
    void maybeAutoWrapBeforeNewMessage();

    // port list filter (macOS)
    static bool acceptPortPath(const QString &sysPath);

private:
    // UI pointers (found by objectName)
    QComboBox   *m_portCombo = nullptr;
    QPushButton *m_refreshPortsBtn = nullptr;

    QComboBox   *m_baudCombo = nullptr;
    QComboBox   *m_dataBitsCombo = nullptr;
    QComboBox   *m_parityCombo = nullptr;
    QComboBox   *m_stopBitsCombo = nullptr;
    QComboBox   *m_flowCombo = nullptr;

    QPushButton *m_openBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    QTextEdit   *m_terminalEdit = nullptr;
    QPushButton *m_clearBtn = nullptr;

    QComboBox   *m_recvModeCombo = nullptr;
    QRadioButton *m_showEscapesRadio = nullptr;

    QCheckBox   *m_autoWrapCheck = nullptr;
    QSpinBox    *m_autoWrapMsSpin = nullptr;

    QLineEdit   *m_sendEdit = nullptr;
    QComboBox   *m_sendModeCombo = nullptr;
    QPushButton *m_sendBtn = nullptr;

    QCheckBox   *m_timedSendCheck = nullptr;
    QSpinBox    *m_sendIntervalMsSpin = nullptr;
    QPushButton *m_timedSendToggleBtn = nullptr;
    QLabel      *m_sendCountLabel = nullptr;
    QLabel      *m_failCountLabel = nullptr;

    // serial
    QSerialPort m_serial;

    // timed send
    QTimer m_timedSendTimer;
    quint64 m_sendCount = 0;
    quint64 m_failCount = 0;

    // auto wrap timing
    qint64 m_lastMessageMs = -1; // monotonic ms
    QElapsedTimer m_mono;

signals:
    void statusMessage(const QString &msg, int timeoutMs = 0);
};
