#pragma once
#include "Fields.h"

//Each MID's data field a row at a time, as the specification's tables list it:  `m` is the type or a const one and `v`
//the Reader or the Writer, so one layout serves both ways.  `r` is the frame's revision, 1 to the type's MaxRevision.
namespace Jde::Opc::OpenProtocol{
	template<class T, class TMid> concept Is = std::same_as<std::remove_const_t<T>,TMid>;

	template<uint16_t TId, uint16_t TMaxRevision> Ω Walk( const NoData<TId,TMaxRevision>&, auto&, uint16_t )ι->void{}

	//Tables 13 to 18.
	Ω Walk( Is<Mid0002> auto& m, auto& v, uint16_t r )ι->void{
		v.Num( 1, m.CellId, 4 );
		v.Num( 2, m.ChannelId, 2 );
		v.Text( 3, m.ControllerName, 25 );
		if( r>=2 )
			v.Text( 4, m.SupplierCode, 3 );
		if( r>=3 ){
			v.Text( 5, m.OpenProtocolVersion, 19 );
			v.Text( 6, m.ControllerSoftwareVersion, 19 );
			v.Text( 7, m.ToolSoftwareVersion, 19 );
		}
		if( r>=4 ){
			v.Text( 8, m.RbuType, 24 );
			v.Text( 9, m.ControllerSerialNumber, 10 );
		}
		if( r>=5 ){
			v.Num( 10, m.SystemType, 3 );
			v.Num( 11, m.SystemSubtype, 3 );
		}
		if( r>=6 ){
			v.Num( 12, m.SequenceNumberSupport, 1 );
			v.Num( 13, m.LinkingSupport, 1 );
			v.Num( 14, m.StationId, 10 );
			v.Text( 15, m.StationName, 25 );
			v.Num( 16, m.ClientId, 1 );
		}
	}
	//Tables 21 and 22.
	Ω Walk( Is<Mid0004> auto& m, auto& v, uint16_t r )ι->void{
		v.Num( 0, m.Mid, 4 );
		v.Num( 0, m.Code, r==1 ? 2 : 3 );
	}
	//Table 24.
	Ω Walk( Is<Mid0005> auto& m, auto& v, uint16_t )ι->void{
		v.Num( 0, m.Mid, 4 );
	}
	//Tables 38 to 40.  Table 39's byte column slips by one from the angle max on;  the widths are its Value column's.
	Ω Walk( Is<Mid0015> auto& m, auto& v, uint16_t r )ι->void{
		if( r==1 ){
			v.Num( 0, m.Pset, 3 );
			v.Time( 0, m.PsetChangeTime );
			return;
		}
		v.Num( 1, m.Pset, 3 );
		v.Text( 2, m.PsetName, 25 );
		v.Time( 3, m.PsetChangeTime );
		v.Num( 4, m.RotationDirection, 1 );
		v.Num( 5, m.BatchSize, 2 );
		v.Scaled( 6, m.TorqueMin, 6 );
		v.Scaled( 7, m.TorqueMax, 6 );
		v.Scaled( 8, m.TorqueFinalTarget, 6 );
		v.Num( 9, m.AngleMin, 5 );
		v.Num( 10, m.AngleMax, 5 );
		v.Num( 11, m.AngleFinalTarget, 5 );
		v.Scaled( 12, m.FirstTarget, 6 );
		v.Scaled( 13, m.StartFinalAngle, 6 );
		if( r>=3 ){
			v.Num( 14, m.SelectedIdentifierNumber, 4 );
			v.Text( 15, m.JointId, 25 );
		}
	}
	//The Table 40 that is MID 0018's.
	Ω Walk( Is<Mid0018> auto& m, auto& v, uint16_t )ι->void{
		v.Num( 0, m.Pset, 3 );
	}
	//Tables 69 to 74.
	Ω Walk( Is<Mid0035> auto& m, auto& v, uint16_t r )ι->void{
		v.Num( 1, m.JobId, r==1 ? 2 : 4 );
		v.Num( 2, m.JobStatus, 1 );
		v.Num( 3, m.JobBatchMode, 1 );
		v.Num( 4, m.JobBatchSize, 4 );
		v.Num( 5, m.JobBatchCounter, 4 );
		v.Time( 6, m.Time );
		if( r>=3 ){
			v.Num( 7, m.JobCurrentStep, 3 );
			v.Num( 8, m.JobTotalSteps, 3 );
			v.Num( 9, m.JobStepType, 2 );
		}
		if( r>=4 )
			v.Num( 10, m.JobTighteningStatus, 2 );
		if( r>=5 ){
			v.Num( 11, m.JobSequenceNumber, 5 );
			v.Text( 12, m.Vin, 25 );
			v.Text( 13, m.IdentifierPart2, 25 );
			v.Text( 14, m.IdentifierPart3, 25 );
			v.Text( 15, m.IdentifierPart4, 25 );
		}
		if( r>=6 )
			v.Text( 16, m.JointId, 25 );
	}
	//The Tables 74 and 75 that are MID 0038's.
	Ω Walk( Is<Mid0038> auto& m, auto& v, uint16_t r )ι->void{
		v.Num( 0, m.JobId, r==1 ? 2 : 4 );
	}
	//Table 105.
	Ω Walk( Is<Mid0050> auto& m, auto& v, uint16_t )ι->void{
		v.Rest( m.Vin, 25 );
	}
	//Tables 106 and 107.  Table 107's byte column for parts 3 and 4 is short of their 25 characters.
	Ω Walk( Is<Mid0052> auto& m, auto& v, uint16_t r )ι->void{
		if( r==1 ){
			v.Text( 0, m.Vin, 25 );
			return;
		}
		v.Text( 1, m.Vin, 25 );
		v.Text( 2, m.IdentifierPart2, 25 );
		v.Text( 3, m.IdentifierPart3, 25 );
		v.Text( 4, m.IdentifierPart4, 25 );
	}
	//Tables 108 to 114.
	Ω Walk( Is<Mid0061> auto& m, auto& v, uint16_t r )ι->void{
		v.Num( 1, m.CellId, 4 );
		v.Num( 2, m.ChannelId, 2 );
		v.Text( 3, m.ControllerName, 25 );
		v.Text( 4, m.Vin, 25 );
		if( r==1 ){
			v.Num( 5, m.JobId, 2 );
			v.Num( 6, m.Pset, 3 );
			v.Num( 7, m.BatchSize, 4 );
			v.Num( 8, m.BatchCounter, 4 );
			v.Num( 9, m.Status, 1 );
			v.Num( 10, m.TorqueStatus, 1 );
			v.Num( 11, m.AngleStatus, 1 );
			v.Scaled( 12, m.TorqueMin, 6 );
			v.Scaled( 13, m.TorqueMax, 6 );
			v.Scaled( 14, m.TorqueFinalTarget, 6 );
			v.Scaled( 15, m.Torque, 6 );
			v.Num( 16, m.AngleMin, 5 );
			v.Num( 17, m.AngleMax, 5 );
			v.Num( 18, m.AngleFinalTarget, 5 );
			v.Num( 19, m.Angle, 5 );
			v.Time( 20, m.Time );
			v.Time( 21, m.PsetChangeTime );
			v.Num( 22, m.BatchStatus, 1 );
			v.Num( 23, m.TighteningId, 10 );
			return;
		}
		v.Num( 5, m.JobId, 4 );
		v.Num( 6, m.Pset, 3 );
		v.Num( 7, m.Strategy, 2 );
		v.Num( 8, m.StrategyOptions, 5 );
		v.Num( 9, m.BatchSize, 4 );
		v.Num( 10, m.BatchCounter, 4 );
		v.Num( 11, m.Status, 1 );
		v.Num( 12, m.BatchStatus, 1 );
		v.Num( 13, m.TorqueStatus, 1 );
		v.Num( 14, m.AngleStatus, 1 );
		v.Num( 15, m.RundownAngleStatus, 1 );
		v.Num( 16, m.CurrentMonitoringStatus, 1 );
		v.Num( 17, m.SelfTapStatus, 1 );
		v.Num( 18, m.PrevailTorqueMonitoringStatus, 1 );
		v.Num( 19, m.PrevailTorqueCompensateStatus, 1 );
		v.Num( 20, m.TighteningErrorStatus, 10 );
		v.Scaled( 21, m.TorqueMin, 6 );
		v.Scaled( 22, m.TorqueMax, 6 );
		v.Scaled( 23, m.TorqueFinalTarget, 6 );
		v.Scaled( 24, m.Torque, 6 );
		v.Num( 25, m.AngleMin, 5 );
		v.Num( 26, m.AngleMax, 5 );
		v.Num( 27, m.AngleFinalTarget, 5 );
		v.Num( 28, m.Angle, 5 );
		v.Num( 29, m.RundownAngleMin, 5 );
		v.Num( 30, m.RundownAngleMax, 5 );
		v.Num( 31, m.RundownAngle, 5 );
		v.Num( 32, m.CurrentMonitoringMin, 3 );
		v.Num( 33, m.CurrentMonitoringMax, 3 );
		v.Num( 34, m.CurrentMonitoringValue, 3 );
		v.Scaled( 35, m.SelfTapMin, 6 );
		v.Scaled( 36, m.SelfTapMax, 6 );
		v.Scaled( 37, m.SelfTapTorque, 6 );
		v.Scaled( 38, m.PrevailTorqueMonitoringMin, 6 );
		v.Scaled( 39, m.PrevailTorqueMonitoringMax, 6 );
		v.Scaled( 40, m.PrevailTorque, 6 );
		v.Num( 41, m.TighteningId, 10 );
		v.Num( 42, m.JobSequenceNumber, 5 );
		v.Num( 43, m.SyncTighteningId, 5 );
		v.Text( 44, m.ToolSerialNumber, 14 );
		v.Time( 45, m.Time );
		v.Time( 46, m.PsetChangeTime );
		if( r>=3 ){
			v.Text( 47, m.PsetName, 25 );
			v.Num( 48, m.TorqueUnit, 1 );
			v.Num( 49, m.ResultType, 2 );
		}
		if( r>=4 ){
			v.Text( 50, m.IdentifierPart2, 25 );
			v.Text( 51, m.IdentifierPart3, 25 );
			v.Text( 52, m.IdentifierPart4, 25 );
		}
		if( r>=5 )
			v.Text( 53, m.CustomerErrorCode, 4 );
		if( r>=6 ){
			v.Scaled( 54, m.PrevailTorqueCompensateValue, 6 );
			v.Num( 55, m.TighteningErrorStatus2, 10 );
		}
		if( r>=7 ){
			v.Scaled( 56, m.CompensatedAngle, 7 );
			v.Scaled( 57, m.AngleDecimal, 7 );
		}
	}
	//Table 121.
	Ω Walk( Is<Mid0064> auto& m, auto& v, uint16_t )ι->void{
		v.Num( 0, m.TighteningId, 10 );
	}
	//Tables 123 to 129.
	Ω Walk( Is<Mid0065> auto& m, auto& v, uint16_t r )ι->void{
		v.Num( 1, m.TighteningId, 10 );
		v.Text( 2, m.Vin, 25 );
		if( r==1 ){
			v.Num( 3, m.Pset, 3 );
			v.Num( 4, m.BatchCounter, 4 );
			v.Num( 5, m.Status, 1 );
			v.Num( 6, m.TorqueStatus, 1 );
			v.Num( 7, m.AngleStatus, 1 );
			v.Scaled( 8, m.Torque, 6 );
			v.Num( 9, m.Angle, 5 );
			v.Time( 10, m.Time );
			v.Num( 11, m.BatchStatus, 1 );
			return;
		}
		v.Num( 3, m.JobId, 4 );
		v.Num( 4, m.Pset, 3 );
		v.Num( 5, m.Strategy, 2 );
		v.Num( 6, m.StrategyOptions, 5 );
		v.Num( 7, m.BatchSize, 4 );
		v.Num( 8, m.BatchCounter, 4 );
		v.Num( 9, m.Status, 1 );
		v.Num( 10, m.BatchStatus, 1 );
		v.Num( 11, m.TorqueStatus, 1 );
		v.Num( 12, m.AngleStatus, 1 );
		v.Num( 13, m.RundownAngleStatus, 1 );
		v.Num( 14, m.CurrentMonitoringStatus, 1 );
		v.Num( 15, m.SelfTapStatus, 1 );
		v.Num( 16, m.PrevailTorqueMonitoringStatus, 1 );
		v.Num( 17, m.PrevailTorqueCompensateStatus, 1 );
		v.Num( 18, m.TighteningErrorStatus, 10 );
		v.Scaled( 19, m.Torque, 6 );
		v.Num( 20, m.Angle, 5 );
		v.Num( 21, m.RundownAngle, 5 );
		v.Num( 22, m.CurrentMonitoringValue, 3 );
		v.Scaled( 23, m.SelfTapTorque, 6 );
		v.Scaled( 24, m.PrevailTorque, 6 );
		v.Num( 25, m.JobSequenceNumber, 5 );
		v.Num( 26, m.SyncTighteningId, 5 );
		v.Text( 27, m.ToolSerialNumber, 14 );
		v.Time( 28, m.Time );
		if( r>=3 ){
			v.Num( 29, m.TorqueUnit, 1 );
			v.Num( 30, m.ResultType, 2 );
		}
		if( r>=4 ){
			v.Text( 31, m.IdentifierPart2, 25 );
			v.Text( 32, m.IdentifierPart3, 25 );
			v.Text( 33, m.IdentifierPart4, 25 );
		}
		if( r>=5 )
			v.Text( 34, m.CustomerErrorCode, 4 );
		if( r>=6 ){
			v.Scaled( 35, m.PrevailTorqueCompensateValue, 6 );
			v.Num( 36, m.TighteningErrorStatus2, 10 );
		}
		if( r>=7 ){
			v.Num( 37, m.StationId, 10 );
			v.Text( 38, m.StationName, 25 );
		}
	}
	//Tables 157 to 159.  Table 159 gives the alarm text bytes 60 to 110 and 50 characters;  50 it is.
	Ω Walk( Is<Mid0071> auto& m, auto& v, uint16_t r )ι->void{
		v.Text( 1, m.ErrorCode, r==1 ? 4 : 5 );
		v.Num( 2, m.ControllerReady, 1 );
		v.Num( 3, m.ToolReady, 1 );
		v.Time( 4, m.Time );
		if( r>=3 ){
			v.Num( 5, m.ToolHealth, 1 );
			v.Text( 6, m.AlarmText, 50 );
		}
	}
	//Tables 160 and 161.
	Ω Walk( Is<Mid0074> auto& m, auto& v, uint16_t r )ι->void{
		v.Text( 0, m.ErrorCode, r==1 ? 4 : 5 );
	}
	//Tables 162 to 164.
	Ω Walk( Is<Mid0076> auto& m, auto& v, uint16_t r )ι->void{
		v.Num( 1, m.AlarmStatus, 1 );
		v.Text( 2, m.ErrorCode, r==1 ? 4 : 5 );
		v.Num( 3, m.ControllerReady, 1 );
		v.Num( 4, m.ToolReady, 1 );
		v.Time( 5, m.Time );
		if( r>=3 )
			v.Num( 6, m.ToolHealth, 1 );
	}
	//Table 167.
	Ω Walk( Is<Mid0081> auto& m, auto& v, uint16_t )ι->void{
		v.Time( 0, m.Time );
	}
	//Table 10.
	Ω Walk( Is<Mid9997> auto& m, auto& v, uint16_t )ι->void{
		v.Num( 0, m.Mid, 4 );
	}
	//Table 8.
	Ω Walk( Is<Mid9998> auto& m, auto& v, uint16_t )ι->void{
		v.Num( 0, m.Mid, 4 );
		v.Num( 0, m.Code, 4 );
	}
}