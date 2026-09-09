#include "mainwindow.hpp"
#include "actiondialog.hpp"
#include "srd55_fixture.hpp"
#include <QAction>
#include <QAbstractButton>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <fstream>

class DesktopWorkflow : public QObject {
    Q_OBJECT
private:
    QTemporaryDir settings_;
    void prepareFighter(dnd::MainWindow& window) {
        window.newDocument("bx");
        QVERIFY(window.setChoice("/abilities/str", 16));
        QVERIFY(window.setChoice("/abilities/int", 9));
        QVERIFY(window.setChoice("/abilities/wis", 10));
        QVERIFY(window.setChoice("/abilities/dex", 13));
        QVERIFY(window.setChoice("/abilities/con", 14));
        QVERIFY(window.setChoice("/abilities/cha", 11));
        QVERIFY(window.setChoice("/class", "bx:fighter"));
        QVERIFY(window.setChoice("/alignment", "lawful"));
        QVERIFY(window.setChoice("/level", 1));
        QVERIFY(window.setChoice("/xp", 0));
        QVERIFY(window.setChoice("/hp/0", 7));
        QVERIFY(window.setChoice("/moneyRoll", 12));
        QVERIFY(window.setChoice("/armor", "bx:chain"));
        QVERIFY(window.setChoice("/weapon", "bx:sword"));
        QVERIFY(window.setChoice("/shield", true));
    }
    bool openActionFixture(dnd::MainWindow& window, const QString& filename, const std::string& cls = "fighter", int level = 1) {
        static const auto rules = srd55fixtures::rules();
        const auto result = srd55fixtures::complete(cls, level, rules);
        if (!result.evaluation.complete()) { qWarning().noquote() << QString::fromStdString(srd55fixtures::errors(result.evaluation)); return false; }
        dnd::saveCharacter(filename.toStdString(), result.document);
        return window.openPath(filename, false);
    }
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("DndDesktopTests");
        QCoreApplication::setApplicationName("DesktopWorkflow");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_.path());
        QFile::remove(QString::fromStdString(dnd::autosavePath((QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/untitled.dnd.json").toStdString()).string()));
    }

    void sheetSectionHeadingStaysWithItsFirstRow_data() {
        QTest::addColumn<QString>("firstRowKind");
        QTest::newRow("scalar") << QString("scalar");
        QTest::newRow("nested-table") << QString("nested-table");
        QTest::newRow("notes-only") << QString("notes-only");
        QTest::newRow("additional") << QString("additional");
        QTest::newRow("resources") << QString("resources");
        QTest::newRow("overrides") << QString("overrides");
        QTest::newRow("validation") << QString("validation");
        QTest::newRow("sources") << QString("sources");
    }

    void sheetSectionHeadingStaysWithItsFirstRow() {
        QFETCH(QString, firstRowKind);
        // Vary the preceding content so this exercises real Qt page boundaries,
        // including a section whose first row contains a nested advancement table.
        for (int rows = 1; rows <= 40; ++rows) {
            dnd::CharacterDocument character;
            dnd::Evaluation evaluation;
            dnd::ResolvedRuleset ruleset;
            dnd::SheetSection preceding{"Preceding statistics", {}, {}};
            for (int row = 0; row < rows; ++row) {
                const auto id = "filler." + std::to_string(row);
                evaluation.calculations.push_back({id, "Statistic " + std::to_string(row), row, row});
                preceding.calculationIds.push_back(id);
            }
            evaluation.sections.push_back(preceding);
            QString headingText = "Advancement";
            QString firstRowText = "Advancement inputs";
            if (firstRowKind == "notes-only") {
                firstRowText = "First section note";
                evaluation.sections.push_back({"Advancement", {}, {firstRowText.toStdString(), "Second section note"}});
            } else if (firstRowKind == "resources") {
                headingText = "Current resources";
                firstRowText = "Remaining uses";
                character.resources[firstRowText.toStdString()] = 2;
            } else if (firstRowKind == "overrides") {
                headingText = "DM overrides";
                firstRowText = "Statistic 0: normal 0; effective 0. Reason: Test override";
                evaluation.calculations.front().overrideReason = "Test override";
            } else if (firstRowKind == "validation") {
                headingText = "Validation";
                firstRowText = "error: Correct this validation input";
                evaluation.messages.push_back({"error", "test.validation", "/test", "Correct this validation input", {}});
            } else if (firstRowKind == "sources") {
                headingText = "Sources and campaign";
                firstRowText = "Test source 1.0.0 - Test publisher (homebrew)";
                ruleset.packs.push_back({{{"name", "Test source"}, {"version", "1.0.0"},
                                         {"publisher", "Test publisher"}, {"origin", "homebrew"}}, {}, {}});
            } else {
                const auto values = firstRowKind == "scalar" ? dnd::Json(7)
                    : dnd::Json::array({{{"level", 1}, {"hitDie", 7}, {"hpAdded", 8}},
                                       {{"level", 2}, {"hitDie", 5}, {"hpAdded", 6}}});
                evaluation.calculations.push_back({"advancement", "Advancement inputs", values, values});
                if (firstRowKind == "additional") headingText = "Additional statistics";
                else evaluation.sections.push_back({"Advancement", {"advancement"}, {}});
            }
            QTextDocument sheet;
            const qreal pageHeight = 500;
            sheet.setPageSize(QSizeF(420, pageHeight));
            sheet.setHtml(QString::fromStdString(dnd::renderSheetHtml(character, evaluation, ruleset)));
            sheet.documentLayout()->documentSize();
            int headingPage = -1, dataPage = -1;
            for (auto block = sheet.begin(); block.isValid(); block = block.next()) {
                const auto text = block.text();
                const int page = static_cast<int>(std::floor(
                    sheet.documentLayout()->blockBoundingRect(block).top() / pageHeight));
                if (text == headingText) headingPage = page;
                else if (text == firstRowText) dataPage = page;
            }
            QVERIFY(headingPage >= 0 && dataPage >= 0);
            QVERIFY2(headingPage == dataPage,
                     qPrintable(QString("Heading on page %1, data on page %2 after %3 preceding rows")
                                    .arg(headingPage).arg(dataPage).arg(rows)));
        }
    }

    void sheetCompositeCalculationLabelStaysWithItsFirstValue_data() {
        QTest::addColumn<bool>("arrayValue");
        QTest::newRow("resource-object") << false;
        QTest::newRow("advancement-array") << true;
    }

    void sheetCompositeCalculationLabelStaysWithItsFirstValue() {
        QFETCH(bool, arrayValue);
        for (int rows = 1; rows <= 70; ++rows) {
            dnd::Evaluation evaluation;
            dnd::SheetSection section{"Class features", {}, {}};
            for (int row = 0; row < rows; ++row) {
                const auto id = "filler." + std::to_string(row);
                evaluation.calculations.push_back({id, "Statistic " + std::to_string(row), row, row});
                section.calculationIds.push_back(id);
            }
            const auto values = arrayValue
                ? dnd::Json::array({{{"level", 1}, {"hitDie", 9876}, {"hpAdded", 8}}})
                : dnd::Json{{"current", 9876}, {"maximum", 9999}, {"recharge", "long-rest:all"}};
            evaluation.calculations.push_back({"restoration", "Sorcerous Restoration", values, values});
            section.calculationIds.push_back("restoration");
            evaluation.sections.push_back(section);
            QTextDocument sheet;
            const qreal pageHeight = 500;
            sheet.setPageSize(QSizeF(420, pageHeight));
            sheet.setHtml(QString::fromStdString(dnd::renderSheetHtml({}, evaluation, {})));
            sheet.documentLayout()->documentSize();
            int labelPage = -1, valuePage = -1;
            for (auto block = sheet.begin(); block.isValid(); block = block.next()) {
                const int page = static_cast<int>(std::floor(
                    sheet.documentLayout()->blockBoundingRect(block).top() / pageHeight));
                if (block.text() == "Sorcerous Restoration") labelPage = page;
                else if (block.text() == "9876") valuePage = page;
            }
            QVERIFY(labelPage >= 0 && valuePage >= 0);
            QVERIFY2(labelPage == valuePage,
                     qPrintable(QString("Label on page %1, first value on page %2 after %3 preceding rows")
                                    .arg(labelPage).arg(valuePage).arg(rows)));
        }
    }

    void sheetFeatNotesFlowAfterPrecedingSection() {
        dnd::Evaluation evaluation;
        evaluation.sections.push_back({"Class features", {}, {"First class feature", "Last class feature"}});
        evaluation.sections.push_back({"Feats", {}, {"First feat", "Second feat", "Third feat", "Fourth feat", "Fifth feat"}});
        QTextDocument sheet;
        sheet.setPageSize(QSizeF(420, 700));
        sheet.setHtml(QString::fromStdString(dnd::renderSheetHtml({}, evaluation, {})));
        QCOMPARE(sheet.pageCount(), 1);
    }

    void physicalSpellbookSheetShowsStateWithoutInternalLedgers() {
        const auto rules = srd55fixtures::rules();
        auto fixture = srd55fixtures::complete("wizard", 1, rules);
        QVERIFY2(fixture.evaluation.complete(), srd55fixtures::errors(fixture.evaluation).c_str());
        auto document = fixture.document;
        const auto applyCommand = [&](const std::string& id, const dnd::Json& inputs = dnd::Json::object()) {
            const auto result = dnd::executeCommand(document, rules, {id, inputs});
            if (!result.valid()) return false;
            document = result.document;
            return true;
        };
        QVERIFY(applyCommand("srd55.inventory.initialize"));
        QVERIFY(applyCommand("srd55.history.accept-baseline"));
        std::string original;
        for (const auto& item : document.resources["inventory"]["instances"])
            if (item["itemId"] == "srd55:spellbook") original = item["id"];
        QVERIFY(!original.empty());
        QVERIFY(applyCommand("srd55.spellbooks.initialize", {{"instanceId", original}}));
        QVERIFY(applyCommand("srd55.inventory.acquire", {{"itemId", "srd55:spellbook"}, {"quantity", 1},
            {"source", "gift"}, {"paidCp", 0}, {"reason", "Campaign supplies a blank backup"}}));
        const std::string backup = document.resources["inventory"]["instances"].back()["id"];
        QVERIFY(applyCommand("srd55.spellbooks.register", {{"instanceId", backup}}));
        QVERIFY(applyCommand("srd55.spellbooks.copy", {{"sourceId", original}, {"destinationId", backup},
            {"spells", {"srd55:alarm"}}, {"minutes", 60}, {"paidCp", 1000}}));
        document.resources["notes"] = "Keep the red cover dry";
        document.resources["Camp supplies"] = {{"rations", 3}, {"water", 2}};
        document.campaign["tableNote"] = "A visible campaign note";
        document.overrides.push_back({{"target", "hp.maximum"}, {"value", 12}, {"reason", "DM award for recovering the book"}});
        const auto before = dnd::toJson(document);
        const auto evaluation = dnd::evaluate(document, rules);
        QVERIFY2(evaluation.complete(), srd55fixtures::errors(evaluation).c_str());
        QTextDocument sheet;
        sheet.setPageSize(QSizeF(420, 700));
        const auto html = dnd::renderSheetHtml(document, evaluation, rules);
        sheet.setHtml(QString::fromStdString(html));
        const auto text = sheet.toPlainText();
        for (const auto* visible : {"Current owned inventory", "Physical spellbooks", "Alarm", "9500",
             "Keep the red cover dry", "Camp supplies", "Rations:", "Water:", "A visible campaign note",
             "DM award for recovering the book", "normal 10; effective 12", "System Reference Document 5.2.1"})
            QVERIFY2(text.contains(visible), visible);
        QVERIFY(!text.contains("creationCurrencyCp"));
        QVERIFY(!text.contains("Receipts:"));
        QVERIFY(!text.contains("Destination Id:"));
        QVERIFY(!text.contains("Srd55 History:"));
        QVERIFY(!text.contains("\"rations\""));
        QVERIFY(!text.contains("restWindow"));
        QCOMPARE(dnd::toJson(document), before);
        QTemporaryDir directory;
        const auto filename = (directory.path() + "/books.json").toStdString();
        dnd::saveCharacter(filename, document);
        QCOMPARE(dnd::toJson(dnd::loadCharacter(filename).document), before);
        QCOMPARE(dnd::renderSheetHtml(document, dnd::evaluate(document, rules), rules), html);
        // The edition's transient metadata, not record names in the renderer,
        // decides which bookkeeping trees are omitted from the appendix.
        auto unfiltered = evaluation;
        unfiltered.moduleData.erase("sheet");
        sheet.setHtml(QString::fromStdString(dnd::renderSheetHtml(document, unfiltered, rules)));
        QVERIFY(sheet.toPlainText().contains("Receipts:"));
        QVERIFY(sheet.toPlainText().contains("Destination Id:"));
        QVERIFY(sheet.toPlainText().contains("Srd55 History:"));
    }

    void createEquipAdvanceInspectOverrideSaveReopenExport() {
        QTemporaryDir output;
        dnd::MainWindow window;
        window.show();
        prepareFighter(window);
        QApplication::processEvents();
        auto* name = window.findChild<QLineEdit*>("characterName");
        QVERIFY(name);
        name->setFocus();
        QTest::keyClicks(name, "Mira of the Cairn");
        QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(window.document().name, std::string("Mira of the Cairn"));

        auto* stages = window.findChild<QListWidget*>("builderStages");
        QVERIFY(stages && stages->count() > 3);
        int progress = -1;
        for (std::size_t i = 0; i < window.evaluation().stages.size(); ++i)
            for (const auto& field : window.evaluation().stages[i].fields)
                if (field.path == "/level") progress = static_cast<int>(i) + 1;
        QVERIFY(progress >= 0);
        stages->setCurrentRow(progress);
        auto* level = window.findChild<QSpinBox*>("/level");
        QVERIFY(level);
        level->setValue(2);
        QCOMPARE(window.document().choices["level"].get<int>(), 2);
        QVERIFY(!window.document().advancement.empty());
        QVERIFY(window.setChoice("/xp", 2000));
        QVERIFY(window.setChoice("/hp/1", 5));
        QVERIFY(window.evaluation().find("hp.max"));
        const auto normalHp = window.evaluation().find("hp.max")->normal;
        QVERIFY(normalHp.is_number_integer());
        QCOMPARE(normalHp.get<int>(), 14); // (7 + 1 Constitution) + (5 + 1 Constitution)
        QVERIFY(window.inspectionHtml("hp.max").contains("Normal"));
        QVERIFY(window.inspectionHtml("hp.max").contains("Moldvay"));
        bool inspectorVisible = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("calculationInspector");
            inspectorVisible = dialog && dialog->findChild<QTextBrowser*>("explanationText");
            if (dialog) dialog->accept();
        });
        window.inspect("hp.max");
        QVERIFY(inspectorVisible);
        window.setAdvanced(true);
        QVERIFY(!window.applyOverride("hp.max", 17, "   "));
        QVERIFY(window.applyOverride("hp.max", 17, "DM award for the cairn expedition"));
        QCOMPARE(window.evaluation().find("hp.max")->effective.get<int>(), 17);
        QCOMPARE(window.evaluation().find("hp.max")->normal, normalHp);
        const auto before = dnd::toJson(window.document());
        const auto evaluation = dnd::toJson(window.evaluation());
        const QString path = output.path() + "/mira.dnd.json";
        QVERIFY2(window.saveTo(path), qPrintable(window.lastError()));
        QVERIFY(!window.dirty());
        window.newDocument("bx");
        QVERIFY(window.openPath(path, false));
        QCOMPARE(dnd::toJson(window.document()), before);
        QCOMPARE(dnd::toJson(window.evaluation()), evaluation);
        const QString pdf = output.path() + "/mira.pdf";
        QVERIFY2(window.exportPdf(pdf), qPrintable(window.lastError()));
        QFile file(pdf); QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.read(5) == "%PDF-");
    }

    void advancedVisibilityPreservesChoicesAndOverrides() {
        dnd::MainWindow window;
        prepareFighter(window);
        window.setAdvanced(true);
        QVERIFY(window.setChoice("/options/variableWeaponDamage", true));
        QVERIFY(window.applyOverride("hp.max", 9, "House rule: starting vigor"));
        const auto before = dnd::toJson(window.document());
        window.setAdvanced(false);
        QCOMPARE(dnd::toJson(window.document()), before);
        auto* override = window.findChild<QAction*>("overrideAction");
        QVERIFY(override && !override->isVisible());
        window.setAdvanced(true);
        QVERIFY(override->isVisible());
        QCOMPARE(dnd::toJson(window.document()), before);
    }

    void bxCampaignInheritanceMatchesSettingsAndExplicitHitPointRolls() {
        QTemporaryDir directory;
        for(const bool explicitFalse:{false,true}) {
            const std::vector<int> dice{1,2,6};
            std::size_t rolled=0;
            dnd::MainWindow window({},nullptr,[&](int sides){
                if(sides!=8 || rolled>=dice.size())return sides;
                return dice[rolled++];
            });
            prepareFighter(window);
            auto fixture=window.document();
            fixture.campaign={{"rerollLowFirstHp",true},{"encumbrance","detailed"}};
            fixture.choices["options"]={{"variableWeaponDamage",true}};
            if(explicitFalse)fixture.choices["options"]["rerollLowFirstHp"]=false;
            fixture.choices["hp"]={2};
            fixture.rolls["hp"]["0"]={{"sides",8},{"dice",{2}},{"result",2}};
            const QString path=directory.path()+(explicitFalse?"/exception.json":"/inherited.json");
            dnd::saveCharacter(path.toStdString(),fixture);
            QVERIFY(window.openPath(path,false));
            const bool expectedReroll=!explicitFalse;
            const auto hasPermission=[&](){return std::any_of(window.evaluation().messages.begin(),window.evaluation().messages.end(),[](const dnd::Message& m){return m.code=="bx.hp.reroll.available";});};
            QCOMPARE(hasPermission(),expectedReroll);
            QCOMPARE(window.document().choices["hp"],dnd::Json::array({2}));
            QCOMPARE(window.evaluation().find("hp.max")->normal.get<int>(),3);
            window.setAdvanced(true);
            auto* stages=window.findChild<QListWidget*>("builderStages");
            QVERIFY(stages);
            for(std::size_t i=0;i<window.evaluation().stages.size();++i)
                if(window.evaluation().stages[i].id=="options")stages->setCurrentRow(static_cast<int>(i)+1);
            auto* optionalReroll=window.findChild<QCheckBox*>("/options/rerollLowFirstHp");
            auto* encumbrance=window.findChild<QComboBox*>("/options/encumbrance");
            QVERIFY(optionalReroll && encumbrance);
            QCOMPARE(optionalReroll->isChecked(),expectedReroll);
            QCOMPARE(encumbrance->currentData().toString(),QString("detailed"));

            QAction* campaign=nullptr;QAction* roll=nullptr;
            for(auto* action:window.findChildren<QAction*>()) {
                if(action->text()=="Campaign settings & presets…")campaign=action;
                if(action->text()=="Roll hit points…")roll=action;
            }
            QVERIFY(campaign && roll);
            bool dialogSeen=false,dialogChecked=false;
            QTimer::singleShot(0,&window,[&]{
                if(auto* dialog=window.findChild<QDialog*>("campaignSettingsDialog")) {
                    if(auto* check=dialog->findChild<QCheckBox*>("campaignRerollLowFirstHp")){dialogSeen=true;dialogChecked=check->isChecked();}
                    dialog->reject();
                }
            });
            const auto beforeDialog=dnd::toJson(window.document());
            campaign->trigger();
            QVERIFY(dialogSeen);QCOMPARE(dialogChecked,expectedReroll);
            QCOMPARE(dnd::toJson(window.document()),beforeDialog);
            QCOMPARE(rolled,std::size_t(0));

            // Explicitly clear this fixture's first-level input before exercising the actual action.
            QVERIFY(window.setChoice("/hp",dnd::Json::array()));
            QTimer::singleShot(0,&window,[]{
                if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))question->button(QMessageBox::Yes)->click();
            });
            roll->trigger();
            const auto acceptedDice=window.document().rolls;
            const auto acceptedHp=window.document().choices["hp"];
            QCOMPARE(acceptedHp[0].get<int>(),expectedReroll?6:1);
            QCOMPARE(rolled,expectedReroll?std::size_t(3):std::size_t(1));
            QCOMPARE(acceptedDice["hp"]["0"]["dice"],expectedReroll?dnd::Json::array({1,2,6}):dnd::Json::array({1}));

            // Saving settings and changing visibility preserve the already accepted dice.
            QTimer::singleShot(0,&window,[&]{
                if(auto* dialog=window.findChild<QDialog*>("campaignSettingsDialog")) {
                    if(auto* check=dialog->findChild<QCheckBox*>("campaignRerollLowFirstHp"))check->setChecked(!expectedReroll);
                    dialog->accept();
                }
            });
            campaign->trigger();
            QCOMPARE(dnd::effectiveCampaignOptions(window.document())["rerollLowFirstHp"],dnd::Json(!expectedReroll));
            window.setAdvanced(false);window.setAdvanced(true);
            QCOMPARE(window.document().rolls,acceptedDice);
            QCOMPARE(window.document().choices["hp"],acceptedHp);
            QTimer::singleShot(0,&window,[]{
                if(auto* question=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))question->button(QMessageBox::Yes)->click();
            });
            roll->trigger();
            QCOMPARE(window.document().rolls,acceptedDice);
            QCOMPARE(rolled,expectedReroll?std::size_t(3):std::size_t(1));
            QVERIFY(window.saveTo(path));
            const auto saved=dnd::toJson(window.document());
            const auto evaluation=dnd::toJson(window.evaluation());
            QVERIFY(window.openPath(path,false));
            QCOMPARE(dnd::toJson(window.document()),saved);
            QCOMPARE(dnd::toJson(window.evaluation()),evaluation);
        }
    }

    void unsupportedVersionAndMissingSourcesPreserveOriginal() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        prepareFighter(window);
        auto data = dnd::toJson(window.document());
        data["schemaVersion"] = 999;
        data["futureFields"] = {{"keep", "verbatim"}};
        const QString future = directory.path() + "/future.json";
        QFile file(future); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = QByteArray::fromStdString(data.dump(2)); file.write(bytes); file.close();
        QVERIFY(window.openPath(future, false));
        QVERIFY(window.readOnlyMode());
        QVERIFY(!window.saveTo(future));
        QVERIFY(!window.setChoice("/level", 2));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes); file.close();
        data["schemaVersion"] = 1;
        data["packs"] = {{{"id", "missing-pack"}, {"version", "73.0.0"}}};
        const QString missing = directory.path() + "/missing.json";
        QFile missingFile(missing); QVERIFY(missingFile.open(QIODevice::WriteOnly));
        const QByteArray missingBytes = QByteArray::fromStdString(data.dump(2)); missingFile.write(missingBytes); missingFile.close();
        QVERIFY(window.openPath(missing, false));
        QVERIFY(window.readOnlyMode());
        QVERIFY(!window.saveTo(missing));
        QVERIFY(missingFile.open(QIODevice::ReadOnly)); QCOMPARE(missingFile.readAll(), missingBytes);
    }

    void autosaveContainsAcceptedInputs() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        prepareFighter(window);
        const QString path = directory.path() + "/autosave-test.json";
        QVERIFY(window.saveTo(path));
        QVERIFY(window.setChoice("/level", 2));
        QVERIFY(window.setChoice("/hp/1", 4));
        const QString recovery = QString::fromStdString(dnd::autosavePath(path.toStdString()).string());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(recovery), 3500);
        const auto recovered = dnd::loadCharacter(recovery.toStdString());
        QVERIFY(!recovered.inspectOnly);
        QCOMPARE(recovered.document.choices, window.document().choices);
        QCOMPARE(recovered.document.advancement, window.document().advancement);
        QVERIFY(window.saveTo(path));
        QVERIFY(!QFile::exists(recovery));
    }

    void explicitDiceActionPersistsWithoutRerolling() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        QAction* roll = nullptr;
        for (auto* action : window.findChildren<QAction*>()) if (action->text() == "Roll ability scores…") roll = action;
        QVERIFY(roll);
        QTimer::singleShot(0, &window, [] {
            if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) dialog->button(QMessageBox::Yes)->click();
        });
        roll->trigger();
        QVERIFY(window.document().rolls.contains("abilities"));
        QCOMPARE(window.document().rolls["abilities"].size(), std::size_t(6));
        const auto rolls = window.document().rolls;
        const auto scores = window.document().choices["abilities"];
        for (const auto& score : scores) QVERIFY(score.get<int>() >= 3 && score.get<int>() <= 18);
        window.setAdvanced(true);
        window.setAdvanced(false);
        const QString path = directory.path() + "/dice.json";
        QVERIFY(window.saveTo(path));
        QVERIFY(window.openPath(path, false));
        QCOMPARE(window.document().rolls, rolls);
        QCOMPARE(window.document().choices["abilities"], scores);
    }

    void secondEditionUsesSameEditorAndPersistence() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        window.newDocument("srd55");
        QVERIFY(!window.readOnlyMode());
        QVERIFY(!window.evaluation().stages.empty());
        dnd::Json fighter = {{"classId","srd55:fighter"},{"level",3},{"speciesId","srd55:dwarf"},
            {"backgroundId","srd55:soldier"},{"alignment","Neutral Good"},
            {"languages",{"srd55:dwarvish","srd55:draconic"}},
            {"abilities",{{"strength",15},{"dexterity",14},{"constitution",13},{"intelligence",8},{"wisdom",10},{"charisma",12}}},
            {"backgroundBoosts",{{"strength",2},{"constitution",1}}},
            {"classSkills",{"srd55:perception","srd55:survival"}},{"gamingSet","srd55:dice"},
            {"fightingStyle","srd55:defense"},{"weaponMasteries",{"srd55:greatsword","srd55:flail","srd55:javelin"}},
            {"classEquipment","A"},{"backgroundEquipment","B"},{"armorId","srd55:chain-mail"},
            {"weaponId","srd55:greatsword"},{"subclassId","srd55:champion"}};
        for (auto it = fighter.begin(); it != fighter.end(); ++it) QVERIFY(window.setChoice("/" + it.key(), it.value()));
        QVERIFY2(window.evaluation().complete(), dnd::toJson(window.evaluation())["messages"].dump().c_str());
        QVERIFY(window.evaluation().stages.size() > 3);
        QCOMPARE(window.evaluation().find("armorClass")->normal.get<int>(), 17);
        QCOMPARE(window.evaluation().find("proficiency")->normal.get<int>(), 2);
        QCOMPARE(window.evaluation().find("hp.maximum")->normal.get<int>(), 31);
        const auto before = dnd::toJson(window.document());
        const QString path = directory.path() + "/modern.json";
        QVERIFY(window.saveTo(path));
        window.newDocument("bx");
        QVERIFY(window.openPath(path, false));
        QCOMPARE(window.document().edition, std::string("srd55"));
        QCOMPARE(dnd::toJson(window.document()), before);
        QVERIFY(window.evaluation().complete());
        window.newDocument("srd55");
        dnd::Json wizard = {{"classId","srd55:wizard"},{"level",3},{"speciesId","srd55:dwarf"},
            {"backgroundId","srd55:sage"},{"alignment","Neutral"},{"languages",{"srd55:elvish","srd55:draconic"}},
            {"abilities",{{"strength",8},{"dexterity",12},{"constitution",13},{"intelligence",15},{"wisdom",14},{"charisma",10}}},
            {"backgroundBoosts",{{"intelligence",2},{"constitution",1}}},{"classSkills",{"srd55:insight","srd55:investigation"}},
            {"classEquipment","A"},{"backgroundEquipment","A"},{"weaponId","srd55:dagger"},
            {"cantrips",{"srd55:light","srd55:mage-hand","srd55:ray-of-frost"}},
            {"spellbook",{{"1",{"srd55:detect-magic","srd55:feather-fall","srd55:mage-armor","srd55:magic-missile","srd55:sleep","srd55:thunderwave"}},
                          {"2",{"srd55:shield","srd55:burning-hands"}},{"3",{"srd55:misty-step","srd55:web"}}}},
            {"preparedSpells",{"srd55:mage-armor","srd55:magic-missile","srd55:sleep","srd55:thunderwave","srd55:shield","srd55:web"}},
            {"magicInitiate",{{"background",{{"ability","intelligence"},{"cantrips",{"srd55:fire-bolt","srd55:mending"}},{"spell","srd55:identify"}}}}},
            {"scholarSkill","srd55:arcana"},{"subclassId","srd55:evoker"},{"evocationSavant",{"srd55:scorching-ray","srd55:shatter"}}};
        for (auto it = wizard.begin(); it != wizard.end(); ++it) QVERIFY(window.setChoice("/" + it.key(), it.value()));
        QVERIFY2(window.evaluation().complete(), dnd::toJson(window.evaluation())["messages"].dump().c_str());
        QCOMPARE(window.evaluation().find("spellSlots.1")->normal.get<int>(), 4);
        QCOMPARE(window.evaluation().find("spellSlots.2")->normal.get<int>(), 2);
        QVERIFY(window.saveTo(directory.path() + "/wizard.json"));
        QVERIFY(window.openPath(directory.path() + "/wizard.json", false));
        QVERIFY(window.evaluation().complete());
        QCOMPARE(window.document().choices["preparedSpells"].size(), std::size_t(6));
    }

    void choiceOverrideRequiresKnownRulesAndReason() {
        dnd::MainWindow window;
        prepareFighter(window);
        QVERIFY(window.setChoice("/abilities/int", 8));
        QVERIFY(!window.applyOverride("choice:/class", "bx:elf", ""));
        QVERIFY(!window.applyOverride("choice:/class", "imaginary:class", "DM request"));
        QVERIFY(window.applyOverride("choice:/class", "bx:elf", "DM permits the trained elf despite low Intelligence"));
        QCOMPARE(window.document().choices["class"].get<std::string>(), std::string("bx:elf"));
        bool exceptionAnnotated = false;
        for (const auto& message : window.evaluation().messages)
            if (message.code == "bx.class.eligibility" && message.severity == "warning" && message.text.find("DM exception:") != std::string::npos) exceptionAnnotated = true;
        QVERIFY2(exceptionAnnotated, dnd::toJson(window.evaluation())["messages"].dump().c_str());
    }

    void exactVersionMenusAndSourceFiltering() {
        dnd::MainWindow window;
        auto* legacy = window.findChild<QAction*>("new:srd55:1.0.0");
        auto* expanded = window.findChild<QAction*>("new:srd55:2.0.0");
        QVERIFY(legacy && expanded);
        QVERIFY(legacy->text() != expanded->text());
        legacy->trigger();
        QCOMPARE(window.document().moduleVersion, std::string("1.0.0"));
        QVERIFY(!window.readOnlyMode());
        expanded->trigger();
        QCOMPARE(window.document().moduleVersion, std::string("2.0.0"));
        QVERIFY2(!window.readOnlyMode(), dnd::toJson(window.evaluation())["messages"].dump().c_str());
        QVERIFY(!window.document().packs.empty());
        auto* sources = window.findChild<QAction*>("enabledSourcesAction");
        QVERIFY(sources);
        bool sawExpanded = false, sawLegacy = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("enabledSourcesDialog");
            auto* list = dialog ? dialog->findChild<QListWidget*>("enabledSourcesList") : nullptr;
            if (list) for (int i = 0; i < list->count(); ++i) {
                const QString version = list->item(i)->data(Qt::UserRole + 1).toString();
                sawExpanded = sawExpanded || version == "2.0.0";
                sawLegacy = sawLegacy || version == "1.0.0";
            }
            if (dialog) dialog->reject();
        });
        sources->trigger();
        QVERIFY(sawExpanded);
        QVERIFY(!sawLegacy);
        window.newDocument("srd55");
        QCOMPARE(window.document().moduleVersion, std::string("1.0.0"));
    }

    void openingLegacySaveKeepsItsModule() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        window.newDocument("srd55", "1.0.0");
        QVERIFY(window.setChoice("/classId", "srd55:fighter"));
        QVERIFY(window.setChoice("/level", 3));
        const auto legacyEvaluation = dnd::toJson(window.evaluation());
        const QString path = directory.path() + "/legacy.json";
        QVERIFY(window.saveTo(path));
        window.newDocument("srd55", "2.0.0");
        QVERIFY(window.openPath(path, false));
        QCOMPARE(window.document().moduleVersion, std::string("1.0.0"));
        QCOMPARE(dnd::toJson(window.evaluation()), legacyEvaluation);
        int maximum = 0;
        for (const auto& stage : window.evaluation().stages) for (const auto& field : stage.fields) if (field.path == "/level") maximum = field.maximum;
        QCOMPARE(maximum, 3);
    }

    void upgradePreviewCreatesCopyAndProtectsOriginal() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        window.newDocument("srd55", "1.0.0");
        QVERIFY(window.setChoice("/classId", "srd55:fighter"));
        QVERIFY(window.setChoice("/level", 3));
        QVERIFY(window.setChoice("/fightingStyle", "srd55:defense"));
        QVERIFY(window.setChoice("/subclassId", "srd55:champion"));
        QVERIFY(window.setResource("/hp", 8));
        const QString originalPath = directory.path() + "/legacy.json";
        QVERIFY(window.saveTo(originalPath));
        QFile original(originalPath); QVERIFY(original.open(QIODevice::ReadOnly)); const QByteArray originalBytes = original.readAll(); original.close();
        const auto originalDocument = dnd::toJson(window.document());
        auto* upgrade = window.findChild<QAction*>("upgradeCopyAction"); QVERIFY(upgrade && upgrade->isEnabled());
        bool reviewed = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("upgradeCopyDialog");
            if (!dialog) return;
            for (auto* button : dialog->findChildren<QPushButton*>()) if (button->text() == "Create unsaved copy" && button->isEnabled()) {
                reviewed = true; button->click(); return;
            }
            dialog->reject();
        });
        upgrade->trigger();
        QVERIFY(reviewed);
        QCOMPARE(window.document().moduleVersion, std::string("2.0.0"));
        QVERIFY(window.currentPath().isEmpty());
        QVERIFY(window.dirty());
        QCOMPARE(window.document().resources["hp"].get<int>(), 8);
        QVERIFY(window.document().id != originalDocument["id"].get<std::string>());
        QCOMPARE(window.document().choices.at(dnd::Json::json_pointer("/features/fighter/fightingStyle/0")).get<std::string>(), std::string("srd55:defense"));
        const auto recoveryBase = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/untitled.dnd.json";
        const QString copyRecovery = QString::fromStdString(dnd::autosavePath(recoveryBase.toStdString()).string());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(copyRecovery), 3500);
        QCOMPARE(dnd::loadCharacter(copyRecovery.toStdString()).document.moduleVersion, std::string("2.0.0"));
        QVERIFY(!QFile::exists(QString::fromStdString(dnd::autosavePath(originalPath.toStdString()).string())));
        QVERIFY(!window.saveTo(originalPath));
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), originalBytes); original.close();
        const QString copyPath = directory.path() + "/expanded-copy.json";
        QVERIFY2(window.saveTo(copyPath), qPrintable(window.lastError()));
        QVERIFY(window.openPath(originalPath, false));
        QCOMPARE(dnd::toJson(window.document()), originalDocument);
        QVERIFY(window.openPath(copyPath, false));
        QCOMPARE(window.document().moduleVersion, std::string("2.0.0"));
    }

    void genericDiceFollowRequestsAndMulticlassHitDice() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        window.newDocument("srd55", "2.0.0");
        QVERIFY(window.setChoice("/classId", "srd55:fighter"));
        QVERIFY(window.setChoice("/abilityMethod", "rolled"));
        const auto ability = std::find_if(window.evaluation().rollRequests.begin(), window.evaluation().rollRequests.end(), [](const dnd::RollRequest& request) { return request.path == "/abilities/strength"; });
        QVERIFY(ability != window.evaluation().rollRequests.end());
        const auto abilityRequest = *ability;
        QCOMPARE(abilityRequest.count, 4); QCOMPARE(abilityRequest.sides, 6); QCOMPARE(abilityRequest.dropLowest, 1);
        QVERIFY(window.acceptRoll(abilityRequest.id));
        const auto record = window.document().rolls["requests"][abilityRequest.id];
        QCOMPARE(record["dice"].size(), std::size_t(4)); QCOMPARE(record["kept"].size(), std::size_t(3));
        QCOMPARE(window.document().choices["abilities"]["strength"], record["total"]);
        const auto accepted = dnd::toJson(window.document());
        QVERIFY(!window.acceptRoll(abilityRequest.id));
        QCOMPARE(dnd::toJson(window.document()), accepted);
        QVERIFY(window.acceptRoll(abilityRequest.id, true));
        QCOMPARE(window.document().rolls["history"].size(), std::size_t(1));
        QVERIFY(window.setChoice("/abilities", {{"strength",15},{"dexterity",14},{"constitution",13},{"intelligence",15},{"wisdom",12},{"charisma",10}}));
        QVERIFY(window.setChoice("/level", 3));
        QVERIFY(window.setChoice("/multiclass", true));
        QVERIFY(window.setChoice("/advancement/2/classId", "srd55:wizard"));
        QVERIFY(window.setChoice("/advancement/3/classId", "srd55:fighter"));
        QVERIFY(window.setChoice("/hpMethod", "rolled"));
        const auto requests = window.evaluation().rollRequests;
        bool rolledWizard = false, rolledFighter = false;
        for (const auto& request : requests) {
            if (request.path == "/hp/2") { QCOMPARE(request.sides, 6); QVERIFY(window.acceptRoll(request.id)); rolledWizard = true; }
            if (request.path == "/hp/3") { QCOMPARE(request.sides, 10); QVERIFY(window.acceptRoll(request.id)); rolledFighter = true; }
        }
        QVERIFY(rolledWizard && rolledFighter);
        const auto rolls = window.document().rolls;
        const auto choices = window.document().choices;
        const QString path = directory.path() + "/multiclass-rolls.json";
        QVERIFY(window.saveTo(path));
        QVERIFY(window.openPath(path, false));
        QCOMPARE(window.document().rolls, rolls);
        QCOMPARE(window.document().choices, choices);
    }

    void currentEffectsAndCapacitiesStaySeparateFromChoices() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        window.newDocument("srd55", "2.0.0");
        QVERIFY(window.setChoice("/classId", "srd55:barbarian"));
        QVERIFY(window.setChoice("/level", 2));
        window.setAdvanced(true);
        std::string effectPath;
        int effectStage = -1;
        for (std::size_t i = 0; i < window.evaluation().stages.size(); ++i)
            for (const auto& field : window.evaluation().stages[i].fields)
                if (field.scope == "resources" && field.kind == "boolean") { effectPath = field.path; effectStage = static_cast<int>(i) + 1; }
        QVERIFY(effectStage > 0);
        auto* stages = window.findChild<QListWidget*>("builderStages"); QVERIFY(stages); stages->setCurrentRow(effectStage);
        auto* effect = window.findChild<QCheckBox*>(QString::fromStdString("resources:" + effectPath)); QVERIFY(effect);
        const auto choices = window.document().choices;
        const auto advancement = window.document().advancement;
        effect->setChecked(true);
        QVERIFY(window.document().resources.at(dnd::Json::json_pointer(effectPath)).get<bool>());
        QCOMPARE(window.document().choices, choices);
        QCOMPARE(window.document().advancement, advancement);
        const auto resourcesBefore = window.document().resources;
        window.setAdvanced(false);
        QCOMPARE(window.document().resources, resourcesBefore);
        const auto capacity = std::find_if(window.evaluation().resources.begin(), window.evaluation().resources.end(), [](const dnd::ResourceDefinition& definition) { return definition.maximum > 0; });
        QVERIFY(capacity != window.evaluation().resources.end());
        const auto definition = *capacity;
        auto* resourcesButton = window.findChild<QPushButton*>("editResources"); QVERIFY(resourcesButton);
        QTimer::singleShot(0, &window, [&] {
            if (auto* dialog = window.findChild<QDialog*>("currentResourcesDialog")) if (auto* buttons = dialog->findChild<QDialogButtonBox*>()) buttons->button(QDialogButtonBox::Save)->click();
        });
        resourcesButton->click();
        QCOMPARE(window.document().resources, resourcesBefore); // An untouched dialog never refills anything.
        bool edited = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("currentResourcesDialog");
            if (!dialog) return;
            if (auto* value = dialog->findChild<QSpinBox*>(QString::fromStdString("resource:" + definition.id))) { value->setValue(definition.maximum - 1); edited = true; }
            if (auto* buttons = dialog->findChild<QDialogButtonBox*>()) buttons->button(QDialogButtonBox::Save)->click();
        });
        resourcesButton->click(); QVERIFY(edited);
        QCOMPARE(window.document().resources[definition.id].get<int>(), definition.maximum - 1);
        const QString path = directory.path() + "/current-resources.json";
        QVERIFY(window.saveTo(path)); QVERIFY(window.openPath(path, false));
        QCOMPARE(window.document().resources[definition.id].get<int>(), definition.maximum - 1);
        QCOMPARE(window.document().choices, choices);
    }

    void appendedSpellbookContentsAreVisibleBeforeApply() {
        QTemporaryDir directory;
        const auto rules = srd55fixtures::rules();
        auto fixture = srd55fixtures::complete("wizard", 1, rules);
        QVERIFY2(fixture.evaluation.complete(), srd55fixtures::errors(fixture.evaluation).c_str());
        auto document = fixture.document;
        const auto applyCommand = [&](const std::string& id, const dnd::Json& inputs = dnd::Json::object()) {
            const auto result = dnd::executeCommand(document, rules, {id, inputs});
            if (!result.valid()) return false;
            document = result.document;
            return true;
        };
        QVERIFY(applyCommand("srd55.inventory.initialize"));
        QVERIFY(applyCommand("srd55.history.accept-baseline"));
        std::string original;
        for (const auto& item : document.resources["inventory"]["instances"])
            if (item["itemId"] == "srd55:spellbook") original = item["id"];
        QVERIFY(!original.empty());
        QVERIFY(applyCommand("srd55.spellbooks.initialize", {{"instanceId", original}}));
        QVERIFY(applyCommand("srd55.inventory.acquire", {{"itemId", "srd55:spellbook"}, {"quantity", 1},
            {"source", "gift"}, {"paidCp", 0}, {"reason", "Campaign supplies a blank backup"}}));
        const std::string backup = document.resources["inventory"]["instances"].back()["id"];
        QVERIFY(applyCommand("srd55.spellbooks.register", {{"instanceId", backup}}));
        const auto before = dnd::toJson(document);
        const QString filename = directory.path() + "/books.json";
        dnd::saveCharacter(filename.toStdString(), document);
        dnd::MainWindow window;
        QVERIFY(window.openPath(filename, false));
        bool previewed = false, sawSpells = false, sourcePreserved = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("characterActionsDialog");
            if (!dialog) return;
            auto* picker = dialog->findChild<QComboBox*>("actionPicker");
            if (!picker) { dialog->reject(); return; }
            picker->setCurrentIndex(picker->findData("srd55.spellbooks.copy"));
            auto* source = dialog->findChild<QComboBox*>("actionInput:/sourceId");
            auto* target = dialog->findChild<QComboBox*>("actionInput:/destinationId");
            auto* spells = dialog->findChild<QListWidget*>("actionInput:/spells");
            auto* minutes = dialog->findChild<QSpinBox*>("actionInput:/minutes");
            auto* cost = dialog->findChild<QSpinBox*>("actionInput:/paidCp");
            if (!source || !target || !spells || !minutes || !cost) { dialog->reject(); return; }
            source->setCurrentIndex(source->findData(QString::fromStdString(original)));
            target->setCurrentIndex(target->findData(QString::fromStdString(backup)));
            for (int i = 0; i < spells->count(); ++i)
                if (spells->item(i)->data(Qt::UserRole) == "srd55:alarm" ||
                    spells->item(i)->data(Qt::UserRole) == "srd55:burning-hands")
                    spells->item(i)->setCheckState(Qt::Checked);
            minutes->setValue(120); cost->setValue(2000); // Two first-level spells: 2 hours, 20 GP.
            dialog->findChild<QPushButton*>("previewActionButton")->click();
            previewed = dialog->findChild<QPushButton*>("applyActionButton")->isEnabled();
            const auto text = dialog->findChild<QTextBrowser*>("actionChanges")->toPlainText();
            sawSpells = text.contains("Alarm, Burning Hands") && text.contains("2000") && !text.contains("spells · -");
            sourcePreserved = dnd::toJson(window.document()) == before;
            dialog->reject();
        });
        window.findChild<QAction*>("characterActionsAction")->trigger();
        QVERIFY(previewed); QVERIFY(sawSpells); QVERIFY(sourcePreserved);
        QCOMPARE(dnd::toJson(window.document()), before);
        QCOMPARE(dnd::loadCharacter(filename.toStdString()).original, before);
    }

    void actionPreviewIsPureAndOnlyApplyCommits() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        QVERIFY(openActionFixture(window, directory.path()+"/actions.json"));
        const auto before = dnd::toJson(window.document());
        auto* open = window.findChild<QAction*>("characterActionsAction"); QVERIFY(open && open->isEnabled());
        bool previewValid = false, unchangedBeforeApply = false, showedChanges = false;
        auto review = [&](bool commit) {
            QTimer::singleShot(0, &window, [&, commit] {
                auto* dialog = window.findChild<QDialog*>("characterActionsDialog"); if (!dialog) return;
                auto* picker = dialog->findChild<QComboBox*>("actionPicker");
                if (!picker) { dialog->reject(); return; }
                picker->setCurrentIndex(picker->findData("srd55.resources.spend"));
                auto* resource = dialog->findChild<QComboBox*>("actionInput:/resource");
                auto* amount = dialog->findChild<QSpinBox*>("actionInput:/amount");
                auto* preview = dialog->findChild<QPushButton*>("previewActionButton");
                auto* apply = dialog->findChild<QPushButton*>("applyActionButton");
                if (!resource || !amount || !preview || !apply) { dialog->reject(); return; }
                resource->setCurrentIndex(resource->findData("hp")); amount->setValue(1);
                preview->click();
                previewValid = apply->isEnabled();
                unchangedBeforeApply = dnd::toJson(window.document()) == before && !window.dirty();
                auto* changes = dialog->findChild<QTextBrowser*>("actionChanges");
                showedChanges = changes && changes->toPlainText().contains("History") && changes->toPlainText().contains("13");
                if (commit && previewValid) apply->click(); else dialog->reject();
                if (dialog->isVisible()) dialog->reject();
            });
            open->trigger();
        };
        review(false);
        QVERIFY(previewValid && unchangedBeforeApply && showedChanges);
        QCOMPARE(dnd::toJson(window.document()), before);
        review(true);
        QVERIFY(previewValid && unchangedBeforeApply);
        QCOMPARE(window.document().resources["hp"].get<int>(), 13);
        QCOMPARE(window.document().advancement.size(), std::size_t(1));
        QCOMPARE(window.document().advancement.back()["action"].get<std::string>(), std::string("srd55.resources.spend"));
        QVERIFY(window.dirty());
        const auto after = dnd::toJson(window.document());
        QVERIFY(!window.executeAction("srd55.resources.spend", {{"resource","hp"},{"amount",1000}}));
        QCOMPARE(dnd::toJson(window.document()), after);
        QVERIFY(!window.executeAction("unannounced.action", {}));
        QCOMPARE(dnd::toJson(window.document()), after);
        const QString save = directory.path()+"/applied.json";
        QVERIFY(window.saveTo(save)); QVERIFY(window.openPath(save, false));
        QCOMPARE(dnd::toJson(window.document()), after);
    }

    void staleActionPreviewCannotOverwriteNewerState() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        QVERIFY(openActionFixture(window, directory.path()+"/stale.json"));
        auto* open = window.findChild<QAction*>("characterActionsAction"); QVERIFY(open);
        bool validBeforeChange = false, prevented = false;
        QTimer::singleShot(0, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("characterActionsDialog"); if (!dialog) return;
            auto* picker = dialog->findChild<QComboBox*>("actionPicker"); if (!picker) { dialog->reject(); return; }
            picker->setCurrentIndex(picker->findData("srd55.resources.spend"));
            auto* resource = dialog->findChild<QComboBox*>("actionInput:/resource"); auto* preview = dialog->findChild<QPushButton*>("previewActionButton"); auto* apply = dialog->findChild<QPushButton*>("applyActionButton");
            if (!resource || !preview || !apply) { dialog->reject(); return; }
            resource->setCurrentIndex(resource->findData("hp")); preview->click(); validBeforeChange = apply->isEnabled();
            window.setResource("/notes", "A newer edit made after preview");
            const auto newer = dnd::toJson(window.document());
            apply->click();
            prevented = dnd::toJson(window.document()) == newer && window.lastError().contains("changed after this preview") && !apply->isEnabled();
            dialog->reject();
        });
        open->trigger();
        QVERIFY(validBeforeChange && prevented);
        QVERIFY(!window.document().resources.contains("hp"));
        QVERIFY(window.document().advancement.empty());
    }

    void actionDefaultsAreVisibleAndRemainEditableBeforePreview() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        QVERIFY(openActionFixture(window,directory.path()+"/defaults.json"));
        const auto before = dnd::toJson(window.document());
        auto* open = window.findChild<QAction*>("characterActionsAction"); QVERIFY(open);
        bool defaultsVisible = false, partialRestValid = false, unchanged = false;
        QTimer::singleShot(0,&window,[&] {
            auto* dialog=window.findChild<QDialog*>("characterActionsDialog"); if(!dialog)return;
            auto* picker=dialog->findChild<QComboBox*>("actionPicker"); if(!picker){dialog->reject();return;}
            picker->setCurrentIndex(picker->findData("srd55.resources.long-rest"));
            auto* hours=dialog->findChild<QSpinBox*>("actionInput:/hours");
            auto* sleep=dialog->findChild<QSpinBox*>("actionInput:/sleepHours");
            auto* waiting=dialog->findChild<QSpinBox*>("actionInput:/hoursSincePrevious");
            auto* unfinished=dialog->findChild<QCheckBox*>("actionInput:/unfinished");
            auto* preview=dialog->findChild<QPushButton*>("previewActionButton");
            auto* apply=dialog->findChild<QPushButton*>("applyActionButton");
            if(!hours||!sleep||!waiting||!unfinished||!preview||!apply){dialog->reject();return;}
            defaultsVisible=hours->value()==8 && sleep->value()==6 && waiting->value()==16 && !unfinished->isChecked();
            hours->setValue(1);sleep->setValue(0);unfinished->setChecked(true);preview->click();
            auto* messages=dialog->findChild<QTextBrowser*>("actionChanges");
            partialRestValid=apply->isEnabled() && messages && messages->toPlainText().contains("Short Rest benefits");
            unchanged=dnd::toJson(window.document())==before;dialog->reject();
        });
        open->trigger();
        QVERIFY(defaultsVisible);QVERIFY(partialRestValid);QVERIFY(unchanged);
        QCOMPARE(dnd::toJson(window.document()),before);
    }

    void restAndHitDieActionsRecordInputsAndPreserveCreation() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        const QString path = directory.path()+"/rest.json";
        QVERIFY(openActionFixture(window, path));
        const auto choices = window.document().choices;
        const auto acceptedAbilities = window.document().rolls["abilities"];
        QVERIFY(window.executeAction("srd55.resources.spend", {{"resource","hp"},{"amount",1}}));
        const auto damaged = dnd::toJson(window.document());
        QVERIFY(!window.executeAction("srd55.resources.short-rest", {{"hours",1},{"interrupted",true}}));
        QCOMPARE(dnd::toJson(window.document()), damaged);
        QVERIFY(window.executeAction("srd55.resources.short-rest", {{"hours",1},{"interrupted",false}}));
        QCOMPARE(window.document().resources["hp"].get<int>(), 13);
        QVERIFY(window.executeAction("srd55.resources.spend-hit-die", {{"die","hitDice.d10"},{"roll",4}}));
        QCOMPARE(window.document().resources["hp"].get<int>(), 14);
        QCOMPARE(window.document().resources["hitDice.d10"].get<int>(), 0);
        QCOMPARE(window.document().rolls["actions"].back()["result"].get<int>(), 4);
        QVERIFY(window.executeAction("srd55.resources.long-rest", {{"hours",8},{"sleepHours",6},{"hoursSincePrevious",0},{"interruptions",0},{"unfinished",false}}));
        QCOMPARE(window.document().resources["hitDice.d10"].get<int>(), 1);
        QCOMPARE(window.document().choices, choices);
        QCOMPARE(window.document().rolls["abilities"], acceptedAbilities);
        QCOMPARE(window.document().advancement.size(), std::size_t(4));
        const auto after = dnd::toJson(window.document());
        const QString recovery = QString::fromStdString(dnd::autosavePath(path.toStdString()).string());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(recovery), 3500);
        QCOMPARE(dnd::toJson(dnd::loadCharacter(recovery.toStdString()).document), after);
        QVERIFY(window.saveTo(path)); QVERIFY(window.openPath(path, false));
        QCOMPARE(dnd::toJson(window.document()), after);
    }

    void wizardCopyActionUsesCurrentFundsAndValidatedSpell() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        QVERIFY(openActionFixture(window, directory.path()+"/wizard-actions.json", "wizard", 1));
        QVERIFY2(window.executeAction("srd55.inventory.initialize"), qPrintable(window.lastError()));
        const int funds = window.document().resources["currencyCp"].get<int>();
        std::string spell;
        for (const auto& action : window.evaluation().actions) if (action.id == "srd55.spells.copy")
            for (const auto& field : action.fields) if (field.path == "/spellId")
                for (const auto& choice : field.options) if (choice.available) { spell = choice.id; break; }
        QVERIFY(!spell.empty());
        const auto before = dnd::toJson(window.document());
        const dnd::Json inputs = {{"spellId",spell},{"minutes",120},{"paidCp",5000},{"sourceNote","Found in the ruined observatory spellbook"}};
        auto insufficient = inputs; insufficient["paidCp"] = 4999;
        QVERIFY(!window.executeAction("srd55.spells.copy", insufficient));
        QCOMPARE(dnd::toJson(window.document()), before);
        QVERIFY2(window.executeAction("srd55.spells.copy", inputs), qPrintable(window.lastError()));
        QCOMPARE(window.document().resources["currencyCp"].get<int>(), funds-5000);
        const auto& record = window.document().choices["spellcasting"]["wizard"]["copiedSpells"].back();
        QCOMPARE(record["spellId"].get<std::string>(), spell);
        QCOMPARE(record["paidCp"].get<int>(), 5000);
        QVERIFY(window.evaluation().complete());
        const auto after = dnd::toJson(window.document());
        QVERIFY(!window.executeAction("srd55.spells.copy", inputs));
        QCOMPARE(dnd::toJson(window.document()), after);
    }

    void inventoryAcquisitionEquipmentAndAttunementUseCommands() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        const QString path = directory.path()+"/inventory-actions.json";
        QVERIFY(openActionFixture(window,path));
        QVERIFY2(window.executeAction("srd55.inventory.initialize"), qPrintable(window.lastError()));
        const auto funds = window.document().resources["currencyCp"].get<int>();
        const int normalAc = window.evaluation().find("armorClass")->effective.get<int>();
        QVERIFY2(window.executeAction("srd55.inventory.acquire", {{"itemId","srd55:longsword"},{"quantity",1},{"source","purchase"},{"paidCp",1500},{"reason","Purchased from the village smith"},{"identified",true}}), qPrintable(window.lastError()));
        QCOMPARE(window.document().resources["currencyCp"].get<int>(), funds-1500);
        const auto sword = window.document().resources["inventory"]["instances"].back()["id"].get<std::string>();
        QVERIFY2(window.executeAction("srd55.inventory.equip", {{"instanceId",sword},{"slot","main-hand"}}), qPrintable(window.lastError()));
        QVERIFY(window.document().resources["inventory"]["instances"].back()["equipped"].get<bool>());
        QVERIFY2(window.executeAction("srd55.inventory.acquire", {{"itemId","srd55:magic-cloak-of-protection"},{"quantity",1},{"source","gift"},{"paidCp",0},{"reason","A recorded campaign reward"},{"identified",true}}), qPrintable(window.lastError()));
        const auto cloak = window.document().resources["inventory"]["instances"].back()["id"].get<std::string>();
        QVERIFY(window.executeAction("srd55.inventory.equip", {{"instanceId",cloak},{"slot","cloak"}}));
        QCOMPARE(window.evaluation().find("armorClass")->effective.get<int>(), normalAc);
        const auto beforeAttunement = dnd::toJson(window.document());
        QVERIFY(!window.executeAction("srd55.inventory.attune", {{"instanceId",cloak},{"completedShortRest",false}}));
        QCOMPARE(dnd::toJson(window.document()), beforeAttunement);
        QVERIFY2(window.executeAction("srd55.inventory.attune", {{"instanceId",cloak},{"completedShortRest",true}}), qPrintable(window.lastError()));
        QCOMPARE(window.evaluation().find("armorClass")->effective.get<int>(), normalAc+1);
        QVERIFY(window.document().resources["inventory"]["instances"].back()["attuned"].get<bool>());
        QCOMPARE(window.document().advancement.size(), std::size_t(6));
        const auto after = dnd::toJson(window.document());
        QVERIFY(window.saveTo(path)); QVERIFY(window.openPath(path,false));
        QCOMPARE(dnd::toJson(window.document()), after);
        QCOMPARE(window.evaluation().find("armorClass")->effective.get<int>(), normalAc+1);
    }

    void historyLocksPreventDirectAndAncestorEdits() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        QVERIFY(openActionFixture(window, directory.path()+"/history.json"));
        QVERIFY2(window.executeAction("srd55.history.accept-baseline"), qPrintable(window.lastError()));
        const auto before = dnd::toJson(window.document());
        QVERIFY(!window.setChoice("/abilities/strength", 18));
        QCOMPARE(dnd::toJson(window.document()), before);
        auto abilities = window.document().choices["abilities"]; abilities["strength"] = 18;
        QVERIFY(!window.setChoice("/abilities", abilities));
        QCOMPARE(dnd::toJson(window.document()), before);
        auto* stages = window.findChild<QListWidget*>("builderStages"); QVERIFY(stages);
        bool checkedControl = false;
        for (std::size_t i=0; i<window.evaluation().stages.size(); ++i)
            for (const auto& field : window.evaluation().stages[i].fields) if (field.path == "/abilities/strength") {
                QVERIFY(!field.editable); QVERIFY(!field.readOnlyReason.empty()); stages->setCurrentRow(static_cast<int>(i)+1);
                auto* input = window.findChild<QSpinBox*>("/abilities/strength"); checkedControl = input && !input->isEnabled();
            }
        QVERIFY(checkedControl);
    }

    void permittedSpellReplacementUsesPrefilledTypedActionForm() {
        QTemporaryDir directory;
        dnd::MainWindow window;
        QVERIFY(openActionFixture(window,directory.path()+"/spell-history.json","wizard",1));
        QVERIFY2(window.executeAction("srd55.history.accept-baseline"),qPrintable(window.lastError()));
        QVERIFY2(window.executeAction("srd55.resources.long-rest",{{"hours",8},{"sleepHours",6},{"hoursSincePrevious",16},{"interruptions",0},{"unfinished",false}}),qPrintable(window.lastError()));
        const auto prepared=window.document().choices["spellcasting"]["wizard"]["preparedSpells"];
        std::string actionId,newSpell;
        for(const auto& action:window.evaluation().actions)if(action.id.starts_with("srd55.history.replace.")&&action.label=="Change Wizard prepared spells"){
            QVERIFY(action.available);actionId=action.id;
            QCOMPARE(action.initialInputs.at("value"),prepared);
            for(const auto& field:action.fields)for(const auto& option:field.options)if(option.available&&std::find(prepared.begin(),prepared.end(),dnd::Json(option.id))==prepared.end()){newSpell=option.id;break;}
        }
        QVERIFY(!actionId.empty()&&!newSpell.empty());
        const auto before=dnd::toJson(window.document());
        auto* open=window.findChild<QAction*>("characterActionsAction");QVERIFY(open);
        bool prefilled=false,previewPure=false,applied=false;
        QTimer::singleShot(0,&window,[&]{
            auto* dialog=window.findChild<QDialog*>("characterActionsDialog");if(!dialog)return;
            auto* picker=dialog->findChild<QComboBox*>("actionPicker");if(!picker){dialog->reject();return;}
            picker->setCurrentIndex(picker->findData(QString::fromStdString(actionId)));
            auto* list=dialog->findChild<QListWidget*>("actionInput:/value");auto* preview=dialog->findChild<QPushButton*>("previewActionButton");auto* apply=dialog->findChild<QPushButton*>("applyActionButton");
            if(!list||!preview||!apply){dialog->reject();return;}
            int checked=0;for(int i=0;i<list->count();++i)checked+=list->item(i)->checkState()==Qt::Checked;
            prefilled=checked==static_cast<int>(prepared.size());
            for(int i=0;i<list->count();++i){const auto id=list->item(i)->data(Qt::UserRole).toString().toStdString();if(id==prepared.front().get<std::string>())list->item(i)->setCheckState(Qt::Unchecked);if(id==newSpell)list->item(i)->setCheckState(Qt::Checked);}
            preview->click();previewPure=dnd::toJson(window.document())==before;
            if(apply->isEnabled()){apply->click();applied=true;}else dialog->reject();
            if(dialog->isVisible())dialog->reject();
        });
        open->trigger();QVERIFY(prefilled&&previewPure&&applied);
        // A replacement that needs no additional choices is committed by the
        // module in the same reviewed transition; no extra commit is announced.
        QVERIFY(std::none_of(window.evaluation().actions.begin(),window.evaluation().actions.end(),[](const dnd::ActionDefinition& action){return action.id=="srd55.history.commit";}));
        const auto after=window.document().choices["spellcasting"]["wizard"]["preparedSpells"];
        QCOMPARE(after.size(),prepared.size());
        QVERIFY(std::find(after.begin(),after.end(),dnd::Json(newSpell))!=after.end());
        QVERIFY(std::find(after.begin(),after.end(),prepared.front())==after.end());
        QVERIFY(window.evaluation().complete());
    }

    void malformedJsonActionInputNeverReachesApply() {
        // The UI must parse structured input, while the engine still independently
        // rejects fields that the real module did not announce for that action.
        const auto rules = srd55fixtures::rules(); const auto fixture = srd55fixtures::complete("fighter",1,rules);
        QVERIFY(fixture.evaluation.complete());
        auto announced = fixture.evaluation;
        announced.actions = {dnd::ActionDefinition{"srd55.resources.spend","Structured-input boundary test","Verify that malformed JSON cannot be previewed.",{{"/payload","Payload","json"}},true,{},{}}};
        int applied = 0;
        dnd::ActionDialog dialog(fixture.document,rules,announced,[&](const dnd::TransitionResult&,const dnd::Json&) { ++applied; return QString{}; });
        auto* input = dialog.findChild<QPlainTextEdit*>("actionInput:/payload"); auto* preview = dialog.findChild<QPushButton*>("previewActionButton"); auto* apply = dialog.findChild<QPushButton*>("applyActionButton");
        QVERIFY(input && preview && apply);
        input->setPlainText("{broken"); QVERIFY(!preview->isEnabled()); QVERIFY(!apply->isEnabled()); QCOMPARE(applied,0);
        input->setPlainText("{\"amount\":1}"); QVERIFY(preview->isEnabled());
        preview->click(); QVERIFY(!apply->isEnabled()); QCOMPARE(applied,0);
    }
};

QTEST_MAIN(DesktopWorkflow)
#include "test_gui.moc"
