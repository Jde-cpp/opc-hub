#include "helpers.h"

//A case per MID at each of its revisions, its frame written from the table each function names:  a row is the table's
//Byte column, its parameter id and the value's bytes, with the member the value is beside it.  Where a table's Byte
//column is a misprint the row says so and counts from the widths in its Value column.
namespace Jde::Opc::OpenProtocol::Tests{
	constexpr sv Vin{ "KPOL3456JKLO897" };

	//Tables 13 to 18.
	Ω mid0002( uint16_t r )ι->Case{
		Table<Mid0002> t{ r };
		t.Row( 21, 1, "0001" ); t.M.CellId = 1;
		t.Row( 27, 2, "02" ); t.M.ChannelId = 2;
		t.Row( 31, 3, "Airbag1", 25 ); t.M.ControllerName = "Airbag1";
		if( r==1 )
			t.Ends( 57 );
		if( r>=2 ){
			t.Row( 58, 4, "ACT" ); t.M.SupplierCode = "ACT";
		}
		if( r==2 )
			t.Ends( 62 );
		if( r>=3 ){
			t.Row( 63, 5, "2.20.1", 19 ); t.M.OpenProtocolVersion = "2.20.1";
			t.Row( 84, 6, "3.9.20", 19 ); t.M.ControllerSoftwareVersion = "3.9.20";
			t.Row( 105, 7, "1.4.2", 19 ); t.M.ToolSoftwareVersion = "1.4.2";
		}
		if( r==3 )
			t.Ends( 125 );
		if( r>=4 ){
			t.Row( 126, 8, "RBU Gold", 24 ); t.M.RbuType = "RBU Gold";
			t.Row( 152, 9, "C123456789" ); t.M.ControllerSerialNumber = "C123456789";
		}
		if( r==4 )
			t.Ends( 163 );
		if( r>=5 ){
			t.Row( 164, 10, "003" ); t.M.SystemType = 3;
			t.Row( 169, 11, "001" ); t.M.SystemSubtype = 1;
		}
		if( r==5 )
			t.Ends( 173 );
		if( r>=6 ){
			t.Row( 174, 12, "1" ); t.M.SequenceNumberSupport = 1;
			t.Row( 177, 13, "0" ); t.M.LinkingSupport = 0;
			t.Row( 180, 14, "4294967295" ); t.M.StationId = 4294967295;
			t.Row( 192, 15, "Station 7", 25 ); t.M.StationName = "Station 7";
			t.Row( 219, 16, "2" ); t.M.ClientId = 2;
			t.Ends( 221 );
		}
		return t.ToCase();
	}
	//Tables 21 and 22.
	Ω mid0004( uint16_t r )ι->Case{
		Table<Mid0004> t{ r };
		t.Value( 21, "0018" ); t.M.Mid = 18;
		if( r==1 ){
			t.Value( 25, "02" ); t.M.Code = 2;
			t.Ends( 26 );
		}else{
			t.Value( 25, "103" ); t.M.Code = 103;
			t.Ends( 27 );
		}
		return t.ToCase();
	}
	//Table 24.
	Ω mid0005()ι->Case{
		Table<Mid0005> t{ 1 };
		t.Value( 21, "0018" ); t.M.Mid = 18;
		t.Ends( 24 );
		return t.ToCase();
	}
	//Tables 38 to 40.
	Ω mid0015( uint16_t r )ι->Case{
		Table<Mid0015> t{ r };
		t.M.Pset = 12;
		t.M.PsetChangeTime = At( 2026, 3, 4, 5, 6, 7 );
		if( r==1 ){
			t.Value( 21, "012" );
			t.Value( 24, "2026-03-04:05:06:07" );
			t.Ends( 42 );
			return t.ToCase();
		}
		t.Row( 21, 1, "012" );
		t.Row( 26, 2, "M8 flange", 25 ); t.M.PsetName = "M8 flange";
		t.Row( 53, 3, "2026-03-04:05:06:07" );
		t.Row( 74, 4, "2" ); t.M.RotationDirection = 2;
		t.Row( 77, 5, "06" ); t.M.BatchSize = 6;
		t.Row( 81, 6, "001100" ); t.M.TorqueMin = 11;
		t.Row( 89, 7, "001300" ); t.M.TorqueMax = 13;
		t.Row( 97, 8, "001250" ); t.M.TorqueFinalTarget = 12.5;
		t.Row( 105, 9, "00030" ); t.M.AngleMin = 30;
		t.Row( 112, 10, "00090" ); t.M.AngleMax = 90;//Table 39 has 111-112 and 113-117, overlapping the angle min's 107-111.
		t.Row( 119, 11, "00060" ); t.M.AngleFinalTarget = 60;//118-119 in the table, and one less to its end.
		t.Row( 126, 12, "000400" ); t.M.FirstTarget = 4;
		t.Row( 134, 13, "000575" ); t.M.StartFinalAngle = 5.75;
		if( r==2 )
			t.Ends( 141 );//the table's last byte, which its six-digit start final angle reaches from 136, not its 135.
		if( r>=3 ){
			t.Row( 142, 14, "0042" ); t.M.SelectedIdentifierNumber = 42;
			t.Row( 148, 15, "Joint 15", 25 ); t.M.JointId = "Joint 15";
			t.Ends( 174 );
		}
		return t.ToCase();
	}
	//The Table 40 that is MID 0018's.
	Ω mid0018()ι->Case{
		Table<Mid0018> t{ 1 };
		t.Value( 21, "012" ); t.M.Pset = 12;
		t.Ends( 23 );
		return t.ToCase();
	}
	//Tables 69 to 74.
	Ω mid0035( uint16_t r )ι->Case{
		Table<Mid0035> t{ r };
		t.M.JobId = 7;
		t.M.JobStatus = 1;
		t.M.JobBatchMode = 0;
		t.M.JobBatchSize = 8;
		t.M.JobBatchCounter = 3;
		t.M.Time = At( 2001, 12, 1, 20, 12, 45 );
		if( r==1 ){
			t.Row( 21, 1, "07" );
			t.Row( 25, 2, "1" );
			t.Row( 28, 3, "0" );
			t.Row( 31, 4, "0008" );
			t.Row( 37, 5, "0003" );
			t.Row( 43, 6, "2001-12-01:20:12:45" );
			t.Ends( 63 );
			return t.ToCase();
		}
		t.Row( 21, 1, "0007" );
		t.Row( 27, 2, "1" );
		t.Row( 30, 3, "0" );
		t.Row( 33, 4, "0008" );
		t.Row( 39, 5, "0003" );
		t.Row( 45, 6, "2001-12-01:20:12:45" );
		if( r==2 )
			t.Ends( 65 );
		if( r>=3 ){
			t.Row( 66, 7, "002" ); t.M.JobCurrentStep = 2;
			t.Row( 71, 8, "005" ); t.M.JobTotalSteps = 5;
			t.Row( 76, 9, "01" ); t.M.JobStepType = 1;
		}
		if( r==3 )
			t.Ends( 79 );
		if( r>=4 ){
			t.Row( 80, 10, "11" ); t.M.JobTighteningStatus = 11;
		}
		if( r==4 )
			t.Ends( 83 );
		if( r>=5 ){
			t.Row( 84, 11, "65535" ); t.M.JobSequenceNumber = 65535;
			t.Row( 91, 12, Vin, 25 ); t.M.Vin = Vin;
			t.Row( 118, 13, "part 2", 25 ); t.M.IdentifierPart2 = "part 2";
			t.Row( 145, 14, "part 3", 25 ); t.M.IdentifierPart3 = "part 3";
			t.Row( 172, 15, "part 4", 25 ); t.M.IdentifierPart4 = "part 4";
		}
		if( r==5 )
			t.Ends( 198 );
		if( r>=6 ){
			t.Row( 199, 16, "Joint 16", 25 ); t.M.JointId = "Joint 16";
			t.Ends( 225 );
		}
		return t.ToCase();
	}
	//The Tables 74 and 75 that are MID 0038's.
	Ω mid0038( uint16_t r )ι->Case{
		Table<Mid0038> t{ r };
		if( r==1 ){
			t.Value( 21, "99" ); t.M.JobId = 99;
			t.Ends( 22 );
		}else{
			t.Value( 21, "9999" ); t.M.JobId = 9999;
			t.Ends( 24 );
		}
		return t.ToCase();
	}
	//Table 105:  the number alone, with no padding.
	Ω mid0050()ι->Case{
		Table<Mid0050> t{ 1 };
		t.Value( 21, Vin ); t.M.Vin = Vin;
		t.Ends( 35 );
		return t.ToCase();
	}
	//Tables 106 and 107.
	Ω mid0052( uint16_t r )ι->Case{
		Table<Mid0052> t{ r };
		t.M.Vin = Vin;
		if( r==1 ){
			t.Value( 21, Vin, 25 );
			t.Ends( 45 );
			return t.ToCase();
		}
		t.Row( 21, 1, Vin, 25 );
		t.Row( 48, 2, "part 2", 25 ); t.M.IdentifierPart2 = "part 2";
		t.Row( 75, 3, "part 3", 25 ); t.M.IdentifierPart3 = "part 3";//77-91 in Table 107, ten short of its 25 characters.
		t.Row( 102, 4, "part 4", 25 ); t.M.IdentifierPart4 = "part 4";//92-93 in the table.
		t.Ends( 128 );
		return t.ToCase();
	}
	//Tables 108 to 114.
	Ω mid0061( uint16_t r )ι->Case{
		Table<Mid0061> t{ r };
		auto& m = t.M;
		t.Row( 21, 1, "0001" ); m.CellId = 1;
		t.Row( 27, 2, "02" ); m.ChannelId = 2;
		t.Row( 31, 3, "airbag7", 25 ); m.ControllerName = "airbag7";
		t.Row( 58, 4, Vin, 25 ); m.Vin = Vin;
		if( r==1 ){//Table 108.
			t.Row( 85, 5, "05" ); m.JobId = 5;
			t.Row( 89, 6, "003" ); m.Pset = 3;
			t.Row( 94, 7, "0008" ); m.BatchSize = 8;
			t.Row( 100, 8, "0006" ); m.BatchCounter = 6;
			t.Row( 106, 9, "1" ); m.Status = 1;
			t.Row( 109, 10, "2" ); m.TorqueStatus = 2;
			t.Row( 112, 11, "0" ); m.AngleStatus = 0;
			t.Row( 115, 12, "000840" ); m.TorqueMin = 8.4;
			t.Row( 123, 13, "001400" ); m.TorqueMax = 14;
			t.Row( 131, 14, "001200" ); m.TorqueFinalTarget = 12;
			t.Row( 139, 15, "000739" ); m.Torque = 7.39;
			t.Row( 147, 16, "00010" ); m.AngleMin = 10;
			t.Row( 154, 17, "09999" ); m.AngleMax = 9999;
			t.Row( 161, 18, "00360" ); m.AngleFinalTarget = 360;
			t.Row( 168, 19, "00355" ); m.Angle = 355;
			t.Row( 175, 20, "2001-06-02:09:54:09" ); m.Time = At( 2001, 6, 2, 9, 54, 9 );
			t.Row( 196, 21, "2001-05-29:12:34:33" ); m.PsetChangeTime = At( 2001, 5, 29, 12, 34, 33 );
			t.Row( 217, 22, "3" ); m.BatchStatus = 3;
			t.Row( 220, 23, "0000345675" ); m.TighteningId = 345675;
			t.Ends( 231 );
			return t.ToCase();
		}
		//Table 109.
		t.Row( 85, 5, "0005" ); m.JobId = 5;
		t.Row( 91, 6, "003" ); m.Pset = 3;
		t.Row( 96, 7, "02" ); m.Strategy = 2;
		t.Row( 100, 8, "00003" ); m.StrategyOptions = 3;
		t.Row( 107, 9, "0008" ); m.BatchSize = 8;
		t.Row( 113, 10, "0006" ); m.BatchCounter = 6;
		t.Row( 119, 11, "0" ); m.Status = 0;
		t.Row( 122, 12, "3" ); m.BatchStatus = 3;
		t.Row( 125, 13, "2" ); m.TorqueStatus = 2;
		t.Row( 128, 14, "1" ); m.AngleStatus = 1;
		t.Row( 131, 15, "0" ); m.RundownAngleStatus = 0;
		t.Row( 134, 16, "1" ); m.CurrentMonitoringStatus = 1;
		t.Row( 137, 17, "2" ); m.SelfTapStatus = 2;
		t.Row( 140, 18, "0" ); m.PrevailTorqueMonitoringStatus = 0;
		t.Row( 143, 19, "1" ); m.PrevailTorqueCompensateStatus = 1;
		t.Row( 146, 20, "0000000004" ); m.TighteningErrorStatus = 4;
		t.Row( 158, 21, "000840" ); m.TorqueMin = 8.4;
		t.Row( 166, 22, "001400" ); m.TorqueMax = 14;
		t.Row( 174, 23, "001200" ); m.TorqueFinalTarget = 12;
		t.Row( 182, 24, "001523" ); m.Torque = 15.23;
		t.Row( 190, 25, "00010" ); m.AngleMin = 10;
		t.Row( 197, 26, "09999" ); m.AngleMax = 9999;
		t.Row( 204, 27, "00360" ); m.AngleFinalTarget = 360;
		t.Row( 211, 28, "00355" ); m.Angle = 355;
		t.Row( 218, 29, "00020" ); m.RundownAngleMin = 20;
		t.Row( 225, 30, "00720" ); m.RundownAngleMax = 720;
		t.Row( 232, 31, "00405" ); m.RundownAngle = 405;
		t.Row( 239, 32, "010" ); m.CurrentMonitoringMin = 10;
		t.Row( 244, 33, "120" ); m.CurrentMonitoringMax = 120;
		t.Row( 249, 34, "087" ); m.CurrentMonitoringValue = 87;
		t.Row( 254, 35, "000050" ); m.SelfTapMin = 0.5;
		t.Row( 262, 36, "000300" ); m.SelfTapMax = 3;
		t.Row( 270, 37, "000125" ); m.SelfTapTorque = 1.25;
		t.Row( 278, 38, "000020" ); m.PrevailTorqueMonitoringMin = 0.2;
		t.Row( 286, 39, "000250" ); m.PrevailTorqueMonitoringMax = 2.5;
		t.Row( 294, 40, "000110" ); m.PrevailTorque = 1.1;
		t.Row( 302, 41, "4294967295" ); m.TighteningId = 4294967295;
		t.Row( 314, 42, "00321" ); m.JobSequenceNumber = 321;
		t.Row( 321, 43, "00654" ); m.SyncTighteningId = 654;
		t.Row( 328, 44, "T0123456789", 14 ); m.ToolSerialNumber = "T0123456789";
		t.Row( 344, 45, "2001-06-02:09:54:09" ); m.Time = At( 2001, 6, 2, 9, 54, 9 );
		t.Row( 365, 46, "2001-05-29:12:34:33" ); m.PsetChangeTime = At( 2001, 5, 29, 12, 34, 33 );
		if( r==2 )
			t.Ends( 385 );
		if( r>=3 ){//Table 110.
			t.Row( 386, 47, "M8 flange", 25 ); m.PsetName = "M8 flange";
			t.Row( 413, 48, "1" ); m.TorqueUnit = 1;
			t.Row( 416, 49, "02" ); m.ResultType = 2;
		}
		if( r==3 )
			t.Ends( 419 );
		if( r>=4 ){//Table 111.
			t.Row( 420, 50, "part 2", 25 ); m.IdentifierPart2 = "part 2";
			t.Row( 447, 51, "part 3", 25 ); m.IdentifierPart3 = "part 3";
			t.Row( 474, 52, "part 4", 25 ); m.IdentifierPart4 = "part 4";
		}
		if( r==4 )
			t.Ends( 500 );
		if( r>=5 ){//Table 112.
			t.Row( 501, 53, "C042" ); m.CustomerErrorCode = "C042";
		}
		if( r==5 )
			t.Ends( 506 );
		if( r>=6 ){//Table 113.
			t.Row( 507, 54, "000015" ); m.PrevailTorqueCompensateValue = 0.15;
			t.Row( 515, 55, "0000131072" ); m.TighteningErrorStatus2 = 131072;
		}
		if( r==6 )
			t.Ends( 526 );
		if( r>=7 ){//Table 114.
			t.Row( 527, 56, "0035512" ); m.CompensatedAngle = 355.12;
			t.Row( 536, 57, "0035549" ); m.AngleDecimal = 355.49;
			t.Ends( 544 );
		}
		return t.ToCase();
	}
	//Table 121, at the revision of MID 0065 it asks for.
	Ω mid0064( uint16_t r )ι->Case{
		Table<Mid0064> t{ r };
		t.Value( 21, "0000345675" ); t.M.TighteningId = 345675;
		t.Ends( 30 );
		return t.ToCase();
	}
	//Tables 123 to 129.
	Ω mid0065( uint16_t r )ι->Case{
		Table<Mid0065> t{ r };
		auto& m = t.M;
		t.Row( 21, 1, "0000456789" ); m.TighteningId = 456789;
		t.Row( 33, 2, "AIRBAG", 25 ); m.Vin = "AIRBAG";
		if( r==1 ){//Table 123.
			t.Row( 60, 3, "001" ); m.Pset = 1;
			t.Row( 65, 4, "0002" ); m.BatchCounter = 2;
			t.Row( 71, 5, "0" ); m.Status = 0;
			t.Row( 74, 6, "2" ); m.TorqueStatus = 2;
			t.Row( 77, 7, "1" ); m.AngleStatus = 1;
			t.Row( 80, 8, "001467" ); m.Torque = 14.67;
			t.Row( 88, 9, "00046" ); m.Angle = 46;
			t.Row( 95, 10, "2001-04-22:14:54:34" ); m.Time = At( 2001, 4, 22, 14, 54, 34 );
			t.Row( 116, 11, "3" ); m.BatchStatus = 3;
			t.Ends( 118 );
			return t.ToCase();
		}
		//Table 124.
		t.Row( 60, 3, "0005" ); m.JobId = 5;
		t.Row( 66, 4, "001" ); m.Pset = 1;
		t.Row( 71, 5, "02" ); m.Strategy = 2;
		t.Row( 75, 6, "00003" ); m.StrategyOptions = 3;
		t.Row( 82, 7, "0008" ); m.BatchSize = 8;
		t.Row( 88, 8, "0002" ); m.BatchCounter = 2;
		t.Row( 94, 9, "0" ); m.Status = 0;
		t.Row( 97, 10, "3" ); m.BatchStatus = 3;
		t.Row( 100, 11, "2" ); m.TorqueStatus = 2;
		t.Row( 103, 12, "1" ); m.AngleStatus = 1;
		t.Row( 106, 13, "0" ); m.RundownAngleStatus = 0;
		t.Row( 109, 14, "1" ); m.CurrentMonitoringStatus = 1;
		t.Row( 112, 15, "2" ); m.SelfTapStatus = 2;
		t.Row( 115, 16, "0" ); m.PrevailTorqueMonitoringStatus = 0;
		t.Row( 118, 17, "1" ); m.PrevailTorqueCompensateStatus = 1;
		t.Row( 121, 18, "0000000004" ); m.TighteningErrorStatus = 4;
		t.Row( 133, 19, "001467" ); m.Torque = 14.67;
		t.Row( 141, 20, "00046" ); m.Angle = 46;
		t.Row( 148, 21, "00405" ); m.RundownAngle = 405;
		t.Row( 155, 22, "087" ); m.CurrentMonitoringValue = 87;
		t.Row( 160, 23, "000125" ); m.SelfTapTorque = 1.25;
		t.Row( 168, 24, "000110" ); m.PrevailTorque = 1.1;
		t.Row( 176, 25, "00321" ); m.JobSequenceNumber = 321;
		t.Row( 183, 26, "00654" ); m.SyncTighteningId = 654;
		t.Row( 190, 27, "T0123456789", 14 ); m.ToolSerialNumber = "T0123456789";
		t.Row( 206, 28, "2001-04-22:14:54:34" ); m.Time = At( 2001, 4, 22, 14, 54, 34 );
		if( r==2 )
			t.Ends( 226 );
		if( r>=3 ){//Table 125.
			t.Row( 227, 29, "8" ); m.TorqueUnit = 8;
			t.Row( 230, 30, "01" ); m.ResultType = 1;
		}
		if( r==3 )
			t.Ends( 233 );
		if( r>=4 ){//Table 126.
			t.Row( 234, 31, "part 2", 25 ); m.IdentifierPart2 = "part 2";
			t.Row( 261, 32, "part 3", 25 ); m.IdentifierPart3 = "part 3";
			t.Row( 288, 33, "part 4", 25 ); m.IdentifierPart4 = "part 4";
		}
		if( r==4 )
			t.Ends( 314 );
		if( r>=5 ){//Table 127.
			t.Row( 315, 34, "C042" ); m.CustomerErrorCode = "C042";
		}
		if( r==5 )
			t.Ends( 320 );
		if( r>=6 ){//Table 128.
			t.Row( 321, 35, "000015" ); m.PrevailTorqueCompensateValue = 0.15;
			t.Row( 329, 36, "0000131072" ); m.TighteningErrorStatus2 = 131072;
		}
		if( r==6 )
			t.Ends( 340 );
		if( r>=7 ){//Table 129.
			t.Row( 341, 37, "0000000002" ); m.StationId = 2;
			t.Row( 353, 38, "Station 2", 25 ); m.StationName = "Station 2";
			t.Ends( 379 );
		}
		return t.ToCase();
	}
	//Tables 157 to 159.
	Ω mid0071( uint16_t r )ι->Case{
		Table<Mid0071> t{ r };
		t.M.ControllerReady = 1;
		t.M.ToolReady = 0;
		t.M.Time = At( 2008, 6, 2, 10, 14, 26 );
		if( r==1 ){
			t.Row( 21, 1, "E404" ); t.M.ErrorCode = "E404";
			t.Row( 27, 2, "1" );
			t.Row( 30, 3, "0" );
			t.Row( 33, 4, "2008-06-02:10:14:26" );
			t.Ends( 53 );
			return t.ToCase();
		}
		t.Row( 21, 1, "E1021" ); t.M.ErrorCode = "E1021";
		t.Row( 28, 2, "1" );
		t.Row( 31, 3, "0" );
		t.Row( 34, 4, "2008-06-02:10:14:26" );
		if( r==2 )
			t.Ends( 54 );
		if( r>=3 ){
			t.Row( 55, 5, "2" ); t.M.ToolHealth = 2;
			t.Row( 58, 6, "Tool over temperature", 50 ); t.M.AlarmText = "Tool over temperature";
			t.Ends( 109 );//Table 159 has 60-110 for the text's 50 characters, which end at 109.
		}
		return t.ToCase();
	}
	//Tables 160 and 161.
	Ω mid0074( uint16_t r )ι->Case{
		Table<Mid0074> t{ r };
		if( r==1 ){
			t.Value( 21, "E406" ); t.M.ErrorCode = "E406";
			t.Ends( 24 );
		}else{
			t.Value( 21, "E1021" ); t.M.ErrorCode = "E1021";
			t.Ends( 25 );
		}
		return t.ToCase();
	}
	//Tables 162 to 164.
	Ω mid0076( uint16_t r )ι->Case{
		Table<Mid0076> t{ r };
		t.M.AlarmStatus = 1;
		t.M.ControllerReady = 0;
		t.M.ToolReady = 1;
		t.M.Time = At( 2008, 6, 2, 10, 14, 26 );
		t.Row( 21, 1, "1" );
		if( r==1 ){
			t.Row( 24, 2, "E404" ); t.M.ErrorCode = "E404";
			t.Row( 30, 3, "0" );
			t.Row( 33, 4, "1" );
			t.Row( 36, 5, "2008-06-02:10:14:26" );
			t.Ends( 56 );
			return t.ToCase();
		}
		t.Row( 24, 2, "E1021" ); t.M.ErrorCode = "E1021";
		t.Row( 31, 3, "0" );
		t.Row( 34, 4, "1" );
		t.Row( 37, 5, "2008-06-02:10:14:26" );
		if( r==2 )
			t.Ends( 57 );
		if( r>=3 ){
			t.Row( 58, 6, "1" ); t.M.ToolHealth = 1;
			t.Ends( 60 );
		}
		return t.ToCase();
	}
	//Table 167.
	Ω mid0081()ι->Case{
		Table<Mid0081> t{ 1 };
		t.Value( 21, "2026-11-12:06:30:00" ); t.M.Time = At( 2026, 11, 12, 6, 30, 0 );
		t.Ends( 39 );
		return t.ToCase();
	}
	//Table 10.
	Ω mid9997()ι->Case{
		Table<Mid9997> t{ 1 };
		t.Value( 21, "0061" ); t.M.Mid = 61;
		t.Ends( 24 );
		return t.ToCase();
	}
	//Table 8.
	Ω mid9998()ι->Case{
		Table<Mid9998> t{ 1 };
		t.Value( 21, "0060" ); t.M.Mid = 60;
		t.Value( 25, "0003" ); t.M.Code = 3;
		t.Ends( 28 );
		return t.ToCase();
	}

	//A MID with no data field is its header at each revision.
	Τ Ω addNoData( vector<Case>& y )ι->void{
		if constexpr( std::is_empty_v<T> ){
			for( uint16_t r=1; r<=T::MaxRevision; ++r )
				y.push_back( Table<T>{r}.ToCase() );
		}
	}
	template<class... T> Ω noData( std::type_identity<variant<T...>>, vector<Case>& y )ι->void{ ( addNoData<T>(y), ... ); }

	α Cases()ι->vector<Case>{
		vector<Case> y;
		noData( std::type_identity<Body>{}, y );
		let each = [&]( uint16_t last, Case (*revision)(uint16_t)ι ){
			for( uint16_t r=1; r<=last; ++r )
				y.push_back( revision(r) );
		};
		each( Mid0002::MaxRevision, mid0002 );
		each( Mid0004::MaxRevision, mid0004 );
		y.push_back( mid0005() );
		each( Mid0015::MaxRevision, mid0015 );
		y.push_back( mid0018() );
		each( Mid0035::MaxRevision, mid0035 );
		each( Mid0038::MaxRevision, mid0038 );
		y.push_back( mid0050() );
		each( Mid0052::MaxRevision, mid0052 );
		each( Mid0061::MaxRevision, mid0061 );
		each( Mid0064::MaxRevision, mid0064 );
		each( Mid0065::MaxRevision, mid0065 );
		each( Mid0071::MaxRevision, mid0071 );
		each( Mid0074::MaxRevision, mid0074 );
		each( Mid0076::MaxRevision, mid0076 );
		y.push_back( mid0081() );
		y.push_back( mid9997() );
		y.push_back( mid9998() );
		return y;
	}

	//The specification's example frames, by section.  Each is written to the length its own header gives, the PDF's
	//spacing being a layout's;  the header after the revision is spaces, as printed.  §5.8.6's MID 0065 example is left
	//out:  as printed its batch counter is three digits against Table 123's four and its own length of 118, and its last
	//bytes are no parameter 11.
	α Examples()ι->vector<Case>{
		vector<Case> y;
		let add = [&]( sv section, sv file, string frame, Header head, Body body ){
			head.Mid = MidOf( body );
			y.push_back( {Ƒ("{}'s MID {:04}", section, head.Mid), string{file}, move(frame), Message{head, move(body)}} );
		};
		add( "§2.2.1.1", "example-2.2.1.1-mid0071", ExampleFrame("00530071", "01E404021031042008-09-25:10:14:16"), {},
			Mid0071{.ErrorCode="E404", .ControllerReady=1, .ToolReady=1, .Time=At(2008, 9, 25, 10, 14, 16)} );
		//Communication start asking for MID 0002 at revision 3, its nine header bytes after the revision blank.
		add( "§5.2.1", "example-5.2.1-mid0001", ExampleFrame("00200001003", {}), {.Revision=3}, Mid0001{} );
		add( "§5.2.2", "example-5.2.2-mid0002", ExampleFrame("00570002", "010001020103"+Padded("Airbag1", 25)), {},
			Mid0002{.CellId=1, .ChannelId=1, .ControllerName="Airbag1"} );
		add( "§5.2.4", "example-5.2.4-mid0004", ExampleFrame("00260004", "001802"), {}, Mid0004{.Mid=18, .Code=2} );
		add( "§5.2.5", "example-5.2.5-mid0005", ExampleFrame("00240005", "0018"), {}, Mid0005{.Mid=18} );
		add( "§5.5.6", "example-5.5.6-mid0035", ExampleFrame("00630035", "0101020030040008050003062001-12-01:20:12:45"), {},
			Mid0035{.JobId=1, .JobStatus=0, .JobBatchMode=0, .JobBatchSize=8, .JobBatchCounter=3, .Time=At(2001, 12, 1, 20, 12, 45)} );
		//The tightening id is left-justified with spaces after it, against Table 3's zeroes on the left.
		add( "§5.8.2", "example-5.8.2-mid0061", ExampleFrame("023100610010", "010001020103"+Padded("airbag7", 25)+"04"+Padded(Vin, 25)
			+"05000600307000008000009010011112000840130014001400120015000739160000017099991800000"
			+"1900000202001-06-02:09:54:09212001-05-29:12:34:3322123345675    "), {},
			Mid0061{.CellId=1, .ChannelId=1, .ControllerName="airbag7", .Vin=string{Vin}, .JobId=0, .Pset=3, .BatchSize=0, .BatchCounter=0,
				.Status=0, .BatchStatus=1, .TorqueStatus=0, .AngleStatus=1, .TorqueMin=8.4, .TorqueMax=14, .TorqueFinalTarget=12, .Torque=7.39,
				.AngleMin=0, .AngleMax=9999, .AngleFinalTarget=0, .Angle=0, .TighteningId=345675, .Time=At(2001, 6, 2, 9, 54, 9),
				.PsetChangeTime=At(2001, 5, 29, 12, 34, 33)} );
		add( "§5.10.2", "example-5.10.2-mid0071", ExampleFrame("00530071", "01E404021031042008-06-02:10:14:26"), {},
			Mid0071{.ErrorCode="E404", .ControllerReady=1, .ToolReady=1, .Time=At(2008, 6, 2, 10, 14, 26)} );
		add( "§5.10.5", "example-5.10.5-mid0074", ExampleFrame("00240074", "E406"), {}, Mid0074{.ErrorCode="E406"} );
		add( "§5.10.7", "example-5.10.7-mid0076", ExampleFrame("00560076", "01102E404031041052008-06-02:10:14:26"), {},
			Mid0076{.AlarmStatus=1, .ErrorCode="E404", .ControllerReady=1, .ToolReady=1, .Time=At(2008, 6, 2, 10, 14, 26)} );
		return y;
	}
}