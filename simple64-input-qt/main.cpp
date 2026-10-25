/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   simple64-input-qt - main.cpp                                       *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2008-2011 Richard Goedeken                              *
 *   Copyright (C) 2008 Tillin9                                            *
 *   Copyright (C) 2002 Blight                                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_common.h"
#include "m64p_types.h"
#include "m64p_config.h"
#include "m64p_plugin.h"
#include "main.h"
#include "configdialog.h"
#include "vosk/vosk_api.h"
#include "osal/osal_dynamiclib.h"
#include "vruwords.h"
#include "funcs.h"

#include <QDir>
#include <QDirIterator>
#include <QCoreApplication>
#include <QFile>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLibrary>
#include <QMetaObject>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QStringDecoder>

#define SETTINGS_VER 2
#define AXIS_PEAK 32768
#define MAX_AXIS_VALUE 85
#define DEADZONE_DEFAULT 5.0

#define QT_INPUT_PLUGIN_VERSION 0x020500
#define INPUT_PLUGIN_API_VERSION 0x020101
static int l_PluginInit = 0;
static int l_TalkingState = 0;
static unsigned char myKeyState[SDL_NUM_SCANCODES];
static QSettings* settings;
static QSettings* controllerSettings;
static QSettings* gameSettings;
static QSettings* gameControllerSettings;
static QSettings* controllerOverrideSettings;
static SController controller[4];   // 4 controllers
static m64p_dynlib_handle coreHandle;
static VoskModel *model;
static VoskRecognizer *recognizer;
static QStringList words;
static QList<int> word_indexes;
static QJsonObject vruwordsobject;
static int word_list_length;
static int word_list_count;
static QLibrary *voskLib;
static SDL_AudioDeviceID audio_dev;
static SDL_AudioSpec *hardware_spec;
static SDL_TimerID timer_id;
static QString currentGameMd5;
static QString currentGameGoodName;
static bool pakSwapHeld[4] = {false, false, false, false};
static bool pakSwapIgnoreRelease[4] = {false, false, false, false};
static bool pakSwapReloadCart[4] = {false, false, false, false};
static Uint64 controllerReconnectAttempt[4] = {0, 0, 0, 0};

struct GbLibraryEntry
{
    QString title;
    QString romPath;
    QString savePath;
};

static QList<GbLibraryEntry> gbLibraryEntries;
static bool pakSwapMenuVisible = false;
static int pakSwapMenuController = -1;

typedef VoskModel* (*ptr_vosk_model_new)(const char *model_path);
typedef void (*ptr_vosk_model_free)(VoskModel *model);
typedef VoskRecognizer* (*ptr_vosk_recognizer_new_grm)(VoskModel *model, float sample_rate, const char *grammar);
typedef void (*ptr_vosk_recognizer_free)(VoskRecognizer *recognizer);
typedef int (*ptr_vosk_recognizer_accept_waveform)(VoskRecognizer *recognizer, const char *data, int length);
typedef const char *(*ptr_vosk_recognizer_final_result)(VoskRecognizer *recognizer);
typedef void (*ptr_vosk_set_log_level)(int log_level);
typedef void (*ptr_vosk_recognizer_set_max_alternatives)(VoskRecognizer *recognizer, int max_alternatives);

ptr_vosk_recognizer_accept_waveform VoskAcceptWaveform;
ptr_vosk_recognizer_final_result VoskFinalResult;
ptr_vosk_model_new VoskNewModel;
ptr_vosk_recognizer_new_grm VoskNewRecognizer;
ptr_vosk_model_free VoskFreeModel;
ptr_vosk_recognizer_free VoskFreeRecognizer;
ptr_vosk_set_log_level VoskSetLogLevel;
ptr_vosk_recognizer_set_max_alternatives VoskSetAlternatives;

ptr_ConfigGetUserConfigPath ConfigGetUserConfigPath;
ptr_ConfigGetUserDataPath ConfigGetUserDataPath;

static void (*l_DebugCallback)(void *, int, const char *) = NULL;
void DebugMessage(int level, const char *message, ...);
static bool isBindingActive(int Control, const QString &button);
static bool isMenuDirectionActive(int controlIndex, const QString &dpadBinding, const QString &axisBinding);

static QString normalizeControllerPath(QString path)
{
    path = QDir::fromNativeSeparators(path.trimmed());
    return path.toLower();
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

static bool openControllerByPath(const QString &desiredPath, QList<int> &usedControllers, SDL_GameController **gamepadOut, SDL_Joystick **joystickOut)
{
    const QString normalizedDesiredPath = normalizeControllerPath(desiredPath);
    if (normalizedDesiredPath.isEmpty())
        return false;

    for (int j = 0; j < SDL_NumJoysticks(); ++j)
    {
        if (usedControllers.contains(j))
            continue;

        const char *candidatePathChars = SDL_JoystickPathForIndex(j);
        const QString candidatePath = candidatePathChars ? QString::fromUtf8(candidatePathChars) : QString();
        if (normalizeControllerPath(candidatePath) != normalizedDesiredPath)
            continue;

        if (SDL_IsGameController(j))
        {
            *gamepadOut = SDL_GameControllerOpen(j);
            if (*gamepadOut != nullptr)
            {
                *joystickOut = SDL_GameControllerGetJoystick(*gamepadOut);
                usedControllers.append(j);
                return true;
            }
        }
        else
        {
            *joystickOut = SDL_JoystickOpen(j);
            if (*joystickOut != nullptr)
            {
                usedControllers.append(j);
                return true;
            }
        }
    }

    return false;
}

static QString effectiveControllerSetting(int controllerIndex, const QString &name)
{
    if (controllerOverrideSettings != nullptr && !currentGameMd5.isEmpty())
    {
        const QString overrideKey = "Games/" + currentGameMd5 + "/Controller" + QString::number(controllerIndex + 1) + "/" + name;
        if (controllerOverrideSettings->contains(overrideKey))
            return controllerOverrideSettings->value(overrideKey).toString();
    }

    return gameControllerSettings->value("Controller" + QString::number(controllerIndex + 1) + "/" + name).toString();
}

static bool currentGameRequiresVru()
{
    const QString lowered = currentGameGoodName.toLower();
    return lowered.contains("hey you, pikachu!") || lowered.contains("pikachu genki dechu");
}

static bool controllerReconnectDue(int controlIndex)
{
    constexpr Uint64 reconnectIntervalMs = 250;
    const Uint64 now = SDL_GetTicks64();
    const Uint64 previousAttempt = controllerReconnectAttempt[controlIndex];

    if (previousAttempt != 0 && now - previousAttempt < reconnectIntervalMs)
        return false;

    controllerReconnectAttempt[controlIndex] = now;
    return true;
}

static QList<int> usedControllerIndices(int excludeControl)
{
    QList<int> usedControllers;

    for (int i = 0; i < 4; ++i)
    {
        if (i == excludeControl || controller[i].joystick == nullptr)
            continue;

        const QString desiredPath = normalizeControllerPath(controller[i].device_path);
        if (desiredPath.isEmpty())
            continue;

        for (int j = 0; j < SDL_NumJoysticks(); ++j)
        {
            const char *pathChars = SDL_JoystickPathForIndex(j);
            const QString currentPath = pathChars ? QString::fromUtf8(pathChars) : QString();
            if (normalizeControllerPath(currentPath) == desiredPath)
            {
                usedControllers.append(j);
                break;
            }
        }
    }

    return usedControllers;
}

static void tryOpenControllerForSlot(int controlIndex)
{
    QString gamepad = effectiveControllerSetting(controlIndex, "Gamepad");
    if (currentGameRequiresVru() && controlIndex == 3)
        gamepad = "Emulate VRU";

    if (gamepad == "Keyboard")
    {
        controller[controlIndex].control->Present = 1;
        return;
    }

    if (gamepad == "None" || gamepad == "Emulate VRU")
        return;

    QList<int> usedControllers = usedControllerIndices(controlIndex);

    if (gamepad == "Auto")
    {
        for (int j = 0; j < SDL_NumJoysticks(); ++j)
        {
            if (usedControllers.contains(j))
                continue;

            if (SDL_IsGameController(j))
            {
                controller[controlIndex].gamepad = SDL_GameControllerOpen(j);
                if (controller[controlIndex].gamepad != nullptr)
                {
                    controller[controlIndex].joystick = SDL_GameControllerGetJoystick(controller[controlIndex].gamepad);
                    controller[controlIndex].control->Present = 1;
                    break;
                }
            }
            else
            {
                controller[controlIndex].joystick = SDL_JoystickOpen(j);
                if (controller[controlIndex].joystick != nullptr)
                {
                    controller[controlIndex].control->Present = 1;
                    break;
                }
            }
        }
    }
    else
    {
        QString desiredPath;
        QString desiredName;
        if (parsePathBoundControllerSetting(gamepad, &desiredPath, &desiredName))
        {
            if (openControllerByPath(desiredPath, usedControllers, &controller[controlIndex].gamepad, &controller[controlIndex].joystick))
                controller[controlIndex].control->Present = 1;
        }
        else
        {
            const QStringList parts = gamepad.split(":");
            if (parts.size() >= 2)
            {
                const int controllerIndexValue = parts[0].toInt();
                const QString gamepadName = parts.mid(1).join(":");
                if (!usedControllers.contains(controllerIndexValue))
                {
                    if (SDL_IsGameController(controllerIndexValue) && gamepadName == QString(SDL_JoystickNameForIndex(controllerIndexValue)))
                    {
                        controller[controlIndex].gamepad = SDL_GameControllerOpen(controllerIndexValue);
                        if (controller[controlIndex].gamepad != nullptr)
                        {
                            controller[controlIndex].joystick = SDL_GameControllerGetJoystick(controller[controlIndex].gamepad);
                            controller[controlIndex].control->Present = 1;
                        }
                    }
                    else if (gamepadName == QString(SDL_JoystickNameForIndex(controllerIndexValue)))
                    {
                        controller[controlIndex].joystick = SDL_JoystickOpen(controllerIndexValue);
                        if (controller[controlIndex].joystick != nullptr)
                            controller[controlIndex].control->Present = 1;
                    }
                }
            }
        }
    }

    if (controller[controlIndex].joystick != nullptr && controller[controlIndex].device_path.isEmpty())
        controller[controlIndex].device_path = SDL_JoystickPath(controller[controlIndex].joystick);
}

static QString gbOverrideKey(int controllerIndex, const QString &name)
{
    return "Games/" + currentGameMd5 + "/Controller" + QString::number(controllerIndex + 1) + "/" + name;
}

static QString guiGlobalGbKey(int controllerIndex, const QString &name)
{
    return "Player" + QString::number(controllerIndex + 1) + name;
}

static QString normalizeGbTitle(QString title)
{
    title = title.toLower().trimmed();
    title.replace('&', "and");
    title.replace('\'', "");
    title.replace(QRegularExpression(QStringLiteral(R"(\s*[\(\[].*?[\)\]])")), "");
    title.replace(QRegularExpression(QStringLiteral(R"([^a-z0-9]+)")), " ");
    return title.simplified();
}

static bool isSupportedTransferPakTitle(const QString &title)
{
    const QString normalized = normalizeGbTitle(title);
    static const QStringList exactTitles = {
        "mario golf",
        "mario tennis",
        "mickeys speedway usa",
        "perfect dark",
        "pokemon red version",
        "pokemon blue version",
        "pokemon yellow version special pikachu edition",
        "pokemon gold version",
        "pokemon silver version",
        "pokemon crystal version"
    };

    if (exactTitles.contains(normalized))
        return true;

    if (normalized == "pokemon red" || normalized == "pokemon blue" || normalized == "pokemon yellow" ||
        normalized == "pokemon gold" || normalized == "pokemon silver" || normalized == "pokemon crystal")
        return true;

    return false;
}

static QString transferPakTitleKey(const QString &title)
{
    const QString normalized = normalizeGbTitle(title);

    if (normalized.contains("mario golf"))
        return QStringLiteral("mario golf");
    if (normalized.contains("mario tennis"))
        return QStringLiteral("mario tennis");
    if (normalized.contains("mickeys speedway usa"))
        return QStringLiteral("mickeys speedway usa");
    if (normalized.contains("perfect dark"))
        return QStringLiteral("perfect dark");
    if (normalized.contains("pokemon red"))
        return QStringLiteral("pokemon red");
    if (normalized.contains("pokemon blue"))
        return QStringLiteral("pokemon blue");
    if (normalized.contains("pokemon yellow"))
        return QStringLiteral("pokemon yellow");
    if (normalized.contains("pokemon gold"))
        return QStringLiteral("pokemon gold");
    if (normalized.contains("pokemon silver"))
        return QStringLiteral("pokemon silver");
    if (normalized.contains("pokemon crystal"))
        return QStringLiteral("pokemon crystal");

    return normalized;
}

static QString defaultGbSavePathForRom(const QString &romPath)
{
    const QFileInfo romInfo(romPath);
    return romInfo.dir().filePath(romInfo.completeBaseName() + ".sav");
}

static QString findGbSaveForRom(const QString &romPath)
{
    const QFileInfo romInfo(romPath);
    const QDir romDir = romInfo.dir();
    const QString baseName = romInfo.completeBaseName();
    const QString candidate = romDir.filePath(baseName + ".sav");
    if (QFileInfo::exists(candidate))
        return candidate;

    return defaultGbSavePathForRom(romPath);
}

static QString normalizePathForCompare(const QString &path)
{
    if (path.isEmpty())
        return QString();

    QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(path));
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return normalized;
}

static bool pathsEqual(const QString &left, const QString &right)
{
    return normalizePathForCompare(left) == normalizePathForCompare(right);
}

static QStringList loadGbSaveLibrary()
{
    QStringList savePaths;
    QSet<QString> seenPaths;

    QSettings guiSettings(QDir(QCoreApplication::applicationDirPath()).filePath("simple64-gui.ini"), QSettings::IniFormat);
    const QString folder = guiSettings.value("LibraryGbFolder").toString();
    if (!folder.isEmpty())
    {
        const QStringList filters = {"*.sav", "*.SAV"};
        QDirIterator it(folder, filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString savePath = it.next();
            if (seenPaths.contains(savePath))
                continue;
            savePaths.append(savePath);
            seenPaths.insert(savePath);
        }
    }

    std::sort(savePaths.begin(), savePaths.end(), [](const QString &left, const QString &right) {
        return QFileInfo(left).fileName().localeAwareCompare(QFileInfo(right).fileName()) < 0;
    });
    return savePaths;
}

static QString resolveGbSavePathInLibrary(const QString &savePath, const QStringList &saveLibrary)
{
    if (savePath.isEmpty())
        return QString();

    if (QFileInfo::exists(savePath))
        return savePath;

    const QString targetName = QFileInfo(savePath).fileName();
    for (const QString &candidate : saveLibrary)
    {
        if (QFileInfo(candidate).fileName().compare(targetName, Qt::CaseInsensitive) == 0)
            return candidate;
    }

    return savePath;
}

static void loadGbLibraryCache()
{
    gbLibraryEntries.clear();
    QSet<QString> seenRomPaths;

    QSettings guiSettings(QDir(QCoreApplication::applicationDirPath()).filePath("simple64-gui.ini"), QSettings::IniFormat);
    const QString folder = guiSettings.value("LibraryGbFolder").toString();
    if (folder.isEmpty())
        ;
    else
    {
        const QStringList filters = {"*.gb", "*.GB", "*.gbc", "*.GBC"};
        QDirIterator it(folder, filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString romPath = it.next();
            if (seenRomPaths.contains(romPath))
                continue;

            const QFileInfo info(romPath);
            if (!isSupportedTransferPakTitle(info.completeBaseName()))
                continue;

            GbLibraryEntry entry;
            entry.title = info.completeBaseName();
            entry.romPath = romPath;
            entry.savePath = findGbSaveForRom(romPath);
            gbLibraryEntries.append(entry);
            seenRomPaths.insert(romPath);
        }
    }

    for (int i = 1; i <= 4; ++i)
    {
        const QString romKey = "Player" + QString::number(i) + "GBROM";
        const QString ramKey = "Player" + QString::number(i) + "GBRAM";
        const QString romPath = guiSettings.value(romKey).toString();
        if (romPath.isEmpty() || seenRomPaths.contains(romPath))
            continue;

        const QFileInfo info(romPath);
        if (!isSupportedTransferPakTitle(info.completeBaseName()))
            continue;

        GbLibraryEntry entry;
        entry.title = info.completeBaseName();
        entry.romPath = romPath;
        entry.savePath = guiSettings.value(ramKey).toString();
        if (entry.savePath.isEmpty())
            entry.savePath = findGbSaveForRom(romPath);
        gbLibraryEntries.append(entry);
        seenRomPaths.insert(romPath);
    }
}

static QString effectiveGbMediaSetting(int controllerIndex, const QString &name)
{
    QSettings overrideSettings(QDir(ConfigGetUserConfigPath()).filePath("game-gb-settings.ini"), QSettings::IniFormat);
    const QString override = gbOverrideKey(controllerIndex, name);
    if (!currentGameMd5.isEmpty() && overrideSettings.contains(override))
        return overrideSettings.value(override).toString();

    QSettings guiSettings(QDir(QCoreApplication::applicationDirPath()).filePath("simple64-gui.ini"), QSettings::IniFormat);
    return guiSettings.value(guiGlobalGbKey(controllerIndex, name)).toString();
}

static bool savePakSwapSelection(int controllerIndex, const QString &pak, const QString &romPath, const QString &savePath)
{
    if (controllerIndex < 0 || controllerIndex >= 4 || currentGameMd5.isEmpty())
        return false;

    const QString oldRom = effectiveGbMediaSetting(controllerIndex, "GBROM");
    const QString oldSave = effectiveGbMediaSetting(controllerIndex, "GBRAM");
    if (pak == "Transfer")
    {
        const QString selectedRom = romPath.isEmpty()
            ? QSettings(QDir(QCoreApplication::applicationDirPath()).filePath("simple64-gui.ini"), QSettings::IniFormat).value(guiGlobalGbKey(controllerIndex, "GBROM")).toString()
            : romPath;
        const QString selectedSave = romPath.isEmpty()
            ? QSettings(QDir(QCoreApplication::applicationDirPath()).filePath("simple64-gui.ini"), QSettings::IniFormat).value(guiGlobalGbKey(controllerIndex, "GBRAM")).toString()
            : savePath;
        if (!QFileInfo(selectedRom).isFile() || !QFileInfo(selectedRom).isReadable()
            || !QFileInfo(selectedSave).isFile() || !QFileInfo(selectedSave).isReadable())
        {
            QMessageBox::warning(nullptr, "Pak Swap", "Select an existing GB/GBC cartridge and its .sav file. No settings were changed.");
            return false;
        }
        QFile cartridge(selectedRom);
        const QByteArray header = cartridge.open(QIODevice::ReadOnly) ? cartridge.read(0x150) : QByteArray();
        const qint64 ramSizes[] = {0, 2048, 8192, 32768, 131072, 65536};
        const unsigned int ramCode = header.size() == 0x150 ? static_cast<unsigned char>(header[0x149]) : 255;
        qint64 expectedRam = ramCode < 6 ? ramSizes[ramCode] : -1;
        if (ramCode == 0 && (header[0x147] == 5 || header[0x147] == 6)) expectedRam = 512;
        if (cartridge.size() < 32768 || expectedRam < 0
            || (expectedRam > 0 && QFileInfo(selectedSave).size() != expectedRam))
        {
            QMessageBox::warning(nullptr, "Pak Swap", "The cartridge header or save size is invalid for this GB/GBC cartridge. No settings were changed.");
            return false;
        }
    }

    QSettings overrideSettings(QDir(ConfigGetUserConfigPath()).filePath("game-gb-settings.ini"), QSettings::IniFormat);
    const QString romKey = gbOverrideKey(controllerIndex, "GBROM");
    const QString ramKey = gbOverrideKey(controllerIndex, "GBRAM");
    const QString pakKey = "Games/" + currentGameMd5 + "/Controller" + QString::number(controllerIndex + 1) + "/Pak";
    const QVariant previousPak = controllerOverrideSettings->value(pakKey);
    const QVariant previousRom = overrideSettings.value(romKey);
    const QVariant previousRam = overrideSettings.value(ramKey);

    if (pak == "Transfer" && !romPath.isEmpty())
    {
        overrideSettings.setValue(romKey, romPath);
        overrideSettings.setValue(ramKey, savePath);
    }
    else if (pak == "Transfer")
    {
        overrideSettings.remove(romKey);
        overrideSettings.remove(ramKey);
    }

    if (pak == "Transfer") overrideSettings.sync();
    controllerOverrideSettings->setValue(pakKey, pak);
    controllerOverrideSettings->sync();
    if (controllerOverrideSettings->status() != QSettings::NoError
        || (pak == "Transfer" && overrideSettings.status() != QSettings::NoError))
    {
        const auto restore = [](QSettings* target, const QString& key, const QVariant& value) {
            if (value.isValid()) target->setValue(key, value);
            else target->remove(key);
        };
        restore(controllerOverrideSettings, pakKey, previousPak);
        if (pak == "Transfer") {
            restore(&overrideSettings, romKey, previousRom);
            restore(&overrideSettings, ramKey, previousRam);
            overrideSettings.sync();
        }
        controllerOverrideSettings->sync();
        QMessageBox::warning(nullptr, "Pak Swap", "Could not save the Pak selection. The previous selection was kept.");
        return false;
    }
    if (pak == "Transfer")
        pakSwapReloadCart[controllerIndex] = !pathsEqual(oldRom, effectiveGbMediaSetting(controllerIndex, "GBROM"))
            || !pathsEqual(oldSave, effectiveGbMediaSetting(controllerIndex, "GBRAM"));
    return true;
}

static void openPakSwapMenu(int controllerIndex)
{
    if (pakSwapMenuVisible || currentGameMd5.isEmpty())
        return;

    loadGbLibraryCache();
    pakSwapMenuVisible = true;
    pakSwapMenuController = controllerIndex;

    QMetaObject::invokeMethod(qApp, [controllerIndex]() {
        QWidget *parentWindow = QApplication::activeModalWidget();
        if (parentWindow == nullptr)
            parentWindow = QApplication::activeWindow();

        QDialog dialog(parentWindow);
        dialog.setWindowTitle("Pak Swap");
        dialog.setModal(true);
        dialog.setWindowFlags(dialog.windowFlags() & ~Qt::WindowContextHelpButtonHint);
        dialog.setWindowFlag(Qt::WindowStaysOnTopHint, true);
        dialog.setMinimumWidth(460);

        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        QLabel *label = new QLabel("Changes apply during local gameplay and are saved for this game. Swap only when the game is not saving or accessing a GB cartridge. Netplay keeps its current Pak.", &dialog);
        label->setWordWrap(true);
        layout->addWidget(label);

        QFormLayout *form = new QFormLayout;
        QComboBox *pakCombo = new QComboBox(&dialog);
        pakCombo->addItems(QStringList() << "Memory" << "Rumble" << "Transfer" << "None");
        const QString currentPak = effectiveControllerSetting(controllerIndex, "Pak");
        const int pakIndex = pakCombo->findText(currentPak.isEmpty() ? "Memory" : currentPak);
        pakCombo->setCurrentIndex(pakIndex >= 0 ? pakIndex : 0);
        form->addRow("Pak", pakCombo);

        QComboBox *cartCombo = new QComboBox(&dialog);
        cartCombo->addItem("<Use Default>");
        for (const GbLibraryEntry &entry : gbLibraryEntries)
        {
            QVariantMap data;
            data.insert("romPath", entry.romPath);
            data.insert("savePath", entry.savePath);
            cartCombo->addItem(entry.title, data);
        }

        const QString currentRom = effectiveGbMediaSetting(controllerIndex, "GBROM");
        int selectedCartIndex = 0;
        for (int i = 1; i < cartCombo->count(); ++i)
        {
            const QVariantMap data = cartCombo->itemData(i).toMap();
            if (pathsEqual(data.value("romPath").toString(), currentRom))
            {
                selectedCartIndex = i;
                break;
            }
        }
        cartCombo->setCurrentIndex(selectedCartIndex);
        cartCombo->setEnabled(pakCombo->currentText() == "Transfer");
        form->addRow("GB/GBC Cart", cartCombo);

        QComboBox *saveCombo = new QComboBox(&dialog);
        auto rebuildSaveCombo = [controllerIndex, cartCombo, saveCombo]() {
            const QVariantMap cartData = cartCombo->currentData().toMap();
            const QString defaultSavePath = cartData.value("savePath").toString();
            const QString cartTitleKey = transferPakTitleKey(QFileInfo(cartData.value("romPath").toString()).completeBaseName());
            QStringList saveLibrary = loadGbSaveLibrary();
            const QString configuredSavePath = resolveGbSavePathInLibrary(effectiveGbMediaSetting(controllerIndex, "GBRAM"), saveLibrary);
            const QString currentSelectedPath = saveCombo->currentData().toString();
            const QString preferredSavePath = currentSelectedPath.isEmpty()
                ? configuredSavePath
                : resolveGbSavePathInLibrary(currentSelectedPath, saveLibrary);

            saveCombo->clear();

            QSet<QString> seenPaths;
            auto addSaveOption = [&](const QString &savePath) {
                if (savePath.isEmpty())
                    return;

                if (!cartTitleKey.isEmpty())
                {
                    const QString saveTitleKey = transferPakTitleKey(QFileInfo(savePath).completeBaseName());
                    if (saveTitleKey != cartTitleKey)
                        return;
                }

                const QString normalizedPath = normalizePathForCompare(savePath);
                if (seenPaths.contains(normalizedPath))
                    return;

                saveCombo->addItem(QFileInfo(savePath).fileName(), savePath);
                seenPaths.insert(normalizedPath);
            };

            for (const QString &savePath : saveLibrary)
                addSaveOption(savePath);

            int selectedSaveIndex = 0;
            if (!preferredSavePath.isEmpty())
            {
                for (int i = 0; i < saveCombo->count(); ++i)
                {
                    if (pathsEqual(saveCombo->itemData(i).toString(), preferredSavePath))
                    {
                        selectedSaveIndex = i;
                        break;
                    }
                }
            }
            else if (!defaultSavePath.isEmpty())
            {
                const QString resolvedDefaultSave = resolveGbSavePathInLibrary(defaultSavePath, saveLibrary);
                for (int i = 0; i < saveCombo->count(); ++i)
                {
                    if (pathsEqual(saveCombo->itemData(i).toString(), resolvedDefaultSave))
                    {
                        selectedSaveIndex = i;
                        break;
                    }
                }
            }

            if (saveCombo->count() > 0)
                saveCombo->setCurrentIndex(selectedSaveIndex);
        };
        rebuildSaveCombo();
        saveCombo->setEnabled(pakCombo->currentText() == "Transfer");
        QObject::connect(pakCombo, &QComboBox::currentTextChanged, &dialog, [cartCombo, saveCombo](const QString &text) {
            const bool enableTransferFields = (text == "Transfer");
            cartCombo->setEnabled(enableTransferFields);
            saveCombo->setEnabled(enableTransferFields);
        });
        QObject::connect(cartCombo, &QComboBox::currentIndexChanged, &dialog, [rebuildSaveCombo](int) {
            rebuildSaveCombo();
        });
        form->addRow("Save File", saveCombo);

        layout->addLayout(form);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        QPushButton *okButton = buttons->button(QDialogButtonBox::Ok);
        QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel);
        int currentRow = 0; // 0 pak, 1 cart, 2 save, 3 buttons
        int currentButton = 0; // 0 ok, 1 cancel
        bool editingCombo = false;

        auto applySelectionUi = [&]() {
            const QString comboSelectedStyle = QStringLiteral("border: 2px solid #4f8cff;");
            const QString comboEditingStyle = QStringLiteral("border: 2px solid #2dbf6a;");
            const QString clearStyle = QString();

            pakCombo->setStyleSheet(clearStyle);
            cartCombo->setStyleSheet(clearStyle);
            saveCombo->setStyleSheet(clearStyle);
            okButton->setStyleSheet(clearStyle);
            cancelButton->setStyleSheet(clearStyle);

            if (currentRow == 0)
            {
                pakCombo->setStyleSheet(editingCombo ? comboEditingStyle : comboSelectedStyle);
            }
            else if (currentRow == 1)
            {
                cartCombo->setStyleSheet(editingCombo ? comboEditingStyle : comboSelectedStyle);
            }
            else if (currentRow == 2)
            {
                saveCombo->setStyleSheet(editingCombo ? comboEditingStyle : comboSelectedStyle);
            }
            else
            {
                if (currentButton == 0)
                {
                    okButton->setStyleSheet(comboSelectedStyle);
                }
                else
                {
                    cancelButton->setStyleSheet(comboSelectedStyle);
                }
            }
        };

        auto stepCombo = [](QComboBox *combo, int delta) {
            if (combo == nullptr || !combo->isEnabled() || combo->count() <= 0)
                return;

            int nextIndex = combo->currentIndex() + delta;
            if (nextIndex < 0)
                nextIndex = combo->count() - 1;
            else if (nextIndex >= combo->count())
                nextIndex = 0;
            combo->setCurrentIndex(nextIndex);
        };

        applySelectionUi();

        QTimer navigationTimer(&dialog);
        navigationTimer.setInterval(16);
        struct MenuState
        {
            bool up = false;
            bool down = false;
            bool left = false;
            bool right = false;
            bool confirm = false;
            bool b = false;
        };
        MenuState lastState;

        QObject::connect(&navigationTimer, &QTimer::timeout, &dialog, [&]() {
            SDL_PumpEvents();
            MenuState state;
            state.up = isMenuDirectionActive(controllerIndex, "DPadU", "AxisUp");
            state.down = isMenuDirectionActive(controllerIndex, "DPadD", "AxisDown");
            state.left = isMenuDirectionActive(controllerIndex, "DPadL", "AxisLeft");
            state.right = isMenuDirectionActive(controllerIndex, "DPadR", "AxisRight");
            state.confirm = isBindingActive(controllerIndex, "PakSwap");
            state.b = isBindingActive(controllerIndex, "B");

            if (editingCombo)
            {
                QComboBox *activeCombo = (currentRow == 0) ? pakCombo : (currentRow == 1 ? cartCombo : saveCombo);
                if (state.up && !lastState.up)
                    stepCombo(activeCombo, -1);
                if (state.down && !lastState.down)
                    stepCombo(activeCombo, 1);
                if (state.left && !lastState.left)
                    stepCombo(activeCombo, -1);
                if (state.right && !lastState.right)
                    stepCombo(activeCombo, 1);
                if ((state.confirm && !lastState.confirm) || (state.b && !lastState.b))
                    editingCombo = false;
            }
            else
            {
                if (state.up && !lastState.up)
                {
                    currentRow = (currentRow + 3) % 4;
                }
                if (state.down && !lastState.down)
                {
                    currentRow = (currentRow + 1) % 4;
                }
                if (currentRow == 3)
                {
                    if ((state.left && !lastState.left) || (state.right && !lastState.right))
                        currentButton = 1 - currentButton;
                }
                if (state.confirm && !lastState.confirm)
                {
                    if (currentRow == 0)
                        editingCombo = true;
                    else if (currentRow == 1)
                    {
                        if (cartCombo->isEnabled())
                            editingCombo = true;
                    }
                    else if (currentRow == 2)
                    {
                        if (saveCombo->isEnabled())
                            editingCombo = true;
                    }
                    else if (currentButton == 0)
                        dialog.accept();
                    else
                        dialog.reject();
                }
                if (state.b && !lastState.b)
                    dialog.reject();
            }

            applySelectionUi();
            lastState = state;
        });
        navigationTimer.start();
        applySelectionUi();
        dialog.adjustSize();

        if (dialog.exec() == QDialog::Accepted)
        {
            const QVariantMap data = cartCombo->currentData().toMap();
            QString selectedSave = saveCombo->currentData().toString();
            if (savePakSwapSelection(
                controllerIndex,
                pakCombo->currentText(),
                data.value("romPath").toString(),
                selectedSave))
                DebugMessage(M64MSG_STATUS, "Pak Swap selection saved for controller %d", controllerIndex + 1);
        }

        pakSwapIgnoreRelease[controllerIndex] = true;
        pakSwapMenuVisible = false;
        pakSwapMenuController = -1;
    }, Qt::BlockingQueuedConnection);
}

static void shutdownVosk(bool fullShutdown)
{
    if (timer_id)
    {
        SDL_RemoveTimer(timer_id);
        timer_id = 0;
    }

    l_TalkingState = 0;

    if (audio_dev)
        SDL_PauseAudioDevice(audio_dev, 1);

    if (recognizer)
    {
        VoskFreeRecognizer(recognizer);
        recognizer = nullptr;
    }

    if (fullShutdown)
    {
        if (model)
        {
            VoskFreeModel(model);
            model = nullptr;
        }
        if (voskLib)
        {
            delete voskLib;
            voskLib = NULL;
        }
        if (audio_dev)
        {
            SDL_CloseAudioDevice(audio_dev);
            audio_dev = 0;
        }
        if (hardware_spec != NULL)
        {
            free(hardware_spec);
            hardware_spec = NULL;
        }
    }
}

void DebugMessage(int level, const char *message, ...)
{
    char msgbuf[1024];
    va_list args;

    if (l_DebugCallback == NULL)
        return;

    va_start(args, message);
    vsprintf(msgbuf, message, args);

    (*l_DebugCallback)((char*)"Input", level, msgbuf);

    va_end(args);
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreHandle, void * object, void (*DebugCallback)(void *, int, const char *))
{
    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    coreHandle = CoreHandle;
    l_DebugCallback = DebugCallback;

    ConfigGetUserConfigPath = (ptr_ConfigGetUserConfigPath) osal_dynlib_getproc(CoreHandle, "ConfigGetUserConfigPath");
    ConfigGetUserDataPath = (ptr_ConfigGetUserDataPath) osal_dynlib_getproc(CoreHandle, "ConfigGetUserDataPath");

    QDir ini_path(ConfigGetUserConfigPath());
    settings = new QSettings(ini_path.absoluteFilePath("input-profiles.ini"), QSettings::IniFormat, (QObject*)object);
    controllerSettings = new QSettings(ini_path.absoluteFilePath("input-settings.ini"), QSettings::IniFormat, (QObject*)object);
    controllerOverrideSettings = new QSettings(ini_path.absoluteFilePath("game-input-settings.ini"), QSettings::IniFormat, (QObject*)object);

    if (!settings->contains("version") || settings->value("version").toInt() != SETTINGS_VER)
    {
        settings->clear();
        settings->setValue("version", SETTINGS_VER);
    }
    if (!controllerSettings->contains("version") || controllerSettings->value("version").toInt() != SETTINGS_VER)
    {
        controllerSettings->clear();
        controllerSettings->setValue("version", SETTINGS_VER);
    }

    settings->remove("8BitDo-64-Gamepad");
    settings->remove("Gamepad-C-Clicks");
    settings->remove("N64 Controller Preset");

    QString section;
    for (int i = 1; i < 5; ++i) {
        section = "Controller" + QString::number(i);
        if (!controllerSettings->childGroups().contains(section)) {
            controllerSettings->setValue(section + "/Profile", "Auto");
            controllerSettings->setValue(section + "/Gamepad", "Auto");
            controllerSettings->setValue(section + "/Pak", "Memory");
        }
    }

    QStringList values;
    section = "Auto-Keyboard";
    values.insert(0, "0"/*blank value*/);
    values.insert(1, "0"/*Keyboard*/);
    if (!settings->childGroups().contains(section)) {
        values.replace(0, QString::number(SDL_SCANCODE_LSHIFT));
        settings->setValue(section + "/A", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_LCTRL));
        settings->setValue(section + "/B", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_Z));
        settings->setValue(section + "/Z", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_X));
        settings->setValue(section + "/L", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_C));
        settings->setValue(section + "/R", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_RETURN));
        settings->setValue(section + "/Start", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_A));
        settings->setValue(section + "/DPadL", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_D));
        settings->setValue(section + "/DPadR", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_W));
        settings->setValue(section + "/DPadU", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_S));
        settings->setValue(section + "/DPadD", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_J));
        settings->setValue(section + "/CLeft", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_L));
        settings->setValue(section + "/CRight", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_I));
        settings->setValue(section + "/CUp", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_K));
        settings->setValue(section + "/CDown", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_LEFT));
        settings->setValue(section + "/AxisLeft", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_RIGHT));
        settings->setValue(section + "/AxisRight", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_UP));
        settings->setValue(section + "/AxisUp", values.join(","));
        values.replace(0, QString::number(SDL_SCANCODE_DOWN));
        settings->setValue(section + "/AxisDown", values.join(","));

        settings->setValue(section + "/Deadzone", DEADZONE_DEFAULT);
        settings->setValue(section + "/Sensitivity", 100.0);
    }
    if (!settings->contains(section + "/PakSwap")) {
        values.replace(1, "0"/*Keyboard*/);
        values.replace(0, QString::number(SDL_SCANCODE_TAB));
        if (values.size() > 2)
            values.removeAt(2);
        settings->setValue(section + "/PakSwap", values.join(","));
    }

    section = "Auto-Gamepad";
    values.replace(1, "1"/*Button*/);
    if (!settings->childGroups().contains(section)) {
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_A));
        settings->setValue(section + "/A", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_X));
        settings->setValue(section + "/B", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        values.replace(1, "2"/*Axis*/);
        values.insert(2, "1" /* positive axis value*/);
        settings->setValue(section + "/Z", values.join(","));
        values.removeAt(2);
        values.replace(1, "1"/*Button*/);
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        settings->setValue(section + "/L", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
        settings->setValue(section + "/R", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_START));
        settings->setValue(section + "/Start", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
        settings->setValue(section + "/DPadL", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        settings->setValue(section + "/DPadR", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_DPAD_UP));
        settings->setValue(section + "/DPadU", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        settings->setValue(section + "/DPadD", values.join(","));
        values.replace(1, "2"/*Axis*/);
        values.replace(0, QString::number(SDL_CONTROLLER_AXIS_RIGHTX));
        values.insert(2, "-1" /* negative axis value*/);
        settings->setValue(section + "/CLeft", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_AXIS_RIGHTX));
        values.replace(2, "1" /* positive axis value*/);
        settings->setValue(section + "/CRight", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_AXIS_RIGHTY));
        values.replace(2, "-1" /* negative axis value*/);
        settings->setValue(section + "/CUp", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_AXIS_RIGHTY));
        values.replace(2, "1" /* positive axis value*/);
        settings->setValue(section + "/CDown", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_AXIS_LEFTX));
        values.replace(2, "-1" /* negative axis value*/);
        settings->setValue(section + "/AxisLeft", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_AXIS_LEFTX));
        values.replace(2, "1" /* positive axis value*/);
        settings->setValue(section + "/AxisRight", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_AXIS_LEFTY));
        values.replace(2, "-1" /* negative axis value*/);
        settings->setValue(section + "/AxisUp", values.join(","));
        values.replace(0, QString::number(SDL_CONTROLLER_AXIS_LEFTY));
        values.replace(2, "1" /* positive axis value*/);
        settings->setValue(section + "/AxisDown", values.join(","));

        settings->setValue(section + "/Deadzone", DEADZONE_DEFAULT);
        settings->setValue(section + "/Sensitivity", 100.0);
    }
    if (!settings->contains(section + "/PakSwap")) {
        values.replace(1, "1"/*Button*/);
        values.replace(0, QString::number(SDL_CONTROLLER_BUTTON_BACK));
        if (values.size() > 2)
            values.removeAt(2);
        settings->setValue(section + "/PakSwap", values.join(","));
    }

    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER))
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);

    if (!SDL_WasInit(SDL_INIT_AUDIO))
        SDL_InitSubSystem(SDL_INIT_AUDIO);

    if (!SDL_WasInit(SDL_INIT_TIMER))
        SDL_InitSubSystem(SDL_INIT_TIMER);

    loadGbLibraryCache();

    l_PluginInit = 1;

    return M64ERR_SUCCESS;
}

void closeControllers()
{
    for (int i = 0; i < 4; ++i) {
        if (controller[i].gamepad != NULL)
            SDL_GameControllerClose(controller[i].gamepad);
        else if (controller[i].joystick != NULL)
            SDL_JoystickClose(controller[i].joystick);
        controller[i].gamepad = NULL;
        controller[i].joystick = NULL;
    }
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    closeControllers();
    shutdownVosk(true);

    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_QuitSubSystem(SDL_INIT_TIMER);

    settings->sync();
    controllerSettings->sync();

    l_PluginInit = 0;

    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_INPUT;

    if (PluginVersion != NULL)
        *PluginVersion = QT_INPUT_PLUGIN_VERSION;

    if (APIVersion != NULL)
        *APIVersion = INPUT_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = "simple64 Qt Input Plugin";

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }

    return M64ERR_SUCCESS;
}

static unsigned char DataCRC( unsigned char *Data, int iLenght )
{
    unsigned char Remainder = Data[0];

    int iByte = 1;
    unsigned char bBit = 0;

    while( iByte <= iLenght )
    {
        int HighBit = ((Remainder & 0x80) != 0);
        Remainder = Remainder << 1;

        Remainder += ( iByte < iLenght && Data[iByte] & (0x80 >> bBit )) ? 1 : 0;

        Remainder ^= (HighBit) ? 0x85 : 0;

        bBit++;
        iByte += bBit/8;
        bBit %= 8;
    }

    return Remainder;
}

EXPORT void CALL SendVRUWord(uint16_t length, uint16_t *word, uint8_t lang)
{
    QByteArray word_array;
    uint8_t *bytes = (uint8_t*)word;
    for (int i = 0; i < (length*2); i+=2)
    {
        word_array.append(bytes[i+1]);
        word_array.append(bytes[i]);
    }

    QString hex = word_array.toHex();
    QJsonValue value = vruwordsobject.value(hex.toUpper());
    if (value == QJsonValue::Undefined)
    {
        auto toUtf8 = QStringDecoder(QStringDecoder::Utf8);
        QString encoded_string = toUtf8(word_array);
        if (toUtf8.hasError())
        {
            if (lang == 0 /* English */)
                DebugMessage(M64MSG_ERROR, "Unknown word: %s", hex.toUpper().toUtf8().constData());
            else /* Japanese or demo */
                DebugMessage(M64MSG_ERROR, "Unknown Japanese word: %s", hex.toUpper().toUtf8().constData());
        }
        else
        {
            words.append(encoded_string.toLower());
            word_indexes.append(word_list_count);
        }
    }
    else
    {
        QJsonArray value_list = value.toArray();
        for (int i = 0; i < value_list.size(); ++i)
        {
            words.append(value_list.at(i).toString().toLower());
            word_indexes.append(word_list_count);
            DebugMessage(M64MSG_VERBOSE, "word loaded: %s, index: %d", words.last().toUtf8().constData(), word_indexes.last());
        }
    }

    ++word_list_count;
    if (word_list_count == word_list_length)
    {
        words.append("[unk]");
        word_indexes.append(word_list_count);
        QJsonArray array = QJsonArray::fromStringList(words);
        QJsonDocument doc;
        doc.setArray(array);
        if (recognizer)
            VoskFreeRecognizer(recognizer);
        recognizer = VoskNewRecognizer(model, (float)hardware_spec->freq, doc.toJson().constData());
        VoskSetAlternatives(recognizer, 3);
    }
}

Uint32 StopTalking(Uint32, void *)
{
    l_TalkingState = 0;
    timer_id = 0;
    return(0);
}

EXPORT void CALL SetMicState(int state)
{
    if (timer_id)
    {
        SDL_RemoveTimer(timer_id);
        timer_id = 0;
    }
    l_TalkingState = state;

    if (state)
    {
        timer_id = SDL_AddTimer(2000, StopTalking, NULL);
        SDL_ClearQueuedAudio(audio_dev);
        SDL_PauseAudioDevice(audio_dev, 0);
    }
    else
        SDL_PauseAudioDevice(audio_dev, 1);
}

EXPORT void CALL ClearVRUWords(uint8_t length)
{
    word_list_count = 0;
    word_list_length = length;
    words.clear();
    word_indexes.clear();
    if (recognizer)
        VoskFreeRecognizer(recognizer);
    DebugMessage(M64MSG_VERBOSE, "word list cleared");
    recognizer = nullptr;
}

EXPORT void CALL SetVRUWordMask(uint8_t, uint8_t *)
{
}

static bool compare(QString &s1, QString &s2)
{
    return s1.size() > s2.size();
}

EXPORT void CALL ReadVRUResults(uint16_t *error_flags, uint16_t *num_results, uint16_t *mic_level, uint16_t *voice_level, uint16_t *voice_length, uint16_t *matches)
{
    SDL_PauseAudioDevice(audio_dev, 1);
    uint16_t match[5] = {0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF};
    *error_flags = 0;
    QByteArray data(SDL_GetQueuedAudioSize(audio_dev), 0);
    uint32_t audio_length = SDL_DequeueAudio(audio_dev, data.data(), SDL_GetQueuedAudioSize(audio_dev));
    VoskAcceptWaveform(recognizer, data.constData(), audio_length);
    QJsonDocument doc = QJsonDocument::fromJson(VoskFinalResult(recognizer));
    QJsonObject object = doc.object();
    QStringList found;
    QStringList alternatives;
    QString textResult;
    QJsonArray alternatives_array = object.value("alternatives").toArray();
    for (int i = 0; i < alternatives_array.size(); ++i)
    {
        textResult = alternatives_array.at(i).toObject().value("text").toString();
        if (!textResult.isEmpty())
            alternatives.append(textResult);
    }
    for (int i = 0; i < words.size() - 1 /* -1 to skip the [unk] entry */; ++i)
    {
        if (!found.contains(words.at(i)) && alternatives.filter(words.at(i)).size() > 0)
        {
            /* found a match */
            found.append(words.at(i));
            if (found.size() == 5)
                break;
        }
    }
    /* Use the longest match */
    std::sort(found.begin(), found.end(), compare);
    for (int i = 0; i < found.size(); ++i)
    {
        match[i] = word_indexes.at(words.indexOf(found.at(i)));
        DebugMessage(M64MSG_INFO, "match %d: %s (%u)", i, found.at(i).toUtf8().constData(), match[i]);
    }
    if (found.size() == 0 && alternatives.size() > 0)
    {
        /* heard something but it didn't match anything */
        *error_flags = 0x4000;
        match[0] = 0; // we match index 0, but mark the error flag saying we are really not sure
        found.append("0");
        DebugMessage(M64MSG_INFO, "heard a noise, but no word match");
    }

    *num_results = found.size();
    /* The N64 programming manual states what range these values should be within */
    *mic_level = 0xBB8;
    *voice_level = 0xBB8;
    *voice_length = 0x8004;
    matches[0] = match[0];
    matches[1] = *error_flags ? 0x700 : 0;
    matches[2] = match[1];
    matches[3] = (match[1] == 0x7FFF) ? 0 : 0x100;
    matches[4] = match[2];
    matches[5] = (match[2] == 0x7FFF) ? 0 : 0x200;
    matches[6] = match[3];
    matches[7] = (match[3] == 0x7FFF) ? 0 : 0x300;
    matches[8] = match[4];
    matches[9] = (match[4] == 0x7FFF) ? 0 : 0x400;
}

EXPORT void CALL ControllerCommand(int Control, unsigned char *Command)
{
    unsigned char *Data = &Command[5];

    if (Control == -1)
        return;

    switch (Command[2])
    {
        case RD_GETSTATUS:
        case RD_READKEYS:
            break;
        case RD_READPAK:
            if (controller[Control].control->Plugin == PLUGIN_RAW)
            {
                unsigned int dwAddress = (Command[3] << 8) + (Command[4] & 0xE0);

                if(( dwAddress >= 0x8000 ) && ( dwAddress < 0x9000 ) )
                    memset( Data, 0x80, 32 );
                else
                    memset( Data, 0x00, 32 );

                Data[32] = DataCRC( Data, 32 );
            }
            break;
        case RD_WRITEPAK:
            if (controller[Control].control->Plugin == PLUGIN_RAW)
            {
                unsigned int dwAddress = (Command[3] << 8) + (Command[4] & 0xE0);
                if(dwAddress == PAK_IO_RUMBLE) {
                    if (*Data) {
                        if (controller[Control].gamepad)
                            SDL_GameControllerRumble(controller[Control].gamepad, 0xFFFF, 0xFFFF, SDL_HAPTIC_INFINITY);
                        else
                            SDL_JoystickRumble(controller[Control].joystick, 0xFFFF, 0xFFFF, SDL_HAPTIC_INFINITY);
                    } else {
                        if (controller[Control].gamepad)
                            SDL_GameControllerRumble(controller[Control].gamepad, 0, 0, 0);
                        else
                            SDL_JoystickRumble(controller[Control].joystick, 0, 0, 0);
                    }
                }
                Data[32] = DataCRC( Data, 32 );
            }
            break;
        case RD_RESETCONTROLLER:
        case RD_READEEPROM:
        case RD_WRITEEPROM:
            break;
        }
}

void boundAxis(double* x, double* y) {
    double radius = 95.0; // this is roughly the maxium diagonal distance of the controller

    // Calculate the distance from the origin (0, 0)
    double distance = sqrt((*x) * (*x) + (*y) * (*y));

    // If the distance is greater than the radius, scale the coordinates
    if (distance > radius) {
        double scaleFactor = radius / distance;
        *x *= scaleFactor;
        *y *= scaleFactor;
    }
}

int modifyAxisValue(int axis_value, int Control, int direction)
{
    axis_value = ((abs(axis_value) - controller[Control].deadzone) * MAX_AXIS_VALUE) / controller[Control].range;
    axis_value *= direction;
    axis_value = (float)axis_value * controller[Control].sensitivity;
    axis_value = std::max(-MAX_AXIS_VALUE, std::min(axis_value, MAX_AXIS_VALUE));

    return axis_value;
}

void setAxis(int Control, int axis, BUTTONS *Keys, QString axis_dir, int direction)
{
    int axis_value;
    QStringList value = gameSettings->value(controller[Control].profile + "/" + axis_dir).toString().split(",");

    switch (value.at(1).toInt()) {
        case 0 /*Keyboard*/:
            if (myKeyState[value.at(0).toInt()]) {
                if (axis == 0)
                    Keys->X_AXIS = (int8_t)(MAX_AXIS_VALUE * direction);
                else
                    Keys->Y_AXIS = (int8_t)(MAX_AXIS_VALUE * direction);
            }
            break;
        case 1 /*Button*/:
            if (SDL_GameControllerGetButton(controller[Control].gamepad, (SDL_GameControllerButton)value.at(0).toInt())) {
                if (axis == 0)
                    Keys->X_AXIS = (int8_t)(MAX_AXIS_VALUE * direction);
                else
                    Keys->Y_AXIS = (int8_t)(MAX_AXIS_VALUE * direction);
            }
            break;
        case 2 /*Axis*/:
            axis_value = SDL_GameControllerGetAxis(controller[Control].gamepad, (SDL_GameControllerAxis)value.at(0).toInt());
            if (abs(axis_value) > controller[Control].deadzone && axis_value * value.at(2).toInt() > 0) {
                axis_value = modifyAxisValue(axis_value, Control, direction);
                if (axis == 0)
                    Keys->X_AXIS = (int8_t)axis_value;
                else
                    Keys->Y_AXIS = (int8_t)axis_value;
            }
            break;
        case 3 /*Joystick Hat*/:
            if (SDL_JoystickGetHat(controller[Control].joystick, value.at(0).toInt()) & value.at(2).toInt()) {
                if (axis == 0)
                    Keys->X_AXIS = (int8_t)(MAX_AXIS_VALUE * direction);
                else
                    Keys->Y_AXIS = (int8_t)(MAX_AXIS_VALUE * direction);
            }
            break;
        case 4 /*Joystick Button*/:
            if (SDL_JoystickGetButton(controller[Control].joystick, value.at(0).toInt())) {
                if (axis == 0)
                    Keys->X_AXIS = (int8_t)(MAX_AXIS_VALUE * direction);
                else
                    Keys->Y_AXIS = (int8_t)(MAX_AXIS_VALUE * direction);
            }
            break;
        case 5 /*Joystick Axis*/:
            axis_value = SDL_JoystickGetAxis(controller[Control].joystick, value.at(0).toInt());
            if (abs(axis_value) > controller[Control].deadzone && axis_value * value.at(2).toInt() > 0) {
                axis_value = modifyAxisValue(axis_value, Control, direction);
                if (axis == 0)
                    Keys->X_AXIS = (int8_t)axis_value;
                else
                    Keys->Y_AXIS = (int8_t)axis_value;
            }
            break;
    }
}

void setKey(int Control, uint32_t key, BUTTONS *Keys, QString button)
{
    int axis_value;
    QStringList value = gameSettings->value(controller[Control].profile + "/" + button).toString().split(",");
    switch (value.at(1).toInt()) {
        case 0 /*Keyboard*/:
            if (myKeyState[value.at(0).toInt()])
                Keys->Value |= key;
            break;
        case 1 /*Button*/:
            if (SDL_GameControllerGetButton(controller[Control].gamepad, (SDL_GameControllerButton)value.at(0).toInt()))
                Keys->Value |= key;
            break;
        case 2 /*Axis*/:
            axis_value = SDL_GameControllerGetAxis(controller[Control].gamepad, (SDL_GameControllerAxis)value.at(0).toInt());
            if (abs(axis_value) >= (AXIS_PEAK / 2) && axis_value * value.at(2).toInt() > 0)
                Keys->Value |= key;
            break;
        case 3 /*Joystick Hat*/:
            if (SDL_JoystickGetHat(controller[Control].joystick, value.at(0).toInt()) & value.at(2).toInt())
                Keys->Value |= key;
            break;
        case 4 /*Joystick Button*/:
            if (SDL_JoystickGetButton(controller[Control].joystick, value.at(0).toInt()))
                Keys->Value |= key;
            break;
        case 5 /*Joystick Axis*/:
            axis_value = SDL_JoystickGetAxis(controller[Control].joystick, value.at(0).toInt());
            if (key == 0x0020/*Z*/ || key == 0x1000/*R_TRIG*/ || key == 0x2000/*L_TRIG*/)
            {
                if (axis_value > 0)
                    Keys->Value |= key;
                break;
            }

            if (abs(axis_value) >= (AXIS_PEAK / 2) && axis_value * value.at(2).toInt() > 0)
                Keys->Value |= key;
            break;
    }
}

static bool isBindingActive(int Control, const QString &button)
{
    int axis_value;
    QStringList value = gameSettings->value(controller[Control].profile + "/" + button).toString().split(",");
    if (value.size() < 2)
        return false;

    switch (value.at(1).toInt()) {
        case 0 /*Keyboard*/:
            return myKeyState[value.at(0).toInt()] != 0;
        case 1 /*Button*/:
            return controller[Control].gamepad &&
                   SDL_GameControllerGetButton(controller[Control].gamepad, (SDL_GameControllerButton)value.at(0).toInt());
        case 2 /*Axis*/:
            if (!controller[Control].gamepad || value.size() < 3)
                return false;
            axis_value = SDL_GameControllerGetAxis(controller[Control].gamepad, (SDL_GameControllerAxis)value.at(0).toInt());
            return abs(axis_value) >= (AXIS_PEAK / 2) && axis_value * value.at(2).toInt() > 0;
        case 3 /*Joystick Hat*/:
            return controller[Control].joystick &&
                   value.size() >= 3 &&
                   (SDL_JoystickGetHat(controller[Control].joystick, value.at(0).toInt()) & value.at(2).toInt());
        case 4 /*Joystick Button*/:
            return controller[Control].joystick &&
                   SDL_JoystickGetButton(controller[Control].joystick, value.at(0).toInt());
        case 5 /*Joystick Axis*/:
            if (!controller[Control].joystick || value.size() < 3)
                return false;
            axis_value = SDL_JoystickGetAxis(controller[Control].joystick, value.at(0).toInt());
            return abs(axis_value) >= (AXIS_PEAK / 2) && axis_value * value.at(2).toInt() > 0;
        default:
            return false;
    }
}

static bool isMenuDirectionActive(int controlIndex, const QString &dpadBinding, const QString &axisBinding)
{
    return isBindingActive(controlIndex, dpadBinding) || isBindingActive(controlIndex, axisBinding);
}

void setPak(int Control)
{
    const int previousPak = controller[Control].control->Plugin;
    QString pak = effectiveControllerSetting(Control, "Pak");
    if (controller[Control].control->Type == CONT_TYPE_VRU)
        controller[Control].control->Plugin = PLUGIN_NONE;
    else if (pak == "Transfer")
        controller[Control].control->Plugin = PLUGIN_TRANSFER_PAK;
    else if (pak == "Rumble")
        controller[Control].control->Plugin = PLUGIN_RAW;
    else if (pak == "None")
        controller[Control].control->Plugin = PLUGIN_NONE;
    else
        controller[Control].control->Plugin = PLUGIN_MEMPAK;

    /* Once Plugin changes, ControllerCommand no longer accepts Rumble Pak
     * writes. Stop the motor explicitly before the core removes the old Pak. */
    if (previousPak == PLUGIN_RAW && controller[Control].control->Plugin != PLUGIN_RAW)
    {
        if (controller[Control].gamepad)
            SDL_GameControllerRumble(controller[Control].gamepad, 0, 0, 0);
        else if (controller[Control].joystick)
            SDL_JoystickRumble(controller[Control].joystick, 0, 0, 0);
    }
}

EXPORT void CALL GetKeys( int Control, BUTTONS *Keys )
{
    if (controller[Control].control->Present == 0 && controllerReconnectDue(Control))
        tryOpenControllerForSlot(Control);

    if (controller[Control].control->Present == 0)
        return;

    if (controller[Control].joystick &&
        SDL_JoystickGetAttached(controller[Control].joystick) == SDL_FALSE &&
        controllerReconnectDue(Control))
    {
        for (int j = 0; j < SDL_NumJoysticks(); ++j)
        {
            const char *pathChars = SDL_JoystickPathForIndex(j);
            const QString currentPath = pathChars ? QString::fromUtf8(pathChars) : QString();
            if (normalizeControllerPath(controller[Control].device_path) == normalizeControllerPath(currentPath))
            {
                if (SDL_IsGameController(j))
                {
                    SDL_GameControllerClose(controller[Control].gamepad);
                    controller[Control].gamepad = SDL_GameControllerOpen(j);
                    controller[Control].joystick = SDL_GameControllerGetJoystick(controller[Control].gamepad);
                }
                else
                {
                    SDL_JoystickClose(controller[Control].joystick);
                    controller[Control].joystick = SDL_JoystickOpen(j);
                }
                break;
            }
        }
    }

    setPak(Control);
    if (pakSwapMenuVisible && pakSwapMenuController == Control)
    {
        Keys->Value = 0;
        Keys->X_AXIS = 0;
        Keys->Y_AXIS = 0;
        return;
    }
    if (controller[Control].control->Type == CONT_TYPE_VRU)
    {
        if (l_TalkingState == 0)
        {
            Keys->Value = 0;
            return;
        }
        else
        {
            // In the future it would be better to only set this when the player is talking
            Keys->Value = 0x0020;
            return;
        }
    }

    Keys->Value = 0;
    setKey(Control, 0x0001/*R_DPAD*/, Keys, "DPadR");
    setKey(Control, 0x0002/*L_DPAD*/, Keys, "DPadL");
    setKey(Control, 0x0004/*D_DPAD*/, Keys, "DPadD");
    setKey(Control, 0x0008/*U_DPAD*/, Keys, "DPadU");
    setKey(Control, 0x0010/*START_BUTTON*/, Keys, "Start");
    setKey(Control, 0x0020/*Z_TRIG*/, Keys, "Z");
    setKey(Control, 0x0040/*B_BUTTON*/, Keys, "B");
    setKey(Control, 0x0080/*A_BUTTON*/, Keys, "A");
    setKey(Control, 0x0100/*R_CBUTTON*/, Keys, "CRight");
    setKey(Control, 0x0200/*L_CBUTTON*/, Keys, "CLeft");
    setKey(Control, 0x0400/*D_CBUTTON*/, Keys, "CDown");
    setKey(Control, 0x0800/*U_CBUTTON*/, Keys, "CUp");
    setKey(Control, 0x1000/*R_TRIG*/, Keys, "R");
    setKey(Control, 0x2000/*L_TRIG*/, Keys, "L");

    setAxis(Control, 0/*X_AXIS*/, Keys, "AxisLeft", -1);
    setAxis(Control, 0/*X_AXIS*/, Keys, "AxisRight", 1);
    setAxis(Control, 1/*Y_AXIS*/, Keys, "AxisUp", 1);
    setAxis(Control, 1/*Y_AXIS*/, Keys, "AxisDown", -1);

    const bool pakSwapActive = isBindingActive(Control, "PakSwap");
    if (pakSwapIgnoreRelease[Control])
    {
        if (!pakSwapActive)
            pakSwapIgnoreRelease[Control] = false;
    }
    else if (!pakSwapActive && pakSwapHeld[Control])
    {
        openPakSwapMenu(Control);
        /* This callback runs on the emulation thread after the modal menu
         * returns. Re-read only the Pak; never reinitialize controller IDs. */
        setPak(Control);
        Keys->Value = 0;
    }
    pakSwapHeld[Control] = pakSwapActive;

    if (pakSwapReloadCart[Control])
    {
        Keys->Value |= 0x8000; // Consumed and stripped by the core input backend.
        pakSwapReloadCart[Control] = false;
    }

    double boundX = Keys->X_AXIS;
    double boundY = Keys->Y_AXIS;
    boundAxis(&boundX, &boundY);
    Keys->X_AXIS = round(boundX);
    Keys->Y_AXIS = round(boundY);
}

static int setupVosk()
{
    if (model != nullptr && hardware_spec != nullptr && voskLib != nullptr)
        return 1;

    if (voskLib == nullptr)
    {
        voskLib = new QLibrary((QDir(QCoreApplication::applicationDirPath()).filePath("vosk")));
        VoskAcceptWaveform = (ptr_vosk_recognizer_accept_waveform) voskLib->resolve("vosk_recognizer_accept_waveform");
        VoskFinalResult = (ptr_vosk_recognizer_final_result) voskLib->resolve("vosk_recognizer_final_result");
        VoskNewModel = (ptr_vosk_model_new) voskLib->resolve("vosk_model_new");
        VoskNewRecognizer = (ptr_vosk_recognizer_new_grm) voskLib->resolve("vosk_recognizer_new_grm");
        VoskFreeModel = (ptr_vosk_model_free) voskLib->resolve("vosk_model_free");
        VoskFreeRecognizer = (ptr_vosk_recognizer_free) voskLib->resolve("vosk_recognizer_free");
        VoskSetLogLevel = (ptr_vosk_set_log_level) voskLib->resolve("vosk_set_log_level");
        VoskSetAlternatives = (ptr_vosk_recognizer_set_max_alternatives) voskLib->resolve("vosk_recognizer_set_max_alternatives");

        VoskSetLogLevel(-1);

        QJsonDocument vruwordjson = QJsonDocument::fromJson(vruwords.toUtf8());
        vruwordsobject = vruwordjson.object();
    }

    l_TalkingState = 0;
    if (hardware_spec == NULL)
    {
        SDL_AudioSpec *desired, *obtained;
        desired = (SDL_AudioSpec*)malloc(sizeof(SDL_AudioSpec));
        obtained = (SDL_AudioSpec*)malloc(sizeof(SDL_AudioSpec));
        desired->freq = 44100;
        desired->format = AUDIO_S16SYS;
        desired->channels = 1;
        desired->samples = 1024;
        desired->callback = NULL;
        desired->userdata = NULL;
        audio_dev = SDL_OpenAudioDevice(NULL, 1, desired, obtained, SDL_AUDIO_ALLOW_SAMPLES_CHANGE | SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        free(desired);
        hardware_spec = obtained;
    }
    recognizer = nullptr;
    timer_id = 0;
    if (model == nullptr && QFile::exists(QDir(ConfigGetUserDataPath()).filePath("vosk-model-small-en-us-0.15/conf/mfcc.conf")))
    {
        model = VoskNewModel(QDir(ConfigGetUserDataPath()).filePath("vosk-model-small-en-us-0.15").toUtf8().constData());
    }

    if (model != nullptr)
        return 1;

    shutdownVosk(true);
    return 0;
}

EXPORT void CALL InitiateControllers(CONTROL_INFO ControlInfo)
{
    recognizer = nullptr;
    gameSettings = new QSettings(settings->fileName(), QSettings::IniFormat);
    gameControllerSettings = new QSettings(controllerSettings->fileName(), QSettings::IniFormat);
    currentGameMd5.clear();
    currentGameGoodName.clear();
    loadGbLibraryCache();
    ptr_CoreDoCommand CoreDoCommand = (ptr_CoreDoCommand) osal_dynlib_getproc(coreHandle, "CoreDoCommand");
    m64p_rom_settings romSettings;
    memset(&romSettings, 0, sizeof(romSettings));
    if (CoreDoCommand && (*CoreDoCommand)(M64CMD_ROM_GET_SETTINGS, sizeof(romSettings), &romSettings) == M64ERR_SUCCESS)
    {
        currentGameMd5 = QString::fromUtf8(romSettings.MD5).trimmed();
        currentGameGoodName = QString::fromUtf8(romSettings.goodname).trimmed();
    }

    int i, j;

    for (i = 0; i < SDL_NUM_SCANCODES; i++)
    {
        myKeyState[i] = 0;
    }
    for (i = 0; i < 4; ++i)
    {
        pakSwapHeld[i] = false;
        pakSwapIgnoreRelease[i] = false;
        pakSwapReloadCart[i] = false;
        controllerReconnectAttempt[i] = 0;
    }
    // set our CONTROL struct pointers to the array that was passed in to this function from the core
    // this small struct tells the core whether each controller is plugged in, and what type of pak is connected
    QString gamepad;
    int controller_index;
    QString gamepad_name;
    QList<int> used_controllers;
    for (i = 0; i < 4; i++) {
        controller[i].control = ControlInfo.Controls + i;
        controller[i].control->RawData = 0;
        controller[i].control->Present = 0;
        controller[i].control->Type = CONT_TYPE_STANDARD;
        controller[i].gamepad = NULL;
        controller[i].joystick = NULL;
        controller[i].device_path.clear();
        gamepad = effectiveControllerSetting(i, "Gamepad");
        if (currentGameRequiresVru() && i == 3)
            gamepad = "Emulate VRU";
        if (gamepad == "Keyboard")
            controller[i].control->Present = 1;
        else if (gamepad == "None")
            controller[i].control->Present = 0;
        else if (gamepad == "Auto") {
            for (j = 0; j < SDL_NumJoysticks(); ++j)
            {
                if (used_controllers.contains(j))
                    continue;

                if (SDL_IsGameController(j)) {
                    controller[i].gamepad = SDL_GameControllerOpen(j);
                    if (controller[i].gamepad)
                    {
                        controller[i].control->Present = 1;
                        used_controllers.append(j);
                        break;
                    }
                }
                else {
                    controller[i].joystick = SDL_JoystickOpen(j);
                    if (controller[i].joystick)
                    {
                        controller[i].control->Present = 1;
                        used_controllers.append(j);
                        break;
                    }
                }
            }
            if (i == 0) controller[i].control->Present = 1; //Player 1
        }
        else if (gamepad == "Emulate VRU") {
            controller[i].control->Type = CONT_TYPE_VRU;
            controller[i].control->Plugin = PLUGIN_NONE;
            if (!setupVosk())
            {
                controller[i].control->Present = 0;
                DebugMessage(M64MSG_ERROR, "Could not set up Vosk library, disabling VRU");
            }
            else
                controller[i].control->Present = 1;
        }
        else /*specific gamepad selected*/ {
            QString desiredPath;
            QString desiredName;
            if (parsePathBoundControllerSetting(gamepad, &desiredPath, &desiredName))
            {
                if (openControllerByPath(desiredPath, used_controllers, &controller[i].gamepad, &controller[i].joystick))
                {
                    controller[i].control->Present = 1;
                }
            }
            else
            {
                controller_index = gamepad.split(":")[0].toInt();
                gamepad_name = gamepad.split(":")[1];
                if (SDL_IsGameController(controller_index) && gamepad_name == QString(SDL_JoystickNameForIndex(controller_index))) {
                    controller[i].gamepad = SDL_GameControllerOpen(controller_index);
                    if (controller[i].gamepad)
                    {
                        controller[i].control->Present = 1;
                        used_controllers.append(controller_index);
                    }
                }
                else if (gamepad_name == QString(SDL_JoystickNameForIndex(controller_index))) {
                    controller[i].joystick = SDL_JoystickOpen(controller_index);
                    if (controller[i].joystick)
                    {
                        controller[i].control->Present = 1;
                        used_controllers.append(controller_index);
                    }
                }
            }
            if (controller[i].control->Present == 0 && gamepad == "Keyboard")
            {
                controller[i].control->Present = 1;
            }
            else if (controller[i].control->Present == 0)
            {
                controller[i].control->Present = 0;
            }
        }

        if (controller[i].gamepad)
            controller[i].joystick = SDL_GameControllerGetJoystick(controller[i].gamepad);
        if (controller[i].joystick)
            controller[i].device_path = SDL_JoystickPath(controller[i].joystick);

        controller[i].profile = effectiveControllerSetting(i, "Profile");
        if (!gameSettings->childGroups().contains(controller[i].profile))
            controller[i].profile = "Auto";

        if (controller[i].profile == "Auto") {
            if (controller[i].gamepad)
                controller[i].profile = "Auto-Gamepad";
            else
                controller[i].profile = "Auto-Keyboard";
        }

        if (!gameSettings->contains(controller[i].profile + "/Deadzone"))
            gameSettings->setValue(controller[i].profile + "/Deadzone", DEADZONE_DEFAULT);
        if (!gameSettings->contains(controller[i].profile + "/Sensitivity"))
            gameSettings->setValue(controller[i].profile + "/Sensitivity", 100.0);

        controller[i].deadzone = AXIS_PEAK * (gameSettings->value(controller[i].profile + "/Deadzone").toFloat() / 100.0);
        controller[i].range = AXIS_PEAK - controller[i].deadzone;
        controller[i].sensitivity = gameSettings->value(controller[i].profile + "/Sensitivity").toFloat() / 100.0;

        setPak(i);
    }
}

EXPORT void CALL ReadController(int, unsigned char *)
{
}

EXPORT int CALL RomOpen(void)
{
    return 1;
}

EXPORT void CALL RomClosed(void)
{
    closeControllers();
    currentGameMd5.clear();
    currentGameGoodName.clear();

    gameSettings->sync();
    gameControllerSettings->sync();
    shutdownVosk(false);
    delete gameSettings;
    delete gameControllerSettings;
}

EXPORT void CALL SDL_KeyDown(int, int keysym)
{
    myKeyState[keysym] = 1;
}

EXPORT void CALL SDL_KeyUp(int, int keysym)
{
    myKeyState[keysym] = 0;
}

EXPORT void CALL PluginConfig()
{
    ConfigDialog config(coreHandle, settings, controllerSettings);
    config.exec();
}
