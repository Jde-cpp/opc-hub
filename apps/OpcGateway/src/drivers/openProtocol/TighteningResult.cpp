#include "TighteningResult.h"
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/UAString.h>

#define let const auto
namespace Jde::Opc::Gateway{
	using OpenProtocol::Mid0061;
	alignas(UA_String) constexpr std::byte _zero[sizeof(UA_String)]{};//every type here is all zeroes at its zero, and none is larger.

	//What walk hands each variable to:  `Put( name, since, type, scalar )`, the scalar null where the result sent no value.
	template<class TPut> struct Each final{
		α Text( sv name, uint8_t since, str value )ι->void{
			let ua = ToUV( value );
			Put( name, since, UA_TYPES[UA_TYPES_STRING], &ua );
		}
		α Flag( sv name, uint8_t since, optional<uint8_t> value )ι->void{
			const UA_Boolean ua{ value==1 };
			Put( name, since, UA_TYPES[UA_TYPES_BOOLEAN], value ? &ua : nullptr );
		}
		α Number( sv name, uint8_t since, optional<uint8_t> value )ι->void{ Scalar<UA_Byte>( name, since, UA_TYPES_BYTE, value ); }
		α Number( sv name, uint8_t since, optional<uint16_t> value )ι->void{ Scalar<UA_UInt16>( name, since, UA_TYPES_UINT16, value ); }
		α Number( sv name, uint8_t since, optional<uint32_t> value )ι->void{ Scalar<UA_UInt32>( name, since, UA_TYPES_UINT32, value ); }
		α Real( sv name, uint8_t since, optional<double> value )ι->void{ Scalar<UA_Double>( name, since, UA_TYPES_DOUBLE, value ); }
		α Degrees( sv name, uint8_t since, optional<uint32_t> value )ι->void{ Real( name, since, value ? optional<double>{ *value } : nullopt ); }
		//Whole seconds local to the row's zone (TighteningResult::ToSys).
		α Time( sv name, uint8_t since, optional<OpenProtocol::LocalTime> value )ι->void{
			let ua = value ? UADateTime{ TighteningResult::ToSys(*value, *Zone, Seen) }.UA() : UA_DateTime{};
			Put( name, since, UA_TYPES[UA_TYPES_DATETIME], value ? &ua : nullptr );
		}
		TPut Put;
		const std::chrono::time_zone* Zone;
		TimePoint Seen;
	private:
		template<class T, class U> α Scalar( sv name, uint8_t since, uint16_t type, optional<U> value )ι->void{
			const T ua{ value.value_or(U{}) };
			Put( name, since, UA_TYPES[type], value ? &ua : nullptr );
		}
	};
	template<class TPut> Each( TPut, const std::chrono::time_zone*, TimePoint )->Each<TPut>;

	//Tables 108 to 114, a row a variable, as the type has them (jde/opc/openProtocol/Mids.h).
	Ω walk( const Mid0061& r, auto&& v )ι->void{
		v.Text( "Vin", 1, r.Vin );
		v.Number( "JobId", 1, r.JobId );
		v.Number( "Pset", 1, r.Pset );
		v.Number( "Strategy", 2, r.Strategy );
		v.Number( "StrategyOptions", 2, r.StrategyOptions );
		v.Number( "BatchSize", 1, r.BatchSize );
		v.Number( "BatchCounter", 1, r.BatchCounter );
		v.Flag( "Status", 1, r.Status );
		v.Number( "BatchStatus", 1, r.BatchStatus );
		v.Number( "TorqueStatus", 1, r.TorqueStatus );
		v.Number( "AngleStatus", 1, r.AngleStatus );
		v.Number( "RundownAngleStatus", 2, r.RundownAngleStatus );
		v.Number( "CurrentMonitoringStatus", 2, r.CurrentMonitoringStatus );
		v.Number( "SelfTapStatus", 2, r.SelfTapStatus );
		v.Number( "PrevailTorqueMonitoringStatus", 2, r.PrevailTorqueMonitoringStatus );
		v.Number( "PrevailTorqueCompensateStatus", 2, r.PrevailTorqueCompensateStatus );
		v.Number( "TighteningErrorStatus", 2, r.TighteningErrorStatus );
		v.Real( "TorqueMin", 1, r.TorqueMin );
		v.Real( "TorqueMax", 1, r.TorqueMax );
		v.Real( "TorqueFinalTarget", 1, r.TorqueFinalTarget );
		v.Real( "Torque", 1, r.Torque );
		v.Degrees( "AngleMin", 1, r.AngleMin );
		v.Degrees( "AngleMax", 1, r.AngleMax );
		v.Degrees( "AngleFinalTarget", 1, r.AngleFinalTarget );
		if( r.AngleDecimal )//revision 7's, to two decimals, for the whole degrees.
			v.Real( "Angle", 1, r.AngleDecimal );
		else
			v.Degrees( "Angle", 1, r.Angle );
		v.Degrees( "RundownAngleMin", 2, r.RundownAngleMin );
		v.Degrees( "RundownAngleMax", 2, r.RundownAngleMax );
		v.Degrees( "RundownAngle", 2, r.RundownAngle );
		v.Number( "CurrentMonitoringMin", 2, r.CurrentMonitoringMin );
		v.Number( "CurrentMonitoringMax", 2, r.CurrentMonitoringMax );
		v.Number( "CurrentMonitoringValue", 2, r.CurrentMonitoringValue );
		v.Real( "SelfTapMin", 2, r.SelfTapMin );
		v.Real( "SelfTapMax", 2, r.SelfTapMax );
		v.Real( "SelfTapTorque", 2, r.SelfTapTorque );
		v.Real( "PrevailTorqueMonitoringMin", 2, r.PrevailTorqueMonitoringMin );
		v.Real( "PrevailTorqueMonitoringMax", 2, r.PrevailTorqueMonitoringMax );
		v.Real( "PrevailTorque", 2, r.PrevailTorque );
		v.Number( "JobSequenceNumber", 2, r.JobSequenceNumber );
		v.Number( "SyncTighteningId", 2, r.SyncTighteningId );
		v.Text( "ToolSerialNumber", 2, r.ToolSerialNumber );
		v.Time( "Time", 1, r.Time );
		v.Time( "PsetChangeTime", 1, r.PsetChangeTime );
		v.Text( "PsetName", 3, r.PsetName );
		v.Number( "TorqueUnit", 3, r.TorqueUnit );
		v.Number( "ResultType", 3, r.ResultType );
		v.Text( "IdentifierPart2", 4, r.IdentifierPart2 );
		v.Text( "IdentifierPart3", 4, r.IdentifierPart3 );
		v.Text( "IdentifierPart4", 4, r.IdentifierPart4 );
		v.Text( "CustomerErrorCode", 5, r.CustomerErrorCode );
		v.Real( "PrevailTorqueCompensateValue", 6, r.PrevailTorqueCompensateValue );
		v.Number( "TighteningErrorStatus2", 6, r.TighteningErrorStatus2 );
		v.Real( "CompensatedAngle", 7, r.CompensatedAngle );
		v.Number( "TighteningId", 1, r.TighteningId );
	}
	α TighteningResult::Variables()ι->const vector<Variable>&{
		static const vector<Variable> variables = []{
			vector<Variable> y;
			walk( Mid0061{}, Each{[&]( sv name, uint8_t since, const UA_DataType& type, const void* ){ y.push_back( {name, since, &type} ); }, nullptr, TimePoint{}} );
			return y;
		}();
		return variables;
	}
	α TighteningResult::Scalar( const void* scalar, const UA_DataType& type, UA_StatusCode status )ι->Value{
		UA_DataValue view; UA_DataValue_init( &view );
		UA_Variant_setScalar( &view.value, const_cast<void*>(scalar), &type );
		view.hasValue = true;
		view.status = status;
		view.hasStatus = status!=UA_STATUSCODE_GOOD;
		return Value{ view };
	}
	α TighteningResult::Unset( const UA_DataType& type, UA_StatusCode status )ι->Value{
		return Scalar( _zero, type, status );
	}
	α TighteningResult::ToSys( OpenProtocol::LocalTime local, const std::chrono::time_zone& zone, TimePoint seen )ι->TimePoint{
		const TimePoint first{ zone.to_sys(local, std::chrono::choose::earliest) };
		const TimePoint second{ zone.to_sys(local, std::chrono::choose::latest) };
		return second-seen<seen-first ? second : first;
	}
	α TighteningResult::Values( const Mid0061& result, uint16_t revision, const std::chrono::time_zone& zone, TimePoint seen )ι->vector<std::pair<sv,Value>>{
		vector<std::pair<sv,Value>> y;
		y.reserve( Variables().size() );
		walk( result, Each{[&]( sv name, uint8_t since, const UA_DataType& type, const void* value ){
			if( since<=revision )
				y.emplace_back( name, value ? Scalar(value, type) : Unset(type, UA_STATUSCODE_BADNOTSUPPORTED) );
		}, &zone, seen} );
		return y;
	}
}