#pragma once
#include <jde/fwk/crypto/md5.h>

//boost::json::string converts to sv, but fmt has no formatter for it:  formatted as its text, specs and all.
template<> struct fmt::formatter<boost::json::string> : fmt::formatter<fmt::string_view>{
	α format( const boost::json::string& s, fmt::format_context& ctx )const{
		return fmt::formatter<fmt::string_view>::format( fmt::string_view{s.data(), s.size()}, ctx );
	}
};

namespace Jde::Logging{
	namespace Internal{
		//Not constexpr, so a format reaching it fails to compile, here:  a stored entry keeps one string per field, the
		//arguments in order, so each field takes the next argument - no "{0}", no "{name}" and no nested "{:{}}" - and a
		//spec is kept for the first 64.
		inline α UnsupportedLogFormat()ι->void{}

		//Walks a format string fmt has checked:  text( run ) for each stretch of literal text, braces still escaped, and
		//field( whole, spec ) for each replacement field, as "{:x}" and "x".
		constexpr α ForEachField( sv f, auto&& text, auto&& field )ι->void{
			uint run{};
			for( uint i=0; i<f.size(); ++i ){
				if( f[i]=='}' || (f[i]=='{' && i+1<f.size() && f[i+1]=='{') ){//an escaped brace, "}}" or "{{".
					++i;
					continue;
				}
				if( f[i]!='{' )
					continue;
				const auto end = f.find( '}', i );
				const auto inner = f.substr( i+1, end-i-1 );
				if( (inner.size() && inner[0]!=':') || inner.find('{')!=sv::npos )
					UnsupportedLogFormat();
				text( f.substr(run, i-run) );
				field( f.substr(i, end-i+1), inner.size() ? inner.substr(1) : sv{} );
				run = end+1;
				i = end;
			}
			text( f.substr(run) );
		}
		//The template as an entry stores it:  each field a plain {}, since its argument is stored formatted with its spec.
		constexpr α StripSpecs( sv f )ι->string{
			string y;
			ForEachField( f, [&y]( sv run ){ y += run; }, [&y]( sv, sv ){ y += "{}"; } );
			return y;
		}
		constexpr α SpecFields( sv f )ι->uint64_t{
			uint64_t y{};
			uint i{};
			ForEachField( f, []( sv ){}, [&]( sv, sv spec ){
				if( spec.size() ){
					if( i>=64 )
						UnsupportedLogFormat();
					y |= 1ull << i;
				}
				++i;
			} );
			return y;
		}
	}

	//A Format with its arguments' types erased:  what the out-of-line Emit and Entry take.
	struct FormatView final{
		α Template()Ι->string{ return Specs ? Internal::StripSpecs( Text ) : string{ Text }; }//as stored:  what Id names.
		sv Text;//as written:  what the console formats.
		uint64_t Specs;//bit i:  field i has a spec.
		StringMd5 Id;
	};
	//An argument as a stored entry keeps it, a log line's and an exception's alike, and as Log.proto's Arg stores it:  the
	//value when fmt said a plain {} field's argument was a number or a bool, otherwise its text, formatted with its field's
	//spec or as fmt's {}.
	using Argument = variant<string,int64_t,uint64_t,double,bool>;
	Γ α ToArguments( const FormatView& m, fmt::format_args args )ι->vector<Argument>;
	Γ α ArgText( const Argument& arg )ι->string;//as fmt's {} renders it, which is how the entry's message shows it.
	Γ α Vformat( sv text, const vector<Argument>& args )ε->string;//text, with a plain {} for each argument.

	//A log call's or an exception's format string:  fmt's checked format_string, and the id its entries are stored under -
	//the md5 of the template with its specs stripped - both settled by the compiler at the call site, so a line never hashes
	//its template.  An argument is stored formatted with its field's spec, so a stored entry renders as the console line.
	template<class... Args>
	struct BasicFormat final{
		template<class S> requires std::convertible_to<const S&, sv> && (!std::same_as<S,string>)//a string is never a constant, and would compete with Entry's string overloads.
		consteval BasicFormat( const S& s )ι:
			Fmt{ s },
			Specs{ Internal::SpecFields(sv{s}) },
			Id{ Specs ? Crypto::Md5(Internal::StripSpecs(sv{s})) : Crypto::Md5(sv{s}) }
		{}
		α Text()Ι->sv{ return sv{ Fmt.get().data(), Fmt.get().size() }; }
		α Template()Ι->string{ return View().Template(); }
		α View()Ι->FormatView{ return { Text(), Specs, Id }; }

		fmt::format_string<Args...> Fmt;
		uint64_t Specs;//bit i:  field i has a spec.
		StringMd5 Id;
	};
	//Args come from the call's arguments, never from the format, as with fmt::format_string.
	template<class... Args> using Format = BasicFormat<std::type_identity_t<Args>...>;
}