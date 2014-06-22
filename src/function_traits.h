#ifndef _FUNCTION_TRAITS_H
#define _FUNCTION_TRAITS_H

#include <tuple>
#include <type_traits>

namespace chc {
	/* usage: decltype(return_args(&Class::memberfunction)) */
	template<typename Base, typename Result, typename... Args>
	std::tuple<Args...> return_args(Result (Base::*func)(Args...));

	/* usage: decltype(result_of(&Class::memberfunction)) */
	template<typename Base, typename Result, typename... Args>
	Result result_of(Result (Base::*func)(Args...));

	/* usage: arg_remove_reference<0, decltype(return_args(&Class::memberfunction))> */
	template <size_t i, typename Tuple>
	using arg_remove_reference = typename std::remove_reference<typename std::tuple_element<i, Tuple>::type>::type;
}

#endif
