#pragma once
#include "dnd/engine.hpp"
#include <QDialog>
#include <QString>
#include <functional>
#include <map>

class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QTextBrowser;

namespace dnd {
class ActionDialog final : public QDialog {
public:
    // Empty return means the owner committed the exact reviewed candidate.
    using Apply = std::function<QString(const TransitionResult&, const Json&)>;
    ActionDialog(CharacterDocument document, ResolvedRuleset ruleset, Evaluation evaluation, Apply apply, QWidget* parent = nullptr);
private:
    CharacterDocument source_;
    ResolvedRuleset ruleset_;
    Evaluation evaluation_;
    Apply apply_;
    Json inputs_ = Json::object();
    Json expectedSource_;
    Json previewInputs_;
    TransitionResult candidate_;
    bool previewReady_ = false;
    std::map<std::string, QString> inputErrors_;
    QComboBox* picker_ = nullptr;
    QLabel* description_ = nullptr;
    QLabel* status_ = nullptr;
    QScrollArea* fields_ = nullptr;
    QTextBrowser* changes_ = nullptr;
    QTextBrowser* sheet_ = nullptr;
    QPushButton* previewButton_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    const ActionDefinition* selected() const;
    void buildFields();
    void setInput(const std::string& path, const Json& value);
    void invalidatePreview();
    void preview();
    QString changesHtml(const TransitionResult& result) const;
};
}
