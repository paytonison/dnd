#include "mainwindow.hpp"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTextStream>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DungeoningADragon");
    QCoreApplication::setApplicationName("Dungeoning a Dragon");
    QCoreApplication::setApplicationVersion("0.1.0");
    QApplication::setWindowIcon(QIcon(":/icons/app-icon.png"));
    QGuiApplication::setDesktopFileName("dungeoning-a-dragon");
    dnd::MainWindow window;
    const QStringList args = app.arguments();
    if (args.contains("--export-pdf")) {
        const int index = args.indexOf("--export-pdf");
        if (index + 2 >= args.size()) {
            QTextStream(stderr) << "Usage: --export-pdf CHARACTER_FILE OUTPUT_PDF\n";
            return 1;
        }
        if (!window.openPath(args[index + 1], false) || !window.exportPdf(args[index + 2])) {
            QTextStream(stderr) << window.lastError() << '\n';
            return 1;
        }
        QTextStream(stdout) << "Exported " << args[index + 2] << '\n';
        return window.evaluation().complete() ? 0 : 2;
    }
    if (args.contains("--smoke")) {
        if (QApplication::windowIcon().isNull()) {
            QTextStream(stderr) << "Desktop acceptance FAILED: app icon resource missing.\n";
            return 1;
        }
        const int index = args.indexOf("--smoke");
        const QString output = index + 1 < args.size() ? args[index + 1] : QDir::tempPath() + "/dnd-smoke";
        QDir().mkpath(output);
        window.newDocument("bx");
        window.setChoice("/abilities/str", 16);
        window.setChoice("/abilities/int", 12);
        window.setChoice("/abilities/wis", 10);
        window.setChoice("/abilities/dex", 13);
        window.setChoice("/abilities/con", 14);
        window.setChoice("/abilities/cha", 11);
        window.setChoice("/class", "bx:fighter");
        window.setChoice("/alignment", "lawful");
        window.setChoice("/level", 2);
        window.setChoice("/xp", 2000);
        window.setChoice("/hp/0", 7);
        window.setChoice("/hp/1", 5);
        window.setChoice("/moneyRoll", 12);
        window.setChoice("/armor", "bx:chain");
        window.setChoice("/weapon", "bx:sword");
        window.setChoice("/shield", true);
        const auto& calculations = window.evaluation().calculations;
        const dnd::Calculation* target = window.evaluation().find("hp");
        if (!target) target = window.evaluation().find("hp.max");
        if (!target && !calculations.empty()) target = &calculations.front();
        if (target && target->normal.is_number_integer())
            window.applyOverride(target->id, target->normal.get<int>() + 1, "Smoke acceptance: DM grants one bonus point.");
        const QString save = output + "/acceptance.dnd.json";
        bool ok = window.evaluation().complete() && window.evaluation().find("hp.max") && window.evaluation().find("ac") &&
            window.evaluation().find("hp.max")->normal == 14 && window.evaluation().find("ac")->normal == 3 &&
            window.saveTo(save) && window.openPath(save, false) && window.exportPdf(output + "/acceptance.pdf");
        if (!ok) for (const auto& message : window.evaluation().messages) QTextStream(stderr) << QString::fromStdString(message.text) << '\n';
        if (ok) { window.show(); app.processEvents(); window.grab().save(output + "/acceptance-ui.png"); }
        QTextStream(stdout) << (ok ? "Desktop acceptance passed: " : "Desktop acceptance FAILED: ") << (ok ? output : window.lastError()) << '\n';
        return ok ? 0 : 1;
    }
    if (args.size() > 1 && QFileInfo::exists(args[1])) window.openPath(args[1]);
    window.show();
    return app.exec();
}
