#pragma once

//A type per MID the driver speaks, each the union of its revisions' fields under the specification's names (Atlas Copco
//9836 4415 01, Release 2.20.1).  A number is nullopt and a text empty where the wire's field is all spaces, a value not
//supported or not defined (§2.3, Table 3), and where the frame's revision does not carry the field.  A text comes without
//its padding.  A torque is the wire's integer over 100, in the unit the result's TorqueUnit names.  A number's type is its
//field's range, a tightening id's ten digits a uint32_t, and the decoder refuses one past it.
namespace Jde::Opc::OpenProtocol{
	using LocalTime = std::chrono::local_seconds;//the wire's YYYY-MM-DD:HH:MM:SS as it stands, in no zone.

	//A MID with no data field, at revisions 1 to TMaxRevision.
	template<uint16_t TId, uint16_t TMaxRevision=1> struct NoData{
		static constexpr uint16_t Id{ TId };
		static constexpr uint16_t MaxRevision{ TMaxRevision };
		α operator==( const NoData& )Ι->bool = default;
	};

	using Mid0001 = NoData<1,6>;//Communication start, whose revisions 1 to 6 carry no data (§5.2.1).
	//Communication start acknowledge, Tables 13 to 18.
	struct Mid0002{
		static constexpr uint16_t Id{ 2 };
		static constexpr uint16_t MaxRevision{ 6 };
		optional<uint16_t> CellId;
		optional<uint8_t> ChannelId;
		string ControllerName;
		string SupplierCode;//revision 2.
		string OpenProtocolVersion;//revision 3.
		string ControllerSoftwareVersion;
		string ToolSoftwareVersion;
		string RbuType;//revision 4.
		string ControllerSerialNumber;
		optional<uint16_t> SystemType;//revision 5:  1 Power Focus 4000, 2 Power MACS 4000, 3 Power Focus 6000, 4 Micro Torque Focus 6000.
		optional<uint16_t> SystemSubtype;
		optional<uint8_t> SequenceNumberSupport;//revision 6:  1 where the controller numbers its messages.
		optional<uint8_t> LinkingSupport;
		optional<uint32_t> StationId;
		string StationName;
		optional<uint8_t> ClientId;
		α operator==( const Mid0002& )Ι->bool = default;
	};
	using Mid0003 = NoData<3>;//Communication stop.
	//Negative acknowledge, Tables 21 and 22:  the code is two digits at revision 1 and three at 2, and ErrorName names it.
	struct Mid0004{
		static constexpr uint16_t Id{ 4 };
		static constexpr uint16_t MaxRevision{ 2 };
		optional<uint16_t> Mid;
		optional<uint16_t> Code;
		α operator==( const Mid0004& )Ι->bool = default;
	};
	//Positive acknowledge, Table 24.
	struct Mid0005{
		static constexpr uint16_t Id{ 5 };
		static constexpr uint16_t MaxRevision{ 1 };
		optional<uint16_t> Mid;
		α operator==( const Mid0005& )Ι->bool = default;
	};

	using Mid0014 = NoData<14,3>;//Parameter set selected subscribe, at the revision of MID 0015 it asks for.
	//Parameter set selected, Tables 38 to 40.  Revision 1 carries the id and the time alone.
	struct Mid0015{
		static constexpr uint16_t Id{ 15 };
		static constexpr uint16_t MaxRevision{ 3 };
		optional<uint16_t> Pset;
		optional<LocalTime> PsetChangeTime;
		string PsetName;//revision 2.
		optional<uint8_t> RotationDirection;//1 CW, 2 CCW.
		optional<uint8_t> BatchSize;
		optional<double> TorqueMin;
		optional<double> TorqueMax;
		optional<double> TorqueFinalTarget;
		optional<uint32_t> AngleMin;
		optional<uint32_t> AngleMax;
		optional<uint32_t> AngleFinalTarget;
		optional<double> FirstTarget;
		optional<double> StartFinalAngle;
		optional<uint16_t> SelectedIdentifierNumber;//revision 3.
		string JointId;
		α operator==( const Mid0015& )Ι->bool = default;
	};
	using Mid0016 = NoData<16>;//Parameter set selected acknowledge.
	using Mid0017 = NoData<17>;//Parameter set selected unsubscribe.
	//Select parameter set, 000 to 999.
	struct Mid0018{
		static constexpr uint16_t Id{ 18 };
		static constexpr uint16_t MaxRevision{ 1 };
		optional<uint16_t> Pset;
		α operator==( const Mid0018& )Ι->bool = default;
	};

	using Mid0034 = NoData<34,6>;//Job info subscribe, at the revision of MID 0035 it asks for.
	//Job info, Tables 69 to 74.  The job id is two digits at revision 1 and four from 2.
	struct Mid0035{
		static constexpr uint16_t Id{ 35 };
		static constexpr uint16_t MaxRevision{ 6 };
		optional<uint16_t> JobId;
		optional<uint8_t> JobStatus;//0 not completed, 1 OK, 2 NOK, and from revision 4, 3 aborted and 4 restarted.
		optional<uint8_t> JobBatchMode;
		optional<uint16_t> JobBatchSize;
		optional<uint16_t> JobBatchCounter;
		optional<LocalTime> Time;
		optional<uint16_t> JobCurrentStep;//revision 3.
		optional<uint16_t> JobTotalSteps;
		optional<uint8_t> JobStepType;
		optional<uint8_t> JobTighteningStatus;//revision 4.
		optional<uint16_t> JobSequenceNumber;//revision 5.
		string Vin;
		string IdentifierPart2;
		string IdentifierPart3;
		string IdentifierPart4;
		string JointId;//revision 6.
		α operator==( const Mid0035& )Ι->bool = default;
	};
	using Mid0036 = NoData<36>;//Job info acknowledge.
	using Mid0037 = NoData<37>;//Job info unsubscribe.
	//Select job, Tables 74 and 75:  00 to 99 at revision 1, 0000 to 9999 at 2.
	struct Mid0038{
		static constexpr uint16_t Id{ 38 };
		static constexpr uint16_t MaxRevision{ 2 };
		optional<uint16_t> JobId;
		α operator==( const Mid0038& )Ι->bool = default;
	};

	using Mid0042 = NoData<42>;//Disable tool, every tool of the connection.
	using Mid0043 = NoData<43>;//Enable tool.

	//Vehicle id number download, Table 105:  the number as it is, 25 characters at most, with no padding.
	struct Mid0050{
		static constexpr uint16_t Id{ 50 };
		static constexpr uint16_t MaxRevision{ 1 };
		string Vin;
		α operator==( const Mid0050& )Ι->bool = default;
	};
	using Mid0051 = NoData<51,2>;//Vehicle id number subscribe, at the revision of MID 0052 it asks for.
	//Vehicle id number, Tables 106 and 107.
	struct Mid0052{
		static constexpr uint16_t Id{ 52 };
		static constexpr uint16_t MaxRevision{ 2 };
		string Vin;
		string IdentifierPart2;//revision 2.
		string IdentifierPart3;
		string IdentifierPart4;
		α operator==( const Mid0052& )Ι->bool = default;
	};
	using Mid0053 = NoData<53>;//Vehicle id number acknowledge.
	using Mid0054 = NoData<54>;//Vehicle id number unsubscribe.

	using Mid0060 = NoData<60,7>;//Last tightening result subscribe, at the revision of MID 0061 it asks for.
	//Last tightening result, Tables 108 to 114.  Revision 1 is a layout of its own and 2 another, which 3 to 7 each add to;
	//a member with no revision named is in every one.
	struct Mid0061{
		static constexpr uint16_t Id{ 61 };
		static constexpr uint16_t MaxRevision{ 7 };
		optional<uint16_t> CellId;
		optional<uint8_t> ChannelId;
		string ControllerName;
		string Vin;
		optional<uint16_t> JobId;
		optional<uint16_t> Pset;
		optional<uint8_t> Strategy;//revision 2.
		optional<uint32_t> StrategyOptions;//revision 2, the bit field's number.
		optional<uint16_t> BatchSize;
		optional<uint16_t> BatchCounter;
		optional<uint8_t> Status;//0 NOK, 1 OK.
		optional<uint8_t> BatchStatus;//0 NOK, 1 OK, 2 not used, 3 running.
		optional<uint8_t> TorqueStatus;//0 low, 1 OK, 2 high, as the statuses below.
		optional<uint8_t> AngleStatus;
		optional<uint8_t> RundownAngleStatus;//revision 2, to TighteningErrorStatus.
		optional<uint8_t> CurrentMonitoringStatus;
		optional<uint8_t> SelfTapStatus;
		optional<uint8_t> PrevailTorqueMonitoringStatus;
		optional<uint8_t> PrevailTorqueCompensateStatus;
		optional<uint32_t> TighteningErrorStatus;
		optional<double> TorqueMin;
		optional<double> TorqueMax;
		optional<double> TorqueFinalTarget;
		optional<double> Torque;
		optional<uint32_t> AngleMin;
		optional<uint32_t> AngleMax;
		optional<uint32_t> AngleFinalTarget;
		optional<uint32_t> Angle;
		optional<uint32_t> RundownAngleMin;//revision 2, to PrevailTorque.
		optional<uint32_t> RundownAngleMax;
		optional<uint32_t> RundownAngle;
		optional<uint16_t> CurrentMonitoringMin;
		optional<uint16_t> CurrentMonitoringMax;
		optional<uint16_t> CurrentMonitoringValue;
		optional<double> SelfTapMin;
		optional<double> SelfTapMax;
		optional<double> SelfTapTorque;
		optional<double> PrevailTorqueMonitoringMin;
		optional<double> PrevailTorqueMonitoringMax;
		optional<double> PrevailTorque;
		optional<uint32_t> TighteningId;
		optional<uint16_t> JobSequenceNumber;//revision 2, to ToolSerialNumber.
		optional<uint16_t> SyncTighteningId;
		string ToolSerialNumber;
		optional<LocalTime> Time;
		optional<LocalTime> PsetChangeTime;
		string PsetName;//revision 3.
		optional<uint8_t> TorqueUnit;//1 Nm, 2 Lbf.ft, 3 Lbf.In, 4 Kpm, 5 Kgf.cm, 6 ozf.in, 7 %, 8 Ncm.
		optional<uint8_t> ResultType;//1 tightening, 2 loosening, 3 batch increment, and on to 10.
		string IdentifierPart2;//revision 4.
		string IdentifierPart3;
		string IdentifierPart4;
		string CustomerErrorCode;//revision 5.
		optional<double> PrevailTorqueCompensateValue;//revision 6.
		optional<uint32_t> TighteningErrorStatus2;
		optional<double> CompensatedAngle;//revision 7.
		optional<double> AngleDecimal;//the final angle to two decimals.
		α operator==( const Mid0061& )Ι->bool = default;
	};
	using Mid0062 = NoData<62>;//Last tightening result acknowledge.
	using Mid0063 = NoData<63>;//Last tightening result unsubscribe.
	//Old tightening result upload request, Table 121, at the revision of MID 0065 it asks for:  id 0 is the latest.
	struct Mid0064{
		static constexpr uint16_t Id{ 64 };
		static constexpr uint16_t MaxRevision{ 7 };
		optional<uint32_t> TighteningId;
		α operator==( const Mid0064& )Ι->bool = default;
	};
	//Old tightening result upload reply, Tables 123 to 129:  a layout of its own and a subset of MID 0061's, with no
	//limits, no parameter set name and no time of its last change.  Its revisions are laid out as MID 0061's are.
	struct Mid0065{
		static constexpr uint16_t Id{ 65 };
		static constexpr uint16_t MaxRevision{ 7 };
		optional<uint32_t> TighteningId;
		string Vin;
		optional<uint16_t> JobId;//revision 2.
		optional<uint16_t> Pset;
		optional<uint8_t> Strategy;//revision 2.
		optional<uint32_t> StrategyOptions;//revision 2.
		optional<uint16_t> BatchSize;//revision 2.
		optional<uint16_t> BatchCounter;
		optional<uint8_t> Status;
		optional<uint8_t> BatchStatus;
		optional<uint8_t> TorqueStatus;
		optional<uint8_t> AngleStatus;
		optional<uint8_t> RundownAngleStatus;//revision 2, to ToolSerialNumber but for Torque and Angle.
		optional<uint8_t> CurrentMonitoringStatus;
		optional<uint8_t> SelfTapStatus;
		optional<uint8_t> PrevailTorqueMonitoringStatus;
		optional<uint8_t> PrevailTorqueCompensateStatus;
		optional<uint32_t> TighteningErrorStatus;
		optional<double> Torque;
		optional<uint32_t> Angle;
		optional<uint32_t> RundownAngle;
		optional<uint16_t> CurrentMonitoringValue;
		optional<double> SelfTapTorque;
		optional<double> PrevailTorque;
		optional<uint16_t> JobSequenceNumber;
		optional<uint16_t> SyncTighteningId;
		string ToolSerialNumber;
		optional<LocalTime> Time;
		optional<uint8_t> TorqueUnit;//revision 3.
		optional<uint8_t> ResultType;
		string IdentifierPart2;//revision 4.
		string IdentifierPart3;
		string IdentifierPart4;
		string CustomerErrorCode;//revision 5.
		optional<double> PrevailTorqueCompensateValue;//revision 6.
		optional<uint32_t> TighteningErrorStatus2;
		optional<uint32_t> StationId;//revision 7.
		string StationName;
		α operator==( const Mid0065& )Ι->bool = default;
	};

	using Mid0070 = NoData<70,3>;//Alarm subscribe, at the revision of MID 0071 and 0076 it asks for.
	//Alarm, Tables 157 to 159.  The error code is four characters at revision 1 and five from 2.
	struct Mid0071{
		static constexpr uint16_t Id{ 71 };
		static constexpr uint16_t MaxRevision{ 3 };
		string ErrorCode;
		optional<uint8_t> ControllerReady;//1 OK, 0 NOK.
		optional<uint8_t> ToolReady;
		optional<LocalTime> Time;
		optional<uint8_t> ToolHealth;//revision 3:  0 not applicable, 1 OK, 2 NOK.
		string AlarmText;
		α operator==( const Mid0071& )Ι->bool = default;
	};
	using Mid0072 = NoData<72>;//Alarm acknowledge.
	using Mid0073 = NoData<73>;//Alarm unsubscribe.
	//Alarm acknowledged on controller, Tables 160 and 161.
	struct Mid0074{
		static constexpr uint16_t Id{ 74 };
		static constexpr uint16_t MaxRevision{ 2 };
		string ErrorCode;
		α operator==( const Mid0074& )Ι->bool = default;
	};
	using Mid0075 = NoData<75>;//Alarm acknowledged on controller acknowledge.
	//Alarm status, Tables 162 to 164.
	struct Mid0076{
		static constexpr uint16_t Id{ 76 };
		static constexpr uint16_t MaxRevision{ 3 };
		optional<uint8_t> AlarmStatus;//1 where an alarm is active.
		string ErrorCode;
		optional<uint8_t> ControllerReady;
		optional<uint8_t> ToolReady;
		optional<LocalTime> Time;
		optional<uint8_t> ToolHealth;//revision 3.
		α operator==( const Mid0076& )Ι->bool = default;
	};
	using Mid0077 = NoData<77>;//Alarm status acknowledge.

	using Mid0080 = NoData<80>;//Read time upload request.
	//Read time upload reply, Table 167.
	struct Mid0081{
		static constexpr uint16_t Id{ 81 };
		static constexpr uint16_t MaxRevision{ 1 };
		optional<LocalTime> Time;
		α operator==( const Mid0081& )Ι->bool = default;
	};

	//Communication acknowledge at link level, Table 10.
	struct Mid9997{
		static constexpr uint16_t Id{ 9997 };
		static constexpr uint16_t MaxRevision{ 1 };
		optional<uint16_t> Mid;
		α operator==( const Mid9997& )Ι->bool = default;
	};
	//Communication acknowledge error, Table 8;  LinkErrorName names the code.
	struct Mid9998{
		static constexpr uint16_t Id{ 9998 };
		static constexpr uint16_t MaxRevision{ 1 };
		optional<uint16_t> Mid;
		optional<uint16_t> Code;
		α operator==( const Mid9998& )Ι->bool = default;
	};
	using Mid9999 = NoData<9999>;//Keep alive, which the controller mirrors.

	using Body = variant<
		Mid0001, Mid0002, Mid0003, Mid0004, Mid0005,
		Mid0014, Mid0015, Mid0016, Mid0017, Mid0018,
		Mid0034, Mid0035, Mid0036, Mid0037, Mid0038,
		Mid0042, Mid0043,
		Mid0050, Mid0051, Mid0052, Mid0053, Mid0054,
		Mid0060, Mid0061, Mid0062, Mid0063, Mid0064, Mid0065,
		Mid0070, Mid0071, Mid0072, Mid0073, Mid0074, Mid0075, Mid0076, Mid0077,
		Mid0080, Mid0081,
		Mid9997, Mid9998, Mid9999>;
}