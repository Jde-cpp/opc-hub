#pragma once
#include <boost/uuid/uuid.hpp>
#include <fmt/format.h>
#include <absl/synchronization/mutex.h>

namespace Jde{
	using namespace std::literals::string_view_literals;

	using uint8=uint_fast8_t;
	using int8=int_fast8_t;

	using uint16=uint_fast16_t;
	using int16=int_fast16_t ;

	using uint32=uint_fast32_t;
	using int32=int_fast32_t;

	using uint=uint_fast64_t;
	using _int=int_fast64_t;
	using Handle=uint;
	using SessionPK=uint32;

	using Clock=std::chrono::system_clock;
	using Duration=Clock::duration;
	using TimePoint=Clock::time_point;

	using std::array;
	using std::atomic;
	using std::atomic_flag;
	using std::byte;
	using std::runtime_error;
	using std::function;
	using namespace std::literals;
	using lg = std::lock_guard<std::mutex>;
	using std::mutex;
	Τ using sp = std::shared_ptr<T>;
	Τ using wp = std::weak_ptr<T>;
	using std::string;
	using std::tuple;
	template <typename T, typename D = std::default_delete<T>> using up = std::unique_ptr<T,D>;
	using std::get;
	using std::static_pointer_cast;
	using ul=absl::MutexLock;
	using rl=absl::ReaderMutexLock;
	using sv = std::string_view;
	Τ using limits = std::numeric_limits<T>;
	using std::ranges::find;
	using std::ranges::find_if;
	using std::ranges::for_each;
	using std::variant;
	using std::move;
	using std::optional;
	using std::chrono::duration_cast;
	using std::chrono::steady_clock;
	using std::make_tuple;
	using std::nullopt;
	#define FWD(a) std::forward<decltype(a)>(a)

	template<class T, class... Args>
	requires std::constructible_from<T, Args...>
	[[gnu::artificial]]
	inline α mu( Args&&... args )noexcept(noexcept(T(std::forward<Args>(args)...)))->up<T>{
		return up<T>( new T(FWD(args)...) );
	}

	template<class T, class... Args>
	requires std::constructible_from<T, Args...>
	[[gnu::artificial]]
	inline α ms( Args&&... args )noexcept(noexcept(T(FWD(args)...)))->sp<T>{
		return std::allocate_shared<T>( std::allocator<typename std::remove_const<T>::type>(), FWD(args)... );
	}

	using std::vector;
	using PortType=unsigned short;
	//using DayIndex=uint_fast16_t;//TODO Refactor remove
	//using Day=uint_fast16_t;

	namespace fs=std::filesystem;
	using std::flat_map;
	using std::flat_multimap;
	using std::flat_set;
	using boost::concurrent_flat_map;
	using boost::concurrent_flat_set;
	using jvalue=boost::json::value;
	using jarray=boost::json::array;
	using jobject=boost::json::object;
	using jstring=boost::json::string;
	using boost::json::parse;
	using boost::json::serialize;
	using boost::noncopyable;
	using boost::uuids::uuid;
	using str = const std::string&;

	template<class T> using vec = const vector<T>&;

	using std::coroutine_handle;
	using std::suspend_never;
	using std::source_location;
	using SL = std::source_location;
	using LogEntryPK=uint;
	using StringMd5=uuid;
	static constexpr boost::uuids::uuid EmptyStringMd5 =
    {{0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
      0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e}};

	Τ struct PK{
		using Type=T;
		Type Value{};
		explicit operator bool()Ι{ return Value!=0; } //std::to_string( pk ) picked bool→int and printed "1"
		α operator !()Ι{ return Value==0; }
		α operator ==( const PK& rhs )Ι{ return Value==rhs.Value; }
		α operator !=( const PK& rhs )Ι{ return Value!=rhs.Value; }
		α operator <( const PK& rhs )Ι{ return Value<rhs.Value; }
	};
	struct UserPK final: PK<uint32>{
		const static UserPK::Type System{ std::numeric_limits<UserPK::Type>::max() };
	};

	using std::coroutine_handle;
	using std::suspend_never;


	using fmt::format;
	ψ Ƒ( fmt::format_string<Args...> fmt, Args&&... args )ε{ return fmt::format<Args...>( fmt, std::forward<Args>(args)... ); }
	enum class ELogLevel : int8{ NoLog=-1, Trace=0, Debug=1, Information=2, Warning=3, Error=4, Critical=5/*, None=6*/ };

#ifdef NDEBUG
	inline constexpr bool _debug{ false };
#else
	inline constexpr bool _debug{ true };
#endif
#ifdef _MSC_VER
	inline constexpr bool _windows{ true };
#else
	inline constexpr bool _windows{ false };
#endif
}