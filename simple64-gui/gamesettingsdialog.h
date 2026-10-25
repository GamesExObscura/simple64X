#ifndef GAMESETTINGSDIALOG_H
#define GAMESETTINGSDIALOG_H

#include "m64p_types.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QList>
#include <QSettings>

class QLabel;
class QLineEdit;

struct GameSettingRow
{
    QString section;
    QString paramName;
    m64p_type type;
    QWidget *editor = nullptr;
    QVariant globalValue;
};

struct GameControllerRow
{
    QComboBox *profileSelect = nullptr;
    QComboBox *gamepadSelect = nullptr;
    QComboBox *pakSelect = nullptr;
};

class GameSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    GameSettingsDialog(const QString &romMd5, const QString &goodName, QWidget *parent = nullptr);
    ~GameSettingsDialog() override;

private slots:
    void saveAndClose();
    void clearOverrides();

private:
    void buildUi();
    void populateSection(QGridLayout *layout, const QString &sectionName, m64p_handle handle);
    void addSettingRow(QGridLayout *layout, const QString &sectionName, const QString &paramName, m64p_type type, m64p_handle handle, int row);
    void populateControllerTab(QGridLayout *layout);
    QString overrideKey(const QString &section, const QString &paramName) const;
    QString controllerOverrideKey(int controllerIndex, const QString &name) const;
    QVariant configValue(m64p_handle handle, const QString &paramName, m64p_type type) const;
    QVariant editorValue(const GameSettingRow *row) const;
    void setEditorValue(GameSettingRow *row, const QVariant &value);
    void loadControllerOverride(QComboBox *box, const QString &key, const QStringList &choices, const QString &globalValue);
    bool romRequiresVru() const;

    QString m_romMd5;
    QString m_goodName;
    m64p_handle m_coreConfigHandle = nullptr;
    m64p_handle m_videoConfigHandle = nullptr;
    QSettings *m_globalConfig = nullptr;
    QSettings *m_overrideSettings = nullptr;
    QSettings *m_profileSettings = nullptr;
    QSettings *m_controllerSettings = nullptr;
    QSettings *m_controllerOverrideSettings = nullptr;
    QComboBox *m_videoCoreSelect = nullptr;
    QComboBox *m_glideAspectSelect = nullptr;
    QComboBox *m_glideResolutionSelect = nullptr;
    QComboBox *m_glideCorrect2DSelect = nullptr;
    QComboBox *m_glideNative2DSelect = nullptr;
    QCheckBox *m_glideStartFullscreenCheck = nullptr;
    bool m_clearCoreVideoSettings = false;
    QList<GameSettingRow *> m_settingRows;
    GameControllerRow m_controllerRows[4];
};

#endif // GAMESETTINGSDIALOG_H
