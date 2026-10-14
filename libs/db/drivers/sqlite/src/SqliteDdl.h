#pragma once
#include <jde/fwk/str.h>

namespace Jde::DB::Sqlite{
	//#241: sqlite has no `alter table … set default`.  Its own manual's route for one (lang_altertable.html, the simpler
	//procedure "for adding, removing, or changing default values on a column") is to edit the CREATE TABLE text stored in
	//sqlite_master - which is what this rewrites, and SqliteServerMeta::SetDefault writes back under `pragma writable_schema`.
	//The text is split at its top-level commas, the item whose first token is `column` found, and its `default` clause -
	//a quoted string, a parenthesized expression, or one bare token - replaced with `default <literal>`, or one appended
	//when the column has none (column constraints are unordered).  Quoted names in any of sqlite's four styles.
	Ξ QuotedEnd( sv sql, uint i )ι->uint{ //sql[i] opens the quote; answers the index of the quote that closes it, or sql.size()-1.
		const char close = sql[i]=='[' ? ']' : sql[i];
		for( ++i; i<sql.size(); ++i ){
			if( sql[i]!=close )
				continue;
			if( close!=']' && i+1<sql.size() && sql[i+1]==close ){ //a doubled quote is the quote itself.
				++i;
				continue;
			}
			return i;
		}
		return sql.size()-1;
	}
	Ξ IsQuote( char c )ι->bool{ return c=='\'' || c=='"' || c=='`' || c=='['; }
	Ξ IsSpace( char c )ι->bool{ return c==' ' || c=='\t' || c=='\n' || c=='\r'; }

	//`default <value>`'s span within the item [start,end), or nullopt when the column declares none.
	Ξ FindDefaultClause( sv sql, uint start, uint end )ι->optional<std::pair<uint,uint>>{
		uint depth{};
		for( uint i=start; i<end; ++i ){
			const char c = sql[i];
			if( IsQuote(c) )
				i = QuotedEnd( sql, i );
			else if( c=='(' )
				++depth;
			else if( c==')' && depth )
				--depth;
			else if( depth==0 && (i==start || IsSpace(sql[i-1])) && i+7<=end && ToIV(sql.substr(i, 7))=="default"_iv && (i+7==end || IsSpace(sql[i+7]) || sql[i+7]=='(' || sql[i+7]=='\'') ){
				uint j = i+7;
				for( ; j<end && IsSpace(sql[j]); ++j );
				if( j<end && (sql[j]=='\'' || ((sql[j]=='x' || sql[j]=='X') && j+1<end && sql[j+1]=='\'')) ) //a string or blob literal.
					j = QuotedEnd( sql, sql[j]=='\'' ? j : j+1 )+1;
				else if( j<end && sql[j]=='(' ){
					for( uint d=0; j<end; ++j ){
						if( IsQuote(sql[j]) )
							j = QuotedEnd( sql, j );
						else if( sql[j]=='(' )
							++d;
						else if( sql[j]==')' && --d==0 ){
							++j;
							break;
						}
					}
				}
				else
					for( ; j<end && !IsSpace(sql[j]); ++j );
				return std::pair{ i, std::min(j, end) };
			}
		}
		return {};
	}

	Ξ RewriteDefault( sv sql, sv column, sv literal )ε->string{
		const auto open = sql.find( '(' ), close = sql.rfind( ')' );
		THROW_IF( open==sv::npos || close==sv::npos || close<open, "'{}' is not a create table statement.", sql );
		uint depth{}, itemStart = open+1;
		for( uint i=itemStart; i<=close; ++i ){
			const char c = sql[i];
			if( i<close && IsQuote(c) )
				i = QuotedEnd( sql, i );
			else if( i<close && c=='(' )
				++depth;
			else if( i<close && c==')' )
				--depth;
			else if( (c==',' && depth==0) || i==close ){
				uint nameStart = itemStart;
				for( ; nameStart<i && IsSpace(sql[nameStart]); ++nameStart );
				uint nameEnd = nameStart;
				if( nameEnd<i && IsQuote(sql[nameEnd]) )
					nameEnd = QuotedEnd( sql, nameEnd )+1;
				else
					for( ; nameEnd<i && !IsSpace(sql[nameEnd]) && sql[nameEnd]!='(' && sql[nameEnd]!=','; ++nameEnd );
				const auto name = nameEnd>nameStart && IsQuote(sql[nameStart]) ? sql.substr( nameStart+1, nameEnd-nameStart-2 ) : sql.substr( nameStart, nameEnd-nameStart );
				if( ToIV(name)==ToIV(column) ){
					string y{ sql };
					if( const auto clause = FindDefaultClause(sql, nameEnd, i); clause )
						y.replace( clause->first, clause->second-clause->first, Ƒ("default {}", literal) );
					else{
						uint tail = i; //append after the item's last character, keeping its trailing whitespace where it was.
						for( ; tail>nameEnd && IsSpace(sql[tail-1]); --tail );
						y.insert( tail, Ƒ(" default {}", literal) );
					}
					return y;
				}
				itemStart = i+1;
			}
		}
		THROW( "Column '{}' is not in '{}'.", column, sql );
	}
}