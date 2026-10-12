#include "TbThreadNavigation.hpp"
#include <QRegExp>
#include <QVariantMap>
#include <limits.h>

namespace tieba {
namespace {
bool isHead(const QVariantMap &row) {
    return !row.contains("postHead") || row.value("postHead").toBool();
}
}
int TbThreadNavigation::positiveFloor(const QString &input) {
    const QString value = input.trimmed();
    if (!QRegExp(QLatin1String("[0-9]+")).exactMatch(value)) return 0;
    bool ok = false;
    const qint64 floor = value.toLongLong(&ok);
    return ok && floor > 0 && floor <= INT_MAX ? int(floor) : 0;
}
int TbThreadNavigation::estimatedPage(int floor, int pageSize) {
    return floor < 1 ? 1 : (floor - 1) / qMax(1, pageSize) + 1;
}
int TbThreadNavigation::locateFloor(const QVariantList &rows, int floor) {
    for (int i = 0; i < rows.size(); ++i) {
        const QVariantMap row = rows.at(i).toMap();
        if (isHead(row) && row.value("floor").toInt() == floor) return i;
    }
    return -1;
}
int TbThreadNavigation::locatePost(const QVariantList &rows, const QString &postId, const QString &subPostId) {
    for (int i = 0; i < rows.size(); ++i) {
        const QVariantMap row = rows.at(i).toMap();
        if (!isHead(row)) continue;
        if (!postId.isEmpty() && (row.value("id").toString() == postId || row.value("postId").toString() == postId)) return i;
        if (!subPostId.isEmpty()) {
            const QVariantList preview = row.value("subPostPreview").toList();
            for (int j = 0; j < preview.size(); ++j)
                if (preview.at(j).toMap().value("id").toString() == subPostId) return i;
        }
    }
    return -1;
}
int TbThreadNavigation::nextFloorPage(int floor, int currentPage, int totalPages,
        const QVariantList &rows, bool reverse, int &lowPage, int &highPage) {
    if (floor <= 0 || currentPage <= 0) return 0;
    int minimum = INT_MAX, maximum = 0;
    for (int i = 0; i < rows.size(); ++i) {
        const QVariantMap row = rows.at(i).toMap();
        const int number = row.value("floor").toInt();
        if (!isHead(row) || number <= 0) continue;
        minimum = qMin(minimum, number); maximum = qMax(maximum, number);
    }
    if (maximum == 0 || (floor >= minimum && floor <= maximum)) return 0;
    lowPage = qMax(1, lowPage);
    if (totalPages > 0) highPage = highPage > 0 ? qMin(highPage, totalPages) : totalPages;
    const bool needLater = reverse ? floor < minimum : floor > maximum;
    if (needLater) lowPage = qMax(lowPage, currentPage + 1);
    else highPage = highPage > 0 ? qMin(highPage, currentPage - 1) : currentPage - 1;
    if (highPage == 0 && !needLater) return 0;
    if (highPage > 0 && lowPage > highPage) return 0;
    if (highPage <= 0) return currentPage + 1;
    const int next = lowPage + (highPage - lowPage) / 2;
    return next == currentPage ? 0 : next;
}
}
