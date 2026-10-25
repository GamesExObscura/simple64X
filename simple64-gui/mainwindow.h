#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "discord/discord_game_sdk.h"
#include "vkwindow.h"
#include "workerthread.h"
#include "logviewer.h"
#include "keypressfilter.h"
extern "C"
{
#include "osal/osal_dynamiclib.h"
}
#include <QMainWindow>
#include <QSettings>
#include <QWidgetAction>
#include <QSlider>
#include <QMessageBox>
#include <QLabel>
#include <QVulkanInstance>
#include <QNetworkReply>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>

namespace Ui
{
    class MainWindow;
}

struct Discord_Application
{
    struct IDiscordCore *core;
    struct IDiscordActivityManager *activities;
};

struct LibraryEntry
{
    QString filePath;
    QString title;
    QString coverPath;
    QString players;
};

struct GbLibraryEntry
{
    QString title;
    QString romPath;
    QString savePath;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    WorkerThread *getWorkerThread();
    VkWindow *getVkWindow();
    QSettings *getSettings();
    LogViewer *getLogViewer();

    m64p_dynlib_handle getAudioPlugin();
    m64p_dynlib_handle getRspPlugin();
    m64p_dynlib_handle getInputPlugin();
    m64p_dynlib_handle getGfxPlugin();

    void openROM(QString filename, QString netplay_ip, int netplay_port, int netplay_player, QJsonObject cheats);
    void setVerbose();
    int getVerbose();
    void setNoGUI();
    int getNoGUI();
    void setTest(int value);
    int getTest();
    void updatePlugins();
    void resetCore();
    m64p_dynlib_handle getCoreLib();
    struct Discord_Application *getDiscordApp();
    explicit MainWindow(QWidget *parent = 0);
    void updateApp();
    void startUpdateCheck(bool asked);
    bool isGameRunning() const;
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event);

public slots:
    void resizeMainWindow(int Width, int Height);
    void toggleFS(int force);
    void createVkWindow(QVulkanInstance *instance);
    void deleteVkWindow();
    void showMessage(QString message);
    void updateDiscordActivity(struct DiscordActivity activity);
    void clearDiscordActivity();
    void addFrameCount();
    void setCheats(QJsonObject cheatsData, bool netplay);

private slots:
    void killThread();

    void updateFrameCount();

    void discordCallback();

    void updateDownloadFinished(QNetworkReply *reply);

    void updateReplyFinished(QNetworkReply *reply);
    void checkForUpdatesCB();
    void toggleUpdateStartupCB(bool checked);

    void volumeValueChanged(int value);

    void on_actionOpen_ROM_triggered();

    void on_actionPlugin_Paths_triggered();

    void on_actionStop_Game_triggered();

    void on_actionExit_triggered();

    void on_actionPlugin_Settings_triggered();

    void on_actionPause_Game_triggered();

    void on_actionMute_triggered();

    void on_actionHard_Reset_triggered();

    void on_actionSoft_Reset_triggered();

    void on_actionTake_Screenshot_triggered();

    void on_actionSave_State_triggered();

    void on_actionLoad_State_triggered();

    void on_actionToggle_Fullscreen_triggered();

    void on_actionCheats_triggered();

    void on_actionSave_State_To_triggered();

    void on_actionLoad_State_From_triggered();

    void on_actionController_Configuration_triggered();

    void on_actionHotkey_Configuration_triggered();

    void on_actionToggle_Speed_Limiter_triggered();

    void on_actionView_Log_triggered();

    void on_actionCreate_Room_triggered();

    void on_actionJoin_Room_triggered();

    void on_actionSupport_on_Patreon_triggered();

    void on_actionSupport_on_GithubSponser_triggered();

    void on_actionOpen_Discord_Channel_triggered();

    void addLibraryFolder();

    void chooseBoxArtFolder();
    void chooseGbLibraryFolder();

    void clearLibraryFolders();

    void showCurrentGameSettings();

    void filterLibrary(const QString &text);

    void launchLibraryEntry(int row, int column);
    void showLibraryContextMenu(const QPoint &pos);
    void openLibraryGameSettings();

private:
    void setupDiscord();
    void stopGame();
    void updateOpenRecent();
    void updateGB(Ui::MainWindow *ui);
    void setupLibraryUi();
    void setupLibraryMenu();
    void refreshLibrary();
    void refreshGbLibrary();
    void writeGbLibraryCache() const;
    void rebuildLibraryTable();
    void openSelectedLibraryEntry();
    QString findCoverForRom(const QString &romPath) const;
    bool getCurrentRomIdentity(QString &md5, QString &goodName) const;
    bool getRomIdentityFromFile(const QString &romPath, QString &md5, QString &goodName);
    void selectVideoCoreForRom(const QString &romPath);
    void applyGlideSettingsForRom(const QString &romMd5);
    void loadCoreLib();
    void loadPlugins();
    void closeCoreLib();
    void closePlugins();
    void simulateInput();
    Ui::MainWindow *ui;
    QMenu *OpenRecent;
    int verbose;
    int nogui;
    int run_test;
    int test_key_value;
    uint32_t frame_count;

    QMessageBox *download_message = nullptr;
    VkWindow *my_window = nullptr;
    WorkerThread *workerThread = nullptr;
    LogViewer logViewer;
    QSettings *settings = nullptr;
    KeyPressFilter keyPressFilter;
    QTimer *frame_timer = nullptr;
    QLabel *FPSLabel = nullptr;
    QTimer *kill_timer = nullptr;
    QTimer *m_glideFullscreenTimer = nullptr;
    bool m_cheatsEnabled = false;
    bool m_restoreMaximizedAfterFullscreen = false;
    bool m_restoreWindowAfterGame = false;
    bool m_preserveWindowSizeDuringGame = false;
    bool m_preGameWasMaximized = false;
    bool m_glideStartFullscreen = true;
    QByteArray m_preGameGeometry;
    QStackedWidget *m_mainStack = nullptr;
    QWidget *m_libraryPage = nullptr;
    QWidget *m_gamePage = nullptr;
    QTableWidget *m_libraryTable = nullptr;
    QLineEdit *m_libraryFilter = nullptr;
    QPushButton *m_libraryOpenButton = nullptr;
    QAction *m_addLibraryFolderAction = nullptr;
    QAction *m_boxArtFolderAction = nullptr;
    QAction *m_gbLibraryFolderAction = nullptr;
    QAction *m_refreshLibraryAction = nullptr;
    QAction *m_clearLibraryFoldersAction = nullptr;
    QAction *m_currentGameSettingsAction = nullptr;
    QList<LibraryEntry> m_libraryEntries;
    QList<LibraryEntry> m_filteredLibraryEntries;
    QList<GbLibraryEntry> m_gbLibraryEntries;
    QString m_currentRomPath;
    bool m_useGlide = false;

    m64p_dynlib_handle coreLib;
    m64p_dynlib_handle rspPlugin;
    m64p_dynlib_handle audioPlugin;
    m64p_dynlib_handle gfxPlugin;
    m64p_dynlib_handle inputPlugin;

    struct Discord_Application discord_app;
};

extern MainWindow *w;

class VolumeAction : public QWidgetAction
{
public:
    VolumeAction(const QString &title, QObject *parent) : QWidgetAction(parent)
    {
        QWidget *pWidget = new QWidget;
        QHBoxLayout *pLayout = new QHBoxLayout(pWidget);
        QLabel *pLabel = new QLabel(title, pWidget);
        pLayout->addWidget(pLabel);
        pSlider = new QSlider(Qt::Horizontal, pWidget);
        pSlider->setMinimum(0);
        pSlider->setMaximum(100);
        pLayout->addWidget(pSlider);
        pWidget->setLayout(pLayout);

        setDefaultWidget(pWidget);
    }
    void releaseWidget(QWidget *widget)
    {
        widget->deleteLater();
    }

    QSlider *slider()
    {
        return pSlider;
    }

private:
    QSlider *pSlider;
};

#endif // MAINWINDOW_H
