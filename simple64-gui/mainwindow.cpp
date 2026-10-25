#include <algorithm>
#include <QString>
#include <QFileDialog>
#include <QCloseEvent>
#include <QColor>
#include <QActionGroup>
#include <QDesktopServices>
#include <QDirIterator>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImageReader>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <thread>
#include <QMessageBox>
#include <QPointer>
#include <QUrl>
#include "UpdateCheck.h"
#include "settingsdialog.h"
#include "plugindialog.h"
#include "hotkeydialog.h"
#include "cheats.h"
#include "gamesettingsdialog.h"
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "interface/common.h"
#include "vidext.h"
#include "netplay/createroom.h"
#include "netplay/joinroom.h"

#include "osal/osal_preproc.h"
#include "interface/core_commands.h"
#include "interface/version.h"
#include "m64p_common.h"
#include "version.h"

#define RECENT_SIZE 10

#define SETTINGS_VER 2

m64p_video_extension_functions vidExtFunctions = {17,
                                                  qtVidExtFuncInit,
                                                  qtVidExtFuncQuit,
                                                  qtVidExtFuncListModes,
                                                  qtVidExtFuncListRates,
                                                  qtVidExtFuncSetMode,
                                                  qtVidExtFuncSetModeWithRate,
                                                  qtVidExtFuncGLGetProc,
                                                  qtVidExtFuncGLSetAttr,
                                                  qtVidExtFuncGLGetAttr,
                                                  qtVidExtFuncGLSwapBuf,
                                                  qtVidExtFuncSetCaption,
                                                  qtVidExtFuncToggleFS,
                                                  qtVidExtFuncResizeWindow,
                                                  qtVidExtFuncGLGetDefaultFramebuffer,
                                                  qtVidExtFuncInitWithRenderMode,
                                                  qtVidExtFuncGetVkSurface,
                                                  qtVidExtFuncGetVkInstExtensions};

namespace
{
QString coverArtTooltip()
{
    return QStringLiteral(
        "Set a Box Art Folder from Library > Box Art Folder..., then place images there with the exact same base name as the ROM.\n"
        "Example: Resident Evil 2.zip -> Resident Evil 2.jpg");
}

QString normalizeLibraryLookupTitle(QString title)
{
    title = title.trimmed();

    static const QRegularExpression articleDashPattern(
        QStringLiteral(R"(^(.*),\s*(The|A|An)\s*-\s*(.*)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression articleEndPattern(
        QStringLiteral(R"(^(.*),\s*(The|A|An)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression regionPattern(QStringLiteral(R"(\s*[\(\[].*?[\)\]])"));
    static const QRegularExpression combiningMarks(QStringLiteral(R"([\x{0300}-\x{036f}])"));
    static const QRegularExpression nonAlphaNumeric(QStringLiteral(R"([^a-zA-Z0-9]+)"));

    QRegularExpressionMatch match = articleDashPattern.match(title);
    if (match.hasMatch())
    {
        title = QStringLiteral("%1 %2 %3")
                    .arg(match.captured(2), match.captured(1), match.captured(3));
    }
    else
    {
        match = articleEndPattern.match(title);
        if (match.hasMatch())
            title = QStringLiteral("%1 %2").arg(match.captured(2), match.captured(1));
    }

    title.remove(regionPattern);
    title = title.normalized(QString::NormalizationForm_D);
    title.remove(combiningMarks);
    title.replace(QLatin1Char('&'), QStringLiteral("and"));
    title.replace(nonAlphaNumeric, QStringLiteral(" "));
    title = title.simplified().toLower();
    if (title.startsWith(QStringLiteral("the ")))
        title.remove(0, 4);
    else if (title.startsWith(QStringLiteral("an ")))
        title.remove(0, 3);
    else if (title.startsWith(QStringLiteral("a ")))
        title.remove(0, 2);
    return title;
}

QString compactLibraryLookupTitle(const QString &title)
{
    QString compact = normalizeLibraryLookupTitle(title);
    compact.remove(QLatin1Char(' '));
    return compact;
}

const QHash<QString, int> &libraryPlayerCountMap()
{
    static const QHash<QString, int> map = []() {
        QHash<QString, int> values;
#define LIBRARY_PLAYER(title, count) values.insert(normalizeLibraryLookupTitle(QStringLiteral(title)), count);
#include "library_player_map.inc"
#undef LIBRARY_PLAYER
        return values;
    }();

    return map;
}

const QHash<QString, int> &libraryCompactPlayerCountMap()
{
    static const QHash<QString, int> map = []() {
        QHash<QString, int> values;
        const auto &baseMap = libraryPlayerCountMap();
        for (auto it = baseMap.constBegin(); it != baseMap.constEnd(); ++it)
            values.insert(compactLibraryLookupTitle(it.key()), it.value());
        return values;
    }();

    return map;
}

const QHash<QString, QString> &libraryPlayerCountAliases()
{
    static const QHash<QString, QString> aliases = {
        { normalizeLibraryLookupTitle(QStringLiteral("007 GoldenEye")), normalizeLibraryLookupTitle(QStringLiteral("GoldenEye 007")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Batman Beyond: Return of the Joker")), normalizeLibraryLookupTitle(QStringLiteral("Batman of the Future: Return of the Joker")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Bugs Life, A")), normalizeLibraryLookupTitle(QStringLiteral("A Bug's Life")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Beast Wars Transmetals")), normalizeLibraryLookupTitle(QStringLiteral("Beast Wars Metals 64")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Transformers - Beast Wars Transmetal")), normalizeLibraryLookupTitle(QStringLiteral("Beast Wars Metals 64")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Mike Piazza Baseball")), normalizeLibraryLookupTitle(QStringLiteral("Mike Piazza's Strike Zone")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Micro Machines GT64")), normalizeLibraryLookupTitle(QStringLiteral("Micro Machines 64 Turbo")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Micro Machines GT 64")), normalizeLibraryLookupTitle(QStringLiteral("Micro Machines 64 Turbo")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Olympic Hockey 98")), normalizeLibraryLookupTitle(QStringLiteral("Olympic Hockey Nagano '98")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Powerpuff Girls")), normalizeLibraryLookupTitle(QStringLiteral("Powerpuff Girls: Chemical X-Traction")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Powerpuff Girls - Chemical X-Traction")), normalizeLibraryLookupTitle(QStringLiteral("Powerpuff Girls: Chemical X-Traction")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Quest 64")), normalizeLibraryLookupTitle(QStringLiteral("Holy Magic Century")) },
        { normalizeLibraryLookupTitle(QStringLiteral("SpaceStation Silicon Valley")), normalizeLibraryLookupTitle(QStringLiteral("Space Station Silicon Valley")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Star Fox 64")), normalizeLibraryLookupTitle(QStringLiteral("Lylat Wars")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Superman - The New Superman Adventures")), normalizeLibraryLookupTitle(QStringLiteral("Superman")) },
        { normalizeLibraryLookupTitle(QStringLiteral("WinBack: Covert Operations")), normalizeLibraryLookupTitle(QStringLiteral("Operation WinBack")) },
        { normalizeLibraryLookupTitle(QStringLiteral("Donald Duck - Goin' Quackers")), normalizeLibraryLookupTitle(QStringLiteral("Donald Duck: Quack Attack")) }
    };

    return aliases;
}

QString romDatabasePath()
{
    const QString appDirIni = QDir(QCoreApplication::applicationDirPath()).filePath("mupen64plus.ini");
    if (QFileInfo::exists(appDirIni))
        return appDirIni;

    return QString();
}

const QHash<QString, int> &libraryRomDatabasePlayerCountMap()
{
    static const QHash<QString, int> map = []() {
        QHash<QString, int> values;
        const QString iniPath = romDatabasePath();
        if (iniPath.isEmpty())
            return values;

        QSettings romDatabase(iniPath, QSettings::IniFormat);
        const QStringList groups = romDatabase.childGroups();
        for (const QString &group : groups)
        {
            romDatabase.beginGroup(group);
            const QString goodName = romDatabase.value("GoodName").toString();
            const int players = romDatabase.value("Players", 0).toInt();
            romDatabase.endGroup();

            if (goodName.isEmpty() || players <= 0)
                continue;

            values.insert(normalizeLibraryLookupTitle(goodName), qMin(players, 4));
        }

        return values;
    }();

    return map;
}

const QHash<QString, int> &libraryCompactRomDatabasePlayerCountMap()
{
    static const QHash<QString, int> map = []() {
        QHash<QString, int> values;
        const auto &baseMap = libraryRomDatabasePlayerCountMap();
        for (auto it = baseMap.constBegin(); it != baseMap.constEnd(); ++it)
            values.insert(compactLibraryLookupTitle(it.key()), it.value());
        return values;
    }();

    return map;
}

QString playerCountLabelForTitle(const QString &title)
{
    QString normalizedTitle = normalizeLibraryLookupTitle(title);
    const QString aliasedTitle = libraryPlayerCountAliases().value(normalizedTitle, normalizedTitle);
    int players = libraryPlayerCountMap().value(aliasedTitle, 0);
    if (players <= 0)
        players = libraryCompactPlayerCountMap().value(compactLibraryLookupTitle(aliasedTitle), 0);
    if (players <= 0)
        players = libraryRomDatabasePlayerCountMap().value(aliasedTitle, 0);
    if (players <= 0)
        players = libraryCompactRomDatabasePlayerCountMap().value(compactLibraryLookupTitle(aliasedTitle), 0);
    if (players <= 0)
        return QString();

    return (players == 1)
        ? QStringLiteral("1 Player")
        : QStringLiteral("%1 Players").arg(players);
}

QFont libraryGameNameFont(const QFont &baseFont)
{
    QFont font(baseFont);
    if (font.pointSize() > 0)
        font.setPointSize(font.pointSize() + 2);
    else
        font.setPixelSize(qMax(font.pixelSize(), 14) + 4);
    return font;
}

QFont libraryPlayersFont(const QFont &baseFont)
{
    QFont font(baseFont);
    font.setBold(true);
    if (font.pointSize() > 0)
        font.setPointSize(font.pointSize() + 1);
    else
        font.setPixelSize(qMax(font.pixelSize(), 13) + 2);
    return font;
}

QString defaultGbSavePathForRom(const QString &romPath)
{
    const QFileInfo romInfo(romPath);
    return romInfo.dir().filePath(romInfo.completeBaseName() + ".sav");
}

QString findGbSaveForRom(const QString &romPath)
{
    const QFileInfo romInfo(romPath);
    const QDir romDir = romInfo.dir();
    const QString baseName = romInfo.completeBaseName();
    const QStringList preferredExtensions = {"sav", "ram"};

    for (const QString &extension : preferredExtensions)
    {
        const QString candidate = romDir.filePath(baseName + "." + extension);
        if (QFileInfo::exists(candidate))
            return candidate;
    }

    return defaultGbSavePathForRom(romPath);
}
}

void MainWindow::updatePlugins()
{
#ifdef PLUGIN_DIR_PATH
    QString pluginPath = PLUGIN_DIR_PATH;
#else
    QString pluginPath = QCoreApplication::applicationDirPath();
#endif
    QDir PluginDir(pluginPath);
    PluginDir.setFilter(QDir::Files);
    QStringList Filter;
    Filter.append("");
    QStringList current;
    QString default_value;

    if (!settings->contains("inputPlugin"))
    {
        Filter.replace(0, "*-input-*");
        current = PluginDir.entryList(Filter);
        default_value = "simple64-input-qt";
        default_value += OSAL_DLL_EXTENSION;
        if (current.isEmpty())
            settings->setValue("inputPlugin", "dummy");
        else if (current.indexOf(default_value) != -1)
            settings->setValue("inputPlugin", default_value);
        else
            settings->setValue("inputPlugin", current.at(0));
    }

    ui->actionController_Configuration->setEnabled(settings->value("inputPlugin").toString().contains("-qt"));
}

void MainWindow::updateGB(Ui::MainWindow *ui)
{
    QMenu *GB = new QMenu(this);
    GB->setTitle("Game Boy Cartridges");
    ui->menuFile->insertMenu(ui->actionTake_Screenshot, GB);

    QAction *fileSelect = new QAction(this);
    QString current = settings->value("Player1GBROM").toString();
    fileSelect->setText("Player 1 ROM: " + current);
    GB->addAction(fileSelect);
    connect(fileSelect, &QAction::triggered, [=]()
            {
        QString filename = QFileDialog::getOpenFileName(this,
            tr("GB ROM File"), NULL, tr("GB ROM Files (*.gb *.gbc)"));
        if (!filename.isNull()) {
            settings->setValue("Player1GBROM", filename);
            QString current = filename;
            fileSelect->setText("Player 1 ROM: " + current);
        } });

    QAction *fileSelect2 = new QAction(this);
    current = settings->value("Player1GBRAM").toString();
    fileSelect2->setText("Player 1 SAV: " + current);
    GB->addAction(fileSelect2);
    connect(fileSelect2, &QAction::triggered, [=]()
            {
        QString filename = QFileDialog::getOpenFileName(this,
            tr("GB RAM File"), NULL, tr("GB SAV Files (*.sav *.ram)"));
        if (!filename.isNull()) {
            settings->setValue("Player1GBRAM", filename);
            QString current = filename;
            fileSelect2->setText("Player 1 SAV: " + current);
        } });

    QAction *clearSelect = new QAction(this);
    clearSelect->setText("Clear Player 1 Selections");
    GB->addAction(clearSelect);
    connect(clearSelect, &QAction::triggered, [=]()
            {
        settings->remove("Player1GBROM");
        settings->remove("Player1GBRAM");
        fileSelect->setText("Player 1 ROM: ");
        fileSelect2->setText("Player 1 SAV: "); });
    GB->addSeparator();

    fileSelect = new QAction(this);
    current = settings->value("Player2GBROM").toString();
    fileSelect->setText("Player 2 ROM: " + current);
    GB->addAction(fileSelect);
    connect(fileSelect, &QAction::triggered, [=]()
            {
        QString filename = QFileDialog::getOpenFileName(this,
            tr("GB ROM File"), NULL, tr("GB ROM Files (*.gb *.gbc)"));
        if (!filename.isNull()) {
            settings->setValue("Player2GBROM", filename);
            QString current = filename;
            fileSelect->setText("Player 2 ROM: " + current);
        } });

    fileSelect2 = new QAction(this);
    current = settings->value("Player2GBRAM").toString();
    fileSelect2->setText("Player 2 SAV: " + current);
    GB->addAction(fileSelect2);
    connect(fileSelect2, &QAction::triggered, [=]()
            {
        QString filename = QFileDialog::getOpenFileName(this,
            tr("GB RAM File"), NULL, tr("GB SAV Files (*.sav *.ram)"));
        if (!filename.isNull()) {
            settings->setValue("Player2GBRAM", filename);
            QString current = filename;
            fileSelect2->setText("Player 2 SAV: " + current);
        } });

    clearSelect = new QAction(this);
    clearSelect->setText("Clear Player 2 Selections");
    GB->addAction(clearSelect);
    connect(clearSelect, &QAction::triggered, [=]()
            {
        settings->remove("Player2GBROM");
        settings->remove("Player2GBRAM");
        fileSelect->setText("Player 2 ROM: ");
        fileSelect2->setText("Player 2 SAV: "); });
    GB->addSeparator();

    fileSelect = new QAction(this);
    current = settings->value("Player3GBROM").toString();
    fileSelect->setText("Player 3 ROM: " + current);
    GB->addAction(fileSelect);
    connect(fileSelect, &QAction::triggered, [=]()
            {
        QString filename = QFileDialog::getOpenFileName(this,
            tr("GB ROM File"), NULL, tr("GB ROM Files (*.gb *.gbc)"));
        if (!filename.isNull()) {
            settings->setValue("Player3GBROM", filename);
            QString current = filename;
            fileSelect->setText("Player 3 ROM: " + current);
        } });

    fileSelect2 = new QAction(this);
    current = settings->value("Player3GBRAM").toString();
    fileSelect2->setText("Player 3 SAV: " + current);
    GB->addAction(fileSelect2);
    connect(fileSelect2, &QAction::triggered, [=]()
            {
        QString filename = QFileDialog::getOpenFileName(this,
            tr("GB RAM File"), NULL, tr("GB SAV Files (*.sav *.ram)"));
        if (!filename.isNull()) {
            settings->setValue("Player3GBRAM", filename);
            QString current = filename;
            fileSelect2->setText("Player 3 SAV: " + current);
        } });

    clearSelect = new QAction(this);
    clearSelect->setText("Clear Player 3 Selections");
    GB->addAction(clearSelect);
    connect(clearSelect, &QAction::triggered, [=]()
            {
        settings->remove("Player3GBROM");
        settings->remove("Player3GBRAM");
        fileSelect->setText("Player 3 ROM: ");
        fileSelect2->setText("Player 3 SAV: "); });
    GB->addSeparator();

    fileSelect = new QAction(this);
    current = settings->value("Player4GBROM").toString();
    fileSelect->setText("Player 4 ROM: " + current);
    GB->addAction(fileSelect);
    connect(fileSelect, &QAction::triggered, [=]()
            {
        QString filename = QFileDialog::getOpenFileName(this,
            tr("GB ROM File"), NULL, tr("GB ROM Files (*.gb *.gbc)"));
        if (!filename.isNull()) {
            settings->setValue("Player4GBROM", filename);
            QString current = filename;
            fileSelect->setText("Player 4 ROM: " + current);
        } });

    fileSelect2 = new QAction(this);
    current = settings->value("Player4GBRAM").toString();
    fileSelect2->setText("Player 4 SAV: " + current);
    GB->addAction(fileSelect2);
    connect(fileSelect2, &QAction::triggered, [=]()
            {
        QString filename = QFileDialog::getOpenFileName(this,
            tr("GB RAM File"), NULL, tr("GB SAV Files (*.sav *.ram)"));
        if (!filename.isNull()) {
            settings->setValue("Player4GBRAM", filename);
            QString current = filename;
            fileSelect2->setText("Player 4 SAV: " + current);
        } });

    clearSelect = new QAction(this);
    clearSelect->setText("Clear Player 4 Selections");
    GB->addAction(clearSelect);
    connect(clearSelect, &QAction::triggered, [=]()
            {
        settings->remove("Player4GBROM");
        settings->remove("Player4GBRAM");
        fileSelect->setText("Player 4 ROM: ");
        fileSelect2->setText("Player 4 SAV: "); });
}

void MainWindow::setupLibraryMenu()
{
    QMenu *libraryMenu = new QMenu("Library", this);
    m_addLibraryFolderAction = libraryMenu->addAction("Add Folder...");
    m_boxArtFolderAction = libraryMenu->addAction("Box Art Folder...");
    m_gbLibraryFolderAction = libraryMenu->addAction("GB/GBC Library Folder...");
    m_refreshLibraryAction = libraryMenu->addAction("Refresh Library");
    m_clearLibraryFoldersAction = libraryMenu->addAction("Clear Library Folders");
    ui->menuBar->insertMenu(ui->menuCommunity->menuAction(), libraryMenu);

    connect(m_addLibraryFolderAction, &QAction::triggered, this, &MainWindow::addLibraryFolder);
    connect(m_boxArtFolderAction, &QAction::triggered, this, &MainWindow::chooseBoxArtFolder);
    connect(m_gbLibraryFolderAction, &QAction::triggered, this, &MainWindow::chooseGbLibraryFolder);
    connect(m_refreshLibraryAction, &QAction::triggered, this, &MainWindow::refreshLibrary);
    connect(m_clearLibraryFoldersAction, &QAction::triggered, this, &MainWindow::clearLibraryFolders);
}

void MainWindow::setupLibraryUi()
{
    m_mainStack = new QStackedWidget(this);
    m_mainStack->setContentsMargins(0, 0, 0, 0);
    setCentralWidget(m_mainStack);

    m_libraryPage = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(m_libraryPage);
    QHBoxLayout *toolbar = new QHBoxLayout;
    m_libraryFilter = new QLineEdit(m_libraryPage);
    m_libraryFilter->setPlaceholderText("Filter library...");
    m_libraryOpenButton = new QPushButton("Open Selected", m_libraryPage);
    m_libraryOpenButton->setEnabled(false);
    toolbar->addWidget(m_libraryFilter, 1);
    toolbar->addWidget(m_libraryOpenButton);
    layout->addLayout(toolbar);

    m_libraryTable = new QTableWidget(m_libraryPage);
    m_libraryTable->setColumnCount(3);
    m_libraryTable->setHorizontalHeaderLabels(QStringList() << "Box Art" << "Game" << "Players");
    m_libraryTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_libraryTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_libraryTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_libraryTable->verticalHeader()->setVisible(false);
    m_libraryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_libraryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_libraryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_libraryTable->setShowGrid(false);
    m_libraryTable->setIconSize(QSize(170, 228));
    m_libraryTable->setContextMenuPolicy(Qt::CustomContextMenu);
    if (m_libraryTable->horizontalHeaderItem(0) != nullptr)
        m_libraryTable->horizontalHeaderItem(0)->setToolTip(coverArtTooltip());
    layout->addWidget(m_libraryTable, 1);

    connect(m_libraryFilter, &QLineEdit::textChanged, this, &MainWindow::filterLibrary);
    connect(m_libraryOpenButton, &QPushButton::released, this, &MainWindow::openSelectedLibraryEntry);
    connect(m_libraryTable, &QTableWidget::cellDoubleClicked, this, &MainWindow::launchLibraryEntry);
    connect(m_libraryTable, &QTableWidget::customContextMenuRequested, this, &MainWindow::showLibraryContextMenu);
    connect(m_libraryTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_libraryOpenButton->setEnabled(m_libraryTable->currentRow() >= 0);
    });

    m_mainStack->addWidget(m_libraryPage);

    m_gamePage = new QWidget(this);
    m_mainStack->addWidget(m_gamePage);
    m_mainStack->setCurrentWidget(m_libraryPage);
}

QString MainWindow::findCoverForRom(const QString &romPath) const
{
    const QString boxArtFolder = settings->value("LibraryBoxArtFolder").toString();
    if (boxArtFolder.isEmpty())
        return QString();

    const QFileInfo romInfo(romPath);
    const QDir coverDir(boxArtFolder);
    if (!coverDir.exists())
        return QString();

    const QStringList extensions = {"png", "jpg", "jpeg", "bmp", "webp"};
    for (const QString &extension : extensions)
    {
        const QString candidate = coverDir.filePath(romInfo.completeBaseName() + "." + extension);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

void MainWindow::refreshLibrary()
{
    m_libraryEntries.clear();
    refreshGbLibrary();
    const QStringList folders = settings->value("LibraryFolders").toStringList();
    const QStringList filters = {"*.n64", "*.N64", "*.z64", "*.Z64", "*.v64", "*.V64", "*.rom", "*.ROM", "*.zip", "*.ZIP", "*.7z"};

    for (const QString &folder : folders)
    {
        QDirIterator it(folder, filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString filePath = it.next();
            QFileInfo info(filePath);
            LibraryEntry entry;
            entry.filePath = filePath;
            entry.title = info.completeBaseName();
            entry.coverPath = findCoverForRom(filePath);
            entry.players = playerCountLabelForTitle(entry.title);
            m_libraryEntries.append(entry);
        }
    }

    std::sort(m_libraryEntries.begin(), m_libraryEntries.end(), [](const LibraryEntry &left, const LibraryEntry &right) {
        return left.title.localeAwareCompare(right.title) < 0;
    });
    filterLibrary(m_libraryFilter ? m_libraryFilter->text() : QString());
}

void MainWindow::refreshGbLibrary()
{
    m_gbLibraryEntries.clear();
    const QString folder = settings->value("LibraryGbFolder").toString();
    if (folder.isEmpty())
    {
        writeGbLibraryCache();
        return;
    }

    const QStringList filters = {"*.gb", "*.GB", "*.gbc", "*.GBC"};
    QDirIterator it(folder, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        const QString romPath = it.next();
        const QFileInfo info(romPath);
        GbLibraryEntry entry;
        entry.title = info.completeBaseName();
        entry.romPath = romPath;
        entry.savePath = findGbSaveForRom(romPath);
        m_gbLibraryEntries.append(entry);
    }

    std::sort(m_gbLibraryEntries.begin(), m_gbLibraryEntries.end(), [](const GbLibraryEntry &left, const GbLibraryEntry &right) {
        return left.title.localeAwareCompare(right.title) < 0;
    });
    writeGbLibraryCache();
}

void MainWindow::writeGbLibraryCache() const
{
    QJsonArray entries;
    for (const GbLibraryEntry &entry : m_gbLibraryEntries)
    {
        QJsonObject object;
        object.insert("title", entry.title);
        object.insert("romPath", entry.romPath);
        object.insert("savePath", entry.savePath);
        entries.append(object);
    }

    QJsonObject root;
    root.insert("folder", settings->value("LibraryGbFolder").toString());
    root.insert("entries", entries);

    QFile file(QDir(QCoreApplication::applicationDirPath()).filePath("gb-library-cache.json"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

void MainWindow::filterLibrary(const QString &text)
{
    m_filteredLibraryEntries.clear();
    for (const LibraryEntry &entry : m_libraryEntries)
    {
        if (text.isEmpty() ||
            entry.title.contains(text, Qt::CaseInsensitive) ||
            entry.filePath.contains(text, Qt::CaseInsensitive))
        {
            m_filteredLibraryEntries.append(entry);
        }
    }
    rebuildLibraryTable();
}

void MainWindow::rebuildLibraryTable()
{
    if (m_libraryTable == nullptr)
        return;

    m_libraryTable->clearContents();
    m_libraryTable->setRowCount(m_filteredLibraryEntries.size());
    m_libraryTable->setColumnWidth(0, 205);
    m_libraryTable->setColumnWidth(2, 120);
    for (int row = 0; row < m_filteredLibraryEntries.size(); ++row)
    {
        const LibraryEntry &entry = m_filteredLibraryEntries.at(row);
        QTableWidgetItem *coverItem = new QTableWidgetItem;
        coverItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        coverItem->setText("");
        coverItem->setToolTip(coverArtTooltip());
        m_libraryTable->setItem(row, 0, coverItem);
        if (!entry.coverPath.isEmpty())
        {
            QPixmap cover(entry.coverPath);
            if (!cover.isNull())
            {
                QLabel *coverLabel = new QLabel(m_libraryTable);
                coverLabel->setAlignment(Qt::AlignCenter);
                coverLabel->setToolTip(coverArtTooltip());
                coverLabel->setPixmap(cover.scaled(170, 228, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_libraryTable->setCellWidget(row, 0, coverLabel);
            }
        }

        QTableWidgetItem *nameItem = new QTableWidgetItem(entry.title);
        nameItem->setData(Qt::UserRole, entry.filePath);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        nameItem->setTextAlignment(Qt::AlignCenter);
        nameItem->setFont(libraryGameNameFont(nameItem->font()));
        m_libraryTable->setItem(row, 1, nameItem);

        QTableWidgetItem *playersItem = new QTableWidgetItem(entry.players);
        playersItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        playersItem->setTextAlignment(Qt::AlignCenter);
        playersItem->setFont(libraryPlayersFont(playersItem->font()));
        m_libraryTable->setItem(row, 2, playersItem);

        m_libraryTable->setRowHeight(row, 236);
    }
    m_libraryTable->clearSelection();
    m_libraryOpenButton->setEnabled(false);
}

void MainWindow::openSelectedLibraryEntry()
{
    const int row = m_libraryTable ? m_libraryTable->currentRow() : -1;
    if (row >= 0)
        launchLibraryEntry(row, 0);
}

void MainWindow::launchLibraryEntry(int row, int)
{
    if (row < 0 || row >= m_filteredLibraryEntries.size())
        return;

    openROM(m_filteredLibraryEntries.at(row).filePath, "", 0, 0, QJsonObject());
}

bool MainWindow::getCurrentRomIdentity(QString &md5, QString &goodName) const
{
    md5 = getCurrentRomMd5();
    goodName = getCurrentRomGoodName();
    return !md5.isEmpty();
}

bool MainWindow::getRomIdentityFromFile(const QString &romPath, QString &md5, QString &goodName)
{
    if (romPath.isEmpty())
        return false;

    stopGame();
    logViewer.clearLog();
    resetCore();

    if (loadROM(romPath) != M64ERR_SUCCESS)
        return false;

    md5 = getCurrentRomMd5();
    goodName = getCurrentRomGoodName();
    (*CoreDoCommand)(M64CMD_ROM_CLOSE, 0, NULL);
    return !md5.isEmpty();
}

void MainWindow::showLibraryContextMenu(const QPoint &pos)
{
    if (m_libraryTable == nullptr)
        return;

    const QModelIndex index = m_libraryTable->indexAt(pos);
    if (!index.isValid())
        return;

    m_libraryTable->selectRow(index.row());
    QMenu menu(this);
    QAction *openAction = menu.addAction("Open Game");
    QAction *settingsAction = menu.addAction("Per-Game Settings");
    QAction *chosen = menu.exec(m_libraryTable->viewport()->mapToGlobal(pos));
    if (chosen == openAction)
        launchLibraryEntry(index.row(), index.column());
    else if (chosen == settingsAction)
        openLibraryGameSettings();
}

void MainWindow::openLibraryGameSettings()
{
    if (m_libraryTable == nullptr)
        return;

    const int row = m_libraryTable->currentRow();
    if (row < 0 || row >= m_filteredLibraryEntries.size())
        return;

    QString md5;
    QString goodName;
    const LibraryEntry &entry = m_filteredLibraryEntries.at(row);
    if (!getRomIdentityFromFile(entry.filePath, md5, goodName))
    {
        QMessageBox msgBox;
        msgBox.setText("Could not read ROM settings for this game.");
        msgBox.exec();
        return;
    }

    if (goodName.isEmpty())
        goodName = entry.title;

    GameSettingsDialog dialog(md5, goodName, this);
    dialog.exec();
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),
                                          ui(new Ui::MainWindow)
{
    verbose = 0;
    nogui = 0;
    run_test = 0;
    test_key_value = 225; // Shift/A
    ui->setupUi(this);
    setupLibraryUi();
    setupLibraryMenu();
    this->setWindowTitle("simple64X    v" SIMPLE64X_VERSION);

    QString ini_path = QDir(QCoreApplication::applicationDirPath()).filePath("simple64-gui.ini");
    settings = new QSettings(ini_path, QSettings::IniFormat, this);

    if (!settings->isWritable())
        settings = new QSettings("simple64", "gui", this);

    if (!settings->contains("version") || settings->value("version").toInt() != SETTINGS_VER)
    {
        settings->clear();
        settings->setValue("version", SETTINGS_VER);
    }

    restoreGeometry(settings->value("geometry").toByteArray());
    restoreState(settings->value("windowState").toByteArray());

    QMenu *helpMenu = ui->menuBar->addMenu("Help");
    QAction *updateCheckAct = helpMenu->addAction("Check for Updates...");
    connect(updateCheckAct, &QAction::triggered, this, &MainWindow::checkForUpdatesCB);
    QAction *updateStartupAct = helpMenu->addAction("Check for Updates at Startup");
    updateStartupAct->setCheckable(true);
    updateStartupAct->setChecked(settings->value("updates/checkAtStartup", true).toBool());
    connect(updateStartupAct, &QAction::toggled, this, &MainWindow::toggleUpdateStartupCB);

    QActionGroup *my_slots_group = new QActionGroup(this);
    QAction *my_slots[10];
    OpenRecent = new QMenu(this);
    QMenu *SaveSlot = new QMenu(this);
    OpenRecent->setTitle("Open Recent");
    SaveSlot->setTitle("Change Save Slot");
    ui->menuFile->insertMenu(ui->actionSave_State, OpenRecent);
    ui->menuFile->insertSeparator(ui->actionSave_State);
    ui->menuFile->insertMenu(ui->actionSave_State_To, SaveSlot);
    ui->menuFile->insertSeparator(ui->actionSave_State_To);
    for (int i = 0; i < 10; ++i)
    {
        my_slots[i] = new QAction(this);
        my_slots[i]->setCheckable(true);
        my_slots[i]->setText("Slot " + QString::number(i));
        my_slots[i]->setActionGroup(my_slots_group);
        SaveSlot->addAction(my_slots[i]);
        QAction *temp_slot = my_slots[i];
        connect(temp_slot, &QAction::triggered, [=](bool checked)
                {
            if (checked) {
                (*CoreDoCommand)(M64CMD_STATE_SET_SLOT, i, NULL);
            } });
    }

    updateOpenRecent();
    updateGB(ui);

    // migrate from old $CONFIG_PATH$ default
    if (settings->value("configDirPath").toString().startsWith("$"))
        settings->remove("configDirPath");

#ifdef CONFIG_DIR_PATH
    settings->setValue("configDirPath", CONFIG_DIR_PATH);
#endif

    updatePlugins();
    m_currentGameSettingsAction = new QAction("Current Game Settings", this);
    ui->menuSettings->insertAction(ui->actionController_Configuration, m_currentGameSettingsAction);
    connect(m_currentGameSettingsAction, &QAction::triggered, this, &MainWindow::showCurrentGameSettings);

    if (!settings->contains("volume"))
        settings->setValue("volume", 100);
    VolumeAction *volumeAction = new VolumeAction(tr("Volume"), this);
    connect(volumeAction->slider(), &QSlider::valueChanged, this, &MainWindow::volumeValueChanged);
    volumeAction->slider()->setValue(settings->value("volume").toInt());
    ui->menuEmulation->insertAction(ui->actionMute, volumeAction);

    coreLib = nullptr;
    gfxPlugin = nullptr;
    rspPlugin = nullptr;
    audioPlugin = nullptr;
    inputPlugin = nullptr;
    loadCoreLib();
    loadPlugins();
    refreshLibrary();

    if (coreLib)
    {
        m64p_handle coreConfigHandle;
        m64p_error res = (*ConfigOpenSection)("Core", &coreConfigHandle);
        if (res == M64ERR_SUCCESS)
        {
            int current_slot = (*ConfigGetParamInt)(coreConfigHandle, "CurrentStateSlot");
            my_slots[current_slot]->setChecked(true);
        }
    }

    setupDiscord();
    FPSLabel = new QLabel(this);
    statusBar()->addPermanentWidget(FPSLabel);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::checkForUpdatesCB()
{
    startUpdateCheck(true);
}

void MainWindow::toggleUpdateStartupCB(bool checked)
{
    settings->setValue("updates/checkAtStartup", checked);
}

// Asks GitHub on a thread of its own, so a slow or absent network never holds
// up the window, and hands the answer back to the GUI thread.
void MainWindow::startUpdateCheck(bool asked)
{
    static bool running = false;
    if (running)
        return;
    running = true;

    QPointer<MainWindow> self(this);
    std::thread([self, asked] {
        const std::string latest = simple64Update::fetchLatestTag64();
        QMetaObject::invokeMethod(qApp, [self, asked, latest] {
            running = false;
            if (self.isNull())
                return;
            const QString current = QString("v") + SIMPLE64X_VERSION;

            if (latest.empty())
            {
                if (asked)
                    QMessageBox::warning(self, "simple64X", "simple64X couldn't reach GitHub to check for a new version. Check your internet connection and try again.");
                return;
            }
            if (simple64Update::parseVersion(latest) <= simple64Update::parseVersion(SIMPLE64X_VERSION))
            {
                if (asked)
                    QMessageBox::information(self, "simple64X", QString("You have the latest version of simple64X (%1).").arg(current));
                return;
            }
            // The startup check never interrupts a game: one started before
            // the answer came back is left alone, and the next start asks again.
            if (!asked && self->isGameRunning())
                return;
            const QString text = QString("You have simple64X %1.\nsimple64X %2 is available.\n\nOpen the download page?")
                                     .arg(current, QString::fromStdString(latest));
            if (QMessageBox::question(self, "simple64X - New version", text) == QMessageBox::Yes)
                QDesktopServices::openUrl(QUrl(simple64Update::releasesPage()));
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::updateApp()
{
    if (settings->value("updates/checkAtStartup", true).toBool())
        startUpdateCheck(false);
#ifdef _AUTOUPDATE
    QString disable_update = qEnvironmentVariable("SIMPLE64_DISABLE_UPDATER");
    if (!disable_update.isEmpty())
        return;
#ifndef __APPLE__
    QNetworkAccessManager *updateManager = new QNetworkAccessManager(this);
    connect(updateManager, &QNetworkAccessManager::finished,
            this, &MainWindow::updateReplyFinished);

    updateManager->get(QNetworkRequest(QUrl("https://api.github.com/repos/simple64/simple64/releases/latest")));
#endif
#endif
}

void MainWindow::setupDiscord()
{
    QLibrary *discordLib = new QLibrary((QDir(QCoreApplication::applicationDirPath()).filePath("discord_game_sdk")), this);

    memset(&discord_app, 0, sizeof(discord_app));

    DiscordCreateParams params;
    DiscordCreateParamsSetDefault(&params);
    params.client_id = 770838334015930398LL;
    params.flags = DiscordCreateFlags_NoRequireDiscord;
    params.event_data = &discord_app;

    typedef EDiscordResult (*CreatePrototype)(DiscordVersion version, struct DiscordCreateParams *params, struct IDiscordCore **result);
    CreatePrototype createFunction = (CreatePrototype)discordLib->resolve("DiscordCreate");
    if (createFunction)
        createFunction(DISCORD_VERSION, &params, &discord_app.core);

    if (discord_app.core)
    {
        discord_app.activities = discord_app.core->get_activity_manager(discord_app.core);

        QTimer *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::discordCallback);
        timer->start(1000);
    }
}

struct Discord_Application *MainWindow::getDiscordApp()
{
    return &discord_app;
}

void MainWindow::updateDiscordActivity(struct DiscordActivity activity)
{
    if (discord_app.activities)
        discord_app.activities->update_activity(discord_app.activities, &activity, &discord_app, nullptr);
}

void MainWindow::clearDiscordActivity()
{
    if (discord_app.activities)
        discord_app.activities->clear_activity(discord_app.activities, &discord_app, nullptr);
}

void MainWindow::discordCallback()
{
    if (discord_app.core)
        discord_app.core->run_callbacks(discord_app.core);
}

void MainWindow::updateDownloadFinished(QNetworkReply *reply)
{
    if (!reply->error())
    {
        QTemporaryDir dir;
        dir.setAutoRemove(false);
        if (dir.isValid())
        {
            download_message->done(QDialog::Accepted);
            QString fullpath = dir.filePath(reply->url().fileName());
            QFile file(fullpath);
            file.open(QIODevice::WriteOnly);
            file.write(reply->readAll());
            file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            file.close();
            QProcess process;
            process.setProgram(fullpath);
            QStringList arguments = {QCoreApplication::applicationDirPath()};
            process.setArguments(arguments);
            process.startDetached();
            reply->deleteLater();
            QCoreApplication::quit();
        }
    }
    reply->deleteLater();
}

void MainWindow::updateReplyFinished(QNetworkReply *reply)
{
    if (!reply->error())
    {
        QJsonDocument json_doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject json = json_doc.object();
        if (json.value("target_commitish").toString() != QString(GUI_VERSION))
        {
            QMessageBox::StandardButton reply;
            reply = QMessageBox::question(this, "Update Available", "A newer version is available, update?", QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes)
            {
                QNetworkAccessManager *updateManager = new QNetworkAccessManager(this);
                connect(updateManager, &QNetworkAccessManager::finished,
                        this, &MainWindow::updateDownloadFinished);
#ifdef _WIN32
                QNetworkRequest req(QUrl("https://github.com/simple64/simple64-updater/releases/latest/download/simple64-updater.exe"));
#else
                QNetworkRequest req(QUrl("https://github.com/simple64/simple64-updater/releases/latest/download/simple64-updater"));
#endif
                req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                updateManager->get(req);
                download_message = new QMessageBox(this);
                download_message->setStandardButtons(QMessageBox::NoButton);
                download_message->setText("Downloading updater");
                download_message->show();
            }
        }
    }
    reply->deleteLater();
}

void MainWindow::volumeValueChanged(int value)
{
    if (value != settings->value("volume").toInt())
    {
        settings->setValue("volume", value);
        settings->sync();
        (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_AUDIO_VOLUME, &value);
    }
}

void MainWindow::setVerbose()
{
    verbose = 1;
}

void MainWindow::setNoGUI()
{
    nogui = 1;
    if (!menuBar()->isNativeMenuBar())
        menuBar()->hide();
    statusBar()->hide();
}

int MainWindow::getNoGUI()
{
    return nogui;
}

void MainWindow::setTest(int value)
{
    run_test = value;
}

int MainWindow::getTest()
{
    return run_test;
}

int MainWindow::getVerbose()
{
    return verbose;
}

void MainWindow::resizeMainWindow(int Width, int Height)
{
    if (isMaximized() || m_restoreMaximizedAfterFullscreen || m_preserveWindowSizeDuringGame)
        return;

    QSize size = this->size();
    Height += (menuBar()->isNativeMenuBar() ? 0 : ui->menuBar->height()) + ui->statusBar->height();
    if (size.width() != Width || size.height() != Height)
        resize(Width, Height);
}

void MainWindow::toggleFS(int force)
{
    if (coreLib == nullptr)
        return;

    int response = M64VIDEO_NONE;
    if (force == M64VIDEO_NONE)
        (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_VIDEO_MODE, &response);
    if (response == M64VIDEO_WINDOWED || force == M64VIDEO_FULLSCREEN)
    {
        QScreen *targetScreen = QGuiApplication::screenAt(frameGeometry().center());
        if (targetScreen == nullptr && windowHandle() != nullptr)
            targetScreen = windowHandle()->screen();
        if (targetScreen == nullptr)
            targetScreen = QGuiApplication::primaryScreen();

        m_restoreMaximizedAfterFullscreen = isMaximized();
        if (!menuBar()->isNativeMenuBar())
            menuBar()->hide();
        statusBar()->hide();
        if (windowHandle() != nullptr && targetScreen != nullptr && windowHandle()->screen() != targetScreen)
            windowHandle()->setScreen(targetScreen);
        showFullScreen();
    }
    else if (response == M64VIDEO_FULLSCREEN || force == M64VIDEO_WINDOWED)
    {
        if (!nogui)
        {
            if (!menuBar()->isNativeMenuBar())
                menuBar()->show();
            statusBar()->show();
        }
        if (m_restoreMaximizedAfterFullscreen || m_preGameWasMaximized || m_preserveWindowSizeDuringGame)
        {
            setWindowState((windowState() & ~Qt::WindowFullScreen) | Qt::WindowMaximized);
            show();
        }
        else
        {
            showNormal();
        }
        m_restoreMaximizedAfterFullscreen = false;
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    stopGame();

    closePlugins();
    closeCoreLib();

    if (discord_app.activities)
        discord_app.activities->clear_activity(discord_app.activities, &discord_app, nullptr);
    if (discord_app.core)
    {
        discord_app.core->run_callbacks(discord_app.core);
        discord_app.core->destroy(discord_app.core);
    }

    settings->setValue("geometry", saveGeometry());
    settings->setValue("windowState", saveState());

    event->accept();
}

void MainWindow::updateOpenRecent()
{
    OpenRecent->clear();
    QAction *recent[RECENT_SIZE];
    QStringList list = settings->value("RecentROMs2").toStringList();
    if (list.isEmpty())
    {
        OpenRecent->setEnabled(false);
        return;
    }

    OpenRecent->setEnabled(true);
    for (int i = 0; i < list.size() && i < RECENT_SIZE; ++i)
    {
        recent[i] = new QAction(this);
        recent[i]->setText(list.at(i));
        OpenRecent->addAction(recent[i]);
        QAction *temp_recent = recent[i];
        connect(temp_recent, &QAction::triggered, [=]()
                { openROM(list.at(i), "", 0, 0, QJsonObject()); });
    }
    OpenRecent->addSeparator();

    QAction *clearRecent = new QAction(this);
    clearRecent->setText("Clear List");
    OpenRecent->addAction(clearRecent);
    connect(clearRecent, &QAction::triggered, [=]()
            {
        settings->remove("RecentROMs2");
        updateOpenRecent(); });
}

void MainWindow::showMessage(QString message)
{
    QMessageBox *msgBox = new QMessageBox(this);
    msgBox->setText(message);
    msgBox->show();
}

void MainWindow::createVkWindow(QVulkanInstance *vulkan_inst)
{
    my_window = new VkWindow;
    my_window->setVulkanInstance(vulkan_inst);

    QWidget *container = QWidget::createWindowContainer(my_window, this);
    container->setFocusPolicy(Qt::StrongFocus);
    container->setContentsMargins(0, 0, 0, 0);

    my_window->setCursor(Qt::BlankCursor);
    m_gamePage = container;
    if (m_mainStack->count() > 1)
    {
        QWidget *oldGamePage = m_mainStack->widget(1);
        m_mainStack->removeWidget(oldGamePage);
        oldGamePage->deleteLater();
    }
    m_mainStack->addWidget(m_gamePage);
    m_mainStack->setCurrentWidget(m_gamePage);

    my_window->installEventFilter(&keyPressFilter);
    this->installEventFilter(&keyPressFilter);
    frame_timer = new QTimer(this);
    connect(frame_timer, &QTimer::timeout, this, &MainWindow::updateFrameCount);
    frame_timer->start(1000);
    frame_count = 0;
}

void MainWindow::deleteVkWindow()
{
    if (m_mainStack != nullptr && m_libraryPage != nullptr)
        m_mainStack->setCurrentWidget(m_libraryPage);
    if (m_gamePage != nullptr)
    {
        m_mainStack->removeWidget(m_gamePage);
        m_gamePage->deleteLater();
        m_gamePage = new QWidget(this);
        m_mainStack->addWidget(m_gamePage);
    }
    delete my_window;
    my_window = nullptr;
    frame_timer->stop();
    frame_timer->deleteLater();
    FPSLabel->clear();

    if (m_restoreWindowAfterGame)
    {
        if (m_preGameWasMaximized)
        {
            setWindowState((windowState() & ~Qt::WindowFullScreen) | Qt::WindowMaximized);
            show();
            QMetaObject::invokeMethod(this, [this]() {
                setWindowState((windowState() & ~Qt::WindowFullScreen) | Qt::WindowMaximized);
                show();
            }, Qt::QueuedConnection);
        }
        else if (!m_preGameGeometry.isEmpty())
            restoreGeometry(m_preGameGeometry);
        m_restoreWindowAfterGame = false;
    }
    m_preserveWindowSizeDuringGame = false;
}

void MainWindow::killThread()
{
    printf("Application hung, exiting\n");
    exit(1); // Force kill the application, it's stuck
}

void MainWindow::stopGame()
{
    if (!coreLib)
        return;

    int response;
    (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &response);
    if (response != M64EMU_STOPPED)
    {
        (*CoreDoCommand)(M64CMD_STOP, 0, NULL);
        kill_timer = new QTimer(this);
        kill_timer->setInterval(5000);
        kill_timer->setSingleShot(true);
        connect(kill_timer, &QTimer::timeout, this, &MainWindow::killThread);
        kill_timer->start();
        while (workerThread->isRunning())
            QCoreApplication::processEvents();
        kill_timer->stop();
        kill_timer->deleteLater();
    }
}

void MainWindow::resetCore()
{
    closePlugins();
    closeCoreLib();
    loadCoreLib();
    loadPlugins();
}

void MainWindow::openROM(QString filename, QString netplay_ip, int netplay_port, int netplay_player, QJsonObject cheats)
{
    stopGame();

    selectVideoCoreForRom(filename);

    logViewer.clearLog();
    m_currentRomPath = filename;
    m_preGameWasMaximized = isMaximized();
    m_preGameGeometry = saveGeometry();
    m_restoreWindowAfterGame = true;
    m_preserveWindowSizeDuringGame = m_preGameWasMaximized;

    resetCore();
    if (coreLib == nullptr || gfxPlugin == nullptr || audioPlugin == nullptr || inputPlugin == nullptr || rspPlugin == nullptr)
        return;

    workerThread = new WorkerThread(netplay_ip, netplay_port, netplay_player, cheats, this);
    workerThread->setFileName(filename);

    QStringList list;
    if (settings->contains("RecentROMs2"))
        list = settings->value("RecentROMs2").toStringList();
    list.removeAll(filename);
    list.prepend(filename);
    if (list.size() > RECENT_SIZE)
        list.removeLast();
    settings->setValue("RecentROMs2", list);
    updateOpenRecent();

    if (m_glideFullscreenTimer != nullptr)
    {
        m_glideFullscreenTimer->stop();
        m_glideFullscreenTimer->deleteLater();
        m_glideFullscreenTimer = nullptr;
    }

    if (m_useGlide && m_glideStartFullscreen)
    {
        m_glideFullscreenTimer = new QTimer(this);
        m_glideFullscreenTimer->setInterval(100);
        connect(m_glideFullscreenTimer, &QTimer::timeout, this, [this]() {
            int emulationState = M64EMU_STOPPED;
            if (coreLib == nullptr ||
                (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &emulationState) != M64ERR_SUCCESS ||
                emulationState != M64EMU_RUNNING)
                return;

            int videoMode = M64VIDEO_NONE;
            if ((*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_VIDEO_MODE, &videoMode) != M64ERR_SUCCESS)
                return;

            if (videoMode == M64VIDEO_FULLSCREEN)
            {
                m_glideFullscreenTimer->stop();
                return;
            }

            if (videoMode == M64VIDEO_WINDOWED)
            {
                int fullscreenMode = M64VIDEO_FULLSCREEN;
                if ((*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_VIDEO_MODE, &fullscreenMode) == M64ERR_SUCCESS)
                    m_glideFullscreenTimer->stop();
            }
        });
        connect(workerThread, &QThread::finished, m_glideFullscreenTimer, &QTimer::stop);
    }

    workerThread->start();
    if (m_glideFullscreenTimer != nullptr)
        m_glideFullscreenTimer->start();
}

void MainWindow::selectVideoCoreForRom(const QString &romPath)
{
    m_useGlide = false;
    m_glideStartFullscreen = true;
    if (coreLib == nullptr || romPath.isEmpty())
        return;

    if (loadROM(romPath) != M64ERR_SUCCESS)
        return;

    const QString romMd5 = getCurrentRomMd5();
    (*CoreDoCommand)(M64CMD_ROM_CLOSE, 0, NULL);
    if (romMd5.isEmpty() || ConfigGetUserConfigPath == nullptr)
        return;

    QSettings gameSettings(QDir(ConfigGetUserConfigPath()).filePath("game-settings.ini"), QSettings::IniFormat);
    const QString key = "Games/" + romMd5 + "/Frontend/VideoCore";
    m_useGlide = gameSettings.value(key).toString().compare("GLideN64", Qt::CaseInsensitive) == 0;
    if (m_useGlide)
        applyGlideSettingsForRom(romMd5);
}

void MainWindow::applyGlideSettingsForRom(const QString &romMd5)
{
    const QString configDir = QDir(ConfigGetUserConfigPath()).absolutePath();
    QSettings gameSettings(QDir(configDir).filePath("game-settings.ini"), QSettings::IniFormat);
    QSettings glideSettings(QDir(configDir).filePath("GLideN64.ini"), QSettings::IniFormat);
    const QString gamePrefix = "Games/" + romMd5 + "/GLideN64/";
    m_glideStartFullscreen = gameSettings.value(gamePrefix + "StartFullscreen", true).toBool();

    glideSettings.setValue("User/frameBufferEmulation/aspect",
                           gameSettings.value(gamePrefix + "AspectRatio", 1).toInt());
    glideSettings.setValue("User/frameBufferEmulation/nativeResFactor",
                           gameSettings.value(gamePrefix + "NativeResolutionFactor", 0).toInt());
    glideSettings.setValue("User/graphics2D/correctTexrectCoords",
                           gameSettings.value(gamePrefix + "Correct2DCoordinates", 0).toInt());
    glideSettings.setValue("User/graphics2D/enableNativeResTexrects",
                           gameSettings.value(gamePrefix + "NativeResolution2D", 0).toInt());
    glideSettings.setValue("User/font/color", QColor(Qt::black));
    glideSettings.sync();
}

void MainWindow::on_actionOpen_ROM_triggered()
{
    QString filename = QFileDialog::getOpenFileName(this,
                                                    tr("Open ROM"), settings->value("ROMdir").toString(), tr("ROM Files (*.n64 *.N64 *.z64 *.Z64 *.v64 *.V64 *.rom *.ROM *.zip *.ZIP *.7z)"));
    if (!filename.isNull())
    {
        QFileInfo info(filename);
        settings->setValue("ROMdir", info.absoluteDir().absolutePath());
        openROM(filename, "", 0, 0, QJsonObject());
    }
}

void MainWindow::on_actionPlugin_Paths_triggered()
{
    SettingsDialog *settings = new SettingsDialog(this);
    settings->show();
}

void MainWindow::on_actionStop_Game_triggered()
{
    stopGame();
}

void MainWindow::on_actionExit_triggered()
{
    this->close();
}

void MainWindow::on_actionPlugin_Settings_triggered()
{
    PluginDialog *settings = new PluginDialog(this);
    settings->show();
}

void MainWindow::on_actionPause_Game_triggered()
{
    int response;
    (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &response);
    if (response == M64EMU_RUNNING)
        (*CoreDoCommand)(M64CMD_PAUSE, 0, NULL);
    else if (response == M64EMU_PAUSED)
        (*CoreDoCommand)(M64CMD_RESUME, 0, NULL);
}

void MainWindow::on_actionMute_triggered()
{
    int response;
    (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_AUDIO_MUTE, &response);
    if (response == 0)
    {
        response = 1;
        (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_AUDIO_MUTE, &response);
    }
    else if (response == 1)
    {
        response = 0;
        (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_AUDIO_MUTE, &response);
    }
}

void MainWindow::on_actionHard_Reset_triggered()
{
    (*CoreDoCommand)(M64CMD_RESET, 1, NULL);
}

void MainWindow::on_actionSoft_Reset_triggered()
{
    (*CoreDoCommand)(M64CMD_RESET, 0, NULL);
}

void MainWindow::on_actionTake_Screenshot_triggered()
{
    (*CoreDoCommand)(M64CMD_TAKE_NEXT_SCREENSHOT, 0, NULL);
}

void MainWindow::on_actionSave_State_triggered()
{
    (*CoreDoCommand)(M64CMD_STATE_SAVE, 1, NULL);
}

void MainWindow::on_actionLoad_State_triggered()
{
    (*CoreDoCommand)(M64CMD_STATE_LOAD, 1, NULL);
}

void MainWindow::on_actionToggle_Fullscreen_triggered()
{
    int response;
    (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_VIDEO_MODE, &response);
    if (response == M64VIDEO_WINDOWED)
    {
        response = M64VIDEO_FULLSCREEN;
        (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_VIDEO_MODE, &response);
    }
    else if (response == M64VIDEO_FULLSCREEN)
    {
        response = M64VIDEO_WINDOWED;
        (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_VIDEO_MODE, &response);
    }
}

void MainWindow::on_actionCheats_triggered()
{
    QString gameName = getCheatGameName();
    if (!gameName.isEmpty())
    {
        CheatsDialog *cheats = new CheatsDialog(gameName, this);
        cheats->show();
    }
    else
    {
        QMessageBox msgBox;
        msgBox.setText("Game must be running.");
        msgBox.exec();
    }
}

void MainWindow::on_actionSave_State_To_triggered()
{
    QString filename = QFileDialog::getSaveFileName(this,
                                                    tr("Save State File"), NULL, NULL);
    if (!filename.isNull())
    {
        if (!filename.contains(".st"))
            filename.append(".state");
        (*CoreDoCommand)(M64CMD_STATE_SAVE, 1, filename.toUtf8().data());
    }
}

void MainWindow::on_actionLoad_State_From_triggered()
{
    QString filename = QFileDialog::getOpenFileName(this,
                                                    tr("Open Save State"), NULL, tr("State Files (*.st* *.pj*)"));
    if (!filename.isNull())
    {
        (*CoreDoCommand)(M64CMD_STATE_LOAD, 1, filename.toUtf8().data());
    }
}

void MainWindow::on_actionController_Configuration_triggered()
{
    if (!coreLib)
        return;

    int response;
    (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_EMU_STATE, &response);
    if (response == M64EMU_STOPPED)
        resetCore();

    typedef void (*Config_Func)();
    Config_Func PluginConfig = (Config_Func)osal_dynlib_getproc(inputPlugin, "PluginConfig");
    if (PluginConfig)
        PluginConfig();
}

void MainWindow::on_actionHotkey_Configuration_triggered()
{
    HotkeyDialog *settings = new HotkeyDialog(this);
    settings->show();
}

void MainWindow::on_actionToggle_Speed_Limiter_triggered()
{
    int value;
    (*CoreDoCommand)(M64CMD_CORE_STATE_QUERY, M64CORE_SPEED_LIMITER, &value);
    value = !value;
    (*CoreDoCommand)(M64CMD_CORE_STATE_SET, M64CORE_SPEED_LIMITER, &value);
}

void MainWindow::on_actionView_Log_triggered()
{
    logViewer.show();
}

void MainWindow::on_actionCreate_Room_triggered()
{
    CreateRoom *createRoom = new CreateRoom(this);
    createRoom->show();
}

void MainWindow::on_actionJoin_Room_triggered()
{
    JoinRoom *joinRoom = new JoinRoom(this);
    joinRoom->show();
}

void MainWindow::on_actionSupport_on_Patreon_triggered()
{
    QDesktopServices::openUrl(QUrl("https://www.patreon.com/loganmc10"));
}

void MainWindow::on_actionSupport_on_GithubSponser_triggered()
{
    QDesktopServices::openUrl(QUrl("https://github.com/sponsors/loganmc10"));
}

void MainWindow::on_actionOpen_Discord_Channel_triggered()
{
    QDesktopServices::openUrl(QUrl("https://discord.gg/tsR3RtYynZ"));
}

void MainWindow::addLibraryFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this,
                                                             tr("Add Library Folder"),
                                                             settings->value("ROMdir").toString(),
                                                             QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (folder.isEmpty())
        return;

    QStringList folders = settings->value("LibraryFolders").toStringList();
    if (!folders.contains(folder))
        folders.append(folder);
    settings->setValue("LibraryFolders", folders);
    refreshLibrary();
}

void MainWindow::clearLibraryFolders()
{
    settings->remove("LibraryFolders");
    refreshLibrary();
}

void MainWindow::chooseBoxArtFolder()
{
    const QString currentFolder = settings->value("LibraryBoxArtFolder").toString();
    const QString folder = QFileDialog::getExistingDirectory(
        this,
        tr("Select Box Art Folder"),
        currentFolder.isEmpty() ? QDir::homePath() : currentFolder);

    if (folder.isEmpty())
        return;

    settings->setValue("LibraryBoxArtFolder", folder);
    refreshLibrary();
}

void MainWindow::chooseGbLibraryFolder()
{
    const QString currentFolder = settings->value("LibraryGbFolder").toString();
    const QString folder = QFileDialog::getExistingDirectory(
        this,
        tr("Select GB/GBC Library Folder"),
        currentFolder.isEmpty() ? QDir::homePath() : currentFolder);

    if (folder.isEmpty())
        return;

    settings->setValue("LibraryGbFolder", folder);
    refreshGbLibrary();
}

void MainWindow::showCurrentGameSettings()
{
    QString md5;
    QString goodName;
    if (!getCurrentRomIdentity(md5, goodName))
    {
        QMessageBox msgBox;
        msgBox.setText("Start a game before editing current game settings.");
        msgBox.exec();
        return;
    }

    GameSettingsDialog dialog(md5, goodName, this);
    dialog.exec();
}

WorkerThread *MainWindow::getWorkerThread()
{
    return workerThread;
}

VkWindow *MainWindow::getVkWindow()
{
    return my_window;
}

QSettings *MainWindow::getSettings()
{
    return settings;
}

LogViewer *MainWindow::getLogViewer()
{
    return &logViewer;
}

m64p_dynlib_handle MainWindow::getAudioPlugin()
{
    return audioPlugin;
}

m64p_dynlib_handle MainWindow::getRspPlugin()
{
    return rspPlugin;
}

m64p_dynlib_handle MainWindow::getInputPlugin()
{
    return inputPlugin;
}

m64p_dynlib_handle MainWindow::getGfxPlugin()
{
    return gfxPlugin;
}

void MainWindow::closeCoreLib()
{
    if (coreLib != nullptr)
    {
        (*ConfigSaveFile)();
        (*CoreShutdown)();
        osal_dynlib_close(coreLib);
        coreLib = nullptr;
    }
}

void MainWindow::loadCoreLib()
{
#ifdef CORE_LIBRARY_PATH
    QString corePath = CORE_LIBRARY_PATH;
#else
    QString corePath = QCoreApplication::applicationDirPath();
#endif
    m64p_error res = osal_dynlib_open(&coreLib, QDir(corePath).filePath(OSAL_DEFAULT_DYNLIB_FILENAME).toUtf8().constData());

    if (res != M64ERR_SUCCESS)
    {
        QMessageBox msgBox;
        msgBox.setText("Failed to load core library");
        msgBox.exec();
        return;
    }

    CoreStartup = (ptr_CoreStartup)osal_dynlib_getproc(coreLib, "CoreStartup");
    CoreShutdown = (ptr_CoreShutdown)osal_dynlib_getproc(coreLib, "CoreShutdown");
    CoreDoCommand = (ptr_CoreDoCommand)osal_dynlib_getproc(coreLib, "CoreDoCommand");
    CoreAttachPlugin = (ptr_CoreAttachPlugin)osal_dynlib_getproc(coreLib, "CoreAttachPlugin");
    CoreDetachPlugin = (ptr_CoreDetachPlugin)osal_dynlib_getproc(coreLib, "CoreDetachPlugin");
    CoreOverrideVidExt = (ptr_CoreOverrideVidExt)osal_dynlib_getproc(coreLib, "CoreOverrideVidExt");

    ConfigGetUserConfigPath = (ptr_ConfigGetUserConfigPath)osal_dynlib_getproc(coreLib, "ConfigGetUserConfigPath");
    ConfigSaveFile = (ptr_ConfigSaveFile)osal_dynlib_getproc(coreLib, "ConfigSaveFile");
    ConfigGetParameter = (ptr_ConfigGetParameter)osal_dynlib_getproc(coreLib, "ConfigGetParameter");
    ConfigGetParameterType = (ptr_ConfigGetParameterType)osal_dynlib_getproc(coreLib, "ConfigGetParameterType");
    ConfigGetParameterHelp = (ptr_ConfigGetParameterHelp)osal_dynlib_getproc(coreLib, "ConfigGetParameterHelp");
    ConfigGetParamInt = (ptr_ConfigGetParamInt)osal_dynlib_getproc(coreLib, "ConfigGetParamInt");
    ConfigGetParamFloat = (ptr_ConfigGetParamFloat)osal_dynlib_getproc(coreLib, "ConfigGetParamFloat");
    ConfigGetParamBool = (ptr_ConfigGetParamBool)osal_dynlib_getproc(coreLib, "ConfigGetParamBool");
    ConfigGetParamString = (ptr_ConfigGetParamString)osal_dynlib_getproc(coreLib, "ConfigGetParamString");
    ConfigSetParameter = (ptr_ConfigSetParameter)osal_dynlib_getproc(coreLib, "ConfigSetParameter");
    ConfigDeleteSection = (ptr_ConfigDeleteSection)osal_dynlib_getproc(coreLib, "ConfigDeleteSection");
    ConfigOpenSection = (ptr_ConfigOpenSection)osal_dynlib_getproc(coreLib, "ConfigOpenSection");
    ConfigSaveSection = (ptr_ConfigSaveSection)osal_dynlib_getproc(coreLib, "ConfigSaveSection");
    ConfigListParameters = (ptr_ConfigListParameters)osal_dynlib_getproc(coreLib, "ConfigListParameters");
    ConfigGetSharedDataFilepath = (ptr_ConfigGetSharedDataFilepath)osal_dynlib_getproc(coreLib, "ConfigGetSharedDataFilepath");

    CoreAddCheat = (ptr_CoreAddCheat)osal_dynlib_getproc(coreLib, "CoreAddCheat");

    QString qtConfigDir = settings->value("configDirPath").toString();

    if (!qtConfigDir.isEmpty())
        (*CoreStartup)(CORE_API_VERSION, qtConfigDir.toUtf8().constData() /*Config dir*/, QCoreApplication::applicationDirPath().toUtf8().constData(), (char *)"Core", DebugCallback, NULL, NULL);
    else
        (*CoreStartup)(CORE_API_VERSION, NULL /*Config dir*/, QCoreApplication::applicationDirPath().toUtf8().constData(), (char *)"Core", DebugCallback, NULL, NULL);

    if (!m_useGlide)
        CoreOverrideVidExt(&vidExtFunctions);
}

void MainWindow::closePlugins()
{
    ptr_PluginShutdown PluginShutdown;

    if (gfxPlugin != nullptr)
    {
        PluginShutdown = (ptr_PluginShutdown)osal_dynlib_getproc(gfxPlugin, "PluginShutdown");
        (*PluginShutdown)();
        osal_dynlib_close(gfxPlugin);
        gfxPlugin = nullptr;
    }
    if (audioPlugin != nullptr)
    {
        PluginShutdown = (ptr_PluginShutdown)osal_dynlib_getproc(audioPlugin, "PluginShutdown");
        (*PluginShutdown)();
        osal_dynlib_close(audioPlugin);
        audioPlugin = nullptr;
    }
    if (inputPlugin != nullptr)
    {
        PluginShutdown = (ptr_PluginShutdown)osal_dynlib_getproc(inputPlugin, "PluginShutdown");
        (*PluginShutdown)();
        osal_dynlib_close(inputPlugin);
        inputPlugin = nullptr;
    }
    if (rspPlugin != nullptr)
    {
        PluginShutdown = (ptr_PluginShutdown)osal_dynlib_getproc(rspPlugin, "PluginShutdown");
        (*PluginShutdown)();
        osal_dynlib_close(rspPlugin);
        rspPlugin = nullptr;
    }
}

typedef void (*ptr_VolumeSetLevel)(int level);
void MainWindow::loadPlugins()
{
    if (coreLib == nullptr)
        return;

    ptr_VolumeSetLevel VolumeSetLevel;
    ptr_PluginStartup PluginStartup;
    ptr_PluginGetVersion PluginGetVersion;
    m64p_error res;
    QMessageBox msgBox;
#ifdef PLUGIN_DIR_PATH
    QString pluginPath = PLUGIN_DIR_PATH;
#else
    QString pluginPath = QCoreApplication::applicationDirPath();
#endif
    QString file_path;
    QString plugin_path;

    file_path = m_useGlide ? "mupen64plus-video-GLideN64" : "simple64-video-parallel";
    file_path += OSAL_DLL_EXTENSION;
    plugin_path = QDir(pluginPath).filePath(file_path);

    res = osal_dynlib_open(&gfxPlugin, plugin_path.toUtf8().constData());
    if (res != M64ERR_SUCCESS)
    {
        msgBox.setText(m_useGlide ? "Failed to load GLideN64 video plugin" : "Failed to load video plugin");
        msgBox.exec();
        return;
    }

    PluginGetVersion = (ptr_PluginGetVersion)osal_dynlib_getproc(gfxPlugin, "PluginGetVersion");
    const char *pluginName;
    (*PluginGetVersion)(NULL, NULL, NULL, &pluginName, NULL);
    const char *expectedVideoPlugin = m_useGlide ? "GLideN64" : "parallel";
    if (strcmp(expectedVideoPlugin, pluginName))
    {
        osal_dynlib_close(gfxPlugin);
        gfxPlugin = nullptr;
        msgBox.setText("Incorrect GFX plugin");
        msgBox.exec();
        return;
    }

    PluginStartup = (ptr_PluginStartup)osal_dynlib_getproc(gfxPlugin, "PluginStartup");
    const char *videoContext = m_useGlide ? "Video-GLideN64" : "Video";
    if ((*PluginStartup)(coreLib, (void *)videoContext, DebugCallback) != M64ERR_SUCCESS)
    {
        osal_dynlib_close(gfxPlugin);
        gfxPlugin = nullptr;
        msgBox.setText(m_useGlide ? "GLideN64 video plugin failed to start" : "Video plugin failed to start");
        msgBox.exec();
        return;
    }

    file_path = "simple64-audio-sdl2";
    file_path += OSAL_DLL_EXTENSION;
    plugin_path = QDir(pluginPath).filePath(file_path);

    res = osal_dynlib_open(&audioPlugin, plugin_path.toUtf8().constData());
    if (res != M64ERR_SUCCESS)
    {
        msgBox.setText("Failed to load audio plugin");
        msgBox.exec();
        return;
    }

    PluginGetVersion = (ptr_PluginGetVersion)osal_dynlib_getproc(audioPlugin, "PluginGetVersion");
    (*PluginGetVersion)(NULL, NULL, NULL, &pluginName, NULL);
    if (strcmp("simple64 SDL2 Audio Plugin", pluginName))
    {
        osal_dynlib_close(audioPlugin);
        audioPlugin = nullptr;
        msgBox.setText("Incorrect Audio plugin");
        msgBox.exec();
        return;
    }

    PluginStartup = (ptr_PluginStartup)osal_dynlib_getproc(audioPlugin, "PluginStartup");
    (*PluginStartup)(coreLib, (char *)"Audio", DebugCallback);
    int volume = 100;
    if (settings->contains("volume"))
        volume = settings->value("volume").toInt();
    VolumeSetLevel = (ptr_VolumeSetLevel)osal_dynlib_getproc(audioPlugin, "VolumeSetLevel");
    (*VolumeSetLevel)(volume);

    res = osal_dynlib_open(&inputPlugin, QDir(pluginPath).filePath(settings->value("inputPlugin").toString()).toUtf8().constData());
    if (res != M64ERR_SUCCESS)
    {
        msgBox.setText("Failed to load input plugin");
        msgBox.exec();
        return;
    }
    PluginStartup = (ptr_PluginStartup)osal_dynlib_getproc(inputPlugin, "PluginStartup");
    if (settings->value("inputPlugin").toString().contains("-qt"))
        (*PluginStartup)(coreLib, this, DebugCallback);
    else
        (*PluginStartup)(coreLib, (char *)"Input", DebugCallback);

    file_path = m_useGlide ? "mupen64plus-rsp-hle" : "simple64-rsp-parallel";
    file_path += OSAL_DLL_EXTENSION;
    plugin_path = QDir(pluginPath).filePath(file_path);

    res = osal_dynlib_open(&rspPlugin, plugin_path.toUtf8().constData());
    if (res != M64ERR_SUCCESS)
    {
        msgBox.setText(m_useGlide ? "Failed to load HLE RSP plugin" : "Failed to load rsp plugin");
        msgBox.exec();
        return;
    }

    PluginGetVersion = (ptr_PluginGetVersion)osal_dynlib_getproc(rspPlugin, "PluginGetVersion");
    (*PluginGetVersion)(NULL, NULL, NULL, &pluginName, NULL);
    const char *expectedRspPlugin = m_useGlide ? "Hacktarux/Azimer High-Level Emulation RSP Plugin" : "ParaLLEl RSP";
    if (strcmp(expectedRspPlugin, pluginName))
    {
        osal_dynlib_close(rspPlugin);
        rspPlugin = nullptr;
        msgBox.setText("Incorrect RSP plugin");
        msgBox.exec();
        return;
    }

    PluginStartup = (ptr_PluginStartup)osal_dynlib_getproc(rspPlugin, "PluginStartup");
    const char *rspContext = m_useGlide ? "RSP-HLE" : "RSP";
    if ((*PluginStartup)(coreLib, (void *)rspContext, DebugCallback) != M64ERR_SUCCESS)
    {
        osal_dynlib_close(rspPlugin);
        rspPlugin = nullptr;
        msgBox.setText(m_useGlide ? "HLE RSP plugin failed to start" : "RSP plugin failed to start");
        msgBox.exec();
        return;
    }

    if (m_useGlide)
    {
        m64p_handle hleConfig;
        if ((*ConfigOpenSection)("Rsp-HLE", &hleConfig) == M64ERR_SUCCESS)
        {
            const QByteArray fallbackPath = QDir(pluginPath).filePath("mupen64plus-rsp-parallel" OSAL_DLL_EXTENSION).toUtf8();
            int enabled = 1;
            int disabled = 0;
            (*ConfigSetParameter)(hleConfig, "RspFallback", M64TYPE_STRING, fallbackPath.constData());
            (*ConfigSetParameter)(hleConfig, "DisplayListToGraphicsPlugin", M64TYPE_BOOL, &enabled);
            (*ConfigSetParameter)(hleConfig, "AudioListToAudioPlugin", M64TYPE_BOOL, &disabled);
        }
    }
}

m64p_dynlib_handle MainWindow::getCoreLib()
{
    return coreLib;
}

void MainWindow::addFrameCount()
{
    ++frame_count;
}

void MainWindow::simulateInput()
{
    if (run_test % 2)
        (*CoreDoCommand)(M64CMD_SEND_SDL_KEYDOWN, test_key_value, NULL);
    else
    {
        (*CoreDoCommand)(M64CMD_SEND_SDL_KEYUP, test_key_value, NULL);
        if (test_key_value == 40)
            test_key_value = 225; // Enter/Start
        else
            test_key_value = 40;
    }
    printf("%u\n", frame_count);
    if (run_test == 1)
    {
        if (frame_count == 0)
            abort();
        else
            (*CoreDoCommand)(M64CMD_STOP, 0, NULL);
    }
    --run_test;
}

void MainWindow::updateFrameCount()
{
    if (run_test)
        simulateInput();
    QString FPS = QString("%1 FPS").arg(frame_count);
    if (m_cheatsEnabled)
    {
        FPS.prepend("Cheats Enabled    ");
    }
    FPSLabel->setText(FPS);
    frame_count = 0;
}

void MainWindow::setCheats(QJsonObject cheatsData, bool netplay)
{
    if (!netplay)
    {
        QString gameName = getCheatGameName();
        QJsonObject gameData = loadCheatData(gameName);
        cheatsData = getCheatsFromSettings(gameName, gameData);
    }
    m_cheatsEnabled = loadCheats(cheatsData);
}

bool MainWindow::isGameRunning() const
{
    return workerThread != nullptr && workerThread->isRunning();
}
