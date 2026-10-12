#include <QCoreApplication>
#include <QDeclarativeEngine>
#include <QDeclarativeContext>
#include <QDeclarativeComponent>
#include <QDeclarativeError>
#include <QtDeclarative/qdeclarative.h>
#include <QFile>
#include <QVariantMap>
#include <stdio.h>

static int checks = 0, failures = 0;
static void check(const char *label, bool ok) {
    ++checks; if (!ok) ++failures; printf("%s %s\n", ok ? "OK" : "FAIL", label);
}
static QString functionSource(const QString &fileName, const QString &name) {
    QFile file(fileName); if (!file.open(QIODevice::ReadOnly)) return QString();
    const QString text = QString::fromUtf8(file.readAll());
    const int begin = text.indexOf("function " + name + "(");
    if (begin < 0) return QString();
    int level = 0; bool started = false;
    for (int i = text.indexOf('{', begin); i < text.size(); ++i) {
        if (text.at(i) == '{') { ++level; started = true; }
        else if (text.at(i) == '}') --level;
        if (started && level == 0) return text.mid(begin, i - begin + 1);
    }
    return QString();
}
static QObject *create(QDeclarativeEngine &engine, const QString &source) {
    QDeclarativeComponent component(&engine);
    component.setData(("import NavigationTest 1.0\n" + source).toUtf8(), QUrl());
    QObject *object = component.create();
    if (!object) foreach (const QDeclarativeError &error, component.errors()) printf("%s\n", qPrintable(error.toString()));
    return object;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qmlRegisterType<QObject>("NavigationTest", 1, 0, "Object");
    const QString pushUtility = functionSource("assets/main.qml", "pushUtility");
    if (pushUtility.isEmpty()) return 2;
    for (int category = 0; category < 2; ++category) {
        QDeclarativeEngine engine;
        QObject *navigation = create(engine, "Object { property int pushes: 0; property string lastId: \"\"; property string lastTitle: \"\"; property bool validNav: false; function push(page) { pushes++; lastId = page.threadId || \"\"; lastTitle = page.threadTitle || \"\"; validNav = page.nav === navigationFixture; } }");
        QObject *controller = create(engine, "Object { property int loads: 0; property int toasts: 0; property string lastId: \"\"; property int lastPage: 0; function loadThread(id, page) { loads++; lastId = id; lastPage = page; } function showToast(text) { toasts++; } }");
        QObject *thread = create(engine, "Object { property variant nav: null; property string threadId: \"\"; property string threadTitle: \"\"; }");
        if (!navigation || !controller || !thread) return 2;
        engine.rootContext()->setContextProperty("navigationFixture", navigation);
        engine.rootContext()->setContextProperty("tieba", controller);
        engine.rootContext()->setContextProperty("threadFixture", thread);
        QObject *threadDefinition = create(engine, "Object { function createObject() { return threadFixture; } }");
        engine.rootContext()->setContextProperty("threadDef", threadDefinition);
        const QString pageFile = category == 0 ? "assets/ThreadLibraryPage.qml" : "assets/FavoritesPage.qml";
        const QString pageId = category == 0 ? "libraryPage" : "favoritesPage";
        const QString openThread = functionSource(pageFile, "openThread");
        QObject *page = create(engine, "Object { id: " + pageId + "; property variant nav: null; property string kind: \"\"; property string initialType: typeof nav; " + openThread + "\n}");
        if (!page || !threadDefinition || openThread.isEmpty()) return 2;
        check("Qt4 null variant reproduces undefined typeof", page->property("initialType").toString() == "undefined");
        engine.rootContext()->setContextProperty("pageFixture", page);
        QObject *definition = create(engine, "Object { function createObject() { return pageFixture; } }");
        engine.rootContext()->setContextProperty("definitionFixture", definition);
        QObject *root = create(engine, "Object { id: root; property variant utilityNav: null; property variant activePane: navigationFixture; " + pushUtility + "\nfunction launch() { pushUtility(definitionFixture, \"history\"); } }");
        if (!definition || !root) return 2;
        check("actual main utility launch runs", QMetaObject::invokeMethod(root, "launch", Qt::DirectConnection));
        check("main pushes utility with real navigation", navigation->property("pushes").toInt() == 1 && navigation->property("validNav").toBool());
        check("utility page receives navigation QObject", page->property("nav").value<QObject *>() == navigation);
        QVariantMap row; row.insert("threadId", "90071992547409931234"); row.insert("title", "A title");
        check("actual page openThread runs", QMetaObject::invokeMethod(page, "openThread", Qt::DirectConnection, Q_ARG(QVariant, QVariant(row))));
        check("created thread gets ID and title", thread->property("threadId").toString() == "90071992547409931234" && thread->property("threadTitle").toString() == "A title");
        check("thread page gets same NavigationPane bridge", thread->property("nav").value<QObject *>() == navigation);
        check("controller loads requested first page", controller->property("loads").toInt() == 1 && controller->property("lastId").toString() == "90071992547409931234" && controller->property("lastPage").toInt() == 1);
        check("navigation pushes newly created thread page", navigation->property("pushes").toInt() == 2 && navigation->property("lastId").toString() == "90071992547409931234" && navigation->property("validNav").toBool());
        QVariantMap oldRow; oldRow.insert("id", "123"); oldRow.insert("title", "Older item");
        QMetaObject::invokeMethod(page, "openThread", Qt::DirectConnection, Q_ARG(QVariant, QVariant(oldRow)));
        check("legacy id field also opens thread", controller->property("loads").toInt() == 2 && navigation->property("lastId").toString() == "123");
        const int beforeHeader = navigation->property("pushes").toInt();
        QMetaObject::invokeMethod(page, "openThread", Qt::DirectConnection, Q_ARG(QVariant, QVariant(QString("2026-10-12"))));
        check("date header never opens thread", navigation->property("pushes").toInt() == beforeHeader && controller->property("loads").toInt() == 2);
        if (category == 1) {
            row.insert("deleted", true);
            QMetaObject::invokeMethod(page, "openThread", Qt::DirectConnection, Q_ARG(QVariant, QVariant(row)));
            check("deleted cloud favorite stays blocked with feedback", navigation->property("pushes").toInt() == beforeHeader && controller->property("toasts").toInt() == 1);
            row.remove("deleted");
        }
        page->setProperty("nav", QVariant());
        const int beforeToast = controller->property("toasts").toInt();
        QMetaObject::invokeMethod(page, "openThread", Qt::DirectConnection, Q_ARG(QVariant, QVariant(row)));
        check("missing navigation gives feedback without loading", controller->property("toasts").toInt() == beforeToast + 1 && controller->property("loads").toInt() == 2);
        delete root; delete definition; delete page; delete threadDefinition; delete thread; delete controller; delete navigation;
    }
    printf("Utility navigation: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
