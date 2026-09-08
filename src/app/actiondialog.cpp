#include "actiondialog.hpp"
#include "dnd/persistence.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <algorithm>
#include <set>

namespace dnd {
namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }
QString escaped(const std::string& value) { return q(value).toHtmlEscaped(); }
Json at(const Json& value, const std::string& path) { try { return value.at(Json::json_pointer(path)); } catch (...) { return nullptr; } }
bool containsPath(const std::string& parent, const std::string& child) { return child == parent || child.starts_with(parent + "/"); }
QString valueText(const Json& value, const ResolvedRuleset& ruleset) {
    if (value.is_null()) return "Not set";
    if (value.is_boolean()) return value.get<bool>() ? "Yes" : "No";
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        if (const auto* entry = ruleset.find(text)) return q(entry->value("name", text));
        return q(text);
    }
    if (value.is_array()) {
        QStringList items; for (const auto& item : value) items << valueText(item, ruleset);
        return items.isEmpty() ? "None" : items.join(", ");
    }
    if (value.is_object()) {
        QStringList items; for (auto it = value.begin(); it != value.end(); ++it) items << q(it.key()) + ": " + valueText(it.value(), ruleset);
        return items.isEmpty() ? "None" : items.join("; ");
    }
    return q(value.dump());
}
QString pathLabel(const std::string& path) {
    QString result = q(path); result.replace("~1", "/"); result.replace("~0", "~");
    if (result.startsWith('/')) result.remove(0, 1);
    result.replace('/', " · "); result.replace('_', ' ');
    if (!result.isEmpty()) result[0] = result[0].toUpper();
    return result;
}
QString sourcesHtml(const std::vector<SourceRef>& refs) {
    QString result;
    for (const auto& ref : refs) {
        result += "<li>" + escaped(ref.publication) + (ref.page.empty() ? "" : " — " + escaped(ref.page));
        if (!ref.url.empty()) result += " · <a href=\"" + escaped(ref.url) + "\">Reference</a>";
        result += "</li>";
    }
    return result.isEmpty() ? QString{} : "<ul>" + result + "</ul>";
}
}

ActionDialog::ActionDialog(CharacterDocument document, ResolvedRuleset ruleset, Evaluation evaluation, Apply apply, QWidget* parent)
    : QDialog(parent), source_(std::move(document)), ruleset_(std::move(ruleset)), evaluation_(std::move(evaluation)), apply_(std::move(apply)), expectedSource_(toJson(source_)) {
    setObjectName("characterActionsDialog"); setWindowTitle("Character actions"); resize(1050, 720); setMinimumSize(790, 540);
    auto* layout = new QVBoxLayout(this);
    auto* instruction = new QLabel("Choose an action, enter its inputs, then preview the result. The character changes only when you apply a valid preview.");
    instruction->setWordWrap(true); layout->addWidget(instruction);
    picker_ = new QComboBox; picker_->setObjectName("actionPicker");
    for (const auto& action : evaluation_.actions) {
        picker_->addItem(q(action.label) + (action.available ? "" : " — unavailable"), q(action.id));
        picker_->setItemData(picker_->count()-1, q(action.reason), Qt::ToolTipRole);
    }
    layout->addWidget(picker_);
    description_ = new QLabel; description_->setObjectName("actionDescription"); description_->setTextFormat(Qt::PlainText); description_->setWordWrap(true); layout->addWidget(description_);
    auto* panes = new QSplitter(Qt::Horizontal); panes->setChildrenCollapsible(false);
    fields_ = new QScrollArea; fields_->setObjectName("actionFields"); fields_->setWidgetResizable(true); fields_->setFrameShape(QFrame::NoFrame); fields_->setMinimumWidth(280); panes->addWidget(fields_);
    auto* tabs = new QTabWidget;
    changes_ = new QTextBrowser; changes_->setObjectName("actionChanges"); changes_->setOpenExternalLinks(true); tabs->addTab(changes_, "Changes and effects");
    sheet_ = new QTextBrowser; sheet_->setObjectName("actionProposedSheet");
    auto paper = sheet_->palette(); paper.setColor(QPalette::Base, Qt::white); paper.setColor(QPalette::Text, Qt::black); sheet_->setPalette(paper);
    tabs->addTab(sheet_, "Sheet after action");
    panes->addWidget(tabs); panes->setSizes({380, 600}); layout->addWidget(panes, 1);
    status_ = new QLabel; status_->setObjectName("actionPreviewStatus"); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    previewButton_ = buttons->addButton("Preview action", QDialogButtonBox::ActionRole); previewButton_->setObjectName("previewActionButton"); previewButton_->setDefault(true);
    applyButton_ = buttons->addButton("Apply action", QDialogButtonBox::AcceptRole); applyButton_->setObjectName("applyActionButton"); applyButton_->setEnabled(false);
    layout->addWidget(buttons);
    connect(picker_, &QComboBox::currentIndexChanged, this, [this] { buildFields(); });
    connect(previewButton_, &QPushButton::clicked, this, [this] { preview(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!previewReady_ || previewInputs_ != inputs_ || !candidate_.valid()) return;
        const QString error = apply_(candidate_, expectedSource_);
        if (error.isEmpty()) accept();
        else {
            invalidatePreview(); status_->setText(error);
            // The owner rejected a previously valid candidate, so this snapshot
            // can no longer authorize an action. Reopen against the current state.
            previewButton_->setEnabled(false); picker_->setEnabled(false); fields_->setEnabled(false);
        }
    });
    buildFields();
}

const ActionDefinition* ActionDialog::selected() const {
    const auto id = picker_->currentData().toString().toStdString();
    const auto it = std::find_if(evaluation_.actions.begin(), evaluation_.actions.end(), [&id](const ActionDefinition& action) { return action.id == id; });
    return it == evaluation_.actions.end() ? nullptr : &*it;
}
void ActionDialog::invalidatePreview() {
    previewReady_ = false; applyButton_->setEnabled(false);
    status_->setText(inputErrors_.empty() ? "Preview the current inputs before applying this action." : inputErrors_.begin()->second);
    previewButton_->setEnabled(selected() && selected()->available && inputErrors_.empty());
    changes_->clear(); sheet_->clear();
}
void ActionDialog::setInput(const std::string& path, const Json& value) {
    try {
        if (path.empty() || path.front() != '/') throw std::runtime_error("Action inputs require a JSON pointer path.");
        Json proposed = inputs_; proposed[Json::json_pointer(path)] = value;
        if (proposed != inputs_) { inputs_ = std::move(proposed); invalidatePreview(); }
    } catch (const std::exception& error) { invalidatePreview(); status_->setText(q(error.what())); }
}
void ActionDialog::buildFields() {
    inputs_ = Json::object(); inputErrors_.clear(); invalidatePreview();
    if (auto* previous = fields_->widget()) {
        for (auto* child : previous->findChildren<QObject*>()) QObject::disconnect(child, nullptr, this, nullptr);
        fields_->takeWidget(); previous->deleteLater();
    }
    auto* body = new QWidget; auto* form = new QFormLayout(body); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); form->setRowWrapPolicy(QFormLayout::WrapLongRows); form->setSpacing(12);
    const auto* action = selected();
    if (!action) { description_->setText("This rules module does not currently offer any actions."); previewButton_->setEnabled(false); fields_->setWidget(body); return; }
    description_->setText(q(action->description) + (action->available ? QString{} : "\n" + q(action->reason)));
    previewButton_->setEnabled(action->available);
    for (const auto& field : action->fields) {
        const Json initial = at(action->initialInputs, field.path);
        QWidget* editor = nullptr;
        if (field.kind == "integer") {
            auto* spin = new QSpinBox; spin->setRange(field.minimum, field.maximum); spin->setKeyboardTracking(false); spin->setValue(initial.is_number_integer() ? initial.get<int>() : field.minimum);
            inputs_[Json::json_pointer(field.path)] = spin->value();
            connect(spin, &QSpinBox::valueChanged, this, [this, path=field.path](int value) { setInput(path, value); }); editor = spin;
        } else if (field.kind == "boolean") {
            auto* check = new QCheckBox; check->setChecked(initial.is_boolean() && initial.get<bool>()); inputs_[Json::json_pointer(field.path)] = check->isChecked();
            connect(check, &QCheckBox::toggled, this, [this, path=field.path](bool value) { setInput(path, value); }); editor = check;
        } else if (field.kind == "select") {
            auto* combo = new QComboBox; combo->addItem("Choose…"); combo->setMinimumContentsLength(16); combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            for (const auto& option : field.options) {
                combo->addItem(q(option.label) + (option.available ? "" : " — unavailable"), q(option.id));
                combo->setItemData(combo->count()-1, q(option.reason), Qt::ToolTipRole);
                if (!option.available) if (auto* model = qobject_cast<QStandardItemModel*>(combo->model())) model->item(combo->count()-1)->setEnabled(false);
            }
            if (initial.is_string()) {
                const QString id = q(initial.get<std::string>()); int index = combo->findData(id);
                if (index < 0) { combo->addItem(id + " — unavailable", id); index = combo->count()-1; }
                combo->setCurrentIndex(index); inputs_[Json::json_pointer(field.path)] = initial;
            }
            connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, path=field.path](int index) {
                if (index > 0) setInput(path, combo->itemData(index).toString().toStdString());
                else { setInput(path, nullptr); }
            }); editor = combo;
        } else if (field.kind == "multiselect") {
            auto* list = new QListWidget; list->setMinimumHeight(130); list->setMaximumHeight(220); inputs_[Json::json_pointer(field.path)] = initial.is_array() ? initial : Json::array();
            for (const auto& option : field.options) {
                auto* item = new QListWidgetItem(q(option.label), list); item->setData(Qt::UserRole, q(option.id)); item->setToolTip(q(option.reason));
                const bool checked = initial.is_array() && std::find(initial.begin(),initial.end(),Json(option.id)) != initial.end();
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
                if (!option.available && !checked) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            }
            if (initial.is_array()) for (const auto& id : initial) if (id.is_string() && std::none_of(field.options.begin(),field.options.end(),[&id](const Choice& choice) { return choice.id == id.get<std::string>(); })) {
                auto* item = new QListWidgetItem(q(id.get<std::string>()) + " — unavailable", list); item->setData(Qt::UserRole,q(id.get<std::string>())); item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(Qt::Checked);
            }
            connect(list, &QListWidget::itemChanged, this, [this, list, path=field.path](QListWidgetItem*) {
                Json values = Json::array(); for (int i=0; i<list->count(); ++i) if (list->item(i)->checkState()==Qt::Checked) values.push_back(list->item(i)->data(Qt::UserRole).toString().toStdString());
                setInput(path, values);
            }); editor = list;
        } else if (field.kind == "json") {
            auto* text = new QPlainTextEdit; text->setMinimumHeight(120); text->setMaximumHeight(230); text->setPlaceholderText("Enter valid JSON for this structured input.");
            if (action->initialInputs.contains(Json::json_pointer(field.path))) { text->setPlainText(q(initial.dump(2))); inputs_[Json::json_pointer(field.path)] = initial; }
            connect(text, &QPlainTextEdit::textChanged, this, [this, text, path=field.path] {
                const auto raw = text->toPlainText().trimmed();
                if (raw.isEmpty()) {
                    inputErrors_.erase(path);
                    const Json::json_pointer pointer(path);
                    if (inputs_.contains(pointer)) {
                        auto& parent = inputs_.at(pointer.parent_pointer());
                        if (parent.is_object()) parent.erase(pointer.back());
                        else inputs_.at(pointer) = nullptr; // Keep adjacent indexed inputs in their original positions.
                    }
                    invalidatePreview(); return;
                }
                try { const auto parsed = Json::parse(raw.toStdString()); inputErrors_.erase(path); setInput(path, parsed); invalidatePreview(); }
                catch (const std::exception& error) { inputErrors_[path] = "Invalid JSON for " + q(path) + ": " + q(error.what()); invalidatePreview(); }
            }); editor = text;
        } else {
            auto* text = new QLineEdit(initial.is_string() ? q(initial.get<std::string>()) : QString{}); inputs_[Json::json_pointer(field.path)] = text->text().toStdString();
            connect(text, &QLineEdit::textChanged, this, [this, path=field.path](const QString& value) { setInput(path, value.toStdString()); }); editor = text;
        }
        editor->setObjectName("actionInput:" + q(field.path)); editor->setToolTip(q(field.help)); editor->setEnabled(action->available && field.editable);
        auto* group = new QWidget; auto* rows = new QVBoxLayout(group); rows->setContentsMargins(0,0,0,0); rows->setSpacing(4); rows->addWidget(editor);
        if (!field.help.empty()) { auto* help = new QLabel(q(field.help)); help->setWordWrap(true); rows->addWidget(help); }
        if (!field.editable && !field.readOnlyReason.empty()) { auto* reason = new QLabel(q(field.readOnlyReason)); reason->setWordWrap(true); rows->addWidget(reason); }
        form->addRow(q(field.label), group);
    }
    if (action->fields.empty()) { auto* text = new QLabel("This action needs no additional inputs. Preview its effects before applying it."); text->setWordWrap(true); form->addRow(text); }
    fields_->setWidget(body);
}

QString ActionDialog::changesHtml(const TransitionResult& result) const {
    QString html = "<h2>" + (selected() ? escaped(selected()->label) : QString("Action")) + "</h2>";
    for (const auto& message : result.messages) html += "<p><b>" + escaped(message.severity) + ":</b> " + escaped(message.text) + "</p>";
    if (selected()) html += sourcesHtml(selected()->sources);
    const Json after = toJson(result.document);
    const auto operations = Json::diff(expectedSource_, after);
    std::set<std::string> printed;
    html += "<h3>Reviewed changes</h3><table width='100%' cellpadding='5'><tr><th>Value</th><th>Before</th><th>After</th></tr>";
    for (const auto& operation : operations) {
        const std::string path = operation.value("path", std::string{});
        if (containsPath("/advancement", path)) continue;
        std::string group = path; QString label = pathLabel(path); std::size_t match = 0;
        for (const auto& stage : evaluation_.stages) for (const auto& field : stage.fields) {
            const std::string full = (field.scope=="resources" ? "/resources" : "/choices") + field.path;
            if (containsPath(full,path) && full.size() > match) { group = full; label = q(field.label); match = full.size(); }
        }
        for (const auto& resource : evaluation_.resources) if (path == "/resources/" + resource.id) { label = q(resource.label); break; }
        if (!printed.insert(group).second) continue;
        html += "<tr><td>" + label.toHtmlEscaped() + "</td><td>" + valueText(at(expectedSource_,group),ruleset_).toHtmlEscaped() + "</td><td>" + valueText(at(after,group),ruleset_).toHtmlEscaped() + "</td></tr>";
    }
    html += "</table>";
    if (source_.advancement.size() != result.document.advancement.size()) html += "<p><b>History:</b> " + QString::number(source_.advancement.size()) + " → " + QString::number(result.document.advancement.size()) + " recorded entries.</p>";
    if (operations.empty()) html += "<p>No character values will change.</p>";
    return html;
}
void ActionDialog::preview() {
    const auto* action = selected(); if (!action || !action->available) return;
    if (!inputErrors_.empty()) { invalidatePreview(); return; }
    invalidatePreview();
    try {
        candidate_ = executeCommand(source_, ruleset_, {action->id, inputs_}); previewInputs_ = inputs_;
        changes_->setHtml(changesHtml(candidate_));
        if (candidate_.valid()) {
            const auto evaluation = evaluate(candidate_.document, ruleset_);
            sheet_->setHtml(q(renderSheetHtml(candidate_.document, evaluation, ruleset_)));
            previewReady_ = true; applyButton_->setEnabled(true);
            status_->setText("Preview valid. Apply action to commit exactly these changes.");
        } else status_->setText("The action is not valid with these inputs. Review the messages, adjust the inputs, and preview again.");
    } catch (const std::exception& error) { status_->setText(q(error.what())); }
}
}
