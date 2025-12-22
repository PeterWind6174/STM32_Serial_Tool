#include "serial_terminal_widget.h"

#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QRadioButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>

#include <QSerialPortInfo>

#include <QTextCursor>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QFont>
#include <QDateTime>
#include <QFontMetrics>

static QString tsHmsZ() {
    return QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
}

SerialTerminalWidget::SerialTerminalWidget(QWidget *tabRoot, QWidget *parent)
    : QWidget(parent) {

    m_mono.start();
    bindUi(tabRoot);

    // serial signals
    connect(&m_serial, &QSerialPort::readyRead, this, &SerialTerminalWidget::onReadyRead);

    // timer
    m_timedSendTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_timedSendTimer, &QTimer::timeout, this, &SerialTerminalWidget::onTimedSendTick);

    if (isUiComplete()) {
        // terminal view styles
        m_terminalEdit->setReadOnly(true);
        m_terminalEdit->setLineWrapMode(QTextEdit::WidgetWidth);
        QFont mono;
        mono.setStyleHint(QFont::Monospace);
#if defined(Q_OS_MAC)
        mono.setFamily("Menlo");
#else
        mono.setFamily("Monospace");
#endif
        m_terminalEdit->setFont(mono);

        // populate defaults if empty
        if (m_baudCombo && m_baudCombo->count() == 0) {
            m_baudCombo->setEditable(true);
            m_baudCombo->addItems({"9600","19200","38400","57600","115200","230400","460800","921600"});
            m_baudCombo->setCurrentText("115200");
        } else if (m_baudCombo) {
            m_baudCombo->setEditable(true);
        }

        if (m_dataBitsCombo && m_dataBitsCombo->count() == 0) {
            m_dataBitsCombo->addItems({"5","6","7","8"});
            m_dataBitsCombo->setCurrentText("8");
        }
        if (m_parityCombo && m_parityCombo->count() == 0) {
            m_parityCombo->addItems({"None","Even","Odd","Mark","Space"});
            m_parityCombo->setCurrentText("None");
        }
        if (m_stopBitsCombo && m_stopBitsCombo->count() == 0) {
            m_stopBitsCombo->addItems({"1","1.5","2"});
            m_stopBitsCombo->setCurrentText("1");
        }
        if (m_flowCombo && m_flowCombo->count() == 0) {
            m_flowCombo->addItems({"None","RTS/CTS","XON/XOFF"});
            m_flowCombo->setCurrentText("None");
        }

        if (m_recvModeCombo && m_recvModeCombo->count() == 0) {
            m_recvModeCombo->addItems({"ASCII","HEX"});
            m_recvModeCombo->setCurrentText("ASCII");
        }
        if (m_sendModeCombo && m_sendModeCombo->count() == 0) {
            m_sendModeCombo->addItems({"ASCII","HEX"});
            m_sendModeCombo->setCurrentText("ASCII");
        }

        if (m_autoWrapMsSpin) {
            m_autoWrapMsSpin->setRange(50, 60000);
            if (m_autoWrapMsSpin->value() == 0) m_autoWrapMsSpin->setValue(300);
        }
        if (m_sendIntervalMsSpin) {
            m_sendIntervalMsSpin->setRange(10, 600000);
            if (m_sendIntervalMsSpin->value() == 0) m_sendIntervalMsSpin->setValue(1000);
        }

        // connect UI
        connect(m_refreshPortsBtn, &QPushButton::clicked, this, &SerialTerminalWidget::onRefreshPorts);
        connect(m_openBtn, &QPushButton::clicked, this, &SerialTerminalWidget::onOpenPort);
        connect(m_closeBtn, &QPushButton::clicked, this, &SerialTerminalWidget::onClosePort);
        connect(m_sendBtn, &QPushButton::clicked, this, &SerialTerminalWidget::onSendOnce);
        connect(m_sendEdit, &QLineEdit::returnPressed, this, &SerialTerminalWidget::onSendOnce);
        connect(m_clearBtn, &QPushButton::clicked, this, &SerialTerminalWidget::onClearTerminal);

        connect(m_timedSendToggleBtn, &QPushButton::clicked, this, &SerialTerminalWidget::onTimedSendToggle);

        // initial state
        setConnectedUi(false);
        onRefreshPorts();
        logSystem("Serial terminal ready.");
        emit statusMessage("串口终端已就绪。",3000);
    } else {
        // allow app to run even if tab not prepared yet
    }
}

SerialTerminalWidget::~SerialTerminalWidget() {
    if (m_serial.isOpen()) m_serial.close();
}

void SerialTerminalWidget::bindUi(QWidget *root) {
    if (!root) return;

    m_portCombo = root->findChild<QComboBox*>("comboBoxSerialPortTerm");
    m_refreshPortsBtn = root->findChild<QPushButton*>("pushButtonRefreshPortsTerm");

    m_baudCombo = root->findChild<QComboBox*>("comboBoxBaudRateTerm");
    m_dataBitsCombo = root->findChild<QComboBox*>("comboBoxDataBits");
    m_parityCombo = root->findChild<QComboBox*>("comboBoxParity");
    m_stopBitsCombo = root->findChild<QComboBox*>("comboBoxStopBits");
    m_flowCombo = root->findChild<QComboBox*>("comboBoxFlowControl");

    m_openBtn = root->findChild<QPushButton*>("pushButtonOpenPort");
    m_closeBtn = root->findChild<QPushButton*>("pushButtonClosePort");

    m_terminalEdit = root->findChild<QTextEdit*>("textEditTerminal");
    m_clearBtn = root->findChild<QPushButton*>("pushButtonClearTerminal");

    m_recvModeCombo = root->findChild<QComboBox*>("comboBoxRecvMode");
    m_showEscapesRadio = root->findChild<QRadioButton*>("radioButtonShowEscapes");

    m_autoWrapCheck = root->findChild<QCheckBox*>("checkBoxAutoWrap");
    m_autoWrapMsSpin = root->findChild<QSpinBox*>("spinBoxAutoWrapMs");

    m_sendEdit = root->findChild<QLineEdit*>("lineEditSendInput");
    m_sendModeCombo = root->findChild<QComboBox*>("comboBoxSendMode");
    m_sendBtn = root->findChild<QPushButton*>("pushButtonSend");

    m_timedSendCheck = root->findChild<QCheckBox*>("checkBoxTimedSend");
    m_sendIntervalMsSpin = root->findChild<QSpinBox*>("spinBoxSendIntervalMs");
    m_timedSendToggleBtn = root->findChild<QPushButton*>("pushButtonTimedSendToggle");
    m_sendCountLabel = root->findChild<QLabel*>("labelSendCount");
    m_failCountLabel = root->findChild<QLabel*>("labelFailCount");
}

bool SerialTerminalWidget::isUiComplete() const {
    return m_portCombo && m_refreshPortsBtn &&
           m_baudCombo && m_dataBitsCombo && m_parityCombo && m_stopBitsCombo && m_flowCombo &&
           m_openBtn && m_closeBtn &&
           m_terminalEdit && m_clearBtn &&
           m_recvModeCombo && m_showEscapesRadio &&
           m_autoWrapCheck && m_autoWrapMsSpin &&
           m_sendEdit && m_sendModeCombo && m_sendBtn &&
           m_timedSendCheck && m_sendIntervalMsSpin && m_timedSendToggleBtn &&
           m_sendCountLabel && m_failCountLabel;
}

SerialTerminalWidget::DisplayMode SerialTerminalWidget::recvMode() const {
    if (!m_recvModeCombo) return DisplayMode::ASCII;
    return (m_recvModeCombo->currentText().trimmed().compare("HEX", Qt::CaseInsensitive) == 0)
               ? DisplayMode::HEX : DisplayMode::ASCII;
}

SerialTerminalWidget::DisplayMode SerialTerminalWidget::sendMode() const {
    if (!m_sendModeCombo) return DisplayMode::ASCII;
    return (m_sendModeCombo->currentText().trimmed().compare("HEX", Qt::CaseInsensitive) == 0)
               ? DisplayMode::HEX : DisplayMode::ASCII;
}

bool SerialTerminalWidget::showEscapes() const {
    return m_showEscapesRadio && m_showEscapesRadio->isChecked();
}

bool SerialTerminalWidget::acceptPortPath(const QString &sysPath) {
    // macOS: only keep /dev/cu.* and filter debug-console & Bluetooth
    if (!sysPath.startsWith("/dev/cu.")) return false;
    if (sysPath.contains("debug-console", Qt::CaseInsensitive)) return false;
    if (sysPath.contains("Bluetooth-Incoming-Port", Qt::CaseInsensitive)) return false;
    return true;
}

void SerialTerminalWidget::onRefreshPorts() {
    if (!m_portCombo) return;
    const QString prev = m_portCombo->currentData().toString();

    m_portCombo->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto &p : ports) {
        const QString sys = p.systemLocation();
        if (!acceptPortPath(sys)) continue;

        const QString desc = p.description().isEmpty() ? "No description" : p.description();
        m_portCombo->addItem(QString("%1  (%2)").arg(sys, desc), sys);
    }

    // restore
    if (!prev.isEmpty()) {
        for (int i = 0; i < m_portCombo->count(); ++i) {
            if (m_portCombo->itemData(i).toString() == prev) {
                m_portCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    // prefer usbserial
    if (m_portCombo->currentIndex() < 0) {
        for (int i = 0; i < m_portCombo->count(); ++i) {
            const QString pth = m_portCombo->itemData(i).toString();
            if (pth.contains("usbserial", Qt::CaseInsensitive) ||
                pth.contains("wch", Qt::CaseInsensitive) ||
                pth.contains("slab", Qt::CaseInsensitive) ||
                pth.contains("usbmodem", Qt::CaseInsensitive)) {
                m_portCombo->setCurrentIndex(i);
                break;
            }
        }
        if (m_portCombo->currentIndex() < 0 && m_portCombo->count() > 0) {
            m_portCombo->setCurrentIndex(0);
        }
    }

    logSystem(QString("Ports refreshed: %1").arg(m_portCombo->count()));
    emit statusMessage(QString("已刷新端口：%1").arg(m_portCombo->count()), 3000);
}

void SerialTerminalWidget::setConnectedUi(bool connected) {
    if (!isUiComplete()) return;

    m_openBtn->setEnabled(!connected);
    m_closeBtn->setEnabled(connected);

    m_portCombo->setEnabled(!connected);
    m_refreshPortsBtn->setEnabled(!connected);

    m_baudCombo->setEnabled(!connected);
    m_dataBitsCombo->setEnabled(!connected);
    m_parityCombo->setEnabled(!connected);
    m_stopBitsCombo->setEnabled(!connected);
    m_flowCombo->setEnabled(!connected);

    m_sendBtn->setEnabled(connected);
    m_sendEdit->setEnabled(connected);

    // timed send can be enabled only when connected (you can change if desired)
    m_timedSendCheck->setEnabled(connected);
    m_sendIntervalMsSpin->setEnabled(connected);
    m_timedSendToggleBtn->setEnabled(connected);
}

void SerialTerminalWidget::logSystem(const QString &msg) {
    if (!m_terminalEdit) return;

    // system line in grey, left aligned
    QTextCursor c = m_terminalEdit->textCursor();
    c.movePosition(QTextCursor::End);

    QTextBlockFormat bf;
    bf.setAlignment(Qt::AlignLeft);
    c.insertBlock(bf);

    QTextCharFormat fmt;
    fmt.setForeground(QColor(80,80,80));
    c.insertText(QString("[%1] %2").arg(tsHmsZ(), msg), fmt);

    m_terminalEdit->setTextCursor(c);
    m_terminalEdit->ensureCursorVisible();
}

void SerialTerminalWidget::appendDividerLine() {
    if (!m_terminalEdit) return;

    QTextCursor c = m_terminalEdit->textCursor();
    c.movePosition(QTextCursor::End);

    QTextBlockFormat bf;
    bf.setAlignment(Qt::AlignLeft);   // 建议左对齐，避免居中造成额外边距影响
    c.insertBlock(bf);

    QTextCharFormat fmt;
    fmt.setForeground(QColor(140,140,140));

    const QString prefix = QString("[%1]---").arg(tsHmsZ());

    // 根据可视宽度动态计算 '-' 数量，确保一行不换行
    const int availablePx = m_terminalEdit->viewport()->width();
    QFontMetrics fm(m_terminalEdit->font());

    const int prefixPx = fm.horizontalAdvance(prefix);
    const int dashPx   = fm.horizontalAdvance(QLatin1String("-"));

    int dashCount = 0;
    if (dashPx > 0 && availablePx > prefixPx) {
        dashCount = (availablePx - prefixPx) / dashPx;
    }
    if (dashCount < 0) dashCount = 0;

    const QString line = prefix + QString(dashCount, QLatin1Char('-'));
    c.insertText(line, fmt);

    m_terminalEdit->setTextCursor(c);
    m_terminalEdit->ensureCursorVisible();
}

QString SerialTerminalWidget::renderHexString(const QByteArray &bytes) const {
    QByteArray hex = bytes.toHex(' ').toUpper();
    return QString::fromLatin1(hex);
}

QVector<QPair<QString,bool>> SerialTerminalWidget::renderAsciiSegments(const QByteArray &bytes, bool escapesEnabled) const {
    QVector<QPair<QString,bool>> segs;
    segs.reserve(bytes.size());

    auto push = [&](const QString &s, bool isEsc) {
        if (s.isEmpty()) return;
        if (!segs.isEmpty() && segs.last().second == isEsc) {
            segs.last().first += s;
        } else {
            segs.push_back({s, isEsc});
        }
    };

    for (unsigned char b : bytes) {
        if (!escapesEnabled) {
            // real meaning: allow \n \r \t to act
            push(QString(QChar(b)), false);
            continue;
        }

        // show escapes explicitly
        switch (b) {
        case '\n': push("\\n", true); break;
        case '\r': push("\\r", true); break;
        case '\t': push("\\t", true); break;
        case 0x1B: push("\\x1B", true); break;
        default:
            if (b < 0x20 || b == 0x7F) {
                push(QString("\\x%1").arg(b, 2, 16, QLatin1Char('0')).toUpper(), true);
            } else {
                push(QString(QChar(b)), false);
            }
            break;
        }
    }
    return segs;
}

void SerialTerminalWidget::appendAlignedText(const QString &text, bool isRx, const QColor &color) {
    if (!m_terminalEdit) return;

    QTextCursor c = m_terminalEdit->textCursor();
    c.movePosition(QTextCursor::End);

    QTextBlockFormat bf;
    bf.setAlignment(isRx ? Qt::AlignLeft : Qt::AlignRight);
    c.insertBlock(bf);

    QTextCharFormat fmt;
    fmt.setForeground(color);
    c.insertText(text, fmt);

    m_terminalEdit->setTextCursor(c);
    m_terminalEdit->ensureCursorVisible();
}

void SerialTerminalWidget::appendAlignedSegments(const QVector<QPair<QString,bool>> &segments, bool isRx,
                                                 const QColor &normalColor, const QColor &escapeColor) {
    if (!m_terminalEdit) return;

    QTextCursor c = m_terminalEdit->textCursor();
    c.movePosition(QTextCursor::End);

    QTextBlockFormat bf;
    bf.setAlignment(isRx ? Qt::AlignLeft : Qt::AlignRight);
    c.insertBlock(bf);

    QTextCharFormat fmtN;
    fmtN.setForeground(normalColor);
    QTextCharFormat fmtE;
    fmtE.setForeground(escapeColor);

    for (const auto &seg : segments) {
        c.insertText(seg.first, seg.second ? fmtE : fmtN);
    }

    m_terminalEdit->setTextCursor(c);
    m_terminalEdit->ensureCursorVisible();
}

void SerialTerminalWidget::maybeAutoWrapBeforeNewMessage() {
    if (!m_autoWrapCheck || !m_autoWrapMsSpin) return;
    if (!m_autoWrapCheck->isChecked()) return;

    const int gap = m_autoWrapMsSpin->value();
    const qint64 now = m_mono.elapsed();

    if (m_lastMessageMs >= 0 && (now - m_lastMessageMs) > gap) {
        // insert a blank line as separator
        QTextCursor c = m_terminalEdit->textCursor();
        c.movePosition(QTextCursor::End);
        c.insertBlock();
        m_terminalEdit->setTextCursor(c);
    }
    m_lastMessageMs = now;
}

void SerialTerminalWidget::appendMessage(const QByteArray &bytes, bool isRx) {
    maybeAutoWrapBeforeNewMessage();
    appendDividerLine();

    const auto mode = isRx ? recvMode() : sendMode();

    // colors: RX green-ish, TX blue-ish; escapes orange-ish
    const QColor rxColor(0, 120, 0);
    const QColor txColor(0, 90, 180);
    const QColor escColor(180, 90, 0);

    if (mode == DisplayMode::HEX) {
        // HEX is "ASCII bytes hex representation" for TX; for RX it is raw bytes hex
        const QString s = renderHexString(bytes);
        appendAlignedText(s, isRx, isRx ? rxColor : txColor);
        return;
    }

    // ASCII mode
    if (showEscapes()) {
        const auto segs = renderAsciiSegments(bytes, true);
        appendAlignedSegments(segs, isRx, isRx ? rxColor : txColor, escColor);
        return;
    }

    // interpret escapes (real newline). To keep alignment consistent, we split lines ourselves.
    QByteArray normalized = bytes;
    normalized.replace("\r\n", "\n");

    const QList<QByteArray> lines = normalized.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = QString::fromLatin1(lines[i]);
        appendAlignedText(line, isRx, isRx ? rxColor : txColor);
        if (i != lines.size() - 1) {
            // keep newline as empty aligned block
            appendAlignedText(QString(), isRx, isRx ? rxColor : txColor);
        }
    }
}

QByteArray SerialTerminalWidget::buildTxBytesFromInput(QString *outDisplayAscii) const {
    if (!m_sendEdit) return {};

    // sending is ASCII-only per your spec
    const QString s = m_sendEdit->text();
    if (outDisplayAscii) *outDisplayAscii = s;

    // use Latin1 (1 char -> 1 byte) to match ASCII expectation
    return s.toLatin1();
}

void SerialTerminalWidget::onOpenPort() {
    if (!isUiComplete()) return;

    const QString portPath = m_portCombo->currentData().toString();
    if (portPath.isEmpty()) {
        logSystem("Open failed: no port selected.");
        emit statusMessage("串口打开失败，未选择端口。", 3000);
        return;
    }

    bool ok = false;
    const int baud = m_baudCombo->currentText().trimmed().toInt(&ok);
    if (!ok || baud <= 0) {
        logSystem("Open failed: invalid baud.");
        emit statusMessage("串口打开失败，波特率无效。", 3000);
        return;
    }

    // apply settings
    m_serial.setPortName(portPath);
    m_serial.setBaudRate(baud);

    // data bits
    QSerialPort::DataBits db = QSerialPort::Data8;
    const QString dbs = m_dataBitsCombo->currentText().trimmed();
    if (dbs == "5") db = QSerialPort::Data5;
    else if (dbs == "6") db = QSerialPort::Data6;
    else if (dbs == "7") db = QSerialPort::Data7;
    else db = QSerialPort::Data8;
    m_serial.setDataBits(db);

    // parity
    QSerialPort::Parity par = QSerialPort::NoParity;
    const QString ps = m_parityCombo->currentText().trimmed();
    if (ps.compare("Even", Qt::CaseInsensitive) == 0) par = QSerialPort::EvenParity;
    else if (ps.compare("Odd", Qt::CaseInsensitive) == 0) par = QSerialPort::OddParity;
    else if (ps.compare("Mark", Qt::CaseInsensitive) == 0) par = QSerialPort::MarkParity;
    else if (ps.compare("Space", Qt::CaseInsensitive) == 0) par = QSerialPort::SpaceParity;
    else par = QSerialPort::NoParity;
    m_serial.setParity(par);

    // stop bits
    QSerialPort::StopBits sb = QSerialPort::OneStop;
    const QString ss = m_stopBitsCombo->currentText().trimmed();
    if (ss == "2") sb = QSerialPort::TwoStop;
    else if (ss == "1.5") sb = QSerialPort::OneAndHalfStop;
    else sb = QSerialPort::OneStop;
    m_serial.setStopBits(sb);

    // flow control
    QSerialPort::FlowControl fc = QSerialPort::NoFlowControl;
    const QString fs = m_flowCombo->currentText().trimmed();
    if (fs.contains("RTS", Qt::CaseInsensitive)) fc = QSerialPort::HardwareControl;
    else if (fs.contains("XON", Qt::CaseInsensitive)) fc = QSerialPort::SoftwareControl;
    else fc = QSerialPort::NoFlowControl;
    m_serial.setFlowControl(fc);

    if (!m_serial.open(QIODevice::ReadWrite)) {
        logSystem(QString("Open failed: %1").arg(m_serial.errorString()));
        emit statusMessage(QString("打开失败: %1").arg(m_serial.errorString()), 3000);
        setConnectedUi(false);
        return;
    }

    m_sendCount = 0;
    m_failCount = 0;
    if (m_sendCountLabel) m_sendCountLabel->setText("0");
    if (m_failCountLabel) m_failCountLabel->setText("0");

    m_lastMessageMs = -1;
    logSystem(QString("Opened %1 @%2").arg(portPath).arg(baud));
    emit statusMessage(QString("已打开 %1 @%2").arg(portPath).arg(baud), 3000);
    setConnectedUi(true);
}

void SerialTerminalWidget::onClosePort() {
    if (m_serial.isOpen()) m_serial.close();
    if (m_timedSendTimer.isActive()) m_timedSendTimer.stop();
    if (m_timedSendToggleBtn) m_timedSendToggleBtn->setText("开始");
    logSystem("Closed.");
    emit statusMessage("已关闭。",3000);
    setConnectedUi(false);
}

void SerialTerminalWidget::onReadyRead() {
    if (!m_serial.isOpen()) return;

    const QByteArray data = m_serial.readAll();
    if (data.isEmpty()) return;

    appendMessage(data, /*isRx=*/true);
}

void SerialTerminalWidget::onSendOnce() {
    if (!m_serial.isOpen()) {
        logSystem("Send failed: port not open.");
        emit statusMessage("发送失败，串口未打开。",3000);
        return;
    }

    QString displayAscii;
    const QByteArray bytes = buildTxBytesFromInput(&displayAscii);

    if (bytes.isEmpty()) {
        logSystem("Send skipped: empty input.");
        emit statusMessage("已跳过，输入为空。",3000);
        return;
    }

    const qint64 written = m_serial.write(bytes);
    if (written < 0) {
        ++m_failCount;
        if (m_failCountLabel) m_failCountLabel->setText(QString::number(m_failCount));
        logSystem(QString("Send failed: %1").arg(m_serial.errorString()));
        emit statusMessage(QString("发送失败: %1").arg(m_serial.errorString()),3000);
        return;
    }

    // show in terminal (TX is right aligned)
    appendMessage(bytes, /*isRx=*/false);

    m_sendEdit->clear();        // 发送成功后清空
    m_sendEdit->setFocus();     // 可选：继续聚焦方便连发

    ++m_sendCount;
    if (m_sendCountLabel) m_sendCountLabel->setText(QString::number(m_sendCount));
}

void SerialTerminalWidget::onClearTerminal() {
    if (m_terminalEdit) m_terminalEdit->clear();
    logSystem("Cleared.");
    emit statusMessage("已清空。",3000);
}

void SerialTerminalWidget::onTimedSendToggle() {
    if (!m_serial.isOpen()) {
        logSystem("Timed send: port not open.");
        emit statusMessage("定时发送：端口未打开",3000);
        return;
    }
    if (!m_timedSendCheck || !m_sendIntervalMsSpin || !m_timedSendToggleBtn) return;

    if (!m_timedSendCheck->isChecked()) {
        // checkbox off => stop
        if (m_timedSendTimer.isActive()) m_timedSendTimer.stop();
        m_timedSendToggleBtn->setText("开始");
        logSystem("Timed send stopped.");
        emit statusMessage("定时发送：已停止。",3000);
        return;
    }

    if (m_timedSendTimer.isActive()) {
        m_timedSendTimer.stop();
        m_timedSendToggleBtn->setText("开始");
        logSystem("Timed send stopped.");
        emit statusMessage("定时发送：已停止。",3000);
        return;
    }

    const int interval = m_sendIntervalMsSpin->value();
    m_timedSendTimer.start(interval);
    m_timedSendToggleBtn->setText("停止");
    logSystem(QString("Timed send started: %1 ms").arg(interval));
    emit statusMessage(QString("定时发送已开始: %1 ms").arg(interval),3000);
}

void SerialTerminalWidget::onTimedSendTick() {
    if (!m_serial.isOpen()) {
        ++m_failCount;
        if (m_failCountLabel) m_failCountLabel->setText(QString::number(m_failCount));
        return;
    }

    QString displayAscii;
    const QByteArray bytes = buildTxBytesFromInput(&displayAscii);
    if (bytes.isEmpty()) {
        ++m_failCount;
        if (m_failCountLabel) m_failCountLabel->setText(QString::number(m_failCount));
        return;
    }

    const qint64 written = m_serial.write(bytes);
    if (written < 0) {
        ++m_failCount;
        if (m_failCountLabel) m_failCountLabel->setText(QString::number(m_failCount));
        return;
    }

    appendMessage(bytes, /*isRx=*/false);

    ++m_sendCount;
    if (m_sendCountLabel) m_sendCountLabel->setText(QString::number(m_sendCount));
}

void SerialTerminalWidget::closeIfOpen() {
    if (m_serial.isOpen()) {
        onClosePort();  // 你已有的关闭逻辑：close + stop timer + UI 状态
    }
}
