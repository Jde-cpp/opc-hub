#include <jde/db/generators/Sql.h>

#define let const auto

namespace Jde::DB{
	α Sql::EmbedParams()Ι->string{ //display/logging only - not escaped for execution.
		string fullSql; fullSql.reserve( Text.size()+Params.size()*8 );
		char quote{};
		for( uint i=0, iParam=0; i<Text.size(); ++i ){
			const char ch = Text[i];
			if( !quote && (ch=='\'' || ch=='"') )
				quote = ch;
			else if( ch==quote )
				quote = 0;
			if( ch!='?' || quote || iParam==Params.size() ){
				fullSql.push_back( ch );
				continue;
			}
			auto& param = Params[iParam++];
			switch( param.Type() ){
				using enum EValue;
			case String: fullSql.append( '\''+Str::Replace(param.get_string(), "\'", "''")+'\'' ); break;
			case Null: fullSql.append( "null" ); break;
			case Bytes: fullSql.append( param.get_bytes().empty() ? string{"Null"} : '\''+param.ToString()+'\'' ); break; //ToString is the Encode64.
			default:
				fullSql.append( param.ToString() );
			}
		}
		return fullSql;
	}
}