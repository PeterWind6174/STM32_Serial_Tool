#pragma once

#include <QMainWindow>
#include <QProcess>

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

private:
    Ui::MainWindow *ui = nullptr;

    QProcess *m_proc = nullptr;
    Step m_step = Step::None;

    QString m_currentElfPath;
    QString m_currentBinPath;
    QString m_currentPortPath;
};
