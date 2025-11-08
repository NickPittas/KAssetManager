#pragma once
#include <QtGlobal>
#include <functional>

namespace Utils {

// Return the smallest x in (lowFalse, highTrue] such that pred(x) is true.
// Requires: pred(lowFalse) == false and pred(highTrue) == true and highTrue > lowFalse.
inline qint64 binarySearchFirstTrue(qint64 lowFalse, qint64 highTrue, const std::function<bool(qint64)>& pred) {
    while (highTrue - lowFalse > 1) {
        const qint64 mid = lowFalse + (highTrue - lowFalse) / 2;
        if (pred(mid)) highTrue = mid; else lowFalse = mid;
    }
    return highTrue;
}

// Return the largest x in [lowTrue, highFalse) such that pred(x) is true.
// Requires: pred(lowTrue) == true and pred(highFalse) == false and highFalse > lowTrue.
inline qint64 binarySearchLastTrue(qint64 lowTrue, qint64 highFalse, const std::function<bool(qint64)>& pred) {
    while (highFalse - lowTrue > 1) {
        const qint64 mid = lowTrue + (highFalse - lowTrue) / 2;
        if (pred(mid)) lowTrue = mid; else highFalse = mid;
    }
    return lowTrue;
}

} // namespace Utils

