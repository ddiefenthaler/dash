#ifndef DASH__ALGORITHM__GENERATE_H__
#define DASH__ALGORITHM__GENERATE_H__

#include <dash/GlobIter.h>
#include <dash/algorithm/LocalRange.h>
#include <dash/algorithm/Operation.h>
#include <dash/dart/if/dart_communication.h>

namespace dash {

/**
 *  Assigns each element in range [first, last) a value generated by the
 *  given function object g.
 *
 *  Being a collaborative operation, each unit will invoke the given function on its local elements only.
 *
 * \tparam      ElementType  Type of the elements in the sequence
 *                           invoke, deduced from parameter \c func
 * \complexity  O(d) + O(nl), with \c d dimensions in the global iterators'
 *              pattern and \c nl local elements within the global range
 *
 * \ingroup     DashAlgorithms
 */
template<
    typename ElementType,
    class    PatternType>
void generate (
    /// Iterator to the initial position in the sequence
    GlobIter<ElementType, PatternType> first,
    /// Iterator to the final position in the sequence
    GlobIter<ElementType, PatternType> last,
    /// Generator function
    ::std::function<ElementType(void)> & g) {
    /// Global iterators to local range:
    auto lrange = dash::local_range(first, last);
    auto lfirst = lrange.begin;
    auto llast  = lrange.end;

    std::generate(lfirst, llast, g);
}

} // namespace dash

#endif // DASH__ALGORITHM__GENERATE_H__
