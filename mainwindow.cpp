#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "aboutdialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QtSerialPort/QSerialPortInfo>

#include <QTextCursor>
#include <QTextCharFormat>
#include <QColor>
#include <QFont>
#include <QProcessEnvironment>

#include <QThread>
#include <QRegularExpression>
#include <QtSerialPort/QSerialPort>

#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

static QString ts() {
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
}

// 将 /dev/cu.usbserial-xxx 转成 Qt 的 portName: "cu.usbserial-xxx"
static QString toQtPortName(const QString &sysOrName) {
    if (sysOrName.startsWith("/dev/")) return sysOrName.mid(5);
    return sysOrName;
}

// /dev/cu.xxx -> /dev/tty.xxx
static QString cuToTtyPath(const QString &p) {
    if (p.startsWith("/dev/cu.")) {
        return "/dev/tty." + p.mid(QString("/dev/cu.").size());
    }
    return p;
}

static void sleepMs(int ms) {
    QThread::msleep(static_cast<unsigned long>(ms));
}

static bool setModemBit(int fd, int bit, bool on, QString *err) {
    int flags = bit;
    int rc = on ? ioctl(fd, TIOCMBIS, &flags) : ioctl(fd, TIOCMBIC, &flags);
    if (rc != 0) {
        if (err) *err = QString("ioctl modem bit failed (bit=%1 on=%2)").arg(bit).arg(on);
        return false;
    }
    return true;
}

static bool setDtrRtsByIoctl(qintptr handle, bool dtr, bool rts, QString *err) {
    int fd = static_cast<int>(handle);
    if (fd < 0) {
        if (err) *err = "invalid serial handle";
        return false;
    }
    if (!setModemBit(fd, TIOCM_DTR, dtr, err)) return false;
    if (!setModemBit(fd, TIOCM_RTS, rts, err)) return false;
    return true;
}

// 你已在终端验证成功的 AUTO GPIO 序列
static const char* kAutoGpioSeq = "dtr,-rts,rts,-dtr";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow) {

    ui->setupUi(this);

    setWindowTitle("STM32 Serial Tool");
    setStatus("就绪");

    // 串口调试 Tab
    if (ui->tabSerialTerminal) {
        m_serialTerminal = new SerialTerminalWidget(ui->tabSerialTerminal, this);

        connect(m_serialTerminal, &SerialTerminalWidget::statusMessage,
                this, [this](const QString &msg, int timeoutMs) {
                    this->setStatus(msg, timeoutMs);
                });
    }

    connect(ui->actionAbout_2, &QAction::triggered, this, [this]() {
        AboutDialog dlg(this);
        dlg.exec();
    });

    // 输出区设置：等宽 + 只读
    ui->textEditOutput->setReadOnly(true);
    QFont mono;
    mono.setStyleHint(QFont::Monospace);
#if defined(Q_OS_MAC)
    mono.setFamily("Menlo");
#else
    mono.setFamily("Monospace");
#endif
    ui->textEditOutput->setFont(mono);

    // 波特率下拉
    if (ui->comboBoxBaudRate) {
        ui->comboBoxBaudRate->setEditable(true);
        if (ui->comboBoxBaudRate->count() == 0) {
            ui->comboBoxBaudRate->addItems({
                "9600","19200","38400","57600","115200","230400","460800","921600"
            });
        }
        ui->comboBoxBaudRate->setCurrentText("115200");
    }

    // 连接 UI 事件
    connect(ui->pushButtonBrowse, &QPushButton::clicked, this, &MainWindow::onBrowseElf);
    connect(ui->pushButtonRefreshPorts, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    connect(ui->pushButtonFlash, &QPushButton::clicked, this, &MainWindow::onFlash);

    if (ui->pushButtonClearOutput) {
        connect(ui->pushButtonClearOutput, &QPushButton::clicked, this, &MainWindow::onClearOutput);
    }

    // 进程
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);

    // 补齐 GUI 环境缺失的 PATH（Homebrew on Apple Silicon）
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");

    if (!path.contains("/opt/homebrew/bin"))
        path = "/opt/homebrew/bin:" + path;

    if (!path.contains("/usr/bin"))
        path += ":/usr/bin:/bin:/usr/sbin:/sbin";

    env.insert("PATH", path);
    m_proc->setProcessEnvironment(env);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, &MainWindow::onProcReadyStdout);
    connect(m_proc, &QProcess::readyReadStandardError,  this, &MainWindow::onProcReadyStderr);
    connect(m_proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &MainWindow::onProcFinished);

    // 初始化串口列表
    onRefreshPorts();
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::setUiEnabled(bool enabled) {
    ui->lineEditElfPath->setEnabled(enabled);
    ui->pushButtonBrowse->setEnabled(enabled);

    ui->comboBoxSerialPort->setEnabled(enabled);
    ui->pushButtonRefreshPorts->setEnabled(enabled);

    if (ui->comboBoxBaudRate) ui->comboBoxBaudRate->setEnabled(enabled);

    ui->pushButtonFlash->setEnabled(enabled);
    if (ui->pushButtonClearOutput) ui->pushButtonClearOutput->setEnabled(enabled);

    if (ui->checkBoxAutoBootRun) ui->checkBoxAutoBootRun->setEnabled(enabled);
}

void MainWindow::setStatus(const QString &msg, int timeoutMs) {
    if (ui->statusbar) ui->statusbar->showMessage(msg, timeoutMs);
    else statusBar()->showMessage(msg, timeoutMs);
}

void MainWindow::appendOutput(const QString &text) {
    appendOutputColored(text, QColor(0, 0, 0));
}

void MainWindow::appendOutputColored(const QString &text, const QColor &color) {
    QTextCharFormat fmt;
    fmt.setForeground(color);

    QTextCursor c = ui->textEditOutput->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(text, fmt);

    ui->textEditOutput->setTextCursor(c);
    ui->textEditOutput->ensureCursorVisible();
}

int MainWindow::currentBaudRate() const {
    if (!ui->comboBoxBaudRate) return 115200;

    const QString s = ui->comboBoxBaudRate->currentText().trimmed();
    bool ok = false;
    int baud = s.toInt(&ok);
    if (!ok || baud <= 0) return -1;
    return baud;
}

QString MainWindow::currentSelectedPortPath() const {
    const int idx = ui->comboBoxSerialPort->currentIndex();
    if (idx < 0) return {};
    return ui->comboBoxSerialPort->itemData(idx).toString();
}

void MainWindow::onClearOutput() {
    ui->textEditOutput->clear();
    setStatus("输出已清空", 3000);
}

void MainWindow::onBrowseElf() {
    const QString file = QFileDialog::getOpenFileName(
        this,
        "选择 ELF 文件",
        QDir::homePath(),
        "ELF Files (*.elf);;All Files (*)"
        );
    if (!file.isEmpty()) {
        ui->lineEditElfPath->setText(file);
        setStatus("已选择 ELF 文件", 3000);
    }
}

void MainWindow::onRefreshPorts() {
    const QString prev = currentSelectedPortPath();

    ui->comboBoxSerialPort->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto &p : ports) {
        const QString sys = p.systemLocation();

        // 只保留 /dev/cu.*，过滤 /dev/tty.*
        if (!sys.startsWith("/dev/cu.")) continue;

        if (sys.contains("debug-console", Qt::CaseInsensitive)) continue;
        if (sys.contains("Bluetooth-Incoming-Port", Qt::CaseInsensitive)) continue;

        const QString desc = p.description().isEmpty() ? "No description" : p.description();
        const QString label = QString("%1  (%2)").arg(sys, desc);

        ui->comboBoxSerialPort->addItem(label, sys);
    }

    // 恢复之前选择
    if (!prev.isEmpty()) {
        for (int i = 0; i < ui->comboBoxSerialPort->count(); ++i) {
            if (ui->comboBoxSerialPort->itemData(i).toString() == prev) {
                ui->comboBoxSerialPort->setCurrentIndex(i);
                break;
            }
        }
    }

    // 优先自动选中 usbserial
    if (ui->comboBoxSerialPort->currentIndex() < 0) {
        for (int i = 0; i < ui->comboBoxSerialPort->count(); ++i) {
            const QString path = ui->comboBoxSerialPort->itemData(i).toString();
            if (path.contains("usbserial", Qt::CaseInsensitive)) {
                ui->comboBoxSerialPort->setCurrentIndex(i);
                break;
            }
        }
        if (ui->comboBoxSerialPort->currentIndex() < 0 && ui->comboBoxSerialPort->count() > 0) {
            ui->comboBoxSerialPort->setCurrentIndex(0);
        }
    }

    appendOutputColored(QString("[%1] Ports refreshed: %2 found.\n")
                            .arg(ts()).arg(ui->comboBoxSerialPort->count()),
                        QColor(80, 80, 80));
    setStatus(QString("串口列表已刷新：%1 个").arg(ui->comboBoxSerialPort->count()), 4000);
}

void MainWindow::onFlash() {
    if (m_proc->state() != QProcess::NotRunning) {
        setStatus("已有任务在运行中，请等待完成", 5000);
        return;
    }

    if (m_serialTerminal) {
        m_serialTerminal->closeIfOpen();
    }

    const QString elfPath = ui->lineEditElfPath->text().trimmed();
    if (elfPath.isEmpty()) {
        setStatus("请先选择 ELF 文件", 6000);
        return;
    }
    QFileInfo fi(elfPath);
    if (!fi.exists() || !fi.isFile()) {
        setStatus("ELF 路径无效或文件不存在", 6000);
        return;
    }

    // 注意：这里必须是 QString（不是 const），AUTO 时会改为 tty
    QString portPath = currentSelectedPortPath();
    if (portPath.isEmpty()) {
        setStatus("请先选择串口设备", 6000);
        return;
    }

    const int baud = currentBaudRate();
    if (baud <= 0) {
        setStatus("波特率无效，请输入正确的数字（如 115200）", 8000);
        return;
    }
    m_currentBaud = baud;

    // 读取自动模式
    m_autoBootRun = (ui->checkBoxAutoBootRun && ui->checkBoxAutoBootRun->isChecked());

    // AUTO 模式：强制使用 tty 设备（macOS 上 GPIO 控制线更可靠）
    if (m_autoBootRun) {
        const QString tty = cuToTtyPath(portPath);
        if (tty != portPath) {
            appendOutputColored(QString("[%1] AUTO uses tty device: %2 (from %3)\n")
                                    .arg(ts(), tty, portPath),
                                    QColor(80, 80, 80));
            portPath = tty;
        }
    }

    const QString binPath = fi.absolutePath() + QDir::separator() + fi.completeBaseName() + ".bin";

    m_currentElfPath = elfPath;
    m_currentBinPath = binPath;
    m_currentPortPath = portPath;

    appendOutputColored(QString("\n[%1] Start flashing\n").arg(ts()), QColor(80, 80, 80));
    appendOutputColored(QString("ELF : %1\n").arg(m_currentElfPath), QColor(80, 80, 80));
    appendOutputColored(QString("BIN : %1\n").arg(m_currentBinPath), QColor(80, 80, 80));
    appendOutputColored(QString("PORT: %1\n").arg(m_currentPortPath), QColor(80, 80, 80));
    appendOutputColored(QString("BAUD: %1\n").arg(m_currentBaud), QColor(80, 80, 80));
    appendOutputColored(QString("AUTO: %1\n").arg(m_autoBootRun ? "ON" : "OFF"), QColor(80, 80, 80));
    if (m_autoBootRun) {
        appendOutputColored(QString("AUTOSEQ: %1\n").arg(kAutoGpioSeq), QColor(80, 80, 80));
    }

    setUiEnabled(false);
    setStatus("正在生成 BIN（objcopy）…");
    startObjcopy(m_currentElfPath, m_currentBinPath);
}

void MainWindow::startObjcopy(const QString &elfPath, const QString &binPath) {
    m_step = Step::Objcopy;

    m_proc->setProgram("/opt/homebrew/bin/arm-none-eabi-objcopy");
    m_proc->setArguments({ "-O", "binary", elfPath, binPath });

    appendOutputColored(QString("\n[%1] Running: arm-none-eabi-objcopy -O binary \"%2\" \"%3\"\n")
                            .arg(ts(), elfPath, binPath),
                        QColor(80, 80, 80));

    m_proc->start();
    if (!m_proc->waitForStarted(2000)) {
        appendOutputColored(QString("[%1] ERROR: failed to start arm-none-eabi-objcopy.\n").arg(ts()),
                            QColor(180, 0, 0));
        setStatus("启动 objcopy 失败：请确认 arm-none-eabi-objcopy 在 PATH 中", 8000);
        setUiEnabled(true);
        m_step = Step::None;
    }
}

void MainWindow::startFlash(const QString &binPath, const QString &portPath) {
    m_step = Step::Flash;

    m_proc->setProgram("/opt/homebrew/bin/stm32flash");

    // 收集本次 stm32flash 输出
    m_procAllText.clear();

    QStringList args;
    args << "-b" << QString::number(m_currentBaud);

    // AUTO 模式：直接使用你验证成功的 stm32flash GPIO 序列
    if (m_autoBootRun) {
        args << "-i" << kAutoGpioSeq;
    }

    args << "-w" << binPath
         << "-v"
         << "-g" << "0x08000000"
         << portPath;

    m_proc->setArguments(args);

    appendOutputColored(QString("\n[%1] Running: stm32flash %2\n")
                            .arg(ts(), args.join(' ')),
                        QColor(80, 80, 80));

    setStatus("正在烧录（stm32flash）…");
    m_proc->start();

    if (!m_proc->waitForStarted(2000)) {
        appendOutputColored(QString("[%1] ERROR: failed to start stm32flash.\n").arg(ts()),
                            QColor(180, 0, 0));
        setStatus("启动 stm32flash 失败：请确认 stm32flash 在 PATH 中", 8000);
        setUiEnabled(true);
        m_step = Step::None;
    }
}

void MainWindow::onProcReadyStdout() {
    const QByteArray data = m_proc->readAllStandardOutput();
    if (!data.isEmpty()) {
        const QString s = QString::fromLocal8Bit(data);
        m_procAllText += s;
        appendOutputColored(s, QColor(0, 120, 0));   // stdout：绿
    }
}

void MainWindow::onProcReadyStderr() {
    const QByteArray data = m_proc->readAllStandardError();
    if (!data.isEmpty()) {
        const QString s = QString::fromLocal8Bit(data);
        m_procAllText += s;
        appendOutputColored(s, QColor(180, 0, 0));   // stderr：红
    }
}

void MainWindow::onProcFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    const QString st = (exitStatus == QProcess::NormalExit) ? "NormalExit" : "CrashExit";
    appendOutputColored(QString("\n[%1] Process finished: %2, exitCode=%3\n")
                            .arg(ts(), st).arg(exitCode),
                        QColor(80, 80, 80));

    if (m_step == Step::Objcopy) {
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            setStatus("BIN 已生成，准备开始烧录…", 3000);
            startFlash(m_currentBinPath, m_currentPortPath);
            return;
        } else {
            appendOutputColored(QString("[%1] ERROR: objcopy failed. Abort.\n").arg(ts()),
                                QColor(180, 0, 0));
            setStatus("生成 BIN 失败（objcopy）", 8000);
        }

    } else if (m_step == Step::Flash) {

        // 不管成功/失败：都输出一次芯片信息（来自 stm32flash 输出）
        appendChipInfoFromText(m_procAllText);

        const bool ok = (exitStatus == QProcess::NormalExit && exitCode == 0);
        if (ok) {
            appendOutputColored(QString("[%1] SUCCESS: flash completed.\n").arg(ts()),
                                QColor(0, 120, 0));
            setStatus("烧录成功", 6000);
        } else {
            appendOutputColored(QString("[%1] ERROR: flash failed.\n").arg(ts()),
                                QColor(180, 0, 0));
            setStatus(QString("烧录失败（stm32flash），exitCode=%1").arg(exitCode), 12000);
        }
    }

    m_step = Step::None;
    setUiEnabled(true);
}

/* ---------------------------
 *  以下两个函数当前不再用于 AUTO（因为已改为 stm32flash -i 序列）
 *  先保留，避免你 mainwindow.h 里已有声明导致链接错误。
 *  你后续想清理的话：把 mainwindow.h 里的声明也一起删掉即可。
 * --------------------------- */

bool MainWindow::enterBootloaderByDtrRts(const QString &portPath, int baud, QString *err) {
    Q_UNUSED(portPath);
    Q_UNUSED(baud);
    if (err) *err = "Deprecated: AUTO uses stm32flash -i sequence now.";
    return false;
}

bool MainWindow::resetToRunByDtrRts(const QString &portPath, int baud, QString *err) {
    Q_UNUSED(portPath);
    Q_UNUSED(baud);
    if (err) *err = "Deprecated: AUTO uses stm32flash -g / -i sequence now.";
    return false;
}

/* ---------------------------
 *  从 stm32flash 输出中提取芯片信息
 * --------------------------- */
void MainWindow::appendChipInfoFromText(const QString &text) {
    // 目标字段
    QString versionHex;
    QString opt1Hex;
    QString opt2Hex;

    QString devId;
    QString devIdDesc;

    QString ram;
    QString flash;
    QString optionRam;
    QString systemRam;

    auto firstMatch = [&](const QRegularExpression &re) -> QRegularExpressionMatch {
        auto it = re.globalMatch(text);
        if (it.hasNext()) return it.next();
        return {};
    };

    auto cap = [&](const QString &pattern, int group = 1) -> QString {
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        auto m = firstMatch(re);
        return m.hasMatch() ? m.captured(group).trimmed() : QString();
    };

    // --- 1) 顶部 Key:Value ---
    // Version      : 0x22
    versionHex = cap(R"(Version\s*:\s*(0x[0-9a-fA-F]+))");

    // Option 1     : 0x00
    opt1Hex = cap(R"(Option\s*1\s*:\s*(0x[0-9a-fA-F]+))");

    // Option 2     : 0x00
    opt2Hex = cap(R"(Option\s*2\s*:\s*(0x[0-9a-fA-F]+))");

    // Device ID    : 0x0410 (STM32F10xxx Medium-density)
    {
        QRegularExpression re(
            R"(Device\s+ID\s*:\s*(0x[0-9a-fA-F]+)(?:\s*\(([^)]+)\))?)",
            QRegularExpression::CaseInsensitiveOption
            );
        auto m = firstMatch(re);
        if (m.hasMatch()) {
            devId = m.captured(1).trimmed();
            devIdDesc = m.captured(2).trimmed();
        }
    }

    // 兼容其他输出：Chip ID / PID
    if (devId.isEmpty()) {
        QRegularExpression re(
            R"((Chip\s+ID|PID)\s*:\s*(0x[0-9a-fA-F]+))",
            QRegularExpression::CaseInsensitiveOption
            );
        auto m = firstMatch(re);
        if (m.hasMatch()) devId = m.captured(2).trimmed();
    }

    // --- 2) 列表项：- RAM / - Flash / - Option RAM / - System RAM ---
    // - RAM        : Up to 20KiB  (512b reserved by bootloader)
    ram = cap(R"(-\s*RAM\s*:\s*([^\r\n]+))");

    // - Flash      : Up to 128KiB (size first sector: 4x1024)
    flash = cap(R"(-\s*Flash\s*:\s*([^\r\n]+))");

    // - Option RAM : 16b
    optionRam = cap(R"(-\s*Option\s*RAM\s*:\s*([^\r\n]+))");

    // - System RAM : 2KiB
    systemRam = cap(R"(-\s*System\s*RAM\s*:\s*([^\r\n]+))");

    // --- 输出 ---
    appendOutputColored(QString("\n[%1] Target info (stm32flash):\n").arg(ts()),
                        QColor(80, 80, 80));

    bool any = false;

    auto emitLine = [&](const QString &k, const QString &v) {
        if (v.isEmpty()) return;
        appendOutputColored(QString("  %-11s %1\n").arg(v).arg(k), QColor(80, 80, 80)); // 这行不对齐也没关系
    };

    // 为了更清晰，直接手动格式化每行
    if (!versionHex.isEmpty()) { appendOutputColored(QString("  Version    : %1\n").arg(versionHex), QColor(80,80,80)); any = true; }
    if (!opt1Hex.isEmpty())    { appendOutputColored(QString("  Option 1   : %1\n").arg(opt1Hex),    QColor(80,80,80)); any = true; }
    if (!opt2Hex.isEmpty())    { appendOutputColored(QString("  Option 2   : %1\n").arg(opt2Hex),    QColor(80,80,80)); any = true; }

    if (!devId.isEmpty()) {
        if (!devIdDesc.isEmpty())
            appendOutputColored(QString("  Device ID  : %1 (%2)\n").arg(devId, devIdDesc), QColor(80,80,80));
        else
            appendOutputColored(QString("  Device ID  : %1\n").arg(devId), QColor(80,80,80));
        any = true;
    }

    if (!ram.isEmpty())      { appendOutputColored(QString("  RAM        : %1\n").arg(ram),      QColor(80,80,80)); any = true; }
    if (!flash.isEmpty())    { appendOutputColored(QString("  Flash      : %1\n").arg(flash),    QColor(80,80,80)); any = true; }
    if (!optionRam.isEmpty()){ appendOutputColored(QString("  Option RAM : %1\n").arg(optionRam),QColor(80,80,80)); any = true; }
    if (!systemRam.isEmpty()){ appendOutputColored(QString("  System RAM : %1\n").arg(systemRam),QColor(80,80,80)); any = true; }

    if (!any) {
        appendOutputColored("  (No parsable device info found in stm32flash output)\n",
                            QColor(180, 90, 0));
    }
}
