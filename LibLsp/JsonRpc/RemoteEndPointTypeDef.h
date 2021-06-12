#pragma once
#include "LibLsp/lsp/lsp_diagnostic.h"
#include "LibLsp/JsonRpc/Cancellation.h"

namespace lsp {
	class ostream;
	class istream;
}

struct lsResponseMessage;
class MessageJsonHandler;
struct InMessage;
struct NotificationInMessage;
class  Endpoint;
struct RequestInMessage;
struct LspMessage;


namespace lsp {
	// internal functionality
	namespace detail {
		template <typename T>
		struct traits {
			static constexpr bool isRequest = std::is_base_of<RequestInMessage, T>::value;
			static constexpr bool isResponse = std::is_base_of<lsResponseMessage, T>::value;
			static constexpr bool isEvent = std::is_base_of<NotificationInMessage, T>::value;
		};

		// ArgTy<F>::type resolves to the first argument type of the function F.
		// F can be a function, static member function, or lambda.
		template <typename F>
		struct ArgTy {
			using type = typename ArgTy<decltype(&F::operator())>::type;
		};

		template <typename R, typename Arg>
		struct ArgTy<R(*)(Arg)> {
			using type = typename std::decay<Arg>::type;
		};

		template <typename R, typename C, typename Arg>
		struct ArgTy<R(C::*)(Arg) const> {
			using type = typename std::decay<Arg>::type;
		};






		template <typename R, typename Arg>
		struct ArgTy<R(*)(Arg,const CancelMonitor&)> {
			using type = typename std::decay<Arg>::type;
			
		};

		template <typename R, typename C, typename Arg>
		struct ArgTy<R(C::*)(Arg, const CancelMonitor&) const> {
			using type = typename std::decay<Arg>::type;
		};

	}  // namespace detail



////////////////////////////////////////////////////////////////////////////////
// ResponseOrError<T>
////////////////////////////////////////////////////////////////////////////////

// ResponseOrError holds either the response to a  request or an error
// message.
	template <typename T>
	struct ResponseOrError {
		using Request = T;

		inline ResponseOrError() = default;
		inline ResponseOrError(const T& response);
		inline ResponseOrError(T&& response);
		inline ResponseOrError(const Rsp_Error& error);
		inline ResponseOrError(Rsp_Error&& error);
		inline ResponseOrError(const ResponseOrError& other);
		inline ResponseOrError(ResponseOrError&& other);

		inline ResponseOrError& operator=(const ResponseOrError& other);
		inline ResponseOrError& operator=(ResponseOrError&& other);
		bool  IsError() const { return  is_error; }
		std::string ToJson()
		{
			if (is_error) return  error.ToJson();
			return  response.ToJson();
		}
		T response;
		Rsp_Error error;  // empty represents success.
		bool is_error;
		
	};

	template <typename T>
	ResponseOrError<T>::ResponseOrError(const T& resp) : response(resp) , is_error(false){}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(T&& resp) : response(std::move(resp)), is_error(false) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(const Rsp_Error& err) : error(err), is_error(true){}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(Rsp_Error&& err) : error(std::move(err)), is_error(true) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(const ResponseOrError& other)
		: response(other.response), error(other.error), is_error(other.is_error) {}
	template <typename T>
	ResponseOrError<T>::ResponseOrError(ResponseOrError&& other)
		: response(std::move(other.response)), error(std::move(other.error)), is_error(other.is_error) {}
	template <typename T>
	ResponseOrError<T>& ResponseOrError<T>::operator=(
		const ResponseOrError& other) {
		response = other.response;
		error = other.error;
		is_error = other.is_error;
		return *this;
	}
	template <typename T>
	ResponseOrError<T>& ResponseOrError<T>::operator=(ResponseOrError&& other) {
		response = std::move(other.response);
		error = std::move(other.error);
		is_error = other.is_error;
		return *this;
	}

}

