#include "TighteningResult.h"
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/UAString.h>

#define let const auto
namespace Jde::Opc::Gateway{
	using OpenProtocol::Mid0061;
	alignas(UA_String) constexpr std::byte _zero[sizeof(UA_String)]{};//every type here is all zeroes at its zero, and none is larger.

	//What walk hands each variable to:  `Put( name, since, stored, type, scalar )`, since and stored the first revisions of MID 0061
	//and of MID 0065 that carry it, and the scalar null where the result sent no value.
	template<class TPut> struct Each final{
		α Text( sv name, uint8_t since, uint8_t stored, str value )ι->void{
			let ua = ToUV( value );
			Put( name, since, stored, UA_TYPES[UA_TYPES_STRING], &ua );
		}
		α Flag( sv name, uint8_t since, uint8_t stored, optional<uint8_t> value )ι->void{
			const UA_Boolean ua{ value==1 };
			Put( name, since, stored, UA_TYPES[UA_TYPES_BOOLEAN], value ? &ua : nullptr );
		}
		α Number( sv name, uint8_t since, uint8_t stored, optional<uint8_t> value )ι->void{ Scalar<UA_Byte>( name, since, stored, UA_TYPES_BYTE, value ); }
		α Number( sv name, uint8_t since, uint8_t stored, optional<uint16_t> value )ι->void{ Scalar<UA_UInt16>( name, since, stored, UA_TYPES_UINT16, value ); }
		α Number( sv name, uint8_t since, uint8_t stored, optional<uint32_t> value )ι->void{ Scalar<UA_UInt32>( name, since, stored, UA_TYPES_UINT32, value ); }
		α Real( sv name, uint8_t since, uint8_t stored, optional<double> value )ι->void{ Scalar<UA_Double>( name, since, stored, UA_TYPES_DOUBLE, value ); }
		α Degrees( sv name, uint8_t since, uint8_t stored, optional<uint32_t> value )ι->void{ Real( name, since, stored, value ? optional<double>{ *value } : nullopt ); }
		//Whole seconds local to the row's zone (TighteningResult::ToSys).
		α Time( sv name, uint8_t since, uint8_t stored, optional<OpenProtocol::LocalTime> value )ι->void{
			let ua = value ? UADateTime{ TighteningResult::ToSys(*value, *Zone, Seen, Stored) }.UA() : UA_DateTime{};
			Put( name, since, stored, UA_TYPES[UA_TYPES_DATETIME], value ? &ua : nullptr );
		}
		TPut Put;
		const std::chrono::time_zone* Zone;
		TimePoint Seen;
		bool Stored;
	private:
		template<class T, class U> α Scalar( sv name, uint8_t since, uint8_t stored, uint16_t type, optional<U> value )ι->void{
			const T ua{ value.value_or(U{}) };
			Put( name, since, stored, UA_TYPES[type], value ? &ua : nullptr );
		}
	};
	template<class TPut> Each( TPut, const std::chrono::time_zone*, TimePoint, bool )->Each<TPut>;

	//Tables 108 to 114, a row a variable, as the type has them (jde/opc/openProtocol/Mids.h).
	Ω walk( const Mid0061& r, auto&& v )ι->void{
		v.Text( "Vin", 1, 1, r.Vin );
		v.Number( "JobId", 1, 2, r.JobId );
		v.Number( "Pset", 1, 1, r.Pset );
		v.Number( "Strategy", 2, 2, r.Strategy );
		v.Number( "StrategyOptions", 2, 2, r.StrategyOptions );
		v.Number( "BatchSize", 1, 2, r.BatchSize );
		v.Number( "BatchCounter", 1, 1, r.BatchCounter );
		v.Flag( "Status", 1, 1, r.Status );
		v.Number( "BatchStatus", 1, 1, r.BatchStatus );
		v.Number( "TorqueStatus", 1, 1, r.TorqueStatus );
		v.Number( "AngleStatus", 1, 1, r.AngleStatus );
		v.Number( "RundownAngleStatus", 2, 2, r.RundownAngleStatus );
		v.Number( "CurrentMonitoringStatus", 2, 2, r.CurrentMonitoringStatus );
		v.Number( "SelfTapStatus", 2, 2, r.SelfTapStatus );
		v.Number( "PrevailTorqueMonitoringStatus", 2, 2, r.PrevailTorqueMonitoringStatus );
		v.Number( "PrevailTorqueCompensateStatus", 2, 2, r.PrevailTorqueCompensateStatus );
		v.Number( "TighteningErrorStatus", 2, 2, r.TighteningErrorStatus );
		v.Real( "TorqueMin", 1, 0, r.TorqueMin );
		v.Real( "TorqueMax", 1, 0, r.TorqueMax );
		v.Real( "TorqueFinalTarget", 1, 0, r.TorqueFinalTarget );
		v.Real( "Torque", 1, 1, r.Torque );
		v.Degrees( "AngleMin", 1, 0, r.AngleMin );
		v.Degrees( "AngleMax", 1, 0, r.AngleMax );
		v.Degrees( "AngleFinalTarget", 1, 0, r.AngleFinalTarget );
		if( r.AngleDecimal )//revision 7's, to two decimals, for the whole degrees.
			v.Real( "Angle", 1, 1, r.AngleDecimal );
		else
			v.Degrees( "Angle", 1, 1, r.Angle );
		v.Degrees( "RundownAngleMin", 2, 0, r.RundownAngleMin );
		v.Degrees( "RundownAngleMax", 2, 0, r.RundownAngleMax );
		v.Degrees( "RundownAngle", 2, 2, r.RundownAngle );
		v.Number( "CurrentMonitoringMin", 2, 0, r.CurrentMonitoringMin );
		v.Number( "CurrentMonitoringMax", 2, 0, r.CurrentMonitoringMax );
		v.Number( "CurrentMonitoringValue", 2, 2, r.CurrentMonitoringValue );
		v.Real( "SelfTapMin", 2, 0, r.SelfTapMin );
		v.Real( "SelfTapMax", 2, 0, r.SelfTapMax );
		v.Real( "SelfTapTorque", 2, 2, r.SelfTapTorque );
		v.Real( "PrevailTorqueMonitoringMin", 2, 0, r.PrevailTorqueMonitoringMin );
		v.Real( "PrevailTorqueMonitoringMax", 2, 0, r.PrevailTorqueMonitoringMax );
		v.Real( "PrevailTorque", 2, 2, r.PrevailTorque );
		v.Number( "JobSequenceNumber", 2, 2, r.JobSequenceNumber );
		v.Number( "SyncTighteningId", 2, 2, r.SyncTighteningId );
		v.Text( "ToolSerialNumber", 2, 2, r.ToolSerialNumber );
		v.Time( "Time", 1, 1, r.Time );
		v.Time( "PsetChangeTime", 1, 0, r.PsetChangeTime );
		v.Text( "PsetName", 3, 0, r.PsetName );
		v.Number( "TorqueUnit", 3, 3, r.TorqueUnit );
		v.Number( "ResultType", 3, 3, r.ResultType );
		v.Text( "IdentifierPart2", 4, 4, r.IdentifierPart2 );
		v.Text( "IdentifierPart3", 4, 4, r.IdentifierPart3 );
		v.Text( "IdentifierPart4", 4, 4, r.IdentifierPart4 );
		v.Text( "CustomerErrorCode", 5, 5, r.CustomerErrorCode );
		v.Real( "PrevailTorqueCompensateValue", 6, 6, r.PrevailTorqueCompensateValue );
		v.Number( "TighteningErrorStatus2", 6, 6, r.TighteningErrorStatus2 );
		v.Real( "CompensatedAngle", 7, 0, r.CompensatedAngle );
		v.Number( "TighteningId", 1, 1, r.TighteningId );
	}
	α TighteningResult::Variables()ι->const vector<Variable>&{
		static const vector<Variable> variables = []{
			vector<Variable> y;
			walk( Mid0061{}, Each{[&]( sv name, uint8_t since, uint8_t stored, const UA_DataType& type, const void* ){ y.push_back( {name, since, stored, &type} ); }, nullptr, TimePoint{}, false} );
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
	α TighteningResult::ToSys( OpenProtocol::LocalTime local, const std::chrono::time_zone& zone, TimePoint seen, bool stored )ι->TimePoint{
		const TimePoint first{ zone.to_sys(local, std::chrono::choose::earliest) };
		const TimePoint second{ zone.to_sys(local, std::chrono::choose::latest) };
		return ( stored ? second<=seen : second-seen<seen-first ) ? second : first;
	}
	//Variables' each:  what the result has for one its message carries at `revision`, MID 0065 where `stored`.
	Ω values( const Mid0061& result, uint16_t revision, const std::chrono::time_zone& zone, TimePoint seen, bool stored )ι->vector<optional<Value>>{
		vector<optional<Value>> y;
		y.reserve( TighteningResult::Variables().size() );
		walk( result, Each{[&]( sv, uint8_t since, uint8_t storedSince, const UA_DataType& type, const void* value ){
			if( let first = stored ? storedSince : since; first && first<=revision )
				y.emplace_back( value ? TighteningResult::Scalar(value, type) : TighteningResult::Unset(type, UA_STATUSCODE_BADNOTSUPPORTED) );
			else
				y.emplace_back();
		}, &zone, seen, stored} );
		return y;
	}
	α TighteningResult::Values( const Mid0061& result, uint16_t revision, const std::chrono::time_zone& zone, TimePoint seen )ι->vector<optional<Value>>{
		return values( result, revision, zone, seen, false );
	}
	//MID 0065's fields under the names MID 0061 has them by:  the rest is what an old result does not carry.
	α TighteningResult::Values( const OpenProtocol::Mid0065& r, uint16_t revision, const std::chrono::time_zone& zone, TimePoint by )ι->vector<optional<Value>>{
		return values( {
			.Vin=r.Vin, .JobId=r.JobId, .Pset=r.Pset, .Strategy=r.Strategy, .StrategyOptions=r.StrategyOptions, .BatchSize=r.BatchSize, .BatchCounter=r.BatchCounter,
			.Status=r.Status, .BatchStatus=r.BatchStatus, .TorqueStatus=r.TorqueStatus, .AngleStatus=r.AngleStatus, .RundownAngleStatus=r.RundownAngleStatus,
			.CurrentMonitoringStatus=r.CurrentMonitoringStatus, .SelfTapStatus=r.SelfTapStatus, .PrevailTorqueMonitoringStatus=r.PrevailTorqueMonitoringStatus,
			.PrevailTorqueCompensateStatus=r.PrevailTorqueCompensateStatus, .TighteningErrorStatus=r.TighteningErrorStatus, .Torque=r.Torque, .Angle=r.Angle,
			.RundownAngle=r.RundownAngle, .CurrentMonitoringValue=r.CurrentMonitoringValue, .SelfTapTorque=r.SelfTapTorque, .PrevailTorque=r.PrevailTorque,
			.TighteningId=r.TighteningId, .JobSequenceNumber=r.JobSequenceNumber, .SyncTighteningId=r.SyncTighteningId, .ToolSerialNumber=r.ToolSerialNumber, .Time=r.Time,
			.TorqueUnit=r.TorqueUnit, .ResultType=r.ResultType, .IdentifierPart2=r.IdentifierPart2, .IdentifierPart3=r.IdentifierPart3, .IdentifierPart4=r.IdentifierPart4,
			.CustomerErrorCode=r.CustomerErrorCode, .PrevailTorqueCompensateValue=r.PrevailTorqueCompensateValue, .TighteningErrorStatus2=r.TighteningErrorStatus2
		}, revision, zone, by, true );
	}
}