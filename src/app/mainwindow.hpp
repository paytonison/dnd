#pragma once
#include "dnd/content.hpp"
#include "dnd/persistence.hpp"
#include <QMainWindow>
#include <QString>
#include <vector>

class QAction;
class QLabel;
class QListWidget;
class QMenu;
class QScrollArea;
class QTextBrowser;
class QTimer;
class QWidget;

namespace dnd {
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(const QString& dataRoot = {}, QWidget* parent = nullptr);
    void newDocument(const std::string& edition, const std::string& moduleVersion = {});
    bool openPath(const QString& path, bool offerRecovery = true);
    bool saveTo(const QString& path);
    bool exportPdf(const QString& path);
    bool setChoice(const std::string& path, const Json& value);
    bool applyOverride(const std::string& target, const Json& value, const QString& reason);
    bool upgradeToCopy(const std::string& moduleVersion);
    bool acceptRoll(const std::string& requestId, bool replaceExisting = false);
    bool setResource(const std::string& path, const Json& value);
    bool executeAction(const std::string& id, const Json& inputs = Json::object());
    QString inspectionHtml(const std::string& target) const;
    const CharacterDocument& document() const { return document_; }
    const Evaluation& evaluation() const { return evaluation_; }
    const ResolvedRuleset& ruleset() const { return ruleset_; }
    const QString& currentPath() const { return path_; }
    const QString& lastError() const { return lastError_; }
    bool readOnlyMode() const { return inspectOnly_ || !ruleset_.valid(); }
    bool dirty() const { return dirty_; }
    void setAdvanced(bool enabled);
    bool advanced() const { return advanced_; }
    void inspect(const std::string& target);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    CharacterDocument document_;
    Json original_;
    std::vector<ContentPack> packs_;
    std::vector<Message> catalogMessages_;
    std::vector<Message> loadMessages_;
    ResolvedRuleset ruleset_;
    Evaluation evaluation_;
    QString dataRoot_, userPacks_, path_, protectedOriginPath_, lastError_;
    bool dirty_ = false, advanced_ = false, inspectOnly_ = false, refreshing_ = false;
    QListWidget* stages_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* stageTitle_ = nullptr;
    QLabel* validation_ = nullptr;
    QScrollArea* fieldsScroll_ = nullptr;
    QTextBrowser* sheet_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* overrideAction_ = nullptr;
    QAction* advancedAction_ = nullptr;
    QAction* upgradeAction_ = nullptr;
    QAction* actionsAction_ = nullptr;
    QTimer* autosaveTimer_ = nullptr;
    void loadCatalog();
    void buildUi();
    void buildMenus();
    void refresh(bool rebuildEditor = true);
    void rebuildFields();
    void markChanged(bool rebuildEditor = true);
    void updateRecent(const QString& path);
    void buildRecent();
    void save();
    void saveAs();
    bool confirmDiscard();
    void browseContent();
    void configureSources();
    void configureCampaign();
    void importPack();
    void overrideDialog(const std::string& target = {});
    void editResources();
    void rollAbilities();
    void rollHitPoints();
    void rollMoney();
    void rollRequestsDialog(const std::string& category = {});
    void upgradeDialog();
    bool adoptMigratedCopy(const MigrationResult& migration);
    bool setFieldValue(const std::string& path, const Json& value, const std::string& scope);
    void actionDialog();
    bool adoptTransition(const TransitionResult& transition, const Json& expectedSource);
    QString lockedChangeReason(const Json& choices, const Json& resources) const;
    void printSheet();
    void recoverUntitled();
    QString untitledRecoveryPath() const;
    void reportError(const QString& title, const QString& detail);
};
}
