#include "gamesettingsdialog.h"

#include "interface/core_commands.h"
#include "mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QIntValidator>
#include <QDoubleValidator>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <SDL.h>

namespace
{
struct ParamCollectorContext
{
    QString section;
    QList<QPair<QString, m64p_type>> params;
};

bool skipCoreParam(const QString &paramName)
{
    static const QStringList ignored = {
        "Version",
        "OnScreenDisplay",
        "NoCompiledJump",
        "EnableDebugger",
        "GbCameraVideoCaptureBackend1",
        "SaveDiskFormat",
        "SaveFilenameFormat"
    };
    return ignored.contains(paramName);
}

void collectParams(void *context, const char *paramName, m64p_type type)
{
    ParamCollectorContext *collector = static_cast<ParamCollectorContext *>(context);
    const QString name = QString::fromUtf8(paramName);
    if (collector->section == "Core" && skipCoreParam(name))
        return;

    collector->params.append(qMakePair(name, type));
}

bool goodNameRequiresVru(const QString &goodName)
{
    const QString lowered = goodName.toLower();
    return lowered.contains("hey you, pikachu!") || lowered.contains("pikachu genki dechu");
}
}

GameSettingsDialog::GameSettingsDialog(const QString &romMd5, const QString &goodName, QWidget *parent)
    : QDialog(parent), m_romMd5(romMd5), m_goodName(goodName)
{
    const QString configDir = QDir(ConfigGetUserConfigPath()).absolutePath();
    m_globalConfig = new QSettings(QDir(configDir).filePath("mupen64plus.cfg"), QSettings::IniFormat, this);
    m_overrideSettings = new QSettings(QDir(configDir).filePath("game-settings.ini"), QSettings::IniFormat, this);
    m_profileSettings = new QSettings(QDir(configDir).filePath("input-profiles.ini"), QSettings::IniFormat, this);
    m_controllerSettings = new QSettings(QDir(configDir).filePath("input-settings.ini"), QSettings::IniFormat, this);
    m_controllerOverrideSettings = new QSettings(QDir(configDir).filePath("game-input-settings.ini"), QSettings::IniFormat, this);

    (*ConfigOpenSection)("Core", &m_coreConfigHandle);
    (*ConfigOpenSection)("Video-Parallel", &m_videoConfigHandle);

    buildUi();
}

GameSettingsDialog::~GameSettingsDialog()
{
    qDeleteAll(m_settingRows);
}

QString GameSettingsDialog::overrideKey(const QString &section, const QString &paramName) const
{
    return "Games/" + m_romMd5 + "/" + section + "/" + paramName;
}

QString GameSettingsDialog::controllerOverrideKey(int controllerIndex, const QString &name) const
{
    return "Games/" + m_romMd5 + "/Controller" + QString::number(controllerIndex + 1) + "/" + name;
}

QVariant GameSettingsDialog::configValue(m64p_handle handle, const QString &paramName, m64p_type type) const
{
    switch (type)
    {
    case M64TYPE_INT:
        return (*ConfigGetParamInt)(handle, paramName.toUtf8().constData());
    case M64TYPE_FLOAT:
        return (*ConfigGetParamFloat)(handle, paramName.toUtf8().constData());
    case M64TYPE_BOOL:
        return (*ConfigGetParamBool)(handle, paramName.toUtf8().constData());
    case M64TYPE_STRING:
        return (*ConfigGetParamString)(handle, paramName.toUtf8().constData());
    default:
        return QVariant();
    }
}

QVariant GameSettingsDialog::editorValue(const GameSettingRow *row) const
{
    if (row->type == M64TYPE_BOOL)
        return static_cast<QCheckBox *>(row->editor)->isChecked();

    return static_cast<QLineEdit *>(row->editor)->text();
}

void GameSettingsDialog::setEditorValue(GameSettingRow *row, const QVariant &value)
{
    if (row->type == M64TYPE_BOOL)
    {
        static_cast<QCheckBox *>(row->editor)->setChecked(value.toBool());
        return;
    }

    QLineEdit *edit = static_cast<QLineEdit *>(row->editor);
    if (row->type == M64TYPE_FLOAT)
        edit->setText(QString::number(value.toFloat()));
    else if (row->type == M64TYPE_INT)
        edit->setText(QString::number(value.toInt()));
    else
        edit->setText(value.toString());
}

void GameSettingsDialog::addSettingRow(QGridLayout *layout, const QString &sectionName, const QString &paramName, m64p_type type, m64p_handle handle, int row)
{
    GameSettingRow *settingRow = new GameSettingRow;
    settingRow->section = sectionName;
    settingRow->paramName = paramName;
    settingRow->type = type;
    settingRow->globalValue = m_globalConfig->value(sectionName + "/" + paramName, configValue(handle, paramName, type));

    QLabel *label = new QLabel(paramName, this);
    const QString help = QString::fromUtf8((*ConfigGetParameterHelp)(handle, paramName.toUtf8().constData()));
    if (!help.isEmpty())
        label->setToolTip(help);

    if (type == M64TYPE_BOOL)
    {
        settingRow->editor = new QCheckBox(this);
    }
    else
    {
        QLineEdit *edit = new QLineEdit(this);
        edit->setStyleSheet("border: 1px solid; padding: 8px");
        edit->setMaximumWidth(260);
        if (type == M64TYPE_INT)
            edit->setValidator(new QIntValidator(edit));
        else if (type == M64TYPE_FLOAT)
            edit->setValidator(new QDoubleValidator(edit));
        settingRow->editor = edit;
    }

    const QString key = overrideKey(sectionName, paramName);
    const bool hasOverride = m_overrideSettings->contains(key);
    setEditorValue(settingRow, hasOverride ? m_overrideSettings->value(key) : settingRow->globalValue);
    if (type == M64TYPE_BOOL)
    {
        connect(static_cast<QCheckBox *>(settingRow->editor), &QCheckBox::checkStateChanged,
                this, [this](Qt::CheckState) { m_clearCoreVideoSettings = false; });
    }
    else
    {
        connect(static_cast<QLineEdit *>(settingRow->editor), &QLineEdit::textEdited,
                this, [this](const QString &) { m_clearCoreVideoSettings = false; });
    }

    layout->addWidget(label, row, 0);
    layout->addWidget(settingRow->editor, row, 1);
    m_settingRows.append(settingRow);
}

void GameSettingsDialog::populateSection(QGridLayout *layout, const QString &sectionName, m64p_handle handle)
{
    ParamCollectorContext collector;
    collector.section = sectionName;
    (*ConfigListParameters)(handle, &collector, collectParams);

    int row = 0;
    for (const auto &entry : collector.params)
    {
        addSettingRow(layout, sectionName, entry.first, entry.second, handle, row);
        ++row;
    }
}

void GameSettingsDialog::loadControllerOverride(QComboBox *box, const QString &key, const QStringList &choices, const QString &globalValue)
{
    const QString value = m_controllerOverrideSettings->value(key).toString();
    if (value.isEmpty())
        box->setCurrentText("Use Global (" + globalValue + ")");
    else if (choices.contains(value))
        box->setCurrentText(value);
}

void GameSettingsDialog::populateControllerTab(QGridLayout *layout)
{
    QStringList profileChoices = {"Use Global"};
    profileChoices.append("Auto");
    QStringList profileNames = m_profileSettings->childGroups();
    profileNames.removeAll("Auto-Keyboard");
    profileNames.removeAll("Auto-Gamepad");
    profileChoices.append(profileNames);

    QStringList pakChoices = {"Use Global", "Memory", "Rumble", "Transfer", "None"};

    for (int i = 0; i < 4; ++i)
    {
        const QString globalProfile = m_controllerSettings->value("Controller" + QString::number(i + 1) + "/Profile", "Auto").toString();
        const QString globalGamepad = m_controllerSettings->value("Controller" + QString::number(i + 1) + "/Gamepad", "Auto").toString();
        const QString globalPak = m_controllerSettings->value("Controller" + QString::number(i + 1) + "/Pak", "Memory").toString();

        const int row = i * 4;
        QLabel *header = new QLabel("Controller " + QString::number(i + 1), this);
        header->setStyleSheet("font-weight: bold; padding-top: 8px");
        layout->addWidget(header, row, 0);

        layout->addWidget(new QLabel("Profile", this), row + 1, 0);
        m_controllerRows[i].profileSelect = new QComboBox(this);
        for (const QString &choice : profileChoices)
        {
            if (choice == "Use Global")
                m_controllerRows[i].profileSelect->addItem("Use Global (" + globalProfile + ")");
            else
                m_controllerRows[i].profileSelect->addItem(choice);
        }
        layout->addWidget(m_controllerRows[i].profileSelect, row + 1, 1);

        layout->addWidget(new QLabel("Gamepad", this), row + 2, 0);
        m_controllerRows[i].gamepadSelect = new QComboBox(this);
        m_controllerRows[i].gamepadSelect->addItem("Use Global (" + globalGamepad + ")");
        m_controllerRows[i].gamepadSelect->addItem("Auto");
        m_controllerRows[i].gamepadSelect->addItem("Keyboard");
        if (i == 3)
            m_controllerRows[i].gamepadSelect->addItem("Emulate VRU");
        m_controllerRows[i].gamepadSelect->addItem("None");
        for (int j = 0; j < SDL_NumJoysticks(); ++j)
            m_controllerRows[i].gamepadSelect->addItem(QString::number(j) + ":" + SDL_JoystickNameForIndex(j));
        layout->addWidget(m_controllerRows[i].gamepadSelect, row + 2, 1);

        layout->addWidget(new QLabel("Pak", this), row + 3, 0);
        m_controllerRows[i].pakSelect = new QComboBox(this);
        for (const QString &choice : pakChoices)
        {
            if (choice == "Use Global")
                m_controllerRows[i].pakSelect->addItem("Use Global (" + globalPak + ")");
            else
                m_controllerRows[i].pakSelect->addItem(choice);
        }
        layout->addWidget(m_controllerRows[i].pakSelect, row + 3, 1);

        loadControllerOverride(m_controllerRows[i].profileSelect, controllerOverrideKey(i, "Profile"), profileChoices, globalProfile);
        loadControllerOverride(m_controllerRows[i].gamepadSelect, controllerOverrideKey(i, "Gamepad"), QStringList(), globalGamepad);
        loadControllerOverride(m_controllerRows[i].pakSelect, controllerOverrideKey(i, "Pak"), pakChoices, globalPak);

        if (romRequiresVru() && i == 3)
        {
            QLabel *note = new QLabel("This game requires the VRU on controller 4.", this);
            note->setWordWrap(true);
            layout->addWidget(note, row, 1);

            m_controllerRows[i].gamepadSelect->setCurrentText("Emulate VRU");
            m_controllerRows[i].gamepadSelect->setEnabled(false);
            m_controllerRows[i].pakSelect->setCurrentText("None");
            m_controllerRows[i].pakSelect->setEnabled(false);
        }
    }
}

void GameSettingsDialog::buildUi()
{
    setWindowTitle("Current Game Settings - " + m_goodName);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QLabel *title = new QLabel("Overrides for " + m_goodName + "\nThese settings apply to this ROM only.", this);
    title->setWordWrap(true);
    title->setStyleSheet("font-weight: bold");
    mainLayout->addWidget(title);

    QGridLayout *videoCoreLayout = new QGridLayout;
    QLabel *videoCoreLabel = new QLabel("Video Core", this);
    const QString videoCoreHelp = "Use GLideN64 + HLE RSP for games that run slowly or have graphical glitches with ParaLLEl.";
    videoCoreLabel->setToolTip(videoCoreHelp);
    m_videoCoreSelect = new QComboBox(this);
    m_videoCoreSelect->addItem("ParaLLEl (Default)", "ParaLLEl");
    m_videoCoreSelect->addItem("GLideN64 + HLE RSP", "GLideN64");
    m_videoCoreSelect->setToolTip(videoCoreHelp);
    const QString videoCore = m_overrideSettings->value(overrideKey("Frontend", "VideoCore"), "ParaLLEl").toString();
    m_videoCoreSelect->setCurrentIndex(videoCore.compare("GLideN64", Qt::CaseInsensitive) == 0 ? 1 : 0);
    videoCoreLayout->addWidget(videoCoreLabel, 0, 0);
    videoCoreLayout->addWidget(m_videoCoreSelect, 0, 1);
    mainLayout->addLayout(videoCoreLayout);

    QTabWidget *tabs = new QTabWidget(this);

    QWidget *coreWidget = new QWidget(this);
    QGridLayout *coreLayout = new QGridLayout(coreWidget);
    populateSection(coreLayout, "Core", m_coreConfigHandle);
    QScrollArea *coreScroll = new QScrollArea(this);
    coreScroll->setWidget(coreWidget);
    coreScroll->setWidgetResizable(true);
    tabs->addTab(coreScroll, tr("Core"));

    QWidget *videoWidget = new QWidget(this);
    QGridLayout *videoLayout = new QGridLayout(videoWidget);
    populateSection(videoLayout, "Video-Parallel", m_videoConfigHandle);
    QScrollArea *videoScroll = new QScrollArea(this);
    videoScroll->setWidget(videoWidget);
    videoScroll->setWidgetResizable(true);
    tabs->addTab(videoScroll, tr("ParaLLEl Video"));

    QWidget *glideWidget = new QWidget(this);
    QGridLayout *glideLayout = new QGridLayout(glideWidget);
    QLabel *glideNote = new QLabel(
        "These values are saved for this ROM and applied when it launches with GLideN64.", this);
    glideNote->setWordWrap(true);
    glideLayout->addWidget(glideNote, 0, 0, 1, 2);

    m_glideStartFullscreenCheck = new QCheckBox("Start game in fullscreen", this);
    m_glideStartFullscreenCheck->setChecked(
        m_overrideSettings->value(overrideKey("GLideN64", "StartFullscreen"), true).toBool());
    m_glideStartFullscreenCheck->setToolTip("Opens GLideN64 directly in fullscreen instead of requiring Alt+Enter after launch.");
    glideLayout->addWidget(m_glideStartFullscreenCheck, 1, 0, 1, 2);

    m_glideAspectSelect = new QComboBox(this);
    m_glideAspectSelect->addItem("Stretch", 0);
    m_glideAspectSelect->addItem("Force 4:3", 1);
    m_glideAspectSelect->addItem("Force 16:9", 2);
    m_glideAspectSelect->addItem("Adjust 4:3", 3);
    m_glideAspectSelect->addItem("Adjust 16:9", 4);

    m_glideResolutionSelect = new QComboBox(this);
    m_glideResolutionSelect->addItem("Automatic (output resolution)", 0);
    for (int factor = 1; factor <= 8; ++factor)
        m_glideResolutionSelect->addItem(QString::number(factor) + "x native", factor);

    m_glideCorrect2DSelect = new QComboBox(this);
    m_glideCorrect2DSelect->addItem("Off", 0);
    m_glideCorrect2DSelect->addItem("Auto", 1);
    m_glideCorrect2DSelect->addItem("Force", 2);

    m_glideNative2DSelect = new QComboBox(this);
    m_glideNative2DSelect->addItem("Off", 0);
    m_glideNative2DSelect->addItem("Optimized", 1);
    m_glideNative2DSelect->addItem("Unoptimized", 2);

    const auto loadGlideValue = [this](QComboBox *box, const QString &name, int defaultValue) {
        const int value = m_overrideSettings->value(overrideKey("GLideN64", name), defaultValue).toInt();
        const int index = box->findData(value);
        box->setCurrentIndex(index >= 0 ? index : box->findData(defaultValue));
    };
    loadGlideValue(m_glideAspectSelect, "AspectRatio", 1);
    loadGlideValue(m_glideResolutionSelect, "NativeResolutionFactor", 0);
    loadGlideValue(m_glideCorrect2DSelect, "Correct2DCoordinates", 0);
    loadGlideValue(m_glideNative2DSelect, "NativeResolution2D", 0);

    QLabel *aspectLabel = new QLabel("Aspect Ratio", this);
    aspectLabel->setToolTip("Controls how GLideN64 fits the N64 image to the display.");
    m_glideAspectSelect->setToolTip(aspectLabel->toolTip());
    glideLayout->addWidget(aspectLabel, 2, 0);
    glideLayout->addWidget(m_glideAspectSelect, 2, 1);

    QLabel *resolutionLabel = new QLabel("Internal Resolution", this);
    resolutionLabel->setToolTip("Sets the framebuffer resolution as a multiple of the N64 native resolution. Automatic follows the output resolution.");
    m_glideResolutionSelect->setToolTip(resolutionLabel->toolTip());
    glideLayout->addWidget(resolutionLabel, 3, 0);
    glideLayout->addWidget(m_glideResolutionSelect, 3, 1);

    QLabel *correct2DLabel = new QLabel("Correct 2D Rectangle Coordinates", this);
    correct2DLabel->setToolTip("Helps remove thin lines or seams between adjoining 2D elements. Auto is the safest correction mode.");
    m_glideCorrect2DSelect->setToolTip(correct2DLabel->toolTip());
    glideLayout->addWidget(correct2DLabel, 4, 0);
    glideLayout->addWidget(m_glideCorrect2DSelect, 4, 1);

    QLabel *native2DLabel = new QLabel("Native-Resolution 2D", this);
    native2DLabel->setToolTip("Renders 2D rectangles at native resolution to fix alignment artifacts. Optimized is recommended when this correction is needed.");
    m_glideNative2DSelect->setToolTip(native2DLabel->toolTip());
    glideLayout->addWidget(native2DLabel, 5, 0);
    glideLayout->addWidget(m_glideNative2DSelect, 5, 1);
    glideLayout->setRowStretch(6, 1);

    QScrollArea *glideScroll = new QScrollArea(this);
    glideScroll->setWidget(glideWidget);
    glideScroll->setWidgetResizable(true);
    const int glideTabIndex = tabs->addTab(glideScroll, tr("GLideN64 Video"));
    const auto updateGlideTab = [this, tabs, glideTabIndex](int) {
        const bool useGlide = m_videoCoreSelect->currentData().toString() == "GLideN64";
        tabs->setTabEnabled(glideTabIndex, useGlide);
    };
    connect(m_videoCoreSelect, &QComboBox::currentIndexChanged, this, updateGlideTab);
    updateGlideTab(m_videoCoreSelect->currentIndex());

    QWidget *controllerWidget = new QWidget(this);
    QGridLayout *controllerLayout = new QGridLayout(controllerWidget);
    populateControllerTab(controllerLayout);
    QScrollArea *controllerScroll = new QScrollArea(this);
    controllerScroll->setWidget(controllerWidget);
    controllerScroll->setWidgetResizable(true);
    tabs->addTab(controllerScroll, tr("Controllers"));

    mainLayout->addWidget(tabs);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    QPushButton *clearButton = new QPushButton("Clear Game Overrides", this);
    QPushButton *saveButton = new QPushButton("Save", this);
    QPushButton *closeButton = new QPushButton("Close", this);
    connect(clearButton, &QPushButton::released, this, &GameSettingsDialog::clearOverrides);
    connect(saveButton, &QPushButton::released, this, &GameSettingsDialog::saveAndClose);
    connect(closeButton, &QPushButton::released, this, &GameSettingsDialog::reject);
    buttonLayout->addWidget(clearButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);
}

void GameSettingsDialog::saveAndClose()
{
    const QString videoCoreKey = overrideKey("Frontend", "VideoCore");
    if (m_videoCoreSelect->currentData().toString() == "GLideN64")
    {
        m_overrideSettings->setValue(videoCoreKey, "GLideN64");
        m_overrideSettings->setValue(overrideKey("GLideN64", "AspectRatio"), m_glideAspectSelect->currentData());
        m_overrideSettings->setValue(overrideKey("GLideN64", "NativeResolutionFactor"), m_glideResolutionSelect->currentData());
        m_overrideSettings->setValue(overrideKey("GLideN64", "Correct2DCoordinates"), m_glideCorrect2DSelect->currentData());
        m_overrideSettings->setValue(overrideKey("GLideN64", "NativeResolution2D"), m_glideNative2DSelect->currentData());
        m_overrideSettings->setValue(overrideKey("GLideN64", "StartFullscreen"), m_glideStartFullscreenCheck->isChecked());
    }
    else
    {
        m_overrideSettings->remove(videoCoreKey);
        m_overrideSettings->remove(overrideKey("GLideN64", "AspectRatio"));
        m_overrideSettings->remove(overrideKey("GLideN64", "NativeResolutionFactor"));
        m_overrideSettings->remove(overrideKey("GLideN64", "Correct2DCoordinates"));
        m_overrideSettings->remove(overrideKey("GLideN64", "NativeResolution2D"));
        m_overrideSettings->remove(overrideKey("GLideN64", "StartFullscreen"));
    }

    for (GameSettingRow *row : m_settingRows)
    {
        const QString key = overrideKey(row->section, row->paramName);
        if (m_clearCoreVideoSettings)
            m_overrideSettings->remove(key);
        else
            m_overrideSettings->setValue(key, editorValue(row));
    }

    for (int i = 0; i < 4; ++i)
    {
        const QString profileKey = controllerOverrideKey(i, "Profile");
        const QString gamepadKey = controllerOverrideKey(i, "Gamepad");
        const QString pakKey = controllerOverrideKey(i, "Pak");

        const bool forceVru = romRequiresVru() && i == 3;
        const QString profileValue = m_controllerRows[i].profileSelect->currentText();
        const QString gamepadValue = forceVru ? "Emulate VRU" : m_controllerRows[i].gamepadSelect->currentText();
        const QString pakValue = forceVru ? "None" : m_controllerRows[i].pakSelect->currentText();

        if (profileValue.startsWith("Use Global"))
            m_controllerOverrideSettings->remove(profileKey);
        else
            m_controllerOverrideSettings->setValue(profileKey, profileValue);

        if (gamepadValue.startsWith("Use Global"))
            m_controllerOverrideSettings->remove(gamepadKey);
        else
            m_controllerOverrideSettings->setValue(gamepadKey, gamepadValue);

        if (pakValue.startsWith("Use Global"))
            m_controllerOverrideSettings->remove(pakKey);
        else
            m_controllerOverrideSettings->setValue(pakKey, pakValue);
    }

    m_overrideSettings->sync();
    m_controllerOverrideSettings->sync();
    accept();
}

bool GameSettingsDialog::romRequiresVru() const
{
    return goodNameRequiresVru(m_goodName);
}

void GameSettingsDialog::clearOverrides()
{
    m_videoCoreSelect->setCurrentIndex(0);
    m_glideAspectSelect->setCurrentIndex(m_glideAspectSelect->findData(1));
    m_glideResolutionSelect->setCurrentIndex(m_glideResolutionSelect->findData(0));
    m_glideCorrect2DSelect->setCurrentIndex(m_glideCorrect2DSelect->findData(0));
    m_glideNative2DSelect->setCurrentIndex(m_glideNative2DSelect->findData(0));
    m_glideStartFullscreenCheck->setChecked(true);

    for (GameSettingRow *row : m_settingRows)
        setEditorValue(row, row->globalValue);
    m_clearCoreVideoSettings = true;

    for (int i = 0; i < 4; ++i)
    {
        m_controllerRows[i].profileSelect->setCurrentIndex(0);
        m_controllerRows[i].gamepadSelect->setCurrentIndex(0);
        m_controllerRows[i].pakSelect->setCurrentIndex(0);
    }
}
