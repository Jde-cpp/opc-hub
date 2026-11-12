#include <jde/opc/openProtocol/Codec.h>

#define let const auto

namespace Jde::Opc{
namespace OpenProtocol{
	struct ErrorCode{ uint16_t Code; sv Name; };
	//Table 23, in its own words and its order.  Its second 102, "Illegal node type", is a misprint for a number it does
	//not give, and is left out.
	constexpr array<ErrorCode,95> Errors{{
		{0, "No Error"}, {1, "Invalid data"}, {2, "Parameter set ID not present"}, {3, "Parameter set can not be set."},
		{4, "Parameter set not running"}, {6, "VIN upload subscription already exists"},
		{7, "VIN upload subscription does not exists"}, {8, "VIN input source not granted"},
		{9, "Last tightening result subscription already exists"}, {10, "Last tightening result subscription does not exist"},
		{11, "Alarm subscription already exists"}, {12, "Alarm subscription does not exist"},
		{13, "Parameter set selection subscription already exists"}, {14, "Parameter set selection subscription does not exist"},
		{15, "Tightening ID requested not found"}, {16, "Connection rejected protocol busy"}, {17, "Job ID not present"},
		{18, "Job info subscription already exists"}, {19, "Job info subscription does not exist"}, {20, "Job can not be set"},
		{21, "Job not running"}, {22, "Not possible to execute dynamic Job request"}, {23, "Job batch decrement failed"},
		{24, "Not possible to create Pset"}, {25, "Programming control not granted"},
		{26, "Wrong tool type to Pset download connected"}, {27, "Tool is inaccessible"}, {28, "Job abortion is in progress"},
		{29, "Tool does not exist"}, {30, "Controller is not a sync Master/station controller"},
		{31, "Multi-spindle status subscription already exists"}, {32, "Multi-spindle status subscription does not exist"},
		{33, "Multi-spindle result subscription already exists"}, {34, "Multi-spindle result subscription does not exist"},
		{35, "Other master client already connected"}, {36, "Lock type not supported"},
		{40, "Job line control info subscription already exists"}, {41, "Job line control info subscription does not exist"},
		{42, "Identifier input source not granted"}, {43, "Multiple identifiers work order subscription already exists"},
		{44, "Multiple identifiers work order subscription does not exist"},
		{50, "Status external monitored inputs subscription already exists"},
		{51, "Status external monitored inputs subscription does not exist"}, {52, "IO device not connected"},
		{53, "Faulty IO device ID"}, {54, "Tool Tag ID unknown"}, {55, "Tool Tag ID subscription already exists"},
		{56, "Tool Tag ID subscription does not exist"}, {57, "Tool Motor tuning failed"}, {58, "No alarm present"},
		{59, "Tool currently in use"}, {60, "No histogram available"}, {61, "Pairing failed"}, {62, "Pairing denied"},
		{63, "Pairing or Pairing abortion attempt on wrong tooltype"}, {64, "Pairing abortion denied"},
		{65, "Pairing abortion failed"}, {66, "Pairing disconnection failed"}, {67, "Pairing in progress or already done"},
		{68, "Pairing denied. No Program Control"}, {69, "Unsupported extra data revision"}, {70, "Calibration failed"},
		{71, "Subscription already exists"}, {72, "Subscription does not exists"}, {73, "Subscribed MID unsupported"},
		{74, "Subscribed MID Revision unsupported"}, {75, "Requested MID unsupported"},
		{76, "Requested MID Revision unsupported"}, {77, "Requested on specific data not supported"},
		{78, "Subscription on specific data not supported"}, {79, "Command failed"},
		{80, "Audi emergency status subscription exists"}, {81, "Audi emergency status subscription does not exist"},
		{82, "Automatic/Manual mode subscribe already exist"}, {83, "Automatic/Manual mode subscribe does not exist"},
		{84, "The relay function subscription already exists"}, {85, "The relay function subscription does not exist"},
		{86, "The selector socket info subscription already exist"}, {87, "The selector socket info subscription does not exist"},
		{88, "The digin info subscription already exist"}, {89, "The digin info subscription does not exist"},
		{90, "Lock at batch done subscription already exist"}, {91, "Lock at batch done subscription does not exist"},
		{92, "Open protocol commands disabled"}, {93, "Open protocol commands disabled subscription already exists"},
		{94, "Open protocol commands disabled subscription does not exist"},
		{95, "Reject request, Power MACS is in manual mode"}, {96, "Reject connection, Client already connected"},
		{97, "MID revision unsupported"}, {98, "Controller internal request timeout"}, {99, "Unknown MID"},
		{100, "Illegal PID"}, {101, "Tightening in progress"}, {102, "Delete of object not possible"},
		{103, "Illegal program ID"}
	}};
	static_assert( std::ranges::is_sorted(Errors, {}, &ErrorCode::Code), "ErrorName searches Errors by code" );
	//Table 9.
	constexpr array<sv,4> LinkErrors{ "Invalid length", "Invalid revision", "Invalid sequence number", "Inconsistency of \"Number of messages\", \"Message number\"" };
}
	α OpenProtocol::ErrorName( uint16_t code )ι->sv{
		if( code>=900 && code<=999 )
			return "Reserved for application specific error codes";
		let found = std::ranges::lower_bound( Errors, code, {}, &ErrorCode::Code );
		return found!=Errors.end() && found->Code==code ? found->Name : sv{};
	}
	α OpenProtocol::LinkErrorName( uint16_t code )ι->sv{
		return code>=1 && code<=LinkErrors.size() ? LinkErrors[code-1] : sv{};
	}
}