#include "Log.pb.h"
#include <jde/fwk/usings.h>
#include <jde/fwk/chrono.h>
#include <jde/app/proto/LogProto.h>
#include <jde/fwk/io/protobuf.h>

#define let const auto
namespace Jde::App{
	using Jde::Protobuf::ToBytes;

	//An Arg's or a ClientArg's typed value:  none for text, which each holds its own way.
	template<class T> Ω typed( const T& arg )ι->optional<Logging::Argument>{
		if( arg.has_integer() )
			return arg.integer();
		if( arg.has_unsigned_integer() )
			return arg.unsigned_integer();
		if( arg.has_real() )
			return arg.real();
		if( arg.has_flag() )
			return arg.flag();
		return {};
	}
	//An argument into an Arg or a ClientArg:  a typed one as its value, text as setText stores it - Arg by its id, ClientArg as itself.
	template<class T> Ω setArg( T& y, auto&& arg, auto&& setText )ι->void{
		std::visit( [&]<class V>( V&& value ){
			using Value = std::remove_cvref_t<V>;
			if constexpr( std::is_same_v<Value,string> )
				setText( y, FWD(value) );
			else if constexpr( std::is_same_v<Value,int64_t> )
				y.set_integer( value );
			else if constexpr( std::is_same_v<Value,uint64_t> )
				y.set_unsigned_integer( value );
			else if constexpr( std::is_same_v<Value,double> )
				y.set_real( value );
			else
				y.set_flag( value );
		}, FWD(arg) );
	}

	Ω clientEntry( Logging::Entry&& e )ι->Log::Proto::LogEntryClient{
		Log::Proto::LogEntryClient proto;
		proto.set_template_id( ToBytes(e.Id()) );
		for( auto& arg : e.Arguments )
			setArg( *proto.add_args(), move(arg), []( Log::Proto::ClientArg& y, string&& text ){ y.set_text( move(text) ); } );
		proto.set_level( (Log::Proto::ELogLevel)e.Level );
		proto.set_tags( (uint)e.Tags );
		*proto.mutable_time() = Protobuf::ToTimestamp( e.Time );
		proto.set_user_pk( e.UserPK.Value );
		if( e.File().size() ){//the line only with its file, as LogEntryFile.
			proto.set_line( e.Line );
			proto.set_file_id( ToBytes(e.FileId()) );
		}
		if( e.Function().size() )
			proto.set_function_id( ToBytes(e.FunctionId()) );
		return proto;
	}
	α LogProto::SentStrings::Batch( vector<Logging::Entry>&& entries )ι->Log::Proto::ClientBatch{
		Log::Proto::ClientBatch y;
		if( _ids.size()>=_cap.Count || _bytes>=_cap.Bytes ){
			_ids.clear();
			_bytes = 0;
			y.set_reset( true );
		}
		auto send = [&]( uuid id, sv text ){
			if( !_ids.emplace(id).second )
				return;
			_bytes += text.size();
			*y.add_strings() = ToString( id, string{text} );
		};
		for( auto& e : entries ){
			send( e.Id(), e.Text() );
			if( e.File().size() )
				send( e.FileId(), e.File() );
			if( e.Function().size() )
				send( e.FunctionId(), e.Function() );
			*y.add_entries() = clientEntry( move(e) );
		}
		return y;
	}

	α LogProto::ReceivedStrings::Read( Log::Proto::ClientBatch&& batch )ε->Entries{
		vector<uuid> ids;//every String checked before the table changes, so a refused batch takes nothing, its reset included.
		ids.reserve( batch.strings_size() );
		for( let& s : batch.strings() ){
			ids.push_back( Protobuf::ToGuid(s.id()) );
			THROW_IFX( ids.back()!=Logging::GenerateId(s.value()),
				Exception(SRCE_CUR, ELogLevel::Warning, "Log batch with a String whose id isn't the md5 of its {}-byte text.", s.value().size()) );
		}
		if( batch.reset() ){
			_strings.clear();
			_bytes = 0;
		}
		else
			THROW_IFX( _strings.size()>=_cap.Count || _bytes>=_cap.Bytes,
				Exception(SRCE_CUR, ELogLevel::Warning, "Log batch past the string table's cap without a reset: {} strings, {} bytes.", _strings.size(), _bytes) );
		for( uint i=0; auto& s : *batch.mutable_strings() ){
			let size = s.value().size();
			if( _strings.try_emplace(ids[i++], move(*s.mutable_value())).second )//once, so the bytes count each String once.
				_bytes += size;
		}
		//A name the entry gave:  none when its id is one this session wasn't sent, empty when it gave none.
		auto name = [this]( const string& idBytes )->optional<Logging::SourceName>{
			if( idBytes.empty() )
				return Logging::SourceName{};
			let id = Protobuf::ToGuid( idBytes );
			let p = idBytes.size()==sizeof(uuid) ? _strings.find( id ) : _strings.end();
			return p==_strings.end() ? optional<Logging::SourceName>{} : Logging::SourceName{ string{p->second}, optional<StringMd5>{id} };
		};
		Entries y;
		for( auto& m : *batch.mutable_entries() ){
			auto text = name( m.template_id() );
			auto file = name( m.file_id() );
			auto function = name( m.function_id() );
			if( !text || !file || !function || m.template_id().empty() ){
				++y.Unresolved;
				continue;
			}
			vector<Logging::Argument> args;
			args.reserve( m.args_size() );
			for( auto& arg : *m.mutable_args() ){
				if( auto value = typed(arg) )
					args.push_back( move(*value) );
				else
					args.emplace_back( move(*arg.mutable_text()) );
			}
			y.Built.emplace_back( (ELogLevel)m.level(), (ELogTags)m.tags(), m.line(), Protobuf::ToTimePoint(m.time()), Jde::UserPK{m.user_pk()},
				move(*text), move(*file), move(*function), move(args) );
		}
		return y;
	}

	α LogProto::LogEntryFile( const Logging::Entry& m, App::ProgramPK appPK, App::ProgInstPK instancePK, SourceFields source )ι->Log::Proto::LogEntryFile{
		Log::Proto::LogEntryFile proto;
		proto.set_template_id( ToBytes(m.Id()) );
		for( let& arg : m.Arguments )
			setArg( *proto.add_args(), arg, []( Log::Proto::Arg& y, const string& text ){ y.set_string_id( ToBytes(Logging::Entry::GenerateId(text)) ); } );
		proto.set_level( (Log::Proto::ELogLevel)m.Level );
		proto.set_tags( (uint)m.Tags );
		*proto.mutable_time() = Protobuf::ToTimestamp( m.Time );
		proto.set_user_pk( m.UserPK.Value );
		if( source.File && m.File().size() ){//none for an entry without a source:  a LogStack line below /logging/stack.
			proto.set_line( m.Line );
			proto.set_file_id( ToBytes(m.FileId()) );
		}
		if( source.Function && m.Function().size() )
			proto.set_function_id( ToBytes(m.FunctionId()) );
		proto.set_app_pk( appPK );//0 for an entry this process wrote:  a proto3 zero is not even on the wire.
		proto.set_app_instance_pk( instancePK );
		return proto;
	}

	α LogProto::ToString( uuid id, string&& value )ι->Log::Proto::String{
		Log::Proto::String m;
		*m.mutable_id() = ToBytes( id );
		*m.mutable_value() = move( value );
		return m;
	}

	α LogProto::ArgText( const Log::Proto::Arg& arg )ι->optional<string>{
		if( let value = typed(arg) )
			return Logging::ArgText( *value );
		return {};
	}

	Ω guidToString( const string& guid )ι->string{
		using Protobuf::ToGuid;
		if( guid.size()!=16 )
			return Ƒ( "invalid guid size: {}", guid.size() );
		let v = ToString( ToGuid(guid) );
		return v.substr(v.size()-4);
	}

	using namespace Log::Proto;
	Ω debugStringLogEntry( const LogEntryFile& f )ι->string{
		string result = Ƒ( "[{}.{}.{}] {} user_pk: {} {{",
			guidToString( f.template_id() ),
			ToString( (Jde::ELogLevel)f.level() ),
			ToString( (ELogTags)f.tags() ),
			Chrono::LocalTimeMilli( Protobuf::ToTimePoint(f.time()) ),
			//guidToString( f.file_id() ),
			//f.line(),
			//guidToString( f.function_id() ),
			f.user_pk()
		);
		for( auto& arg : f.args() )
			result += Ƒ( "{}, ", arg.has_string_id() ? guidToString(arg.string_id()) : LogProto::ArgText(arg).value_or("") );
		result += "}";
		if( f.app_pk() )//forwarded
			result += Ƒ( ", app_pk: {}, app_instance_pk: {}", f.app_pk(), f.app_instance_pk() );
		return result;
	}
	α LogProto::DebugString( const FileEntry& f )ι->string{
		switch( f.value_case() ){
			using Type = FileEntry::ValueCase;
			case Type::kEntry:
				return debugStringLogEntry( f.entry() );
			break;
			case Type::kStr:
				return Ƒ( "[{}]{}", guidToString(f.str().id()), f.str().value() );
			default:
				return "Error";
			break;
		}
	}
	α LogProto::DebugString( const Log::Proto::LogEntryFile& f )ι->string{
		return debugStringLogEntry( f );
	}
}