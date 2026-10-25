#define SDL_MAIN_HANDLED
#include <QApplication>
#include <cstdio>
#include "simple64-input-qt/configdialog.cpp"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    QSettings profiles(dir.filePath("profiles.ini"), QSettings::IniFormat);
    QSettings ports(dir.filePath("ports.ini"), QSettings::IniFormat);
    profiles.setValue("Pad/Deadzone", 12.5);
    profiles.setValue("Other/Deadzone", 12.5);
    ports.setValue("Controller1/Profile", "Pad");
    ports.setValue("Controller1/Gamepad", "Path=old-path|Name=Pad");
    ports.setValue("Controller2/Gamepad", "Path=second-path|Name=Pad");
    const QString identity = "Path64=" + QString::fromLatin1(QByteArray("new-path").toBase64()) + "|Name=Pad";
    ControllerTab tab(1, &profiles, &ports, nullptr);
    QComboBox *devices = nullptr;
    for (auto *box : tab.findChildren<QComboBox*>())
        if (box->findData("None") >= 0) devices = box;
    if (!devices) return 2;
    devices->addItem("Test pad", identity);
    devices->setCurrentIndex(devices->count() - 1);
    if (ports.value("Controller1/Gamepad") != identity || profiles.value("Pad/Controller1/Gamepad") != identity) return 3;
    tab.refreshGamepadChoices();
    if (ports.value("Controller1/Gamepad") != identity) return 4;
    tab.profileSelect->setCurrentText("Other");
    devices->setCurrentIndex(devices->findData("None"));
    tab.profileSelect->setCurrentText("Pad");
    if (ports.value("Controller1/Gamepad") != identity) return 5;
    if (assignedPort("Path64=" + QString::fromLatin1(QByteArray("SECOND-PATH").toBase64()), &ports, 1) != 2) return 6;
    tab.profileSelect->setCurrentText("Auto");
    devices->setCurrentIndex(devices->findData("None"));
    if (ports.value("Controller1/Gamepad") != "None" || profiles.contains("Auto/Controller1/Gamepad")) return 7;
    if (ports.value("Controller2/Gamepad") != "Path=second-path|Name=Pad") return 8;
    puts("PASS: assignment, profile restore, refresh preservation, legacy-path duplicate detection, None/Auto, and other-port preservation");

    QStringList savedIdentities;
    for (int port = 1; port <= 4; ++port)
    {
        const QString key = QString("Controller%1/").arg(port);
        const QString saved = "Path64=" + QString::fromLatin1(QString("pad-%1").arg(port).toUtf8().toBase64()) + "|Name=Identical Pad";
        ports.setValue(key + "Gamepad", saved);
        ports.setValue(key + "Profile", "Auto");
        ports.setValue(key + "Pak", "Memory");
        savedIdentities.append(saved);
    }
    {
        ConfigDialog dialog(nullptr, &profiles, &ports);
        auto *tabs = dialog.findChild<QTabWidget*>();
        if (!tabs || tabs->count() != 5 || tabs->tabText(4) != "Profiles") return 9;
        for (int port = 1; port <= 4; ++port)
        {
            if (tabs->tabText(port - 1) != QString("Player %1").arg(port)) return 10;
            int assignButtons = 0;
            for (auto *button : tabs->widget(port - 1)->findChildren<QPushButton*>())
                if (button->text() == "Assign Controller...") ++assignButtons;
            if (assignButtons != 1) return 11;
            if (ports.value(QString("Controller%1/Gamepad").arg(port)) != savedIdentities.at(port - 1)) return 12;
        }
    }
    ports.sync();
    QSettings reopenedPorts(dir.filePath("ports.ini"), QSettings::IniFormat);
    for (int port = 1; port <= 4; ++port)
        if (reopenedPorts.value(QString("Controller%1/Gamepad").arg(port)) != savedIdentities.at(port - 1)) return 13;
    puts("PASS: no Debug tab; all four Assign Controller buttons retained; saved physical IDs survive dialog opening and settings reload");
    return 0;
}
