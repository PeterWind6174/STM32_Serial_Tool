#pragma once

#include <QMainWindow>
#include <QProcess>
#include <QtSerialPort/QSerialPort>
#include "serial_terminal_widget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onBrowseElf();
    void onRefreshPorts();
    void onFlash();

    void onProcReadyStdout();
    void onProcReadyStderr();
    void onProcFinished(int exitCode, QProcess::ExitStatus exitStatus);

    void onClearOutput();

private:
    enum class Step { None, Objcopy, Flash };

    void setUiEnabled(bool enabled);
    void appendOutput(const QString &text);
    QString currentSelectedPortPath() const;

    void startObjcopy(const QString &elfPath, const QString &binPath);
    void startFlash(const QString &binPath, const QString &portPath);
    void setStatus(const QString &msg, int timeoutMs = 0);

    void appendOutputColored(const QString &text, const QColor &color);
    int currentBaudRate() const;
    int m_currentBaud = 115200;

    SerialTerminalWidget *m_serialTerminal = nullptr;

    // 是否启用“一键烧录并复位运行”
    bool m_autoBootRun = false;

    // 收集当前 QProcess 输出（用于解析芯片信息）
    QString m_procAllText;

    // 控制线序列（RTS=BOOT0, DTR=RESET）
    bool enterBootloaderByDtrRts(const QString &portPath, int baud, QString *err = nullptr);
    bool resetToRunByDtrRts(const QString &portPath, int baud, QString *err = nullptr);

    // 从 stm32flash 输出里提取信息（Device ID / Bootloader ver 等）
    void appendChipInfoFromText(const QString &text);

private:
    Ui::MainWindow *ui = nullptr;

    QProcess *m_proc = nullptr;
    Step m_step = Step::None;

    QString m_currentElfPath;
    QString m_currentBinPath;
    QString m_currentPortPath;
};
