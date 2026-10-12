#ifndef TB_THREAD_NAVIGATION_HPP
#define TB_THREAD_NAVIGATION_HPP
#include <QString>
#include <QVariantList>

namespace tieba {
// Pure helpers: no request, model mutation, or pagination shared with subposts.
class TbThreadNavigation {
public:
    static int positiveFloor(const QString &input);
    static int estimatedPage(int floor, int pageSize = 30);
    static int locateFloor(const QVariantList &rows, int floor);
    static int locatePost(const QVariantList &rows, const QString &postId, const QString &subPostId = QString());
    // Initial bounds are low=1/high=0 (unknown). Update from each response;
    // return next page, or 0 when missing/deleted/exhausted/invalid.
    static int nextFloorPage(int floor, int currentPage, int totalPages,
            const QVariantList &rows, bool reverse, int &lowPage, int &highPage);
};
}
#endif
