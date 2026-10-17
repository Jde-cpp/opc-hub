#include <jde/fwk/io/records/Days.h>
#include <charconv>
#include <jde/fwk/io/durable.h>

#define let const auto
namespace Jde::IO{
	using namespace std::chrono;
	namespace{
		//dir's subdirectories named as DayDirectory names them, a number from min to max with no padding, largest first:  so
		//the path the walk rebuilds from one is the directory's, and the number fits chrono's year, month or day.
		Ω numbered( const fs::path& dir, unsigned min, unsigned max, SL sl )ε->vector<unsigned>{
			vector<unsigned> y;
			std::error_code ec;
			for( fs::directory_iterator p{dir, ec}, end; !ec && p!=end; p.increment(ec) ){
				let name = p->path().filename().string();
				unsigned n{};
				let [last, error] = std::from_chars( name.data(), name.data()+name.size(), n );
				if( error==std::errc{} && last==name.data()+name.size() && n>=min && n<=max && std::to_string(n)==name && p->is_directory(ec) )
					y.push_back( n );
			}
			if( ec )
				throw Failed( dir, ec, sl );
			std::ranges::sort( y, std::greater{} );
			return y;
		}
	}
	α Records::DayDirectory( Day day )ι->fs::path{
		return fs::path{ std::to_string((int)day.year()) }/std::to_string( (unsigned)day.month() )/std::to_string( (unsigned)day.day() );
	}
	α Records::Walk( const fs::path& root, SL sl, const std::function<bool( Day )>& visit )ε->void{
		for( let y : numbered(root, 1601, 9999, sl) ){//UA's range of dates.
			let yearDir = root/std::to_string( y );
			for( let m : numbered(yearDir, 1, 12, sl) ){
				for( let d : numbered(yearDir/std::to_string(m), 1, 31, sl) ){
					const Day day{ year{(int)y}, month{m}, std::chrono::day{d} };
					if( day.ok() && visit(day) )
						return;
				}
			}
		}
	}
	α Records::Days( const fs::path& root, sv fileName, SL sl )ε->vector<Day>{
		vector<Day> y;
		Walk( root, sl, [&]( Day day ){
			std::error_code ec;
			if( fs::exists(root/DayDirectory(day)/fileName, ec) || ec )//one it can't tell of fails the read that opens it.
				y.push_back( day );
			return false;
		});
		std::ranges::reverse( y );
		return y;
	}
}