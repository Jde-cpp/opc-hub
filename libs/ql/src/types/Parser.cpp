#include <jde/ql/types/Parser.h>
#include <jde/db/generators/Functions.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/Introspection.h>
#include "../qlInternal.h"

#define let const auto
namespace Jde{
	flat_set<string> _systemTables{};
	flat_set<string> _systemMutations{};
	α QL::SetSystemTables( flat_set<string>&& jsonNames )ι->void{
		for( auto&& name : jsonNames )
			_systemTables.emplace( move(name) );
	}
	α QL::SetSystemMutations( flat_set<string>&& commands )ι->void{
		for( auto&& command : commands )
			_systemMutations.emplace( move(command) );
	}
	α QL::IsSystemMutation( sv command )ι->bool{ return _systemMutations.contains( string{command} ); }
	Ω isSystem( str name )ι->bool{
		return name.starts_with("__") || name.starts_with("setting") || name=="status" || name=="logs" || _systemTables.contains(name) || _systemMutations.contains(name);
	}

	α QL::Parse( string query, jobject variables, const vector<sp<DB::AppSchema>>& schemas, bool returnRaw, SL /*sl*/ )ε->RequestQL{
		Parser parser{ Str::TrimFirstLast(move(query), '{', '}'), "{}()," };
		if( parser.Trim("query") )
			returnRaw = true;
		auto name = parser.Next();
		auto vars = ms<jobject>( move(variables) );
		if( name=="subscription" )
			return RequestQL{ parser.LoadSubscriptions(vars, schemas) };
		else if( name=="unsubscribe" )
			return RequestQL{ parser.LoadUnsubscriptions() };
		else if( MutationQL::IsMutation(name) ){
			if( parser.Peek()=="{" )
				parser.Next();
			return RequestQL{ {parser.LoadMutations(name=="mutation" ? parser.Next() : move(name), vars, returnRaw, schemas)} };
		}else
			return RequestQL{ parser.LoadTables(move(name), vars, schemas, returnRaw) };
	}
	α QL::ParseSubscriptions( string query, jobject vars, const vector<sp<DB::AppSchema>>& schemas, SL sl )ε->vector<Subscription>{
		auto request = Parse( move(query), move(vars), schemas, true, sl ); THROW_IFSL( !request.IsSubscription(), "Expected subscription query." );
		return request.Subscriptions();
	}

	α QL::ParseQuery( string query, jobject variables, const vector<sp<DB::AppSchema>>& schemas, bool returnRaw, SL sl )ε->TableQL{
		auto ql = Parse( move(query), move(variables), schemas, returnRaw, sl );
		THROW_IFSL( !ql.IsQueries() || ql.Queries().size()!=1, "Expected single query." );
		return move(ql.Queries().front());
	}
	α QL::ParseM( string query, jobject variables, const vector<sp<DB::AppSchema>>& schemas, bool returnRaw, SL sl )ε->MutationQL{
		auto ql = Parse( move(query), move(variables), schemas, returnRaw, sl );
		THROW_IFSL( !ql.IsMutation() || ql.Mutations().size()!=1, "Expected single mutation." );
		return move(ql.Mutations().front());
	}
}
namespace Jde::QL{
	α Parser::SkipWhitespace()ι->void{
		while( _i<_text.size() && isspace(_text[_i]) )
			++_i;
	}

	α Parser::Next()ι->string{
		string result = move( _peekValue );
		if( result.empty() ){
			SkipWhitespace();
			if( _i<_text.size() ){
				let start = _i;
				_i = std::distance( _text.begin(), std::find_if(_text.begin()+_i, _text.end(), [this]( char ch )ι{ return isspace(ch) || _delimiters.find(ch)!=sv::npos;}) );
				result = _i==start ? _text.substr( _i++, 1 ) : _text.substr( start, _i-start );
			}
		}
		else
			_peekValue.clear();

		return result;
	};

	α Parser::Next( char end )ε->string{
		string result;
		if( _peekValue.size() ){
			_i = _i-_peekValue.size();
			_peekValue.clear();
		}
		SkipWhitespace();
		if( _i<_text.size() ){
			uint start = _i;
			for( auto ch = _text[_i]; _i<_text.size()-1 && ch!=end; ch = _text[++_i] ){
				if( ch=='"' ){//string
					bool escape{};
					for( ch = _text[++_i]; _i<_text.size() && !(ch=='"' && !escape); ch = _text[++_i] )
						escape = ch=='\\' && !escape;
					THROW_IF( _i>=_text.size(), "Expected ending quote '{}' @ '{}'.", _text, _i );
				}
			}
			++_i;
			result = _text.substr( start, _i-start );
		}
		return result;
	};

	Ω parseWhitespace( sv json, string& y )ε->uint{
		if( json.empty() )
			return 0;
		uint i{};
		for( char ch=json[i]; isspace(ch) && i<json.size(); ch=json[++i] ){
			y += ch;
			if( i+1==json.size() )
				break;
		}
		return i;
	}
	Ω parseValue( sv json, string& y )ε->uint;
	Ω parseArray( sv json, string& y )ε->uint{
		uint i=0;
		char ch = json[i++];
		ASSERT( ch=='[' );
		y += ch;
		i += parseWhitespace( json.substr(i), y );
		THROW_IF( i>=json.size(), "Expected ']' vs '{}' @ '{}'.", json, i );
		for( char ch = json[i]; ch!=']'; ch = json[i] ){
			i += parseValue( json.substr(i), y );
			i += parseWhitespace( json.substr(i), y );
			THROW_IF( i>=json.size(), "Expected ']' vs '{}' @ '{}'.", json, i );
			if( json[i]==',' )
				y += json[i++];
		}
		y += json[i++];
		return i;
	}
	Ω parseString( sv json, string& y )ε->uint{
		uint i=0;
		char ch = json[i++]; THROW_IF( i==json.size() || ch!='"', "Expected starting quote '{}' @ '{}'.", json, i );
		ASSERT( ch=='"' );
		y += ch;
		bool escape{};
		//#15: the closing quote has to be *seen*, not assumed.  The old loop read its char before testing i<size, so a quote in
		//the last position exited it the same way running out of input did - and an unterminated string then returned json.size(),
		//which parseObject added 1 to, walking substr() off the end into a std::out_of_range no wire handler catches.
		bool closed{};
		while( i<json.size() ){
			let c = json[i++];
			if( c=='"' && !escape ){
				closed = true;
				break;
			}
			escape = c=='\\' && !escape;
			//#19: a raw newline inside a string literal is escaped *here*, where we know we are inside one.  ParseArgs used to do
			//it afterwards over the whole buffer, which also caught the structural newlines parseWhitespace copies between tokens
			//- so every arg list that spanned lines came out as invalid json.
			if( c=='\n' )
				y += "\\n";
			else if( c=='\r' )
				y += "\\r";
			else
				y += c;
		}
		THROW_IF( !closed, "Expected ending quote in '{}' @ '{}'.", json, i );
		y += '"';
		return i;
	}
	Ω parseVariable( sv json, string& y )ε->uint{
		uint i=0;
		char ch = json[i++];
		ASSERT( ch=='$' );
		y += "\"\\b";
		y += ch;
		for( ch=json[i]; (isalnum(ch) || ch=='_') && i<json.size(); ch = json[++i] )
			y += ch;
		y += "\"";
		return i;
	}
	//A json number - -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)? - validated where it is read.  The old scanner took any
	//run of digits, '-' and '.', so it stopped mid-token on `1e5` and left the caller to complain about the stray 'e', while
	//`1.2.3-` went through verbatim for Json::Parse to reject: safe either way, but neither error named the real problem.
	Ω parseNumber( sv json, string& y )ε->uint{
		let isDigit = []( char c )ι->bool{ return c>='0' && c<='9'; }; //not isdigit(): a negative char is undefined there.
		uint i{};
		let digits = [&]( sv expected )->void{
			let start = i;
			for( ; i<json.size() && isDigit(json[i]); ++i );
			THROW_IF( i==start, "Expected {} vs '{}' in '{}' @ '{}'.", expected, i<json.size() ? json.substr(i,1) : sv{"end of input"}, json, i );
		};
		if( i<json.size() && json[i]=='-' )
			++i;
		let integer = i;
		digits( "a digit" );
		THROW_IF( i-integer>1 && json[integer]=='0', "Leading zeros are not allowed in '{}' @ '{}'.", json, integer );
		if( i<json.size() && json[i]=='.' ){
			++i;
			digits( "a digit after the '.'" );
		}
		if( i<json.size() && (json[i]=='e' || json[i]=='E') ){
			++i;
			if( i<json.size() && (json[i]=='+' || json[i]=='-') )
				++i;
			digits( "a digit after the exponent" );
		}
		y += json.substr( 0, i );
		return i;
	}

	//The literal at json[i] - false/null/true, and NaN - appended to y, its length returned.  One spelling for the four: each
	//used to carry its own length check, substr, compare and advance, and the length checks had drifted to literal+1, which
	//reported an unterminated *object* as "Unexpected end" at the literal instead of letting parseObject say what was missing.
	Ω parseLiteral( sv json, uint i, sv literal, string& y )ε->uint{
		THROW_IF( json.size()-i<literal.size(), "Unexpected end vs '{}' @ '{}'.", json, i );
		let found = json.substr( i, literal.size() );
		THROW_IF( found!=literal, "Expected '{}' vs '{}' in '{}' @ '{}'.", literal, found, json, i );
		y += found;
		return literal.size();
	}
	Ω parseObject( sv json, string& y )ε->uint;
	Ω parseValue( sv json, string& y )ε->uint{
		uint i=0;
		i += parseWhitespace( json.substr(i), y );
		THROW_IF( i>=json.size(), "Unexpected end vs '{}' @ '{}'.", json, i );
		char ch=json[i];
		if( ch=='{' )
			i += parseObject( json.substr(i), y );
		else if( ch=='[' )
			i += parseArray( json.substr(i), y );
		else if( ch=='"' )
			i += parseString( json.substr(i), y );
		else if ( ch=='$' )
			i += parseVariable( json.substr(i), y );
		else if( ch=='f' )
			i += parseLiteral( json, i, "false", y );
		else if( ch=='n' )
			i += parseLiteral( json, i, "null", y );
		else if( ch=='N' )
			i += parseLiteral( json, i, "NaN", y );
		else if( ch=='t' )
			i += parseLiteral( json, i, "true", y );
		else if( isdigit(ch) || ch=='-' || ch=='.' ) //'.' can't start a json number, but landing in parseNumber names it better than "unexpected character".
			i += parseNumber( json.substr(i), y );
		else if( ch!=',' )
			THROW( "Unexpected character '{}' @ '{}'.", ch, i );
		return i;
	}

	Ω parseObject( sv json, string& y )ε->uint{
		uint i=0;
		ASSERT( json[i]=='{' );
		y += json[i++];
		function<void()> memberValueParse = [&]()->void {
			i += parseWhitespace( json.substr(i), y );
			THROW_IF( i>=json.size(), "Expected object to end '{}' @ '{}'.", json, i );
			char ch = json[i];
			if( ch=='}' )
				return;
			else if( ch=='"' )
				i += parseString( json.substr(i), y )+1;
			else{
				string name{'"'};
				for( ++i; ch!=':' && i<json.size(); ch=json[i++] ){
					name += ch;
					THROW_IF( i==json.size(), "Could not find ':' in '{}' @ {}", json, i );
				}
				y += Str::RTrim( move(name) )+'"';
			}
			y += ":";
			i += parseValue( json.substr(i), y );
			i+=parseWhitespace( json.substr(i), y );
			THROW_IF( i>=json.size(), "Expected '}}' in '{}' @ '{}'.", json, i );
			if( json[i]==',' ){
				y += json[i++];
				memberValueParse();
			}
		};
		try{
			memberValueParse();
		}
		catch( std::logic_error e ){
			throw Exception( SRCE_CUR, {}, move(e), "Could not parse '{}' @ '{}'.", json, i );
		}
		THROW_IF( i>=json.size(), "Expected '}}' in {} @ '{}'.", json, i );
		THROW_IF( json[i]!='}', "Expected '}}' vs '{}' in {} @ '{}'.", json[i], json, i );
		y+=json[i++];
		return i;
	}

	α Parser::ParseArgs( const string& args )ε->jobject{
		string stringified; stringified.reserve( args.size()*2 );
		parseObject( args, stringified );
		return Json::Parse( stringified );//#19: parseString escapes the newlines that need it; the rest are structural, and json allows them.
	}
	α Parser::ParseArgs()ε->jobject{
		string params{ Next(')') };
		THROW_IF( params.empty(), "params.empty()" );
		THROW_IF( params.front()!='(', "Expected '(' vs {} @ '{}' to start function - '{}'.",  params.front(), Index()-1, Text() );
		//#15: Next(')') returns what it has when the terminator never arrives, so `logs("id"` came back as `("id"` and
		//overwriting its back produced the malformed `{"id}` that walked parseString off the end.
		THROW_IF( params.back()!=')', "Expected ')' vs '{}' @ '{}' to end function - '{}'.", params.back(), Index()-1, Text() );
		params.front()='{'; params.back() = '}';
		return ParseArgs( params );
	}

	α Parser::LoadMutations( string&& command, sp<jobject> vars, bool returnRaw, const vector<sp<DB::AppSchema>>& schemas )ε->vector<MutationQL>{
		vector<MutationQL> y;
		do{
			auto args = ParseArgs();
			let wantsResult = Peek()=="{";
			auto name = get<0>( MutationQL::ParseCommand(command) );
			let system = isSystem(name);
			auto returnCols = wantsResult ? LoadTable( move(name), vars, schemas, system ) : optional<TableQL>{};
			y.push_back( {move(command), move(args), vars, move(returnCols), returnRaw, schemas, system} );//vars is loop-invariant: copy the sp, don't move (2nd+ mutation would get a null Variables).
			command = Next();
		}while( MutationQL::IsMutation(command) );
		return y;
	}

	α Parser::LoadTable( string jsonName, sp<jobject> vars, const vector<sp<DB::AppSchema>>& schemas, bool system, SL sl )ε->TableQL{//__type(name: "Account") { fields { name type { name kind ofType{name kind} } } }
		let j = Peek()=="(" ? ParseArgs() : jobject{};

		TableQL table{ move(jsonName), j, vars, schemas, system, sl };
		if( Peek()=="{" ){//has columns
			Next();
			for( auto token = Next(); token!="}" && token.size(); token = Next() ){
				if( Peek()=="{" || Peek()=="(" ){
					//system-ness is inherited, plus re-derived for a child that is an *explicitly registered* system table
					//(SetSystemTables, e.g. gateway `serverConnections{ opcSessions{count} }`): those resolve no view and a custom
					//await grafts them, so they must take the FindView path (null DBTable is fine).  A merely system-*shaped* name
					//- the `status`/`__x` heuristics in isSystem() - under a real table ("roles{ id status{ x } }") is a bogus
					//sub-table and stays non-system so it hits GetViewPtr and throws "Could not find view", naming what it missed.
					table.Tables.push_back( LoadTable(token, vars, schemas, system || _systemTables.contains(token), sl) );
				}else{
					THROW_IF( token==",", "don't separate columns with: ',' '{}' @ '{}'.", _text, Index()-1 );
					//#43: an argument list written where a column belongs was taken literally - `{ (schema:$schemas)id slug }` came back
					//as the columns '(', 'schema:$schemas' and ')', which the select then asked the database for and addColumn refused with
					//a misleading "column not found".  A legitimate `col(args)` never reaches here: the Peek()=="(" branch above takes it.
					THROW_IF( token=="(" || token==")", "'{}' is an argument list where a column belongs in '{}' @ '{}' - arguments go on the table, before its '{{'.", token, _text, Index()-1 );
					if( token=="..." ){
						THROW_IF( "on"!=Next(), "Expected 'on' after '...' in '{}' @ '{}'.", _text, Index()-1 );
						table.InlineFragments.push_back( LoadTable(Next(), vars, schemas, system, sl) );
						continue;
					}
					table.Columns.emplace_back( ColumnQL{string{token}} );
				}
			}
		}
		return table;
	}
	α Parser::LoadTables( string jsonName, sp<jobject> vars, const vector<sp<DB::AppSchema>>& schemas, bool returnRaw, SL sl )ε->vector<TableQL>{
		vector<TableQL> results;
		do{
			auto alias = jsonName.ends_with(':') ? jsonName.substr( 0, jsonName.size()-1 ) : string{};
			if( alias.size() )
				jsonName = Next();
			let system = isSystem(jsonName) ? jsonName : string{};
			auto table = LoadTable( move(jsonName), vars, schemas, system.size(), sl );
			table.Alias = move(alias);
			if( system.size() ){
				if( system=="__type" ){
					if( auto typeName = table.FindPtr<jstring>( "name" ); typeName && *typeName!="logTags" ){
						let viewName = DB::Names::ToPlural( DB::Names::FromJson(*typeName) );
						let preDefined = FindIntrospection( *typeName );
						let configOnly = preDefined && !preDefined->Extend; //a config-declared type may have no view (e.g. a live sub-object's type); an extend:true entry still needs its table.
						table.SetDBTable( configOnly ? DB::AppSchema::FindView( schemas, viewName ) : DB::AppSchema::GetViewPtr( schemas, viewName, sl ) );
					}
				}
				else if( system=="__schema" ){
					THROW_IF( schemas.empty() || schemas[0]->Tables.empty(), "No schemas found." );
					table.SetDBTable( schemas[0]->Tables.begin()->second );
				}
			}
			table.ReturnRaw = returnRaw;
			results.push_back( move(table) );
			if( Peek().size() )
				jsonName = Next();
		}while( jsonName.size() );
		return results;
	}
	//A subscription's columns are json names the parser never resolves - nothing reads them until a notification is trimmed to
	//them, and a subscription keyed at the wrong table (permissionUpdated against `permissions`, which has neither allowed nor
	//denied) looked healthy until the notification silently matched nothing.  Says so at subscribe time instead.  Resolution
	//mirrors addColumn: the column itself, or an enum stem whose <name>_id names a pk table.
	Ω warnUnknownColumns( const TableQL& table, sv subscription )ι->void{
		let dbTable = table.DBTable();
		if( !dbTable )
			return;//a system table (logs) resolves no view.
		for( let& c : table.Columns ){
			let name = DB::Names::FromJson( c.JsonName );
			if( dbTable->FindColumn(name) || name=="count" )
				continue;
			let stem = dbTable->FindColumn( name+"_id" );
			if( !stem || !stem->PKTable )
				WARNT( ELogTags::QL, "[{}]subscription '{}' asks for column '{}', which the table does not have - it will never be delivered.", dbTable->Name, subscription, c.JsonName );
		}
		for( let& t : table.Tables )
			warnUnknownColumns( t, subscription );
	}
	α Parser::LoadSubscription( sp<jobject> vars, const vector<sp<DB::AppSchema>>& schemas )ε->Subscription{
		let name = Next();
		optional<EMutationQL> type; string tableName;
		for( uint i=0; !type && i<MutationQLNames.size(); ++i ){//the participle of the verb the mutation starts with - one table, see usings.h.
			let& participle = MutationQLNames[i].Participle;
			if( participle.size() && name.ends_with(participle) && name.size()>participle.size() ){
				tableName = DB::Names::ToPlural(DB::Names::FromJson( name.substr(0, name.size()-participle.size())) );
				type = (EMutationQL)i;
			}
		}
		THROW_IF( !type, "Could not find subscription type for '{}'", name );
		Next();	//{
		Next(); //[userCreated]
		auto table = LoadTable( tableName, vars, schemas, tableName=="logs" );
		warnUnknownColumns( table, name );
		return Subscription{ move(tableName), *type, move(table) };

	}
	α Parser::LoadSubscriptions( sp<jobject> vars, const vector<sp<DB::AppSchema>>& schemas )ε->vector<Subscription>{
		vector<Subscription> y;
		do{
			y.push_back( LoadSubscription(vars, schemas) );
		}while( Next()=="subscription" );

		return y;
	}
	α Parser::LoadUnsubscriptions()ε->vector<SubscriptionId>{
		let text{ Next('}') };
		THROW_IF( text.empty(), "text.empty()" );
		THROW_IF( text.front()!='{', "Expected '{{' vs {} @ '{}' to start unsubscribe - '{}'.", text.front(), Index()-1, Text() );
		//#18: the other Next(char) caller, and it has the same hole - the loop stops at size-1 whether or not the terminator
		//arrived, so an unterminated list arrives here a character short.  parseObject would refuse it too, but for the wrong
		//reason and about the wrong character.
		THROW_IF( text.back()!='}', "Expected '}}' vs '{}' @ '{}' to end unsubscribe - '{}'.", text.back(), Index()-1, Text() );
		string stringified; stringified.reserve( text.size()*2 );
		parseObject(text, stringified);
		return Json::FromArray<SubscriptionId>( Json::AsArray(Json::Parse(stringified), "id") );
	}
	α Parser::Trim( sv token )ι->bool{
		let trimmed = Peek()==token;
		if( trimmed ){
			_peekValue = {};
			_i = 0;
			_text = _text.substr(token.size() );
			_text = Str::TrimFirstLast( move(_text), '{', '}' );
		}
		return trimmed;
	}
}