#include "configdialog.h"
#include "funcs.h"
#include "qt2sdl2.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QKeyEvent>
#include <QNetworkReply>
#include <QProcess>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QThread>
#include <QTimer>
#include <QDialogButtonBox>
#include <QSignalBlocker>

static bool isHiddenProfile(const QString &profile)
{
    return profile == "Auto-Gamepad" ||
           profile == "Auto-Keyboard" ||
           profile == "8BitDo-64-Gamepad" ||
           profile == "Gamepad-C-Clicks" ||
           profile == "N64 Controller Preset";
}

static void populateProfileCombo(QComboBox *box, QSettings *settings, bool includeAuto)
{
    const QSignalBlocker blocker(box);
    const QString current = box->currentText();
    box->clear();
    if (includeAuto)
        box->addItem("Auto");

    const QStringList groups = settings->childGroups();
    for (const QString &group : groups)
    {
        if (!isHiddenProfile(group))
            box->addItem(group);
    }

    if (!current.isEmpty() && box->findText(current) != -1)
        box->setCurrentText(current);
    else if (includeAuto)
        box->setCurrentText("Auto");
}

static void setJoystickButtonMapping(CustomButton *button, int joystickButton)
{
    button->type = 4;
    button->joystick_button = joystickButton;
    button->setText("Button " + QString::number(joystickButton));
}

static void setGameControllerButtonMapping(CustomButton *button, SDL_GameControllerButton controllerButton)
{
    button->type = 1;
    button->button = controllerButton;
    button->setText(SDL_GameControllerGetStringForButton(controllerButton));
}

static void setJoystickAxisMapping(CustomButton *button, int joystickAxis, int axisValue)
{
    button->type = 5;
    button->joystick_axis = joystickAxis;
    button->axisValue = axisValue;
    const QString direction = axisValue > 0 ? " +" : " -";
    button->setText("Axis " + QString::number(joystickAxis) + direction);
}

static QString shortControllerPath(QString path)
{
    if (path.isEmpty())
        return QStringLiteral("unknown");

    path = QDir::fromNativeSeparators(path);
    const int marker = path.lastIndexOf('/');
    if (marker != -1 && marker + 1 < path.size())
        return path.mid(marker + 1);

    return path.right(28);
}

static QString controllerSettingForIndex(int index)
{
    const QString name = QString::fromUtf8(SDL_JoystickNameForIndex(index));
    const char *pathChars = SDL_JoystickPathForIndex(index);
    const QString path = pathChars ? QString::fromUtf8(pathChars) : QString();

    if (!path.isEmpty())
        return QStringLiteral("Path64=%1|Name=%2").arg(QString::fromLatin1(path.toUtf8().toBase64()), name);

    return QStringLiteral("%1:%2").arg(index).arg(name);
}

static bool parsePathBoundControllerSetting(const QString &setting, QString *path, QString *name)
{
    const QString pathPrefix64 = QStringLiteral("Path64=");
    const QString pathPrefix = QStringLiteral("Path=");
    if (setting.startsWith(pathPrefix64))
    {
        const int nameIndex = setting.indexOf(QStringLiteral("|Name="));
        const QString encodedPath = (nameIndex == -1)
            ? setting.mid(pathPrefix64.size())
            : setting.mid(pathPrefix64.size(), nameIndex - pathPrefix64.size());
        if (path != nullptr)
            *path = QString::fromUtf8(QByteArray::fromBase64(encodedPath.toLatin1()));
        if (name != nullptr)
            *name = (nameIndex == -1) ? QString() : setting.mid(nameIndex + 6);
        return true;
    }

    if (setting.startsWith(pathPrefix))
    {
        const int nameIndex = setting.indexOf(QStringLiteral("|Name="));
        if (nameIndex == -1)
        {
            if (path != nullptr)
                *path = setting.mid(pathPrefix.size());
            if (name != nullptr)
                name->clear();
            return true;
        }

        if (path != nullptr)
            *path = setting.mid(pathPrefix.size(), nameIndex - pathPrefix.size());
        if (name != nullptr)
            *name = setting.mid(nameIndex + 6);
        return true;
    }

    return false;
}

static QString displayLabelForControllerSetting(const QString &setting)
{
    QString path;
    QString name;
    if (parsePathBoundControllerSetting(setting, &path, &name))
    {
        const QString labelName = name.isEmpty() ? QStringLiteral("Controller") : name;
        return QStringLiteral("%1 (%2)").arg(labelName, shortControllerPath(path));
    }

    return setting;
}

static void populateGamepadCombo(QComboBox *box, unsigned int controllerIndex, const QString &savedSetting)
{
    const QSignalBlocker blocker(box);
    box->clear();
    box->addItem(QStringLiteral("Auto"), QStringLiteral("Auto"));

    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        const QString value = controllerSettingForIndex(i);
        const QString label = QStringLiteral("[%1] %2").arg(i).arg(displayLabelForControllerSetting(value));
        box->addItem(label, value);
    }

    box->addItem(QStringLiteral("Keyboard"), QStringLiteral("Keyboard"));
    if (controllerIndex == 4)
        box->addItem(QStringLiteral("Emulate VRU"), QStringLiteral("Emulate VRU"));
    box->addItem(QStringLiteral("None"), QStringLiteral("None"));

    int settingIndex = box->findData(savedSetting);
    if (settingIndex == -1)
    {
        if (!savedSetting.isEmpty() &&
            savedSetting != QStringLiteral("Auto") &&
            savedSetting != QStringLiteral("Keyboard") &&
            savedSetting != QStringLiteral("Emulate VRU") &&
            savedSetting != QStringLiteral("None"))
        {
            box->addItem(QStringLiteral("Saved: %1").arg(displayLabelForControllerSetting(savedSetting)), savedSetting);
            settingIndex = box->count() - 1;
        }
        else
        {
            settingIndex = box->findData(QStringLiteral("Auto"));
        }
    }

    box->setCurrentIndex(settingIndex);
}

static int assignedPort(const QString &setting, QSettings *settings, unsigned int ownPort)
{
    QString path;
    if (!parsePathBoundControllerSetting(setting, &path, nullptr) || path.isEmpty())
        return 0;
    for (unsigned int port = 1; port <= 4; ++port)
    {
        QString other;
        parsePathBoundControllerSetting(settings->value(QString("Controller%1/Gamepad").arg(port)).toString(), &other, nullptr);
        if (port != ownPort && path.compare(other, Qt::CaseInsensitive) == 0)
            return port;
    }
    return 0;
}

static QString captureController(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Assign physical controller");
    QVBoxLayout layout(&dialog);
    QLabel label("Press and release a button on the intended controller.\n"
                 "Controllers must be connected before opening this dialog.\n"
                 "Cancel or wait 30 seconds to keep the current assignment.", &dialog);
    layout.addWidget(&label);
    QDialogButtonBox buttons(QDialogButtonBox::Cancel, &dialog);
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QList<SDL_Joystick*> pads;
    QList<QString> identities;
    QList<QList<bool>> previous;
    SDL_JoystickUpdate();
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        const QString identity = controllerSettingForIndex(i);
        if (!identity.startsWith("Path64="))
            continue;
        SDL_Joystick *pad = SDL_JoystickOpen(i);
        if (!pad)
            continue;
        pads.append(pad);
        identities.append(identity);
        QList<bool> held;
        for (int button = 0; button < SDL_JoystickNumButtons(pad); ++button)
            held.append(SDL_JoystickGetButton(pad, button) != 0);
        previous.append(held);
    }
    if (pads.isEmpty())
    {
        QMessageBox::information(parent, "Assign Controller", "No connected controllers with stable device paths were found.");
        return {};
    }
    QString selected;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &dialog, [&]() {
        SDL_JoystickUpdate();
        for (int i = 0; i < pads.size(); ++i)
        {
            if (!SDL_JoystickGetAttached(pads[i]))
                continue;
            for (int button = 0; button < previous[i].size(); ++button)
            {
                const bool down = SDL_JoystickGetButton(pads[i], button) != 0;
                if (down && !previous[i][button])
                {
                    selected = identities[i];
                    dialog.accept();
                    return;
                }
                previous[i][button] = down;
            }
        }
    });
    poll.start(20);
    QTimer::singleShot(30000, &dialog, &QDialog::reject);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    poll.stop();
    for (SDL_Joystick *pad : pads)
        SDL_JoystickClose(pad);
    return accepted ? selected : QString();
}

void ControllerTab::handleSslErrors(QNetworkReply *reply, const QList<QSslError> &errors) {
    reply->ignoreSslErrors();
}

void ControllerTab::fileDownloaded(QNetworkReply* pReply) {
    if (pReply->error())
    {
        progress->hide();
        pReply->deleteLater();
        gamepadSelect->setCurrentText("Auto");
        return;
    }
    QTemporaryDir dir;
    QFile file(dir.filePath("vosk-model.zip"));
    if (!file.open(QIODevice::WriteOnly))
    {
        progress->hide();
        pReply->deleteLater();
        gamepadSelect->setCurrentText("Auto");
        return;
    }
    file.write(pReply->readAll());
    file.close();
    QThread::sleep(3);

    QProcess process;
    process.setProgram("7za");
    QString output_path = "-o" + QString(ConfigGetUserDataPath());
    QStringList arguments = { "x", dir.filePath("vosk-model.zip"), output_path };
    process.setArguments(arguments);
    process.start();
    bool success = process.waitForFinished(-1);

    progress->setValue(100);
    QMessageBox msgBox;
    if (success)
        msgBox.setText("Successfully extracted voice model");
    else
    {
       msgBox.setText("Failed to extract voice model");
       gamepadSelect->setCurrentText("Auto");
    }
    msgBox.exec();
    pReply->deleteLater();
}

ControllerTab::ControllerTab(unsigned int controller, QSettings* settings, QSettings* controllerSettings, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_controllerSettings(controllerSettings)
{
    connect(&modelDownloader, &QNetworkAccessManager::finished, this, &ControllerTab::fileDownloaded);
    connect(&modelDownloader, &QNetworkAccessManager::sslErrors, this, &ControllerTab::handleSslErrors);
    QGridLayout *layout = new QGridLayout(this);
    QLabel *profileLabel = new QLabel("Profile", this);
    layout->addWidget(profileLabel, 0, 0);

    profileSelect = new QComboBox(this);
    profileSelect->insertItem(0, "Auto");
    populateProfileCombo(profileSelect, settings, true);
    profileSelect->setCurrentText(controllerSettings->value("Controller" + QString::number(controller) + "/Profile").toString());
    connect(profileSelect, &QComboBox::currentTextChanged, [=](QString text) {
        controllerSettings->setValue("Controller" + QString::number(controller) + "/Profile", text);
        const QString saved = settings->value(text + QString("/Controller%1/Gamepad").arg(controller)).toString();
        if (text != "Auto" && !saved.isEmpty())
        {
            const int other = assignedPort(saved, controllerSettings, controller);
            if (other)
                QMessageBox::warning(this, "Controller already assigned", QString("This profile's controller belongs to Player %1. The current device assignment was kept.").arg(other));
            else
                controllerSettings->setValue(QString("Controller%1/Gamepad").arg(controller), saved);
            refreshGamepadChoices();
        }
    });
    layout->addWidget(profileSelect, 0, 1);

    QLabel *gamepadLabel = new QLabel("Gamepad", this);
    layout->addWidget(gamepadLabel, 1, 0);

    gamepadSelect = new QComboBox(this);
    refreshGamepadChoices();
    connect(gamepadSelect, qOverload<int>(&QComboBox::currentIndexChanged), [=](int index) {
        QString setting = gamepadSelect->itemData(index).toString();
        if (setting.isEmpty())
            setting = gamepadSelect->itemText(index);
        const int other = assignedPort(setting, controllerSettings, controller);
        if (other)
        {
            QMessageBox::warning(this, "Controller already assigned", QString("This controller belongs to Player %1. Change that player's assignment first.").arg(other));
            refreshGamepadChoices();
            return;
        }
        controllerSettings->setValue("Controller" + QString::number(controller) + "/Gamepad", setting);
        if (profileSelect->currentText() != "Auto")
            settings->setValue(profileSelect->currentText() + QString("/Controller%1/Gamepad").arg(controller), setting);
        controllerSettings->sync();
        settings->sync();
        if (setting == "Emulate VRU")
        {
            if (!QFile::exists(QDir(ConfigGetUserDataPath()).filePath("vosk-model-small-en-us-0.15/conf/mfcc.conf")))
            {
                QNetworkRequest request(QUrl("https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"));
                request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                progress = new QProgressDialog("Downloading voice model...", "Abort", 0, 100, this);
                progress->show();
                QNetworkReply *reply = modelDownloader.get(request);
                connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
                connect(reply, &QNetworkReply::downloadProgress, [=](qint64 bytesReceived, qint64 bytesTotal) {
                    int total = ((double)bytesReceived / (double)bytesTotal) * 90.0;
                    progress->setValue(total);
                });
            }
        }
    });
    layout->addWidget(gamepadSelect, 1, 1);

    QPushButton *assign = new QPushButton("Assign Controller...", this);
    assign->setToolTip("Press a button to remember this physical controller for this player and selected profile. Auto saves the player assignment only.");
    layout->addWidget(assign, 2, 1);
    connect(assign, &QPushButton::clicked, this, [=]() {
        const QString selected = captureController(this);
        if (selected.isEmpty())
            return;
        // Let the existing selection handler validate and save the assignment.
        int index = gamepadSelect->findData(selected);
        if (index < 0)
        {
            gamepadSelect->addItem(displayLabelForControllerSetting(selected), selected);
            index = gamepadSelect->count() - 1;
        }
        if (gamepadSelect->currentIndex() == index)
            gamepadSelect->currentIndexChanged(index);
        else
            gamepadSelect->setCurrentIndex(index);
    });

    QLabel *pakLabel = new QLabel("Pak", this);
    layout->addWidget(pakLabel, 3, 0);

    pakSelect = new QComboBox(this);
    pakSelect->addItem("Memory");
    pakSelect->addItem("Rumble");
    pakSelect->addItem("Transfer");
    pakSelect->addItem("None");
    pakSelect->setCurrentText(controllerSettings->value("Controller" + QString::number(controller) + "/Pak").toString());
    connect(pakSelect, &QComboBox::currentTextChanged, [=](QString text) {
        controllerSettings->setValue("Controller" + QString::number(controller) + "/Pak", text);
    });
    layout->addWidget(pakSelect, 3, 1);

    setLayout(layout);
}

void ControllerTab::refreshGamepadChoices()
{
    if (gamepadSelect == nullptr || m_controllerSettings == nullptr)
        return;

    const QString savedSetting = m_controllerSettings->value("Controller" + QString::number(m_controller) + "/Gamepad").toString();
    populateGamepadCombo(gamepadSelect, m_controller, savedSetting);
}

void ProfileTab::setComboBox(QComboBox* box, ControllerTab **_controllerTabs, QSettings* settings, QSettings* controllerSettings)
{
    for (int i = 1; i < 5; ++i) {
        QString current = controllerSettings->value("Controller" + QString::number(i) + "/Profile").toString();
        _controllerTabs[i-1]->profileSelect->setCurrentText(current);
        _controllerTabs[i-1]->refreshGamepadChoices();
        populateProfileCombo(_controllerTabs[i-1]->profileSelect, settings, true);
        _controllerTabs[i-1]->profileSelect->setCurrentText(current);
    }
    populateProfileCombo(box, settings, false);
}

int ProfileTab::checkNotRunning(m64p_dynlib_handle coreHandle)
{
    int value = M64EMU_STOPPED;
    ptr_CoreDoCommand CoreDoCommand = (ptr_CoreDoCommand) osal_dynlib_getproc(coreHandle, "CoreDoCommand");
    (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &value);
    if (value != M64EMU_STOPPED) {
        QMessageBox msgBox;
        msgBox.setText("Stop game before editing profiles.");
        msgBox.exec();
        return 0;
    }
    return 1;
}

ProfileTab::ProfileTab(m64p_dynlib_handle coreHandle, ControllerTab **_controllerTabs, QSettings* settings, QSettings* controllerSettings, QWidget *parent)
    : QWidget(parent)
{
    QGridLayout *layout = new QGridLayout(this);
    QComboBox *profileSelect = new QComboBox(this);
    setComboBox(profileSelect, _controllerTabs, settings, controllerSettings);
    QPushButton *buttonNewKeyboard = new QPushButton("New Profile (Keyboard)", this);
    connect(buttonNewKeyboard, &QPushButton::released, [=]() {
        if (checkNotRunning(coreHandle)) {
            ProfileEditor editor("Auto-Keyboard", settings, this);
            editor.exec();
            setComboBox(profileSelect, _controllerTabs, settings, controllerSettings);
        }
    });
    QPushButton *buttonNewGamepad = new QPushButton("New Profile (Gamepad)", this);
    connect(buttonNewGamepad, &QPushButton::released, [=]() {
        if (checkNotRunning(coreHandle)) {
            ProfileEditor editor("Auto-Gamepad", settings, this);
            editor.exec();
            setComboBox(profileSelect, _controllerTabs, settings, controllerSettings);
        }
    });
    QPushButton *buttonEdit = new QPushButton("Edit Profile", this);
    connect(buttonEdit, &QPushButton::released, [=]() {
        if (!profileSelect->currentText().isEmpty() && checkNotRunning(coreHandle)) {
            ProfileEditor editor(profileSelect->currentText(), settings, this);
            editor.exec();
        }
    });

    QPushButton *buttonDelete = new QPushButton("Delete Profile", this);
    connect(buttonDelete, &QPushButton::released, [=]() {
        if (!profileSelect->currentText().isEmpty() && checkNotRunning(coreHandle)) {
            settings->remove(profileSelect->currentText());
            setComboBox(profileSelect, _controllerTabs, settings, controllerSettings);
        }
    });

    layout->addWidget(profileSelect, 1, 0);
    layout->addWidget(buttonNewKeyboard, 0, 1);
    layout->addWidget(buttonNewGamepad, 0, 2);
    layout->addWidget(buttonEdit, 1, 1);
    layout->addWidget(buttonDelete, 1, 2);
    setLayout(layout);
}

CustomButton::CustomButton(QString section, QString setting, QSettings *settings, QWidget* parent)
{
    ProfileEditor* editor = (ProfileEditor*) parent;
    item = setting;
    QString direction;
    QStringList value = settings->value(section + "/" + item).toString().split(",");
    if (value.size() < 2) {
        type = 0;
        axisValue = 0;
        button = SDL_CONTROLLER_BUTTON_INVALID;
        axis = SDL_CONTROLLER_AXIS_INVALID;
        key = SDL_SCANCODE_UNKNOWN;
        joystick_axis = 0;
        joystick_hat = 0;
        joystick_button = 0;
        this->setText("Unbound");
        connect(this, &QPushButton::released, [=]{
            editor->acceptInput(this);
        });
        return;
    }
    switch (value.at(1).toInt()) {
        case 0/*Keyboard*/:
            type = 0;
            key = (SDL_Scancode)value.at(0).toInt();
            this->setText(SDL_GetScancodeName(key));
            break;
        case 1/*Button*/:
            type = 1;
            button = (SDL_GameControllerButton)value.at(0).toInt();
            this->setText(SDL_GameControllerGetStringForButton((SDL_GameControllerButton)value.at(0).toInt()));
            break;
        case 2/*Axis*/:
            type = 2;
            axis = (SDL_GameControllerAxis)value.at(0).toInt();
            axisValue = value.at(2).toInt();
            direction = axisValue > 0 ? " +" : " -";
            this->setText(SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)value.at(0).toInt()) + direction);
            break;
        case 3/*Joystick Hat*/:
            type = 3;
            joystick_hat = value.at(0).toInt();
            axisValue = value.at(2).toInt();
            this->setText("Hat " + QString::number(joystick_hat) + " " + QString::number(axisValue));
            break;
        case 4/*Joystick Button*/:
            type = 4;
            joystick_button = value.at(0).toInt();
            this->setText("Button " + QString::number(joystick_button));
            break;
        case 5/*Joystick Axis*/:
            type = 5;
            joystick_axis = value.at(0).toInt();
            axisValue = value.at(2).toInt();
            direction = axisValue > 0 ? " +" : " -";
            this->setText("Axis " + QString::number(joystick_axis) + direction);
           break;
    }
    connect(this, &QPushButton::released, [=]{
        editor->acceptInput(this);
    });
}

ConfigDialog::ConfigDialog(m64p_dynlib_handle CoreHandle, QSettings* settings, QSettings* controllerSettings)
{
    setWindowTitle("Input Configuration");
    resize(760, 560);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    tabWidget = new QTabWidget(this);

    for (int i = 0; i < 4; ++i)
    {
        controllerTabs[i] = new ControllerTab(i + 1, settings, controllerSettings, this);
        tabWidget->addTab(controllerTabs[i], "Player " + QString::number(i + 1));
    }

    ProfileTab *profileTab = new ProfileTab(CoreHandle, controllerTabs, settings, controllerSettings, this);
    tabWidget->addTab(profileTab, "Profiles");

    mainLayout->addWidget(tabWidget);
}

void ProfileEditor::keyReleaseEvent(QKeyEvent *event)
{
    int modValue = QT2SDL2MOD(event->modifiers());
    int keyValue = QT2SDL2(event->key());
    SDL_Scancode value = (SDL_Scancode)((modValue << 16) + keyValue);
    if (activeButton != nullptr) {
        killTimer(timer);
        activeButton->type = 0;
        activeButton->key = value;
        activeButton->setText(SDL_GetScancodeName(activeButton->key));
        activeButton = nullptr;
        for (int i = 0; i < buttonList.size(); ++i)
            buttonList.at(i)->setDisabled(0);
    }
}

void ProfileEditor::timerEvent(QTimerEvent *)
{
    int i;
    SDL_Event e;
    bool haveControllerButton = false;
    SDL_GameControllerButton controllerButton = SDL_CONTROLLER_BUTTON_INVALID;
    bool haveControllerAxis = false;
    SDL_GameControllerAxis controllerAxis = SDL_CONTROLLER_AXIS_INVALID;
    int controllerAxisValue = 0;
    bool haveJoystickHat = false;
    int joystickHat = 0;
    int joystickHatValue = 0;
    bool haveJoystickButton = false;
    int joystickButton = 0;
    bool haveJoystickAxis = false;
    int joystickAxis = 0;
    int joystickAxisValue = 0;
    SDL_PumpEvents();
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_CONTROLLERBUTTONDOWN:
                haveControllerButton = true;
                controllerButton = (SDL_GameControllerButton)e.cbutton.button;
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (abs(e.caxis.value) > 16384) {
                    haveControllerAxis = true;
                    controllerAxis = (SDL_GameControllerAxis)e.caxis.axis;
                    controllerAxisValue = e.caxis.value > 0 ? 1 : -1;
                }
                break;
            case SDL_JOYHATMOTION:
                haveJoystickHat = true;
                joystickHat = e.jhat.hat;
                joystickHatValue = e.jhat.value;
                break;
            case SDL_JOYBUTTONDOWN:
                haveJoystickButton = true;
                joystickButton = e.jbutton.button;
                break;
            case SDL_JOYAXISMOTION:
                if (abs(e.jaxis.value) > 16384) {
                    haveJoystickAxis = true;
                    joystickAxis = e.jaxis.axis;
                    joystickAxisValue = e.jaxis.value > 0 ? 1 : -1;
                }
                break;
        }
    }

    if (haveJoystickButton || haveJoystickAxis || haveJoystickHat || haveControllerButton || haveControllerAxis) {
        killTimer(timer);

        if (haveJoystickButton) {
            activeButton->type = 4;
            activeButton->joystick_button = joystickButton;
            activeButton->setText("Button " + QString::number(activeButton->joystick_button));
        }
        else if (haveJoystickAxis) {
            activeButton->type = 5;
            activeButton->joystick_axis = joystickAxis;
            activeButton->axisValue = joystickAxisValue;
            QString direction = activeButton->axisValue > 0 ? " +" : " -";
            activeButton->setText("Axis " + QString::number(activeButton->joystick_axis) + direction);
        }
        else if (haveJoystickHat) {
            activeButton->type = 3;
            activeButton->joystick_hat = joystickHat;
            activeButton->axisValue = joystickHatValue;
            activeButton->setText("Hat " + QString::number(activeButton->joystick_hat) + " " + QString::number(activeButton->axisValue));
        }
        else if (haveControllerButton) {
            activeButton->type = 1;
            activeButton->button = controllerButton;
            activeButton->setText(SDL_GameControllerGetStringForButton(controllerButton));
        }
        else {
            activeButton->type = 2;
            activeButton->axis = controllerAxis;
            activeButton->axisValue = controllerAxisValue;
            QString direction = activeButton->axisValue > 0 ? " +" : " -";
            activeButton->setText(SDL_GameControllerGetStringForAxis(controllerAxis) + direction);
        }

        activeButton = nullptr;
        for (i = 0; i < buttonList.size(); ++i)
            buttonList.at(i)->setDisabled(0);
        return;
    }

    if (buttonTimer == 0) {
        killTimer(timer);
        activeButton->setText(activeButton->origText);
        activeButton = nullptr;
        for (i = 0; i < buttonList.size(); ++i) {
            buttonList.at(i)->setDisabled(0);
        }
        return;
    }
    --buttonTimer;
    activeButton->setText(QString::number(ceil(buttonTimer/10.0)));
}

void ProfileEditor::acceptInput(CustomButton* button)
{
    activeButton = button;
    for (int i = 0; i < buttonList.size(); ++i) {
        buttonList.at(i)->setDisabled(1);
    }
    buttonTimer = 50;
    activeButton->origText = activeButton->text();
    activeButton->setText(QString::number(buttonTimer/10));
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
    SDL_FlushEvent(SDL_CONTROLLERAXISMOTION);
    SDL_FlushEvent(SDL_JOYHATMOTION);
    SDL_FlushEvent(SDL_JOYBUTTONDOWN);
    SDL_FlushEvent(SDL_JOYAXISMOTION);
    timer = startTimer(100);
}

ProfileEditor::~ProfileEditor()
{
    for (int i = 0; i < 4; ++i) {
        if (joystick[i]) {
            SDL_JoystickClose(joystick[i]);
            joystick[i] = NULL;
        }
    }
}

ProfileEditor::ProfileEditor(QString profile, QSettings *settings, QWidget *parent)
    : QDialog(parent)
{
    memset(joystick, 0, sizeof(SDL_Joystick*) * 4);
    for (int i = 0; i < 4; ++i)
        joystick[i] = SDL_JoystickOpen(i);

    activeButton = nullptr;
    QString section = profile;
    QRegularExpression rx("[a-zA-Z0-9\\-]+");
    QValidator *validator = new QRegularExpressionValidator(rx, this);
    QLineEdit *profileName = new QLineEdit(this);
    profileName->setValidator(validator);
    profileName->setMaxLength(30);
    if (profile == "Auto-Keyboard" || profile == "Auto-Gamepad") {
        profileName->setDisabled(0);
        profile = "";
    }
    else
        profileName->setDisabled(1);

    QGridLayout *layout = new QGridLayout(this);
    QLabel *profileNameLabel = new QLabel("Profile Name", this);
    profileName->setText(profile);
    layout->addWidget(profileNameLabel, 0, 3);
    layout->addWidget(profileName, 0, 4);

    QFrame* lineH = new QFrame(this);
    lineH->setFrameShape(QFrame::HLine);
    lineH->setFrameShadow(QFrame::Sunken);
    layout->addWidget(lineH, 1, 0, 1, 8);

    QLabel *buttonLabelA = new QLabel("A", this);
    buttonLabelA->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushA = new CustomButton(section, "A", settings, this);
    buttonList.append(buttonPushA);
    layout->addWidget(buttonLabelA, 2, 0);
    layout->addWidget(buttonPushA, 2, 1);

    QLabel *buttonLabelB = new QLabel("B", this);
    buttonLabelB->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushB = new CustomButton(section, "B", settings, this);
    buttonList.append(buttonPushB);
    layout->addWidget(buttonLabelB, 3, 0);
    layout->addWidget(buttonPushB, 3, 1);

    QLabel *buttonLabelZ = new QLabel("Z Trigger", this);
    buttonLabelZ->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushZ = new CustomButton(section, "Z", settings, this);
    buttonList.append(buttonPushZ);
    layout->addWidget(buttonLabelZ, 4, 0);
    layout->addWidget(buttonPushZ, 4, 1);

    QLabel *buttonLabelStart = new QLabel("Start", this);
    buttonLabelStart->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushStart = new CustomButton(section, "Start", settings, this);
    buttonList.append(buttonPushStart);
    layout->addWidget(buttonLabelStart, 5, 0);
    layout->addWidget(buttonPushStart, 5, 1);

    QLabel *buttonLabelLTrigger = new QLabel("L Button", this);
    buttonLabelLTrigger->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushLTrigger = new CustomButton(section, "L", settings, this);
    buttonList.append(buttonPushLTrigger);
    layout->addWidget(buttonLabelLTrigger, 6, 0);
    layout->addWidget(buttonPushLTrigger, 6, 1);

    QLabel *buttonLabelRTrigger = new QLabel("R Button", this);
    buttonLabelRTrigger->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushRTrigger = new CustomButton(section, "R", settings, this);
    buttonList.append(buttonPushRTrigger);
    layout->addWidget(buttonLabelRTrigger, 7, 0);
    layout->addWidget(buttonPushRTrigger, 7, 1);

    QLabel *buttonLabelPakSwap = new QLabel("Pak Swap", this);
    buttonLabelPakSwap->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushPakSwap = new CustomButton(section, "PakSwap", settings, this);
    buttonList.append(buttonPushPakSwap);
    layout->addWidget(buttonLabelPakSwap, 8, 0);
    layout->addWidget(buttonPushPakSwap, 8, 1);

    QFrame* lineV = new QFrame(this);
    lineV->setFrameShape(QFrame::VLine);
    lineV->setFrameShadow(QFrame::Sunken);
    layout->addWidget(lineV, 2, 2, 7, 1);

    QLabel *buttonLabelDPadL = new QLabel("DPad Left", this);
    buttonLabelDPadL->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushDPadL = new CustomButton(section, "DPadL", settings, this);
    buttonList.append(buttonPushDPadL);
    layout->addWidget(buttonLabelDPadL, 2, 3);
    layout->addWidget(buttonPushDPadL, 2, 4);

    QLabel *buttonLabelDPadR = new QLabel("DPad Right", this);
    buttonLabelDPadR->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushDPadR = new CustomButton(section, "DPadR", settings, this);
    buttonList.append(buttonPushDPadR);
    layout->addWidget(buttonLabelDPadR, 3, 3);
    layout->addWidget(buttonPushDPadR, 3, 4);

    QLabel *buttonLabelDPadU = new QLabel("DPad Up", this);
    buttonLabelDPadU->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushDPadU = new CustomButton(section, "DPadU", settings, this);
    buttonList.append(buttonPushDPadU);
    layout->addWidget(buttonLabelDPadU, 4, 3);
    layout->addWidget(buttonPushDPadU, 4, 4);

    QLabel *buttonLabelDPadD = new QLabel("DPad Down", this);
    buttonLabelDPadD->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushDPadD = new CustomButton(section, "DPadD", settings, this);
    buttonList.append(buttonPushDPadD);
    layout->addWidget(buttonLabelDPadD, 5, 3);
    layout->addWidget(buttonPushDPadD, 5, 4);

    QFrame* lineV2 = new QFrame(this);
    lineV2->setFrameShape(QFrame::VLine);
    lineV2->setFrameShadow(QFrame::Sunken);
    layout->addWidget(lineV2, 2, 5, 7, 1);

    QLabel *buttonLabelCL = new QLabel("C Left", this);
    buttonLabelCL->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushCL = new CustomButton(section, "CLeft", settings, this);
    buttonList.append(buttonPushCL);
    layout->addWidget(buttonLabelCL, 2, 6);
    layout->addWidget(buttonPushCL, 2, 7);

    QLabel *buttonLabelCR = new QLabel("C Right", this);
    buttonLabelCR->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushCR = new CustomButton(section, "CRight", settings, this);
    buttonList.append(buttonPushCR);
    layout->addWidget(buttonLabelCR, 3, 6);
    layout->addWidget(buttonPushCR, 3, 7);

    QLabel *buttonLabelCU = new QLabel("C Up", this);
    buttonLabelCU->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushCU = new CustomButton(section, "CUp", settings, this);
    buttonList.append(buttonPushCU);
    layout->addWidget(buttonLabelCU, 4, 6);
    layout->addWidget(buttonPushCU, 4, 7);

    QLabel *buttonLabelCD = new QLabel("C Down", this);
    buttonLabelCD->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushCD = new CustomButton(section, "CDown", settings, this);
    buttonList.append(buttonPushCD);
    layout->addWidget(buttonLabelCD, 5, 6);
    layout->addWidget(buttonPushCD, 5, 7);

    QLabel *buttonLabelStickL = new QLabel("Control Stick Left", this);
    buttonLabelStickL->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushStickL = new CustomButton(section, "AxisLeft", settings, this);
    buttonList.append(buttonPushStickL);
    layout->addWidget(buttonLabelStickL, 6, 3);
    layout->addWidget(buttonPushStickL, 6, 4);

    QLabel *buttonLabelStickR = new QLabel("Control Stick Right", this);
    buttonLabelStickR->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushStickR = new CustomButton(section, "AxisRight", settings, this);
    buttonList.append(buttonPushStickR);
    layout->addWidget(buttonLabelStickR, 7, 3);
    layout->addWidget(buttonPushStickR, 7, 4);

    QLabel *buttonLabelStickU = new QLabel("Control Stick Up", this);
    buttonLabelStickU->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushStickU = new CustomButton(section, "AxisUp", settings, this);
    buttonList.append(buttonPushStickU);
    layout->addWidget(buttonLabelStickU, 6, 6);
    layout->addWidget(buttonPushStickU, 6, 7);

    QLabel *buttonLabelStickD = new QLabel("Control Stick Down", this);
    buttonLabelStickD->setAlignment(Qt::AlignCenter);
    CustomButton *buttonPushStickD = new CustomButton(section, "AxisDown", settings, this);
    buttonList.append(buttonPushStickD);
    layout->addWidget(buttonLabelStickD, 7, 6);
    layout->addWidget(buttonPushStickD, 7, 7);

    QFrame* lineH2 = new QFrame(this);
    lineH2->setFrameShape(QFrame::HLine);
    lineH2->setFrameShadow(QFrame::Sunken);
    layout->addWidget(lineH2, 9, 0, 1, 8);

    QLabel *buttonLabelDeadzone = new QLabel("Deadzone", this);
    buttonLabelDeadzone->setAlignment(Qt::AlignCenter);
    QLabel *buttonLabelDeadzoneValue = new QLabel(this);
    if (!settings->contains(section + "/Deadzone"))
        settings->setValue(section + "/Deadzone", 12.5);
    float deadzoneValue = settings->value(section + "/Deadzone").toFloat();
    buttonLabelDeadzoneValue->setText(QString::number(deadzoneValue) + "%");
    buttonLabelDeadzoneValue->setAlignment(Qt::AlignCenter);
    QSlider *sliderDeadzone = new QSlider(Qt::Horizontal, this);
    sliderDeadzone->setMinimum(0);
    sliderDeadzone->setMaximum(250);
    sliderDeadzone->setTickPosition(QSlider::TicksBothSides);
    sliderDeadzone->setTickInterval(5);
    sliderDeadzone->setSliderPosition((int)(deadzoneValue * 10.0));
    connect(sliderDeadzone, &QSlider::valueChanged, [=](int value) {
        float percent = value / 10.0;
        buttonLabelDeadzoneValue->setText(QString::number(percent, 'f', 1) + "%");
    });

    layout->addWidget(buttonLabelDeadzone, 10, 0);
    layout->addWidget(buttonLabelDeadzoneValue, 10, 1);
    layout->addWidget(sliderDeadzone, 10, 2, 1, 6);

    QLabel *buttonLabelSensitivity = new QLabel("Analog Sensitivity", this);
    buttonLabelSensitivity->setAlignment(Qt::AlignCenter);
    QLabel *buttonLabelSensitivityValue = new QLabel(this);
    if (!settings->contains(section + "/Sensitivity"))
        settings->setValue(section + "/Sensitivity", 100.0);
    float sensitivityValue = settings->value(section + "/Sensitivity").toFloat();
    buttonLabelSensitivityValue->setText(QString::number(sensitivityValue) + "%");
    buttonLabelSensitivityValue->setAlignment(Qt::AlignCenter);
    QSlider *sliderSensitivity = new QSlider(Qt::Horizontal, this);
    sliderSensitivity->setMinimum(800);
    sliderSensitivity->setMaximum(1200);
    sliderSensitivity->setTickPosition(QSlider::TicksBothSides);
    sliderSensitivity->setTickInterval(5);
    sliderSensitivity->setSliderPosition((int)(sensitivityValue * 10.0));
    connect(sliderSensitivity, &QSlider::valueChanged, [=](int value) {
        float percent = value / 10.0;
        buttonLabelSensitivityValue->setText(QString::number(percent, 'f', 1) + "%");
    });

    layout->addWidget(buttonLabelSensitivity, 11, 0);
    layout->addWidget(buttonLabelSensitivityValue, 11, 1);
    layout->addWidget(sliderSensitivity, 11, 2, 1, 6);

    QFrame* lineH3 = new QFrame(this);
    lineH3->setFrameShape(QFrame::HLine);
    lineH3->setFrameShadow(QFrame::Sunken);
    layout->addWidget(lineH3, 12, 0, 1, 8);

    QPushButton *buttonPushSave = new QPushButton("Save and Close", this);
    connect(buttonPushSave, &QPushButton::released, [=]() {
        const QString saveSection = profileName->text();
        if (!saveSection.startsWith("Auto-") && !saveSection.isEmpty() && (!settings->contains(saveSection + "/A") || !profileName->isEnabled())) {
            QStringList value;
            for (int i = 0; i < buttonList.size(); ++i) {
                value.clear();
                switch (buttonList.at(i)->type) {
                    case 0:
                        value.insert(0, QString::number(buttonList.at(i)->key));
                        value.insert(1, QString::number(0));
                        break;
                    case 1:
                        value.insert(0, QString::number(buttonList.at(i)->button));
                        value.insert(1, QString::number(1));
                        break;
                    case 2:
                        value.insert(0, QString::number(buttonList.at(i)->axis));
                        value.insert(1, QString::number(2));
                        value.insert(2, QString::number(buttonList.at(i)->axisValue));
                        break;
                    case 3:
                        value.insert(0, QString::number(buttonList.at(i)->joystick_hat));
                        value.insert(1, QString::number(3));
                        value.insert(2, QString::number(buttonList.at(i)->axisValue));
                        break;
                    case 4:
                        value.insert(0, QString::number(buttonList.at(i)->joystick_button));
                        value.insert(1, QString::number(4));
                        break;
                    case 5:
                        value.insert(0, QString::number(buttonList.at(i)->joystick_axis));
                        value.insert(1, QString::number(5));
                        value.insert(2, QString::number(buttonList.at(i)->axisValue));
                        break;
                }
                settings->setValue(saveSection + "/" + buttonList.at(i)->item, value.join(","));
            }
            float percent = sliderDeadzone->value() / 10.0;
            settings->setValue(saveSection + "/Deadzone", percent);
            percent = sliderSensitivity->value() / 10.0;
            settings->setValue(saveSection + "/Sensitivity", percent);
            this->done(1);
        }
        else {
            QMessageBox msgBox;
            msgBox.setText("Invalid profile name.");
            msgBox.exec();
        }
    });
    layout->addWidget(buttonPushSave, 13, 0, 1, 2);
    QPushButton *buttonPushClose = new QPushButton("Close Without Saving", this);
    connect(buttonPushClose, &QPushButton::released, [=]() {
        this->done(1);
    });
    layout->addWidget(buttonPushClose, 13, 6, 1, 2);

    setLayout(layout);
    setWindowTitle(tr("Profile Editor"));
}
