#include "Log.pb.h"
#include <jde/fwk/usings.h>
#include <jde/fwk/chrono.h>
#include <jde/app/proto/LogProto.h>
#include <jde/fwk/io/protobuf.h>

#define let const auto
namespace Jde::App{
	using Jde::Protobuf::ToBytes;

	α LogProto::FromLogEntry( Log::Proto::LogEntryClient&& m )ι->Logging::Entry{
		return Logging::Entry{
			( ELogLevel )m.level(),
			( ELogTags )m.tags(),
			m.line(),
			Protobuf::ToTimePoint( m.time() ),
			{ m.user_pk() },
			move( *m.mutable_text() ),
			move( *m.mutable_file() ),
			move( *m.mutable_function() ),
			Protobuf::ToVector( move(*m.mutable_args()) )
		};
	}

	α LogProto::ToEntry( Log::Proto::LogEntryFileExternal&& x )ι->Log::Proto::LogEntryFile{
		Log::Proto::LogEntryFile y;
		*y.mutable_time() = x.time();
		y.set_template_id( move(*x.mutable_template_id()) );
		*y.mutable_args() = move( *x.mutable_args() );
		y.set_level( x.level() );
		y.set_tags( x.tags() );
		y.set_line( x.line() );
		y.set_user_pk( x.user_pk() );
		y.set_file_id( move(*x.mutable_file_id()) );
		y.set_function_id( move(*x.mutable_function_id()) );
		y.set_app_pk( x.app_pk() );//L3: carried, not dropped - this is the only place a forwarded entry's attribution could survive.
		y.set_app_instance_pk( x.app_instance_pk() );
		return y;
	}

	α LogProto::LogEntryClient( Logging::Entry&& e )ι->Log::Proto::LogEntryClient{
		Log::Proto::LogEntryClient proto;
		proto.set_text( move(e.Text) );
		*proto.mutable_args() = Protobuf::FromVector( move(e.Arguments) );
		proto.set_level( (Log::Proto::ELogLevel)e.Level );
		proto.set_tags( (uint)e.Tags );
		proto.set_line( e.Line );
		*proto.mutable_time() = Protobuf::ToTimestamp( e.Time );
		proto.set_user_pk( e.UserPK.Value );
		proto.set_file( move(e.FileString()) );
		proto.set_function( move(e.FunctionString()) );

		return proto;
	}

	α LogProto::LogEntryFile( const Logging::Entry& m, App::ProgramPK appPK, App::ProgInstPK instancePK )ι->Log::Proto::LogEntryFile{
		Log::Proto::LogEntryFile proto;
		proto.set_template_id( ToBytes(m.Id()) );
		for( auto& arg : m.Arguments )
			*proto.add_args() = ToBytes( Logging::Entry::GenerateId(arg) );
		proto.set_level( (Log::Proto::ELogLevel)m.Level );
		proto.set_tags( (uint)m.Tags );
		proto.set_line( m.Line );
		*proto.mutable_time() = Protobuf::ToTimestamp( m.Time );
		proto.set_user_pk( m.UserPK.Value );
		proto.set_file_id( ToBytes(m.FileId()) );
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
			result += Ƒ( "{}, ", guidToString(arg) );
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
			case Type::kExternalEntry:
				return debugStringLogEntry( ToEntry(LogEntryFileExternal{f.external_entry()}) );
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