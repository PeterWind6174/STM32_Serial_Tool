#include "aboutdialog.h"
#include "ui_aboutdialog.h"

#include <QApplication>
#include <QIcon>
#include <QPixmap>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AboutDialog) {
    ui->setupUi(this);

    setWindowTitle("About");
    setModal(true);

    // 复用应用图标（Dock/窗口图标）。确保你在 main.cpp 里已设置 a.setWindowIcon(...)
    setWindowIcon(qApp->windowIcon());

    // 左侧图标（优先用应用图标）
    QPixmap pm = qApp->windowIcon().pixmap(96, 96);
    if (!pm.isNull()) {
        ui->labelIcon->setPixmap(pm);
    } else {
        // 如果你更想强制用资源图标，改成：
        // ui->labelIcon->setPixmap(QPixmap(":/icons/app.png").scaled(96,96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    ui->labelName->setText("STM32 Serial Tool");
    ui->labelVersion->setText("Version: 2.0.3");
    ui->labelAuthor->setText("Author: Ventus Tu");

    // GitHub 链接（可点击）
    ui->labelGithub->setText(
        "<a href=\"https://github.com/PeterWind6174/STM32_Serial_Tool\">"
        "https://github.com/PeterWind6174/STM32_Serial_Tool"
        "</a>"
        );
    ui->labelGithub->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->labelGithub->setOpenExternalLinks(true);

    connect(ui->pushButtonClose, &QPushButton::clicked, this, &QDialog::accept);
}

AboutDialog::~AboutDialog() {
    delete ui;
}
