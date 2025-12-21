#include "mainwindow.h"
#include "ui_mainwindow.h"

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

static QString ts() {
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow) {

    ui->setupUi(this);

    setWindowTitle("STM32 Serial Tool");
    setStatus("就绪");

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

    // 波特率下拉：如果你在 UI 里已设置 editable/选项，这段也不会冲突
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

    // 保底加上系统路径（可选但推荐）
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
}

void MainWindow::setStatus(const QString &msg, int timeoutMs) {
    if (ui->statusbar) ui->statusbar->showMessage(msg, timeoutMs);
    else statusBar()->showMessage(msg, timeoutMs);
}

void MainWindow::appendOutput(const QString &text) {
    // 默认输出（用于一般日志）
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

        // 1) 只保留 /dev/cu.*（过滤所有 /dev/tty.*）
        if (!sys.startsWith("/dev/cu.")) {
            continue;
        }

        // 2) 过滤掉 debug-console、蓝牙伪串口
        if (sys.contains("debug-console", Qt::CaseInsensitive)) {
            continue;
        }
        if (sys.contains("Bluetooth-Incoming-Port", Qt::CaseInsensitive)) {
            continue;
        }

        const QString desc = p.description().isEmpty() ? "No description" : p.description();
        const QString label = QString("%1  (%2)").arg(sys, desc);

        ui->comboBoxSerialPort->addItem(label, sys);
    }

    // 3) 恢复之前选择
    if (!prev.isEmpty()) {
        for (int i = 0; i < ui->comboBoxSerialPort->count(); ++i) {
            if (ui->comboBoxSerialPort->itemData(i).toString() == prev) {
                ui->comboBoxSerialPort->setCurrentIndex(i);
                break;
            }
        }
    }

    // 4) 如果没有之前选择，优先自动选中 usbserial（你这类 USB-UART 最常见）
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

    const QString portPath = currentSelectedPortPath();
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

    const QString binPath = fi.absolutePath() + QDir::separator() + fi.completeBaseName() + ".bin";

    m_currentElfPath = elfPath;
    m_currentBinPath = binPath;
    m_currentPortPath = portPath;

    appendOutputColored(QString("\n[%1] Start flashing\n").arg(ts()), QColor(80, 80, 80));
    appendOutputColored(QString("ELF : %1\n").arg(m_currentElfPath), QColor(80, 80, 80));
    appendOutputColored(QString("BIN : %1\n").arg(m_currentBinPath), QColor(80, 80, 80));
    appendOutputColored(QString("PORT: %1\n").arg(m_currentPortPath), QColor(80, 80, 80));
    appendOutputColored(QString("BAUD: %1\n").arg(m_currentBaud), QColor(80, 80, 80));

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
        appendOutputColored(QString("[%1] ERROR: failed to start arm-none-eabi-objcopy. Is it in PATH?\n").arg(ts()),
                            QColor(180, 0, 0));
        setStatus("启动 objcopy 失败：请确认 arm-none-eabi-objcopy 在 PATH 中", 8000);
        setUiEnabled(true);
        m_step = Step::None;
    }
}

void MainWindow::startFlash(const QString &binPath, const QString &portPath) {
    m_step = Step::Flash;

    m_proc->setProgram("/opt/homebrew/bin/stm32flash");
    m_proc->setArguments({
        "-b", QString::number(m_currentBaud),
        "-w", binPath,
        "-v",
        "-g", "0x08000000",
        portPath
    });

    appendOutputColored(QString("\n[%1] Running: stm32flash -b %2 -w \"%3\" -v -g 0x08000000 %4\n")
                            .arg(ts()).arg(m_currentBaud).arg(binPath).arg(portPath),
                        QColor(80, 80, 80));

    setStatus("正在烧录（stm32flash）…");
    m_proc->start();

    if (!m_proc->waitForStarted(2000)) {
        appendOutputColored(QString("[%1] ERROR: failed to start stm32flash. Is it in PATH?\n").arg(ts()),
                            QColor(180, 0, 0));
        setStatus("启动 stm32flash 失败：请确认 stm32flash 在 PATH 中", 8000);
        setUiEnabled(true);
        m_step = Step::None;
    }
}

void MainWindow::onProcReadyStdout() {
    const QByteArray data = m_proc->readAllStandardOutput();
    if (!data.isEmpty()) {
        // stdout：深绿
        appendOutputColored(QString::fromLocal8Bit(data), QColor(0, 120, 0));
    }
}

void MainWindow::onProcReadyStderr() {
    const QByteArray data = m_proc->readAllStandardError();
    if (!data.isEmpty()) {
        // stderr：红色
        appendOutputColored(QString::fromLocal8Bit(data), QColor(180, 0, 0));
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
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            appendOutputColored(QString("[%1] SUCCESS: flash completed.\n").arg(ts()),
                                QColor(0, 120, 0));
            setStatus("烧录成功", 8000);
        } else {
            appendOutputColored(QString("[%1] ERROR: flash failed.\n").arg(ts()),
                                QColor(180, 0, 0));
            setStatus(QString("烧录失败（stm32flash），exitCode=%1").arg(exitCode), 10000);
        }
    }

    m_step = Step::None;
    setUiEnabled(true);
}
