#include "mainwindow.hpp"
#include "actiondialog.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTemporaryFile>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <algorithm>
#include <filesystem>

#ifndef DND_DATA_DIR
#define DND_DATA_DIR "data/packs"
#endif

namespace dnd {
namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }
QString esc(const std::string& value) { return q(value).toHtmlEscaped(); }
QString display(const Json& value) { return value.is_string() ? q(value.get<std::string>()) : q(value.dump()); }
Json at(const Json& value, const std::string& path) {
    try { return value.at(Json::json_pointer(path)); } catch (...) { return nullptr; }
}
std::string pointerKey(const std::string& key) {
    std::string escaped;
    for (const char c : key) escaped += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return "/" + escaped;
}
QString fileIdentity(const QString& path) {
    const QFileInfo info(path);
    return info.exists() ? info.canonicalFilePath() : QDir::cleanPath(info.absoluteFilePath());
}
QString sourcesHtml(const std::vector<SourceRef>& sources) {
    QString html;
    for (const auto& source : sources) {
        html += "<li>" + esc(source.publication) + (source.page.empty() ? "" : " — " + esc(source.page));
        if (!source.url.empty()) html += " · <a href=\"" + esc(source.url) + "\">Reference</a>";
        html += "</li>";
    }
    return html.isEmpty() ? QString{} : "<ul>" + html + "</ul>";
}
bool hasErrors(const std::vector<Message>& messages) {
    return std::any_of(messages.begin(), messages.end(), [](const Message& m) { return m.severity == "error"; });
}
QString errorText(const std::vector<Message>& messages) {
    QStringList lines;
    for (const auto& m : messages) lines << q(m.severity + ": " + (m.path.empty() ? std::string{} : "[" + m.path + "] ") + m.text);
    return lines.join('\n');
}
QString packId(const ContentPack& pack) { return q(pack.manifest.value("id", std::string{})); }
QString moduleLabel(const EditionModule& module) {
    QString label = q(module.name);
    if (module.experimental && !label.contains("experimental", Qt::CaseInsensitive)) label += " (experimental)";
    return label;
}
bool supportsModuleVersion(const ContentPack& pack, const std::string& version) {
    if (!pack.manifest.contains("moduleVersions")) return version == "1.0.0";
    const auto& versions = pack.manifest["moduleVersions"];
    return versions.is_array() && std::any_of(versions.begin(), versions.end(), [&version](const Json& item) { return item.is_string() && item.get<std::string>() == version; });
}
int die(int sides) { return QRandomGenerator::global()->bounded(1, sides + 1); }
}

MainWindow::MainWindow(const QString& dataRoot, QWidget* parent, std::function<int(int)> rollDie)
    : QMainWindow(parent), rollDie_(rollDie ? std::move(rollDie) : die) {
    dataRoot_ = dataRoot;
    if (dataRoot_.isEmpty()) {
        const QString bundled = QCoreApplication::applicationDirPath() + "/../Resources/packs";
        const QString adjacent = QCoreApplication::applicationDirPath() + "/packs";
        const QString installed = QCoreApplication::applicationDirPath() + "/../share/dungeoning-a-dragon/packs";
        dataRoot_ = QDir(bundled).exists() ? bundled : QDir(adjacent).exists() ? adjacent : QDir(installed).exists() ? installed : QString::fromUtf8(DND_DATA_DIR);
    }
    userPacks_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/packs";
    loadCatalog();
    buildUi();
    buildMenus();
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setSingleShot(true);
    autosaveTimer_->setInterval(1500);
    connect(autosaveTimer_, &QTimer::timeout, this, [this] {
        if (!dirty_ || readOnlyMode()) return;
        try {
            const auto base = path_.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/untitled.dnd.json" : path_;
            QDir().mkpath(QFileInfo(base).absolutePath());
            writeAutosave(base.toStdString(), document_);
            statusBar()->showMessage("Recovery copy updated", 2500);
        } catch (const std::exception& e) { statusBar()->showMessage("Autosave failed: " + q(e.what())); }
    });
    newDocument("bx");
    resize(1420, 900);
    setMinimumSize(940, 620);
    if (!QCoreApplication::arguments().contains("--smoke") && !QCoreApplication::arguments().contains("--export-pdf"))
        QTimer::singleShot(0, this, [this] { recoverUntitled(); });
}

void MainWindow::loadCatalog() {
    catalogMessages_.clear();
    packs_ = loadPackDirectory(dataRoot_.toStdString(), catalogMessages_);
    if (QDir(userPacks_).exists()) {
        auto user = loadPackDirectory(userPacks_.toStdString(), catalogMessages_);
        for (auto& pack : user) packs_.push_back(std::move(pack));
    }
}

void MainWindow::buildUi() {
    auto* outer = new QWidget(this);
    auto* layout = new QVBoxLayout(outer);
    layout->setContentsMargins(18, 14, 18, 12);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    summary_->setObjectName("documentSummary");
    layout->addWidget(summary_);
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName("builderSplitter");
    splitter->setChildrenCollapsible(false);
    auto* stagesPane = new QWidget;
    auto* stagesLayout = new QVBoxLayout(stagesPane);
    stagesLayout->setContentsMargins(0, 10, 8, 0);
    auto* stagesLabel = new QLabel("BUILD CHARACTER");
    stagesLabel->setStyleSheet("font-weight: 600; font-size: 11px;");
    stagesLayout->addWidget(stagesLabel);
    stages_ = new QListWidget;
    stages_->setObjectName("builderStages");
    stages_->setSpacing(4);
    stages_->setWordWrap(true);
    stages_->setMinimumWidth(195);
    stagesLayout->addWidget(stages_);
    auto* content = new QPushButton("Browse content…");
    connect(content, &QPushButton::clicked, this, [this] { browseContent(); });
    stagesLayout->addWidget(content);
    splitter->addWidget(stagesPane);

    auto* editor = new QWidget;
    auto* editorLayout = new QVBoxLayout(editor);
    editorLayout->setContentsMargins(10, 8, 10, 0);
    stageTitle_ = new QLabel;
    stageTitle_->setStyleSheet("font-size: 20px; font-weight: 600; margin-bottom: 8px;");
    editorLayout->addWidget(stageTitle_);
    fieldsScroll_ = new QScrollArea;
    fieldsScroll_->setWidgetResizable(true);
    fieldsScroll_->setFrameShape(QFrame::NoFrame);
    fieldsScroll_->setMinimumWidth(315);
    fieldsScroll_->setObjectName("fieldsScroll");
    editorLayout->addWidget(fieldsScroll_, 1);
    auto* validationGroup = new QGroupBox("Validation & next steps");
    auto* validationLayout = new QVBoxLayout(validationGroup);
    auto* validationScroll = new QScrollArea;
    validationScroll->setWidgetResizable(true);
    validationScroll->setFrameShape(QFrame::NoFrame);
    validationScroll->setMaximumHeight(180);
    validation_ = new QLabel;
    validation_->setTextFormat(Qt::RichText);
    validation_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    validation_->setWordWrap(true);
    validation_->setObjectName("validationMessages");
    validationScroll->setWidget(validation_);
    validationLayout->addWidget(validationScroll);
    editorLayout->addWidget(validationGroup);
    splitter->addWidget(editor);

    auto* sheetPane = new QWidget;
    auto* sheetLayout = new QVBoxLayout(sheetPane);
    sheetLayout->setContentsMargins(10, 8, 0, 0);
    auto* sheetHeading = new QLabel("Live character sheet");
    sheetHeading->setStyleSheet("font-size: 20px; font-weight: 600; margin-bottom: 8px;");
    sheetLayout->addWidget(sheetHeading);
    sheet_ = new QTextBrowser;
    sheet_->setObjectName("liveSheet");
    // The printable page keeps paper colors while the surrounding controls use system appearance.
    QPalette paper = sheet_->palette(); paper.setColor(QPalette::Base, Qt::white); paper.setColor(QPalette::Text, Qt::black); sheet_->setPalette(paper);
    sheet_->setOpenLinks(false);
    sheet_->setMinimumWidth(320);
    connect(sheet_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        if (url.scheme() == "why" || url.scheme() == "explain") inspect(QUrl::fromPercentEncoding(url.path().toUtf8()).toStdString());
    });
    sheetLayout->addWidget(sheet_);
    auto* resourceButton = new QPushButton("Edit current resources…");
    resourceButton->setObjectName("editResources");
    connect(resourceButton, &QPushButton::clicked, this, [this] { editResources(); });
    sheetLayout->addWidget(resourceButton);
    splitter->addWidget(sheetPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({235, 490, 630});
    layout->addWidget(splitter, 1);
    setCentralWidget(outer);
    connect(stages_, &QListWidget::currentRowChanged, this, [this] { if (!refreshing_) rebuildFields(); });
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu("&File");
    auto* create = file->addMenu("&New character");
    for (const auto& module : editions()) {
        auto* action = create->addAction(moduleLabel(module) + " · module v" + q(module.version));
        action->setObjectName("new:" + q(module.id) + ":" + q(module.version));
        connect(action, &QAction::triggered, this, [this, id = module.id, version = module.version] { if (confirmDiscard()) newDocument(id, version); });
    }
    create->menuAction()->setShortcut(QKeySequence::New);
    auto* open = file->addAction("&Open…", QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
        if (!confirmDiscard()) return;
        const QString filename = QFileDialog::getOpenFileName(this, "Open character", {}, "Character files (*.json *.dnd);;All files (*)");
        if (!filename.isEmpty() && !openPath(filename)) reportError("Could not open character", lastError_);
    });
    recentMenu_ = file->addMenu("Open &recent");
    buildRecent();
    file->addSeparator();
    saveAction_ = file->addAction("&Save", QKeySequence::Save);
    saveAsAction_ = file->addAction("Save &as…", QKeySequence::SaveAs);
    connect(saveAction_, &QAction::triggered, this, [this] { save(); });
    connect(saveAsAction_, &QAction::triggered, this, [this] { saveAs(); });
    upgradeAction_ = file->addAction("Create upgraded copy…");
    upgradeAction_->setObjectName("upgradeCopyAction");
    connect(upgradeAction_, &QAction::triggered, this, [this] { upgradeDialog(); });
    file->addSeparator();
    auto* pdf = file->addAction("Export &PDF…");
    connect(pdf, &QAction::triggered, this, [this] {
        QString filename = QFileDialog::getSaveFileName(this, "Export character sheet", "character-sheet.pdf", "PDF (*.pdf)");
        if (!filename.isEmpty() && !exportPdf(filename)) reportError("PDF export failed", lastError_);
    });
    connect(file->addAction("&Print…", QKeySequence::Print), &QAction::triggered, this, [this] { printSheet(); });
    file->addSeparator();
    connect(file->addAction("Close", QKeySequence::Close), &QAction::triggered, this, &QWidget::close);

    auto* character = menuBar()->addMenu("&Character");
    actionsAction_ = character->addAction("Actions…"); actionsAction_->setObjectName("characterActionsAction");
    connect(actionsAction_, &QAction::triggered, this, [this] { actionDialog(); });
    character->addSeparator();
    connect(character->addAction("Roll ability scores…"), &QAction::triggered, this, [this] { rollAbilities(); });
    connect(character->addAction("Roll hit points…"), &QAction::triggered, this, [this] { rollHitPoints(); });
    connect(character->addAction("Roll B/X starting money…"), &QAction::triggered, this, [this] { rollMoney(); });
    auto* rollInputs = character->addAction("Roll accepted inputs…");
    rollInputs->setObjectName("rollRequestsAction");
    connect(rollInputs, &QAction::triggered, this, [this] { rollRequestsDialog(); });
    character->addSeparator();
    connect(character->addAction("Current resources…"), &QAction::triggered, this, [this] { editResources(); });
    overrideAction_ = character->addAction("DM override…");
    overrideAction_->setObjectName("overrideAction");
    connect(overrideAction_, &QAction::triggered, this, [this] { overrideDialog(); });

    auto* rules = menuBar()->addMenu("&Rules");
    connect(rules->addAction("Campaign settings & presets…"), &QAction::triggered, this, [this] { configureCampaign(); });
    auto* sourcesAction = rules->addAction("Enabled sources…");
    sourcesAction->setObjectName("enabledSourcesAction");
    connect(sourcesAction, &QAction::triggered, this, [this] { configureSources(); });
    connect(rules->addAction("Search content…", QKeySequence("Ctrl+K")), &QAction::triggered, this, [this] { browseContent(); });
    connect(rules->addAction("Import content pack…"), &QAction::triggered, this, [this] { importPack(); });

    auto* view = menuBar()->addMenu("&View");
    advancedAction_ = view->addAction("Advanced mode");
    advancedAction_->setObjectName("advancedAction");
    advancedAction_->setCheckable(true);
    connect(advancedAction_, &QAction::toggled, this, [this](bool value) { setAdvanced(value); });
    connect(view->addAction("Why this value?…"), &QAction::triggered, this, [this] {
        if (!evaluation_.calculations.empty()) inspect(evaluation_.calculations.front().id);
    });
    auto* help = menuBar()->addMenu("&Help");
    connect(help->addAction("About Dungeoning a Dragon"), &QAction::triggered, this, [this] {
        QMessageBox::about(this, "Dungeoning a Dragon", "<h3>Dungeoning a Dragon v" DND_APP_VERSION "</h3><p>Basic, minimum application. Offline C++ character builder.</p><p>Original 1981 B/X, experimental 5E (2014 / SRD 5.1), and 5.5E (2024 / SRD 5.2.1).</p><p>Use the source browser for publication, coverage, and content license details. Application code: BSD-3-Clause.</p><p>Logo and icon adapted with AI assistance from <a href='https://pixabay.com/vectors/dragon-red-symbol-fantasy-isolated-312035/'>Dragon</a> by Clker-Free-Vector-Images, published in 2014 on Pixabay. Source: <a href='https://pixabay.com/service/terms/'>CC0 under Pixabay's pre-2019 terms</a>. Ruby glass artwork with local transparency cleanup; see the packaged artwork notice for provenance.</p>");
    });
}

void MainWindow::newDocument(const std::string& edition, const std::string& moduleVersion) {
    if (autosaveTimer_) autosaveTimer_->stop();
    document_ = newCharacter(edition, moduleVersion);
    original_ = Json{};
    if (document_.id.empty()) document_.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    // The engine supplies exact default pack pins for this module version.
    // Imported supplements and other installed module versions remain explicit choices.
    path_.clear();
    protectedOriginPath_.clear();
    loadMessages_.clear();
    inspectOnly_ = false;
    dirty_ = false;
    refresh();
}

void MainWindow::refresh(bool rebuildEditor) {
    refreshing_ = true;
    ruleset_ = resolveRuleset(document_, packs_);
    evaluation_ = evaluate(document_, ruleset_);
    const int previous = stages_->currentRow();
    const QString selectedId = previous >= 0 ? stages_->item(previous)->data(Qt::UserRole).toString() : QString{};
    stages_->clear();
    auto* identity = new QListWidgetItem("Identity", stages_);
    identity->setData(Qt::UserRole, "__identity");
    identity->setSizeHint(QSize(190, 34));
    for (const auto& stage : evaluation_.stages) {
        auto* item = new QListWidgetItem(q(stage.label), stages_);
        item->setData(Qt::UserRole, q(stage.id));
        item->setSizeHint(QSize(190, 40));
    }
    int selected = 0;
    for (int i = 0; i < stages_->count(); ++i)
        if (stages_->item(i)->data(Qt::UserRole).toString() == selectedId) selected = i;
    stages_->setCurrentRow(selected);
    const auto* module = findEdition(document_.edition, document_.moduleVersion);
    const QString editionName = module ? q(module->name) : q(document_.edition);
    const QString name = document_.name.empty() ? "Untitled character" : q(document_.name);
    setWindowTitle(name + (dirty_ ? " *" : "") + " — Dungeoning a Dragon");
    setWindowFilePath(path_);
    QString state = readOnlyMode() ? "<b>Inspection only.</b> Resolve the file or source errors before editing or saving." :
        (evaluation_.complete() ? "Ready to play" : "Draft — follow the validation notes to finish this character");
    summary_->setText("<b>" + name.toHtmlEscaped() + "</b> · " + editionName.toHtmlEscaped() + " · module v" + esc(document_.moduleVersion) + " · " + state);
    QString messages;
    std::vector<Message> combined = loadMessages_;
    combined.insert(combined.end(), ruleset_.messages.begin(), ruleset_.messages.end());
    combined.insert(combined.end(), evaluation_.messages.begin(), evaluation_.messages.end());
    for (const auto& m : combined)
        messages += "<p><b>" + esc(m.severity) + "</b> · " + esc(m.text) + (m.path.empty() ? "" : " <small>(" + esc(m.path) + ")</small>") + "</p>";
    if (messages.isEmpty()) messages = "No validation issues.";
    validation_->setText(messages);
    QString html = q(renderSheetHtml(document_, evaluation_, ruleset_));
    // Screen reading has a larger type scale than the separate printed document.
    html.replace("font-size:10pt", "font-size:12pt");
    html.replace("font-size:8pt", "font-size:10pt");
    QString why = "<h3>Calculation explanations</h3><p>Inspect the normal calculation, sources, and any DM override.</p><ul>";
    for (const auto& calc : evaluation_.calculations)
        why += "<li><a href=\"why:" + QString::fromUtf8(QUrl::toPercentEncoding(q(calc.id))) + "\">" + esc(calc.label) + ": " + display(calc.effective).toHtmlEscaped() + "</a></li>";
    why += "</ul>";
    const qsizetype bodyEnd = html.lastIndexOf("</body>");
    if (bodyEnd >= 0) html.insert(bodyEnd, why); else html += why;
    sheet_->setHtml(html);
    saveAction_->setEnabled(!readOnlyMode());
    saveAsAction_->setEnabled(!readOnlyMode());
    overrideAction_->setVisible(advanced_);
    overrideAction_->setEnabled(!readOnlyMode());
    actionsAction_->setEnabled(!readOnlyMode() && !evaluation_.actions.empty());
    upgradeAction_->setEnabled(!readOnlyMode() && std::any_of(editions().begin(), editions().end(), [this](const EditionModule& candidate) {
        return candidate.id == document_.edition && QVersionNumber::fromString(q(candidate.version)) > QVersionNumber::fromString(q(document_.moduleVersion));
    }));
    refreshing_ = false;
    if (rebuildEditor) rebuildFields();
}

void MainWindow::rebuildFields() {
    auto* body = new QWidget;
    auto* form = new QFormLayout(body);
    form->setContentsMargins(4, 4, 12, 16);
    form->setSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    const int row = stages_->currentRow();
    if (row <= 0 || row > static_cast<int>(evaluation_.stages.size())) {
        stageTitle_->setText("Character identity");
        auto* name = new QLineEdit(q(document_.name));
        name->setObjectName("characterName");
        name->setPlaceholderText("Name your adventurer");
        connect(name, &QLineEdit::editingFinished, this, [this, name] {
            if (!readOnlyMode() && document_.name != name->text().toStdString()) {
                document_.name = name->text().toStdString(); markChanged(false);
            }
        });
        form->addRow("Name", name);
        const auto* module = findEdition(document_.edition, document_.moduleVersion);
        form->addRow("Rules", new QLabel(module ? q(module->name) : q(document_.edition)));
        form->addRow("Module", new QLabel(q(document_.moduleVersion)));
        auto* note = new QLabel("Select each building stage on the left. The sheet and validation notes update as you choose. Accepted dice results are saved inputs; refreshing the sheet never rolls dice.");
        note->setWordWrap(true);
        form->addRow(note);
        if (module && module->experimental) {
            auto* experimental = new QLabel(document_.edition == "srd55" && document_.moduleVersion == "1.0.0" ?
                "Experimental legacy coverage: fighter and wizard, levels 1–3. This character retains its saved rules module." :
                "Experimental rules module. Consult the publication coverage and validation messages before relying on an option.");
            experimental->setWordWrap(true); form->addRow(experimental);
        }
        if (readOnlyMode()) {
            auto* raw = new QTextBrowser;
            raw->setPlainText(q((original_.is_null() ? toJson(document_) : original_).dump(2)));
            form->addRow("Preserved file data", raw);
        }
    } else {
        const Stage stage = evaluation_.stages[static_cast<std::size_t>(row - 1)];
        stageTitle_->setText(q(stage.label));
        for (const auto& field : stage.fields) {
            if (field.advanced && !advanced_) continue;
            const Json value = field.scope == "choices" && field.path.starts_with("/options/")
                ? at(effectiveCampaignOptions(document_), field.path.substr(8))
                : at(field.scope == "resources" ? document_.resources : document_.choices, field.path);
            QWidget* editor = nullptr;
            if (field.kind == "integer") {
                auto* spin = new QSpinBox;
                spin->setRange(field.minimum - 1, field.maximum);
                spin->setKeyboardTracking(false);
                spin->setValue(value.is_number_integer() ? value.get<int>() : field.minimum - 1);
                spin->setSpecialValueText("Not entered");
                connect(spin, &QSpinBox::valueChanged, this, [this, path = field.path, scope = field.scope, minimum = field.minimum](int n) { setFieldValue(path, n < minimum ? Json(nullptr) : Json(n), scope); });
                editor = spin;
            } else if (field.kind == "boolean") {
                auto* check = new QCheckBox;
                check->setChecked(value.is_boolean() && value.get<bool>());
                connect(check, &QCheckBox::toggled, this, [this, path = field.path, scope = field.scope](bool b) { setFieldValue(path, b, scope); });
                editor = check;
            } else if (field.kind == "select") {
                auto* combo = new QComboBox;
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                combo->setMinimumContentsLength(18);
                combo->addItem(field.scope == "resources" ? "None / inactive" : "Choose…", QVariant{});
                int current = 0;
                for (const auto& choice : field.options) {
                    combo->addItem(q(choice.label) + (choice.available ? "" : " — unavailable"), q(choice.id));
                    const int index = combo->count() - 1;
                    combo->setItemData(index, q(choice.reason), Qt::ToolTipRole);
                    if (value.is_string() && value.get<std::string>() == choice.id) current = index;
                    if (!choice.available) {
                        if (auto* model = qobject_cast<QStandardItemModel*>(combo->model())) model->item(index)->setEnabled(false);
                    }
                }
                if (value.is_string() && current == 0 && !value.get<std::string>().empty()) {
                    combo->addItem(display(value) + " — missing source", display(value)); current = combo->count() - 1;
                }
                combo->setCurrentIndex(current);
                connect(combo, &QComboBox::activated, this, [this, combo, path = field.path, scope = field.scope](int index) {
                    if (index > 0) setFieldValue(path, combo->itemData(index).toString().toStdString(), scope);
                    else if (scope == "resources") setResource(path, "");
                });
                editor = combo;
            } else if (field.kind == "multiselect") {
                auto* list = new QListWidget;
                list->setMinimumHeight(110);
                list->setMaximumHeight(210);
                for (const auto& choice : field.options) {
                    auto* item = new QListWidgetItem(q(choice.label), list);
                    item->setData(Qt::UserRole, q(choice.id));
                    item->setToolTip(q(choice.reason));
                    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                    bool selected = value.is_array() && std::find(value.begin(), value.end(), Json(choice.id)) != value.end();
                    item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
                    if (!choice.available && !selected) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                }
                // Retain selected identifiers whose source has been removed.
                if (value.is_array()) for (const auto& selected : value) if (selected.is_string()) {
                    const auto id = selected.get<std::string>();
                    if (std::none_of(field.options.begin(), field.options.end(), [&id](const Choice& c) { return c.id == id; })) {
                        auto* item = new QListWidgetItem(q(id) + " — missing source", list);
                        item->setData(Qt::UserRole, q(id));
                        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                        item->setCheckState(Qt::Checked);
                    }
                }
                connect(list, &QListWidget::itemChanged, this, [this, list, path = field.path, scope = field.scope](QListWidgetItem*) {
                    Json selected = Json::array();
                    for (int i = 0; i < list->count(); ++i) if (list->item(i)->checkState() == Qt::Checked)
                        selected.push_back(list->item(i)->data(Qt::UserRole).toString().toStdString());
                    setFieldValue(path, selected, scope);
                });
                editor = list;
            } else {
                auto* text = new QLineEdit(value.is_null() ? QString{} : display(value));
                connect(text, &QLineEdit::editingFinished, this, [this, text, path = field.path, scope = field.scope] { setFieldValue(path, text->text().toStdString(), scope); });
                editor = text;
            }
            editor->setObjectName((field.scope == "resources" ? "resources:" : "") + q(field.path));
            editor->setProperty("fieldScope", q(field.scope));
            editor->setToolTip(q(field.help));
            editor->setEnabled(field.editable);
            auto* fieldGroup = new QWidget;
            auto* groupLayout = new QVBoxLayout(fieldGroup);
            groupLayout->setContentsMargins(0, 0, 0, 0);
            groupLayout->setSpacing(4);
            groupLayout->addWidget(editor);
            if (!field.help.empty()) {
                auto* help = new QLabel(q(field.help)); help->setWordWrap(true); help->setStyleSheet("font-size: 11px;");
                groupLayout->addWidget(help);
            }
            if (!field.editable && !field.readOnlyReason.empty()) {
                auto* reason = new QLabel(q(field.readOnlyReason)); reason->setObjectName("readOnlyReason:" + q(field.path)); reason->setWordWrap(true); groupLayout->addWidget(reason);
            }
            if (!field.options.empty()) {
                auto* why = new QPushButton("Why is an option unavailable?");
                why->setFlat(true);
                connect(why, &QPushButton::clicked, this, [this, field] {
                    QString html = "<h2>" + esc(field.label) + "</h2>";
                    for (const auto& c : field.options) html += "<h3>" + esc(c.label) + "</h3><p>" + (c.available ? "Available" : esc(c.reason)) + "</p>" + sourcesHtml(c.sources);
                    QDialog dialog(this); dialog.setWindowTitle("Choice availability"); dialog.resize(600, 500);
                    auto* layout = new QVBoxLayout(&dialog); auto* browser = new QTextBrowser; browser->setHtml(html); browser->setOpenExternalLinks(true); layout->addWidget(browser);
                    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(buttons); dialog.exec();
                });
                groupLayout->addWidget(why);
            }
            form->addRow(q(field.label), fieldGroup);
        }
    }
    if (readOnlyMode()) {
        for (auto* input : body->findChildren<QLineEdit*>()) input->setReadOnly(true);
        for (auto* input : body->findChildren<QSpinBox*>()) input->setEnabled(false);
        for (auto* input : body->findChildren<QComboBox*>()) input->setEnabled(false);
        for (auto* input : body->findChildren<QCheckBox*>()) input->setEnabled(false);
        for (auto* input : body->findChildren<QListWidget*>()) input->setEnabled(false);
    }
    // Deferred destruction avoids deleting the editor whose signal initiated refresh.
    if (auto* previous = fieldsScroll_->takeWidget()) previous->deleteLater();
    fieldsScroll_->setWidget(body);
}

void MainWindow::markChanged(bool rebuildEditor) {
    dirty_ = true;
    refresh(rebuildEditor);
    autosaveTimer_->start();
}

bool MainWindow::setChoice(const std::string& path, const Json& value) {
    if (readOnlyMode()) { lastError_ = "This document is inspection only."; return false; }
    try {
        if (path.empty() || path.front() != '/') throw std::runtime_error("A choice must use a JSON pointer path.");
        const Json previous = at(document_.choices, path);
        if (previous == value) return true;
        Json choices = document_.choices, advancement = document_.advancement;
        if (path == "/level" && previous.is_number_integer() && value.is_number_integer() && previous != value) {
            advancement.push_back({{"fromLevel", previous}, {"toLevel", value}, {"choices", choices}, {"acceptedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()}});
        }
        choices[Json::json_pointer(path)] = value;
        const auto reason = lockedChangeReason(choices, document_.resources);
        if (!reason.isEmpty()) { lastError_ = reason; return false; }
        document_.choices = std::move(choices); document_.advancement = std::move(advancement);
        markChanged();
        return true;
    } catch (const std::exception& e) { lastError_ = q(e.what()); return false; }
}

bool MainWindow::setResource(const std::string& path, const Json& value) {
    if (readOnlyMode()) { lastError_ = "This document is inspection only."; return false; }
    try {
        if (path.empty()) throw std::runtime_error("A resource needs a key or JSON pointer path.");
        const std::string pointer = path.front() == '/' ? path : pointerKey(path);
        if (at(document_.resources, pointer) == value) return true;
        Json resources = document_.resources;
        resources[Json::json_pointer(pointer)] = value;
        const auto reason = lockedChangeReason(document_.choices, resources);
        if (!reason.isEmpty()) { lastError_ = reason; return false; }
        document_.resources = std::move(resources);
        markChanged(); return true;
    } catch (const std::exception& e) { lastError_ = q(e.what()); return false; }
}

bool MainWindow::setFieldValue(const std::string& path, const Json& value, const std::string& scope) {
    if (scope == "resources") return setResource(path, value);
    if (scope == "choices") return setChoice(path, value);
    lastError_ = "The rules module requested an unsupported field scope."; return false;
}

QString MainWindow::lockedChangeReason(const Json& choices, const Json& resources) const {
    for (const auto& stage : evaluation_.stages) for (const auto& field : stage.fields) if (!field.editable) {
        const Json& before = field.scope == "resources" ? document_.resources : document_.choices;
        const Json& after = field.scope == "resources" ? resources : choices;
        if (at(before, field.path) != at(after, field.path))
            return q(field.readOnlyReason.empty() ? field.label + " is managed through character actions." : field.readOnlyReason);
    }
    return {};
}

bool MainWindow::adoptTransition(const TransitionResult& transition, const Json& expectedSource) {
    if (readOnlyMode()) { lastError_ = "This document is inspection only."; return false; }
    if (toJson(document_) != expectedSource) { lastError_ = "The character changed after this preview. Close Actions and reopen it to review the current state."; return false; }
    if (!transition.valid()) { lastError_ = errorText(transition.messages); if (lastError_.isEmpty()) lastError_ = "The action did not produce a valid transition."; return false; }
    if (transition.document.id != document_.id || transition.document.edition != document_.edition || transition.document.moduleVersion != document_.moduleVersion) {
        lastError_ = "A lifecycle action cannot replace the character identity or rules module. Use the explicit version-copy workflow."; return false;
    }
    document_ = transition.document;
    lastError_.clear();
    markChanged();
    statusBar()->showMessage("Action applied and recorded in character history.", 5000);
    return true;
}

bool MainWindow::executeAction(const std::string& id, const Json& inputs) {
    if (readOnlyMode()) { lastError_ = "This document is inspection only."; return false; }
    try {
        const auto expected = toJson(document_);
        return adoptTransition(executeCommand(document_, ruleset_, {id, inputs}), expected);
    } catch (const std::exception& error) { lastError_ = q(error.what()); return false; }
}

void MainWindow::actionDialog() {
    if (readOnlyMode()) return;
    ActionDialog dialog(document_, ruleset_, evaluation_, [this](const TransitionResult& result, const Json& expected) {
        return adoptTransition(result, expected) ? QString{} : lastError_;
    }, this);
    dialog.exec();
}

void MainWindow::setAdvanced(bool enabled) {
    advanced_ = enabled;
    if (advancedAction_->isChecked() != enabled) { QSignalBlocker block(advancedAction_); advancedAction_->setChecked(enabled); }
    overrideAction_->setVisible(enabled);
    rebuildFields();
}

void MainWindow::reportError(const QString& title, const QString& detail) {
    lastError_ = detail; QMessageBox::warning(this, title, detail);
}

bool MainWindow::openPath(const QString& path, bool offerRecovery) {
    try {
        const auto loaded = loadCharacter(path.toStdString());
        auto doc = loaded.document;
        auto messages = loaded.messages;
        bool recovered = false;
        const QString recovery = q(autosavePath(path.toStdString()).string());
        if (offerRecovery && !loaded.inspectOnly && QFileInfo::exists(recovery) && QFileInfo(recovery).lastModified() > QFileInfo(path).lastModified()) {
            if (QMessageBox::question(this, "Recover changes?", "A newer recovery copy exists for this character. Restore those changes?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
                const auto backup = loadCharacter(recovery.toStdString());
                if (!backup.inspectOnly && !hasErrors(backup.messages)) { doc = backup.document; recovered = true; }
            }
        }
        autosaveTimer_->stop();
        document_ = std::move(doc);
        original_ = loaded.original;
        path_ = QFileInfo(path).absoluteFilePath();
        protectedOriginPath_.clear();
        inspectOnly_ = loaded.inspectOnly;
        loadMessages_ = std::move(messages);
        dirty_ = recovered;
        lastError_.clear();
        refresh();
        updateRecent(path_);
        return true;
    } catch (const std::exception& e) { lastError_ = q(e.what()); return false; }
}

bool MainWindow::saveTo(const QString& path) {
    if (readOnlyMode()) { lastError_ = "Inspection-only documents cannot be saved. The original file is preserved."; return false; }
    if (!protectedOriginPath_.isEmpty() && fileIdentity(path) == protectedOriginPath_) {
        lastError_ = "Save the upgraded copy with a new filename. Its original character file is protected."; return false;
    }
    try {
        saveCharacter(path.toStdString(), document_);
        const QString previousRecovery = path_.isEmpty() ? untitledRecoveryPath() : q(autosavePath(path_.toStdString()).string());
        QFile::remove(previousRecovery);
        path_ = QFileInfo(path).absoluteFilePath();
        QFile::remove(q(autosavePath(path_.toStdString()).string()));
        dirty_ = false;
        autosaveTimer_->stop();
        lastError_.clear();
        updateRecent(path_);
        refresh(false);
        statusBar()->showMessage("Saved " + path_, 4000);
        return true;
    } catch (const std::exception& e) { lastError_ = q(e.what()); return false; }
}

void MainWindow::save() { if (path_.isEmpty()) saveAs(); else if (!saveTo(path_)) reportError("Save failed", lastError_); }
void MainWindow::saveAs() {
    const QString path = QFileDialog::getSaveFileName(this, "Save character", path_.isEmpty() ? "character.dnd.json" : path_, "Character files (*.dnd.json *.json)");
    if (!path.isEmpty() && !saveTo(path)) reportError("Save failed", lastError_);
}
bool MainWindow::confirmDiscard() {
    if (!dirty_) return true;
    const auto reply = QMessageBox::warning(this, "Unsaved character", "Save changes to this character before continuing?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (reply == QMessageBox::Cancel) return false;
    if (reply == QMessageBox::Save) { save(); return !dirty_; }
    autosaveTimer_->stop();
    QFile::remove(path_.isEmpty() ? untitledRecoveryPath() : q(autosavePath(path_.toStdString()).string()));
    return true;
}
void MainWindow::closeEvent(QCloseEvent* event) { if (confirmDiscard()) event->accept(); else event->ignore(); }

void MainWindow::updateRecent(const QString& path) {
    QSettings settings;
    auto recent = settings.value("recentFiles").toStringList(); recent.removeAll(path); recent.prepend(path);
    while (recent.size() > 12) recent.removeLast(); settings.setValue("recentFiles", recent); buildRecent();
}
void MainWindow::buildRecent() {
    recentMenu_->clear();
    for (const auto& path : QSettings().value("recentFiles").toStringList()) {
        auto* action = recentMenu_->addAction(QFileInfo(path).fileName()); action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] { if (confirmDiscard() && !openPath(path)) reportError("Open failed", lastError_); });
    }
    if (recentMenu_->isEmpty()) recentMenu_->addAction("No recent characters")->setEnabled(false);
}

QString MainWindow::untitledRecoveryPath() const { return q(autosavePath((QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/untitled.dnd.json").toStdString()).string()); }
void MainWindow::recoverUntitled() {
    const QString path = untitledRecoveryPath();
    if (!path_.isEmpty() || !QFileInfo::exists(path)) return;
    if (QMessageBox::question(this, "Recover unsaved character?", "An unsaved character has a recovery copy. Open it now?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        if (openPath(path, false)) { path_.clear(); dirty_ = true; refresh(); }
    }
}

bool MainWindow::exportPdf(const QString& path) {
    if (readOnlyMode()) { lastError_ = "Resolve the ruleset errors before exporting a calculated sheet."; return false; }
    QTemporaryFile temporary(QFileInfo(path).absolutePath() + "/.dnd-sheet-XXXXXX.pdf");
    if (!temporary.open()) { lastError_ = "Cannot create a PDF in the selected directory: " + temporary.errorString(); return false; }
    const QString temporaryPath = temporary.fileName();
    temporary.close();
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(temporaryPath);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(14, 14, 14, 14), QPageLayout::Millimeter);
    QTextDocument sheet;
    sheet.setHtml(q(renderSheetHtml(document_, evaluation_, ruleset_)));
    sheet.print(&printer);
    QFile generated(temporaryPath);
    if (!generated.open(QIODevice::ReadOnly) || generated.peek(5) != "%PDF-" || printer.printerState() == QPrinter::Error) {
        lastError_ = "The PDF renderer could not produce a valid output file."; return false;
    }
    const QByteArray bytes = generated.readAll();
    QSaveFile target(path);
    if (!target.open(QIODevice::WriteOnly) || target.write(bytes) != bytes.size() || !target.commit()) {
        lastError_ = "Could not save the PDF: " + target.errorString(); return false;
    }
    lastError_.clear(); return true;
}
void MainWindow::printSheet() {
    if (readOnlyMode()) { reportError("Cannot print", "Resolve the ruleset errors before printing a calculated sheet."); return; }
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() == QDialog::Accepted) { QTextDocument sheet; sheet.setHtml(q(renderSheetHtml(document_, evaluation_, ruleset_))); sheet.print(&printer); }
}

bool MainWindow::adoptMigratedCopy(const MigrationResult& migration) {
    if (!migration.valid()) { lastError_ = errorText(migration.messages); return false; }
    const auto candidateRules = resolveRuleset(migration.document, packs_);
    if (!candidateRules.valid()) { lastError_ = errorText(candidateRules.messages); return false; }
    autosaveTimer_->stop();
    protectedOriginPath_ = path_.isEmpty() ? protectedOriginPath_ : fileIdentity(path_);
    document_ = migration.document;
    original_ = Json{};
    path_.clear();
    inspectOnly_ = false;
    loadMessages_ = migration.messages;
    dirty_ = true;
    refresh();
    autosaveTimer_->start();
    statusBar()->showMessage("Upgraded copy created. Review its validation notes and save with a new filename.", 8000);
    return true;
}

bool MainWindow::upgradeToCopy(const std::string& moduleVersion) {
    if (readOnlyMode()) { lastError_ = "Resolve the current document's ruleset before creating an upgraded copy."; return false; }
    return adoptMigratedCopy(migrateCharacterVersion(document_, moduleVersion));
}

void MainWindow::upgradeDialog() {
    if (readOnlyMode()) return;
    QDialog dialog(this); dialog.setObjectName("upgradeCopyDialog"); dialog.setWindowTitle("Review upgraded character copy"); dialog.resize(800, 650);
    auto* layout = new QVBoxLayout(&dialog);
    auto* explanation = new QLabel("The original character keeps its saved rules module and file. Review the proposed copy below; accepting creates an unsaved copy that must use a new filename. Validation may identify new choices to complete.");
    explanation->setWordWrap(true); layout->addWidget(explanation);
    auto* versions = new QComboBox;
    for (const auto& candidate : editions())
        if (candidate.id == document_.edition && QVersionNumber::fromString(q(candidate.version)) > QVersionNumber::fromString(q(document_.moduleVersion)))
            versions->addItem(moduleLabel(candidate) + " · module v" + q(candidate.version), q(candidate.version));
    layout->addWidget(versions);
    auto* messages = new QLabel; messages->setWordWrap(true); layout->addWidget(messages);
    auto* tabs = new QTabWidget; auto* sheet = new QTextBrowser; auto* inputs = new QTextBrowser;
    tabs->addTab(sheet, "Proposed sheet"); tabs->addTab(inputs, "Preserved and migrated inputs"); layout->addWidget(tabs, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto* create = buttons->addButton("Create unsaved copy", QDialogButtonBox::AcceptRole); layout->addWidget(buttons);
    MigrationResult proposal;
    auto review = [this, versions, sheet, inputs, messages, create, &proposal] {
        proposal = migrateCharacterVersion(document_, versions->currentData().toString().toStdString());
        const auto rules = resolveRuleset(proposal.document, packs_);
        create->setEnabled(proposal.valid() && rules.valid());
        messages->setText(errorText(proposal.messages) + (rules.valid() ? QString{} : "\n" + errorText(rules.messages)));
        sheet->setHtml(q(renderSheetHtml(proposal.document, evaluate(proposal.document, rules), rules)));
        inputs->setPlainText(q(toJson(proposal.document).dump(2)));
    };
    connect(versions, &QComboBox::currentIndexChanged, &dialog, review);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { if (adoptMigratedCopy(proposal)) dialog.accept(); else messages->setText(lastError_); });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (versions->count() == 0) { reportError("No upgrade available", "No newer rules module is installed for this edition."); return; }
    review(); dialog.exec();
}

QString MainWindow::inspectionHtml(const std::string& target) const {
    const auto* calc = evaluation_.find(target);
    if (!calc) return "<p>This calculation is unavailable.</p>";
    QString html = "<h2>" + esc(calc->label) + "</h2><p><b>Normal:</b> " + display(calc->normal).toHtmlEscaped() + "<br><b>Effective:</b> " + display(calc->effective).toHtmlEscaped() + "</p><ol>";
    for (const auto& step : calc->steps) html += "<li>" + esc(step) + "</li>";
    html += "</ol>";
    if (!calc->overrideReason.empty()) html += "<p><b>DM override reason:</b> " + esc(calc->overrideReason) + "</p>";
    return html + "<h3>Sources</h3>" + sourcesHtml(calc->sources);
}
void MainWindow::inspect(const std::string& target) {
    QDialog dialog(this); dialog.setObjectName("calculationInspector"); dialog.setWindowTitle("Why this value?"); dialog.resize(600, 560);
    auto* layout = new QVBoxLayout(&dialog); auto* picker = new QComboBox;
    for (const auto& c : evaluation_.calculations) picker->addItem(q(c.label), q(c.id));
    picker->setCurrentIndex(picker->findData(q(target))); layout->addWidget(picker);
    auto* browser = new QTextBrowser; browser->setObjectName("explanationText"); browser->setOpenExternalLinks(true); layout->addWidget(browser);
    connect(picker, &QComboBox::currentIndexChanged, &dialog, [this, picker, browser] { browser->setHtml(inspectionHtml(picker->currentData().toString().toStdString())); });
    browser->setHtml(inspectionHtml(target));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    if (advanced_ && !readOnlyMode()) {
        auto* override = buttons->addButton("DM override…", QDialogButtonBox::ActionRole);
        connect(override, &QPushButton::clicked, &dialog, [this, picker, browser] { const auto target = picker->currentData().toString().toStdString(); overrideDialog(target); browser->setHtml(inspectionHtml(target)); });
    }
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(buttons); dialog.exec();
}

bool MainWindow::applyOverride(const std::string& target, const Json& value, const QString& reason) {
    if (readOnlyMode()) { lastError_ = "Ruleset errors must be resolved before applying overrides."; return false; }
    if (reason.trimmed().isEmpty()) { lastError_ = "A DM override requires a reason."; return false; }
    const bool choiceTarget = target.starts_with("choice:");
    if (choiceTarget) {
        const std::string path = target.substr(7);
        const Field* selected = nullptr;
        for (const auto& stage : evaluation_.stages) for (const auto& field : stage.fields) if (field.path == path && field.scope == "choices") selected = &field;
        if (selected && !selected->editable) { lastError_ = q(selected->readOnlyReason.empty() ? "This choice is managed through character actions." : selected->readOnlyReason); return false; }
        if (!selected || !value.is_string() || std::none_of(selected->options.begin(), selected->options.end(), [&value](const Choice& c) { return value.get<std::string>() == c.id; })) {
            lastError_ = "Choose an existing supported option. An override cannot supply missing rules."; return false;
        }
        document_.choices[Json::json_pointer(path)] = value;
    } else {
        const auto* calc = evaluation_.find(target);
        if (!calc) { lastError_ = "This calculation does not support an override."; return false; }
        if (calc->normal.type() != value.type() && !(calc->normal.is_number() && value.is_number())) { lastError_ = "The override must use the same value type as the normal calculation."; return false; }
    }
    Json retained = Json::array();
    for (const auto& existing : document_.overrides) if (existing.value("target", std::string{}) != target) retained.push_back(existing);
    retained.push_back({{"target", target}, {"value", value}, {"reason", reason.trimmed().toStdString()}});
    document_.overrides = std::move(retained);
    markChanged(); return true;
}

void MainWindow::overrideDialog(const std::string& target) {
    if (readOnlyMode()) return;
    QDialog dialog(this); dialog.setWindowTitle("DM override"); dialog.resize(570, 360);
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    auto* targets = new QComboBox;
    for (const auto& c : evaluation_.calculations) targets->addItem(q(c.label), q(c.id));
    for (const auto& stage : evaluation_.stages) for (const auto& field : stage.fields)
        if (field.kind == "select" && field.scope == "choices" && field.editable) targets->addItem("Choice: " + q(field.label), "choice:" + q(field.path));
    if (!target.empty()) targets->setCurrentIndex(targets->findData(q(target)));
    auto* value = new QLineEdit; auto* choices = new QComboBox; auto* normal = new QLabel; normal->setWordWrap(true);
    auto update = [this, targets, value, choices, normal] {
        const std::string target = targets->currentData().toString().toStdString();
        const bool choice = target.starts_with("choice:"); value->setVisible(!choice); choices->setVisible(choice);
        if (choice) {
            choices->clear();
            for (const auto& stage : evaluation_.stages) for (const auto& field : stage.fields) if (field.path == target.substr(7) && field.scope == "choices")
                for (const auto& c : field.options) { choices->addItem(q(c.label), q(c.id)); choices->setItemData(choices->count() - 1, q(c.reason), Qt::ToolTipRole); }
            normal->setText("Select an existing option. The reason records the eligibility exception; source and missing-rule errors remain blocking.");
        } else if (const auto* calc = evaluation_.find(target)) { value->setText(display(calc->effective)); normal->setText("Normal calculation: " + display(calc->normal)); }
    };
    connect(targets, &QComboBox::currentIndexChanged, &dialog, update); update();
    auto* reason = new QLineEdit; reason->setPlaceholderText("Required: explain the DM ruling");
    form->addRow("Target", targets); form->addRow(normal); form->addRow("Value", value); form->addRow("Choice", choices); form->addRow("Reason", reason); layout->addLayout(form);
    auto* error = new QLabel; error->setWordWrap(true); layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    auto* remove = buttons->addButton("Remove override", QDialogButtonBox::DestructiveRole);
    connect(remove, &QPushButton::clicked, &dialog, [this, targets, &dialog] {
        const auto selected = targets->currentData().toString().toStdString(); Json retained = Json::array();
        for (const auto& item : document_.overrides) if (item.value("target", std::string{}) != selected) retained.push_back(item);
        document_.overrides = retained; markChanged(); dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, targets, value, choices, reason, error, &dialog] {
        const std::string selected = targets->currentData().toString().toStdString();
        Json result;
        if (selected.starts_with("choice:")) result = choices->currentData().toString().toStdString();
        else if (const auto* calc = evaluation_.find(selected)) {
            if (calc->normal.is_string()) result = value->text().toStdString();
            else { result = Json::parse(value->text().toStdString(), nullptr, false); if (result.is_discarded()) { error->setText("Enter a valid number, boolean, or JSON value."); return; } }
        }
        if (applyOverride(selected, result, reason->text())) dialog.accept(); else error->setText(lastError_);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(buttons); dialog.exec();
}

void MainWindow::configureSources() {
    if (inspectOnly_) { reportError("Inspection only", "This save version cannot be edited by this application."); return; }
    QDialog dialog(this); dialog.setObjectName("enabledSourcesDialog"); dialog.setWindowTitle("Enabled content sources · module v" + q(document_.moduleVersion)); dialog.resize(650, 450);
    auto* layout = new QVBoxLayout(&dialog);
    auto* note = new QLabel("Source versions are saved with the character. Disabling a source preserves its selections and explains what becomes unavailable."); note->setWordWrap(true); layout->addWidget(note);
    auto* list = new QListWidget; list->setObjectName("enabledSourcesList"); layout->addWidget(list);
    for (const auto& pack : packs_) {
        if (pack.manifest.value("edition", std::string{}) != document_.edition || !supportsModuleVersion(pack, document_.moduleVersion)) continue;
        const auto id = pack.manifest.value("id", std::string{}); const auto version = pack.manifest.value("version", std::string{});
        auto* item = new QListWidgetItem(q(pack.manifest.value("name", id)) + " · " + q(version) + "\n" + q(pack.manifest.value("publisher", std::string{})), list);
        item->setData(Qt::UserRole, q(id)); item->setData(Qt::UserRole + 1, q(version)); item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool enabled = std::any_of(document_.packs.begin(), document_.packs.end(), [&id, &version](const PackPin& p) { return p.id == id && p.version == version; });
        item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    }
    for (const auto& pin : document_.packs) if (std::none_of(packs_.begin(), packs_.end(), [this, &pin](const ContentPack& p) { return p.manifest.value("id", std::string{}) == pin.id && p.manifest.value("version", std::string{}) == pin.version && p.manifest.value("edition", std::string{}) == document_.edition && supportsModuleVersion(p, document_.moduleVersion); })) {
        auto* item = new QListWidgetItem(q(pin.id + " · " + pin.version) + " — missing or incompatible", list); item->setData(Qt::UserRole, q(pin.id)); item->setData(Qt::UserRole + 1, q(pin.version)); item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(Qt::Checked);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        document_.packs.clear();
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->checkState() == Qt::Checked) document_.packs.push_back({list->item(i)->data(Qt::UserRole).toString().toStdString(), list->item(i)->data(Qt::UserRole + 1).toString().toStdString()});
        markChanged();
    }
}

void MainWindow::configureCampaign() {
    if (readOnlyMode()) return;
    QDialog dialog(this); dialog.setObjectName("campaignSettingsDialog"); dialog.setWindowTitle("Campaign settings"); dialog.resize(530, 350);
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    auto* preset = new QComboBox; preset->addItems({"Original defaults", "Published B/X optional rules", "Custom campaign"}); preset->setCurrentIndex(2);
    if (document_.edition != "bx") if (auto* model = qobject_cast<QStandardItemModel*>(preset->model())) model->item(1)->setEnabled(false);
    auto* name = new QLineEdit(q(document_.campaign.value("name", std::string{})));
    form->addRow("Campaign name", name); form->addRow("Preset", preset);
    const Json options = effectiveCampaignOptions(document_);
    auto enabled = [&](const std::string& key) { return options.value(key, Json(false)) == Json(true); };
    auto* variable = new QCheckBox("Variable weapon damage"); variable->setChecked(enabled("variableWeaponDamage"));
    auto* initiative = new QCheckBox("Individual initiative"); initiative->setChecked(enabled("individualInitiative"));
    auto* encumbrance = new QComboBox; encumbrance->addItems({"basic", "detailed"}); encumbrance->setCurrentText(q(stringChoice(options,"encumbrance","basic")));
    auto* reroll = new QCheckBox("Reroll low first-level hit points"); reroll->setObjectName("campaignRerollLowFirstHp"); reroll->setChecked(enabled("rerollLowFirstHp"));
    auto* expert = new QCheckBox("Expert weapon rules (two-handed weapons and crossbows)"); expert->setChecked(enabled("expertWeaponRules"));
    auto* optional = new QGroupBox("B/X optional rules"); auto* optionalLayout = new QFormLayout(optional);
    optionalLayout->addRow(variable); optionalLayout->addRow(initiative); optionalLayout->addRow("Encumbrance", encumbrance); optionalLayout->addRow(reroll); optionalLayout->addRow(expert);
    optional->setVisible(advanced_ && document_.edition == "bx");
    connect(preset, &QComboBox::activated, &dialog, [variable, initiative, encumbrance, reroll, expert](int index) {
        if (index == 0 || index == 1) { variable->setChecked(index == 1); initiative->setChecked(index == 1); encumbrance->setCurrentText(index == 1 ? "detailed" : "basic"); reroll->setChecked(index == 1); expert->setChecked(index == 1); }
    });
    layout->addLayout(form); layout->addWidget(optional);
    if (!advanced_) { auto* note = new QLabel("Advanced mode reveals optional settings. Existing settings are preserved when Advanced mode is off."); note->setWordWrap(true); layout->addWidget(note); }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        document_.campaign["name"] = name->text().toStdString();
        if (document_.edition == "bx" && (advanced_ || preset->currentIndex() != 2)) {
            document_.campaign["variableWeaponDamage"] = variable->isChecked(); document_.campaign["individualInitiative"] = initiative->isChecked(); document_.campaign["encumbrance"] = encumbrance->currentText().toStdString(); document_.campaign["rerollLowFirstHp"] = reroll->isChecked(); document_.campaign["expertWeaponRules"] = expert->isChecked();
            document_.choices["options"]["variableWeaponDamage"] = variable->isChecked(); document_.choices["options"]["individualInitiative"] = initiative->isChecked(); document_.choices["options"]["encumbrance"] = encumbrance->currentText().toStdString(); document_.choices["options"]["rerollLowFirstHp"] = reroll->isChecked(); document_.choices["options"]["expertWeaponRules"] = expert->isChecked();
        }
        markChanged();
    }
}

void MainWindow::browseContent() {
    QDialog dialog(this); dialog.setWindowTitle("Content & publication browser"); dialog.resize(950, 680);
    auto* layout = new QVBoxLayout(&dialog); auto* search = new QLineEdit; search->setPlaceholderText("Search names, identifiers, publications, publishers, or kinds…"); search->setObjectName("contentSearch"); layout->addWidget(search);
    auto* table = new QTableWidget; table->setColumnCount(5); table->setHorizontalHeaderLabels({"Name", "Kind", "Publication", "Pack", "Status"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection); table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch); layout->addWidget(table, 2);
    auto* details = new QTextBrowser; details->setOpenExternalLinks(true); layout->addWidget(details, 1);
    for (const auto& pack : packs_) for (const auto& entry : pack.entries) {
        const auto source = sourceFromJson(entry.value("source", Json::object()));
        const auto id = entry.value("id", std::string{}); const int row = table->rowCount(); table->insertRow(row);
        QString detail = "<h3>" + esc(entry.value("name", id)) + "</h3><p>Identifier: " + esc(id) + "</p>" + sourcesHtml({source});
        detail += "<p><b>Publisher:</b> " + esc(pack.manifest.value("publisher", std::string{})) + "<br><b>Origin:</b> " + esc(pack.manifest.value("origin", std::string{})) + "<br><b>License:</b> " + display(pack.manifest.value("license", Json("Unspecified"))).toHtmlEscaped() + "</p>";
        detail += "<pre>" + q(entry.dump(2)).toHtmlEscaped() + "</pre>";
        auto* name = new QTableWidgetItem(q(entry.value("name", id))); name->setData(Qt::UserRole, detail); name->setData(Qt::UserRole + 1, q(entry.dump() + pack.manifest.dump())); table->setItem(row, 0, name);
        const auto packVersion = pack.manifest.value("version", std::string{});
        const bool enabled = std::any_of(document_.packs.begin(), document_.packs.end(), [&pack, &packVersion](const PackPin& pin) { return pin.id == pack.manifest.value("id", std::string{}) && pin.version == packVersion; });
        const bool compatible = pack.manifest.value("edition", std::string{}) == document_.edition && supportsModuleVersion(pack, document_.moduleVersion);
        table->setItem(row, 1, new QTableWidgetItem(q(entry.value("kind", std::string{})))); table->setItem(row, 2, new QTableWidgetItem(q(source.publication))); table->setItem(row, 3, new QTableWidgetItem(packId(pack) + " · " + q(packVersion))); table->setItem(row, 4, new QTableWidgetItem(enabled && compatible ? "Enabled" : compatible ? "Not enabled" : "Other rules module"));
    }
    connect(search, &QLineEdit::textChanged, &dialog, [table](const QString& query) { for (int row = 0; row < table->rowCount(); ++row) table->setRowHidden(row, !table->item(row, 0)->data(Qt::UserRole + 1).toString().contains(query, Qt::CaseInsensitive)); });
    connect(table, &QTableWidget::currentCellChanged, &dialog, [table, details](int row) { if (row >= 0) details->setHtml(table->item(row, 0)->data(Qt::UserRole).toString()); });
    if (!catalogMessages_.empty()) { auto* messages = new QLabel(errorText(catalogMessages_)); messages->setWordWrap(true); layout->addWidget(messages); }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); layout->addWidget(buttons); dialog.exec();
}
void MainWindow::importPack() {
    const QString folder = QFileDialog::getExistingDirectory(this, "Choose content pack folder (manifest.json)");
    if (folder.isEmpty()) return;
    const auto messages = installPack(folder.toStdString(), userPacks_.toStdString(), packs_);
    if (hasErrors(messages)) { reportError("Content pack was not installed", errorText(messages)); return; }
    loadCatalog(); refresh(); QMessageBox::information(this, "Content pack imported", "The validated pack is installed. Enable it for this character in Rules → Enabled sources.");
}

void MainWindow::editResources() {
    if (readOnlyMode()) return;
    QDialog dialog(this); dialog.setObjectName("currentResourcesDialog"); dialog.setWindowTitle("Current resources"); dialog.resize(570, 620);
    auto* layout = new QVBoxLayout(&dialog);
    auto* note = new QLabel("Track remaining uses and current hit points here. Capacities and recharge rules come from this character's rules module. Values change only when you edit them; reevaluation never spends or refills a resource.");
    note->setWordWrap(true); layout->addWidget(note);
    auto* scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); layout->addWidget(scroll, 1);
    auto* body = new QWidget; auto* form = new QFormLayout(body); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); scroll->setWidget(body);
    auto definitions = evaluation_.resources;
    if (definitions.empty()) {
        definitions.push_back({"hp", "Current hit points", 99999, "Tracked separately from advancement inputs", {}});
        definitions.push_back({"gp", "Current gold pieces", 999999, "Current carried total", {}});
    }
    std::vector<std::pair<ResourceDefinition, QSpinBox*>> editors;
    for (const auto& definition : definitions) {
        auto* group = new QWidget; auto* rows = new QVBoxLayout(group); rows->setContentsMargins(0, 0, 0, 0);
        auto* value = new QSpinBox; value->setObjectName("resource:" + q(definition.id)); value->setKeyboardTracking(false);
        const Json current = document_.resources.value(definition.id, Json{});
        const int initial = current.is_number_integer() ? current.get<int>() : -1;
        value->setRange(-1, std::max(std::max(0, definition.maximum), initial));
        value->setSpecialValueText("Not tracked"); value->setValue(initial); value->setProperty("edited", false);
        Json hypothetical = document_.resources; hypothetical[definition.id] = initial == 0 ? 1 : 0;
        const QString readOnlyReason = lockedChangeReason(document_.choices, hypothetical);
        value->setEnabled(readOnlyReason.isEmpty());
        connect(value, &QSpinBox::valueChanged, &dialog, [value] { value->setProperty("edited", true); });
        rows->addWidget(value);
        auto* capacity = new QLabel("Maximum " + QString::number(definition.maximum) + (definition.recharge.empty() ? QString{} : " · " + q(definition.recharge)));
        capacity->setWordWrap(true); capacity->setStyleSheet("font-size: 11px;"); capacity->setToolTip(sourcesHtml(definition.sources)); rows->addWidget(capacity);
        if (!readOnlyReason.isEmpty()) { auto* reason = new QLabel(readOnlyReason); reason->setWordWrap(true); rows->addWidget(reason); }
        if (initial > definition.maximum) { auto* warning = new QLabel("The saved value exceeds the current capacity. It is retained until you edit it."); warning->setWordWrap(true); rows->addWidget(warning); }
        form->addRow(q(definition.label), group); editors.push_back({definition, value});
    }
    const Json oldNotes = document_.resources.value("notes", Json{});
    auto* notes = new QLineEdit(oldNotes.is_string() ? q(oldNotes.get<std::string>()) : QString{}); notes->setObjectName("resourceNotes");
    form->addRow("Session notes", notes);
    auto* error = new QLabel; error->setWordWrap(true); layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        Json proposed = document_.resources;
        for (const auto& [definition, spin] : editors) if (spin->property("edited").toBool()) {
            if (spin->value() > definition.maximum) { error->setText(q(definition.label) + " cannot exceed its current capacity of " + QString::number(definition.maximum) + "."); return; }
            if (spin->value() < 0) proposed.erase(definition.id); else proposed[definition.id] = spin->value();
        }
        if (notes->isModified()) proposed["notes"] = notes->text().toStdString();
        const auto reason = lockedChangeReason(document_.choices, proposed);
        if (!reason.isEmpty()) { error->setText(reason); return; }
        if (proposed != document_.resources) { document_.resources = std::move(proposed); markChanged(); }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.exec();
}

bool MainWindow::acceptRoll(const std::string& requestId, bool replaceExisting) {
    if (readOnlyMode()) { lastError_ = "This document is inspection only."; return false; }
    const auto request = std::find_if(evaluation_.rollRequests.begin(), evaluation_.rollRequests.end(), [&requestId](const RollRequest& candidate) { return candidate.id == requestId; });
    if (request == evaluation_.rollRequests.end()) { lastError_ = "This rules module has no current roll request with that identifier."; return false; }
    if (request->path.empty() || request->path.front() != '/' || request->count < 1 || request->count > 100 || request->sides < 1 || request->sides > 1000000 || request->dropLowest < 0 || request->dropLowest >= request->count) {
        lastError_ = "The rules module supplied an invalid dice request."; return false;
    }
    if (!replaceExisting && !at(document_.choices, request->path).is_null()) {
        lastError_ = "An accepted input already exists. Select it explicitly in the dice dialog to replace it."; return false;
    }
    try {
        std::vector<int> dice;
        for (int i = 0; i < request->count; ++i) dice.push_back(rollDie_(request->sides));
        auto kept = dice; std::sort(kept.begin(), kept.end()); kept.erase(kept.begin(), kept.begin() + request->dropLowest);
        int total = 0; for (const int value : kept) total += value;
        Json choices = document_.choices, rolls = document_.rolls;
        choices[Json::json_pointer(request->path)] = total;
        const Json record = {{"id", request->id}, {"label", request->label}, {"path", request->path}, {"category", request->category},
            {"sides", request->sides}, {"count", request->count}, {"dropLowest", request->dropLowest}, {"dice", dice}, {"kept", kept}, {"total", total},
            {"acceptedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()}};
        if (rolls.contains("requests") && rolls["requests"].contains(request->id)) rolls["history"].push_back(rolls["requests"][request->id]);
        rolls["requests"][request->id] = record;
        const auto reason = lockedChangeReason(choices, document_.resources);
        if (!reason.isEmpty()) { lastError_ = reason; return false; }
        document_.choices = std::move(choices); document_.rolls = std::move(rolls);
        markChanged(); return true;
    } catch (const std::exception& e) { lastError_ = q(e.what()); return false; }
}

void MainWindow::rollRequestsDialog(const std::string& category) {
    if (readOnlyMode()) return;
    QDialog dialog(this); dialog.setObjectName("acceptedRollsDialog"); dialog.setWindowTitle("Accept dice results"); dialog.resize(630, 450);
    auto* layout = new QVBoxLayout(&dialog);
    auto* notice = new QLabel("Select the inputs to roll. Existing accepted values start unchecked; selecting one explicitly replaces it. The rules module determines each die and its destination, including different class hit dice at different levels.");
    notice->setWordWrap(true); layout->addWidget(notice);
    auto* list = new QListWidget; list->setObjectName("rollRequestsList"); layout->addWidget(list, 1);
    for (const auto& request : evaluation_.rollRequests) if (category.empty() || request.category == category) {
        const auto saved = at(document_.choices, request.path);
        QString label = q(request.label) + " · " + QString::number(request.count) + "d" + QString::number(request.sides);
        if (request.dropLowest) label += ", drop lowest " + QString::number(request.dropLowest);
        if (!saved.is_null()) label += " · accepted " + display(saved);
        auto* item = new QListWidgetItem(label, list); item->setData(Qt::UserRole, q(request.id)); item->setToolTip(q(request.path));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(saved.is_null() ? Qt::Checked : Qt::Unchecked);
    }
    auto* error = new QLabel; error->setWordWrap(true); layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel); auto* roll = buttons->addButton("Roll selected and accept", QDialogButtonBox::AcceptRole); layout->addWidget(buttons);
    if (!list->count()) { error->setText("There are no current dice requests for this selection. Choose the rolled generation method or complete the preceding class and advancement choices first."); roll->setEnabled(false); }
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::vector<std::string> selected;
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->checkState() == Qt::Checked) selected.push_back(list->item(i)->data(Qt::UserRole).toString().toStdString());
        if (selected.empty()) { error->setText("Select at least one input to roll."); return; }
        for (const auto& id : selected) {
            if (!acceptRoll(id, true)) { error->setText(lastError_ + " Previously completed inputs remain accepted and unchecked."); return; }
            for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toString().toStdString() == id) list->item(i)->setCheckState(Qt::Unchecked);
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.exec();
}

void MainWindow::rollAbilities() {
    if (readOnlyMode()) return;
    if (document_.edition == "srd51" || document_.moduleVersion != "1.0.0" || !evaluation_.rollRequests.empty()) { rollRequestsDialog("abilities"); return; }
    const bool bx = document_.edition == "bx";
    if (QMessageBox::question(this, "Roll ability scores", bx ? "Roll 3d6 in order and replace the six ability inputs? Accepted results will be saved." : "Roll 4d6, drop the lowest, for each ability and select the rolled method? Accepted results will be saved.") != QMessageBox::Yes) return;
    const std::vector<std::string> names = bx ? std::vector<std::string>{"str", "int", "wis", "dex", "con", "cha"} : std::vector<std::string>{"strength", "intelligence", "wisdom", "dexterity", "constitution", "charisma"};
    Json recorded = Json::object();
    for (const auto& name : names) {
        std::vector<int> dice{rollDie_(6), rollDie_(6), rollDie_(6)}; if (!bx) dice.push_back(rollDie_(6));
        int total = 0; for (int n : dice) total += n; if (!bx) total -= *std::min_element(dice.begin(), dice.end());
        document_.choices["abilities"][name] = total; recorded[name] = {{"dice", dice}, {"total", total}};
    }
    document_.rolls["abilities"] = recorded;
    if (!bx) document_.choices["abilityMethod"] = "rolled";
    markChanged();
}

void MainWindow::rollHitPoints() {
    if (readOnlyMode()) return;
    if (document_.edition == "srd51" || document_.moduleVersion != "1.0.0" || !evaluation_.rollRequests.empty()) { rollRequestsDialog("hitPoints"); return; }
    const bool bx = document_.edition == "bx";
    const int level = integerChoice(document_.choices, "level", 1);
    const std::string cls = stringChoice(document_.choices, bx ? "class" : "classId");
    if (cls.empty()) { reportError("Select a class first", "The hit die depends on your class."); return; }
    const auto* entry = ruleset_.find(cls);
    if (!entry || !entry->contains("hitDie") || !(*entry)["hitDie"].is_number_integer()) {
        reportError("Class rules unavailable", "The selected class must provide a supported hit die before rolling."); return;
    }
    const int sides = (*entry)["hitDie"].get<int>();
    if (sides < 2 || sides > 100) { reportError("Unsupported hit die", "This class has an invalid hit die."); return; }
    const int count = std::min(level, bx ? entry->value("rolledHitDiceLimit", 0) : entry->value("maxLevel", 3));
    if (count < 1) { reportError("Progression unavailable", "The class must provide supported hit-point progression before rolling."); return; }
    if (QMessageBox::question(this, "Roll hit points", "Roll missing hit-point inputs through level " + QString::number(count) + " using d" + QString::number(sides) + "? Existing accepted results are preserved.") != QMessageBox::Yes) return;
    if (!bx) document_.choices["hpMethod"] = "rolled";
    for (int i = bx ? 0 : 2; i < (bx ? count : count + 1); ++i) {
        const std::string path = "/hp/" + std::to_string(i);
        if (!at(document_.choices, path).is_null()) continue;
        int result = rollDie_(sides); std::vector<int> dice{result};
        if (bx && i == 0 && sides > 2 && effectiveCampaignOptions(document_).value("rerollLowFirstHp", Json(false)) == Json(true))
            while (result <= 2) { result = rollDie_(sides); dice.push_back(result); }
        document_.choices[Json::json_pointer(path)] = result;
        document_.rolls["hp"][std::to_string(i)] = {{"sides", sides}, {"dice", dice}, {"result", result}};
    }
    markChanged();
}
void MainWindow::rollMoney() {
    if (readOnlyMode()) return;
    if (document_.edition == "srd51" || document_.moduleVersion != "1.0.0" || !evaluation_.rollRequests.empty()) { rollRequestsDialog("money"); return; }
    if (document_.edition != "bx") return;
    if (QMessageBox::question(this, "Roll starting money", "Roll 3d6 × 10 gold pieces and replace the creation-time money roll? Current gold is separate.") != QMessageBox::Yes) return;
    const std::vector<int> dice{rollDie_(6), rollDie_(6), rollDie_(6)}; const int total = dice[0] + dice[1] + dice[2];
    document_.choices["moneyRoll"] = total; document_.rolls["startingMoney"] = {{"dice", dice}, {"total", total}, {"gold", total * 10}}; markChanged();
}
}
