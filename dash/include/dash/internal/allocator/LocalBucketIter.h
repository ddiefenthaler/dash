#ifndef DASH__INTERNAL__ALLOCATOR__LOCAL_BUCKET_ITER_H__INCLUDED
#define DASH__INTERNAL__ALLOCATOR__LOCAL_BUCKET_ITER_H__INCLUDED

#include <dash/dart/if/dart.h>
#include <dash/Types.h>
#include <dash/internal/Logging.h>
#include <dash/internal/allocator/GlobDynamicMemTypes.h>

#include <type_traits>
#include <list>
#include <vector>
#include <iterator>
#include <sstream>
#include <iostream>
#include <memory>


namespace dash {
namespace internal {

/**
 * Iterator on local buckets. Represents local pointer type.
 */
template<
  typename ElementType,
  typename IndexType,
  typename PointerType   = ElementType *,
  typename ReferenceType = ElementType & >
class LocalBucketIter
: public std::iterator<
           std::random_access_iterator_tag,
           ElementType,
           IndexType,
           PointerType,
           ReferenceType >
{
  template<typename E_, typename I_, typename P_, typename R_>
    friend class LocalBucketIter;

private:
  typedef LocalBucketIter<ElementType, IndexType>
    self_t;

public:
  typedef IndexType                                              index_type;
  typedef typename std::make_unsigned<index_type>::type           size_type;

/// Type definitions required for std::iterator_traits:
public:
  typedef std::random_access_iterator_tag                 iterator_category;
  typedef IndexType                                         difference_type;
  typedef ElementType                                            value_type;
  typedef ElementType *                                             pointer;
  typedef ElementType &                                           reference;

  typedef glob_dynamic_mem_bucket_type<size_type, value_type>   bucket_type;

private:
  typedef typename std::list<bucket_type>
    bucket_list;

  typedef typename
    std::conditional<
      std::is_const<value_type>::value,
      typename bucket_list::const_iterator,
      typename bucket_list::iterator
    >::type
    bucket_iterator;

public:
  template<typename BucketIterator>
  LocalBucketIter(
    const BucketIterator & bucket_first,
    const BucketIterator & bucket_last,
    index_type             position,
    const BucketIterator & bucket_it,
    index_type             bucket_phase)
  : _bucket_first(bucket_first),
    _bucket_last(bucket_last),
    _idx(position),
    _bucket_it(bucket_it),
    _bucket_phase(bucket_phase),
    _is_nullptr(false)
  { }

  template<typename BucketIterator>
  LocalBucketIter(
    const BucketIterator & bucket_first,
    const BucketIterator & bucket_last,
    index_type             position)
  : _bucket_first(bucket_first),
    _bucket_last(bucket_last),
    _idx(position),
    _bucket_it(bucket_first),
    _bucket_phase(0),
    _is_nullptr(false)
  {
    DASH_LOG_TRACE_VAR("LocalBucketIter(idx)", position);
#ifdef DASH_ENABLE_TRACE_LOGGING
    index_type bucket_idx = 0;
#endif
    for (_bucket_it = _bucket_first;
         _bucket_it != _bucket_last; ++_bucket_it) {
      if (position >= _bucket_it->size) {
        position -= _bucket_it->size;
      } else {
        _bucket_phase = position;
        break;
      }
#ifdef DASH_ENABLE_TRACE_LOGGING
      ++bucket_idx;
#endif
    }
    DASH_LOG_TRACE("LocalBucketIter(idx) >",
                   "bucket:", bucket_idx,
                   "phase:",  _bucket_phase);
  }

  LocalBucketIter() = default;

  LocalBucketIter(const self_t & other)
  : _bucket_first(other._bucket_first),
    _bucket_last(other._bucket_last),
    _idx(other._idx),
    _bucket_it(other._bucket_it),
    _bucket_phase(other._bucket_phase),
    _is_nullptr(other._is_nullptr)
  { }

  self_t & operator=(const self_t & rhs)
  {
    if (this != &rhs) {
      _bucket_first = rhs._bucket_first;
      _bucket_last  = rhs._bucket_last;
      _idx          = rhs._idx;
      _bucket_it    = rhs._bucket_it;
      _bucket_phase = rhs._bucket_phase;
      _is_nullptr   = rhs._is_nullptr;
    }
    return *this;
  }

#if 0
  /**
   * Conversion to const iterator.
   */
  operator LocalBucketIter<const value_type>() const
  {
  }
#endif

  LocalBucketIter(std::nullptr_t)
  : _is_nullptr(true)
  { }

  self_t & operator=(std::nullptr_t)
  {
    _is_nullptr = true;
    return *this;
  }

  inline bool operator==(std::nullptr_t) const
  {
    return _is_nullptr;
  }

  inline bool operator!=(std::nullptr_t) const
  {
    return !_is_nullptr;
  }

  reference operator*()
  {
    DASH_ASSERT(!_is_nullptr);
    return _bucket_it->lptr[_bucket_phase];
  }

  reference operator[](index_type offset)
  {
    DASH_ASSERT(!_is_nullptr);
    if (_bucket_phase + offset < _bucket_it->size) {
      // element is in bucket currently referenced by this iterator:
      return _bucket_it->lptr[_bucket_phase + offset];
    } else {
      // find bucket containing element at given offset:
      for (auto b_it = _bucket_it; b_it != _bucket_last; ++b_it) {
        if (offset >= b_it->size) {
          offset -= b_it->size;
        } else if (offset < b_it->size) {
          return b_it->lptr[offset];
        }
      }
    }
    DASH_THROW(dash::exception::OutOfRange,
               "offset " << offset << " is out of range");
  }

  operator pointer() const
  {
    DASH_LOG_TRACE("LocalBucketIter.pointer()", "nullptr:", _is_nullptr);
    pointer lptr = nullptr;
    if (!_is_nullptr) {
      auto bucket_size = _bucket_it->size;
      DASH_LOG_TRACE("LocalBucketIter.pointer",
                     "bucket size:",  bucket_size, ",",
                     "bucket phase:", _bucket_phase);
      DASH_ASSERT_LT(
        _bucket_phase, bucket_size,
        "bucket phase out of bounds, " <<
        "got: " << _bucket_phase <<
        "max: " << bucket_size);
      lptr = _bucket_it->lptr + _bucket_phase;
    }
    DASH_LOG_TRACE_VAR("LocalBucketIter.pointer >", lptr);
    return lptr;
  }

  self_t & operator++()
  {
    increment(1);
    return *this;
  }

  self_t & operator--()
  {
    decrement(1);
    return *this;
  }

  self_t & operator++(int)
  {
    auto res = *this;
    increment(1);
    return res;
  }

  self_t & operator--(int)
  {
    auto res = *this;
    decrement(1);
    return res;
  }

  self_t & operator+=(int offset)
  {
    increment(offset);
    return *this;
  }

  self_t & operator-=(int offset)
  {
    decrement(offset);
    return *this;
  }

  self_t operator+(int offset) const
  {
    auto res = *this;
    res += offset;
    return res;
  }

  self_t operator-(int offset) const
  {
    auto res = *this;
    res -= offset;
    return res;
  }

  inline index_type operator+(
    const self_t & other) const
  {
    return _idx + other._idx;
  }

  inline index_type operator-(
    const self_t & other) const
  {
    return _idx - other._idx;
  }

  template<typename E_, typename I_, typename P_, typename R_>
  inline bool operator<(const LocalBucketIter<E_,I_,P_,R_> & other) const
  {
    return (_idx < other._idx);
  }

  template<typename E_, typename I_, typename P_, typename R_>
  inline bool operator<=(const LocalBucketIter<E_,I_,P_,R_> & other) const
  {
    return (_idx <= other._idx);
  }

  template<typename E_, typename I_, typename P_, typename R_>
  inline bool operator>(const LocalBucketIter<E_,I_,P_,R_> & other) const
  {
    return (_idx > other._idx);
  }

  template<typename E_, typename I_, typename P_, typename R_>
  inline bool operator>=(const LocalBucketIter<E_,I_,P_,R_> & other) const
  {
    return (_idx >= other._idx);
  }

  template<typename E_, typename I_, typename P_, typename R_>
  inline bool operator==(const LocalBucketIter<E_,I_,P_,R_> & other) const
  {
    return (this == std::addressof(other) || _idx == other._idx);
  }

  template<typename E_, typename I_, typename P_, typename R_>
  inline bool operator!=(const LocalBucketIter<E_,I_,P_,R_> & other) const
  {
    return !(*this == other);
  }

  constexpr bool is_local() const
  {
    return true;
  }

  index_type pos() const
  {
    return _idx;
  }

private:
  void increment(int offset)
  {
    DASH_ASSERT(!_is_nullptr);
    _idx += offset;
    if (_bucket_phase + offset < _bucket_it->size) {
      // element is in bucket currently referenced by this iterator:
      _bucket_phase += offset;
    } else {
      // find bucket containing element at given offset:
      for (; _bucket_it != _bucket_last; ++_bucket_it) {
        if (offset >= _bucket_it->size) {
          offset -= _bucket_it->size;
        } else if (offset < _bucket_it->size) {
          _bucket_phase = offset;
          break;
        }
      }
    }
    // end iterator
    if (_bucket_it == _bucket_last) {
      _bucket_phase = offset;
    }
  }

  void decrement(int offset)
  {
    DASH_ASSERT(!_is_nullptr);
    if (offset > _idx) {
      DASH_THROW(dash::exception::OutOfRange,
                 "offset " << offset << " is out of range");
    }
    _idx -= offset;
    if (offset <= _bucket_phase) {
      // element is in bucket currently referenced by this iterator:
      _bucket_phase -= offset;
    } else {
      offset -= _bucket_phase;
      // find bucket containing element at given offset:
      for (; _bucket_it != _bucket_first; --_bucket_it) {
        if (offset >= _bucket_it->size) {
          offset -= _bucket_it->size;
        } else if (offset < _bucket_it->size) {
          _bucket_phase = _bucket_it->size - offset;
          break;
        }
      }
    }
    if (_bucket_it == _bucket_first) {
      _bucket_phase = _bucket_it->size - offset;
    }
    if (false) {
      DASH_THROW(dash::exception::OutOfRange,
                 "offset " << offset << " is out of range");
    }
  }

private:
  bucket_iterator _bucket_first;
  bucket_iterator _bucket_last;
  index_type      _idx           = 0;
  bucket_iterator _bucket_it;
  index_type      _bucket_phase  = 0;
  bool            _is_nullptr    = false;

}; // class LocalBucketIter

/**
 * Resolve the number of elements between two local bucket iterators.
 *
 * \complexity  O(1)
 *
 * \ingroup     Algorithms
 */
template<
  typename ElementType,
  typename IndexType,
  class    Pointer,
  class    Reference>
auto distance(
  /// Global iterator to the first position in the global sequence
  const dash::internal::LocalBucketIter<
          ElementType, IndexType, Pointer, Reference> & first,
  /// Global iterator to the final position in the global sequence
  const dash::internal::LocalBucketIter<
          ElementType, IndexType, Pointer, Reference> & last)
-> IndexType
{
  return last - first;
}

} // namespace internal

template<
  typename ElementType,
  typename IndexType,
  class    Pointer,
  class    Reference>
std::ostream & operator<<(
  std::ostream & os,
  const dash::internal::LocalBucketIter<
          ElementType, IndexType, Pointer, Reference> & it)
{
  std::ostringstream ss;
  ElementType * lptr = it;
  ss << "dash::internal::LocalBucketIter<"
     << typeid(ElementType).name() << ">("
     << "idx:"  << it.pos() << ", "
     << "lptr:" << lptr     << ")";
  return operator<<(os, ss.str());
}

} // namespace dash

#endif // DASH__INTERNAL__ALLOCATOR__LOCAL_BUCKET_ITER_H__INCLUDED
