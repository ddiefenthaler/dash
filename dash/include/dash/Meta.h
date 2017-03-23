#ifndef DASH__META_H__INCLUDED
#define DASH__META_H__INCLUDED

#include <dash/meta/TypeInfo.h>

#include <type_traits>


#ifndef DOXYGEN

#define DASH__META__DEFINE_TRAIT__HAS_TYPE(DepType) \
  template<typename T> \
  struct has_type_##DepType { \
  private: \
    typedef char                      yes; \
    typedef struct { char array[2]; } no; \
    template<typename C> static yes test(typename C:: DepType *); \
    template<typename C> static no  test(...); \
  public: \
    static constexpr bool value = sizeof(test<T>(0)) == sizeof(yes); \
  };

#endif // DOXYGEN

namespace dash {


template<class...> struct conjunction : std::true_type { };

template<class Cond0> struct conjunction<Cond0> : Cond0 { };

template<class Cond0, class... CondN>
struct conjunction<Cond0, CondN...> 
: std::conditional<
    bool(Cond0::value),
    conjunction<CondN...>,
    Cond0 >
{ };



/**
 * Definition of type trait \c dash::has_type_iterator<T>
 * with static member \c value indicating whether type \c T provides
 * dependent type \c iterator.
 */
DASH__META__DEFINE_TRAIT__HAS_TYPE(iterator);
/**
 * Definition of type trait \c dash::has_type_const_iterator<T>
 * with static member \c value indicating whether type \c T provides
 * dependent type \c const_iterator.
 */
DASH__META__DEFINE_TRAIT__HAS_TYPE(const_iterator);
/**
 * Definition of type trait \c dash::has_type_reference<T>
 * with static member \c value indicating whether type \c T provides
 * dependent type \c reference.
 */
DASH__META__DEFINE_TRAIT__HAS_TYPE(reference);
/**
 * Definition of type trait \c dash::has_type_const_reference<T>
 * with static member \c value indicating whether type \c T provides
 * dependent type \c const_reference.
 */
DASH__META__DEFINE_TRAIT__HAS_TYPE(const_reference);
/**
 * Definition of type trait \c dash::has_type_value_type<T>
 * with static member \c value indicating whether type \c T provides
 * dependent type \c value_type.
 */
DASH__META__DEFINE_TRAIT__HAS_TYPE(value_type);


/**
 * Type trait indicating whether the specified type is eligible for
 * elements of DASH containers.
 */
template <class T>
struct is_container_compatible :
  public std::integral_constant<bool,
              std::is_standard_layout<T>::value
#if ( !defined(__CRAYC) && !defined(__GNUC__) ) || \
    ( defined(__GNUG__) && __GNUC__ >= 5 )
              // The Cray compiler (as of CCE8.5.6) does not support
              // std::is_trivially_copyable.
           && std::is_trivially_copyable<T>::value
#elif defined(__GNUG__) && __GNUC__ < 5
           && std::has_trivial_copy_constructor<T>::value
#endif
         >
{ };

/**
 * Type trait indicating whether a type can be used for global atomic
 * operations.
 */
template <typename T>
struct is_atomic_compatible
: public std::integral_constant<
           bool,
              dash::is_container_compatible<T>::value
           && sizeof(T) <= sizeof(std::size_t)
         >
{ };

} // namespace dash

#include <type_traits>
#include <functional>

#ifndef DOXYGEN

namespace dash {

/*
 * For reference, see
 *
 *   "Working Draft, C++ Extension for Ranges"
 *   http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/n4569.pdf
 *
 */

template <class I>
using reference_t = typename std::add_lvalue_reference<I>::type;

template <class I>
using rvalue_reference_t =
  decltype(std::move(std::declval<dash::reference_t<I>>()));

// void f(ValueType && val) { 
//   std::string i(move(val)); 
//   // ...
// } 
//
// ValueType val;
// std::bind(f, dash::make_adv(std::move(val)))();

template<typename T> struct adv { 
  T _value; 
  explicit adv(T && value) : _value(std::forward<T>(value)) {} 
  template<typename ...U> T && operator()(U &&...) { 
    return std::forward<T>(_value); 
  } 
}; 

template<typename T> adv<T> make_adv(T && value) { 
  return adv<T> { std::forward<T>(value) }; 
}

} // namespace dash

namespace std {
  // Must be defined in global std namespace.
  template<typename T> 
  struct is_bind_expression< dash::adv<T> > : std::true_type {}; 
} 

#endif // DOXYGEN

#endif // DASH__META_H__INCLUDED
