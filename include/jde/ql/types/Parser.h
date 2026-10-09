#pragma once
#include <boost/json/parse_options.hpp>
#include <jde/ql/LocalSubscriptions.h>
#include <jde/ql/types/MutationQL.h>
#include <jde/ql/types/TableQL.h>


namespace Jde::QL{
	struct Subscription;
	Ξ ToString( EMutationQL type )ι->string{
		auto i = underlying(type);
		return i<MutationQLNames.size() ?
			string{ MutationQLNames[i].Verb } :
			std::to_string(i);
	}
	struct Parser{
		constexpr static uint MaxArgDepth{ boost::json::parse_options{}.max_depth };//Json::Parse's own limit: deeper args would fail it anyway.
		constexpr static uint MaxSelectionDepth{ 16 };
		Parser( string text, sv delimiters )ι: _text{move(text)}, _delimiters{delimiters}{}
		α Next()ι->string;
		α Next( char end )ε->string;
		α Peek()ι->str{ return _peekValue.empty() ? _peekValue = Next() : _peekValue; }
		α Index()ι->uint{ return _i; }
		α Trim( sv token )ι->bool;
		α Text()ι->string{ return _i<_text.size() ? _text.substr(_i) : string{}; }
		Ω ParseArgs( const string& args )ε->jobject;

		α LoadMutations( string&& command, sp<jobject> variables, bool returnRaw, const vector<sp<DB::AppSchema>>& schemas )ε->vector<MutationQL>;
		α LoadTables( string jsonName, sp<jobject> variables, const vector<sp<DB::AppSchema>>& schemas, bool returnRaw, SRCE )ε->vector<TableQL>;
		α LoadSubscriptions( sp<jobject> variables, const vector<sp<DB::AppSchema>>& schemas )ε->vector<Subscription>;
		α LoadUnsubscriptions()ε->vector<SubscriptionId>;
	private:
		α LoadTable( string jsonName, sp<jobject> variables, const vector<sp<DB::AppSchema>>& schemas, bool system=false, uint depth=0, SRCE )ε->TableQL;

		α LoadSubscription( sp<jobject> variables, const vector<sp<DB::AppSchema>>& schemas )ε->Subscription;
		α ParseArgs()ε->jobject;
		α SkipWhitespace()ι->void;
		uint _i{0};
		string _text;
		sv _delimiters;
		string _peekValue;
	};
}