// Opc.PlcEmulator.jsonnet with data-quality faults (OPC 10000-4 7.38 StatusCodes) scheduled on the pumps - the stock
// config stays all-Good, which is what the soak and the walk-throughs expect.  See the README's "Status codes".
//
// One example of every shape the UI decodes - each severity, a Good sub-code, each kind of InfoBit - on a 2 min cycle
// with one scheduled fault at a time, so a Children table shows them in turn:
//
//    20-30 s  pump2       UncertainSensorNotAccurate - the value keeps moving, only its quality drops
//    30-35 s  pumpManual  Good+SemanticsChanged
//    40-55 s  pump3       BadSensorFailure - Bad holds the last reading from before the fault (the gateway's REST shape
//                         is the code in place of the value; the UI keeps the last value, dimmed and locked);  the ramp
//                         runs on underneath and the reading jumps to wherever it got to when the window closes
//    60-75 s  pump1       GoodLocalOverride - a Good sub-code:  the value is good, and an operator forced it
//    80-90 s  pump4       Good+Overflow
//    90-95 s  pumpManual  Good+StructureChanged
//  100-115 s  pump4       UncertainLastUsableValue+Constant - held, as the LastUsableValue codes are by default
//
// and all the while pump2.motorRpm (sine 800-1600) is read by a transmitter ranged 900-1500, so each peak and trough is
// published pinned at the range as UncertainEngineeringUnitsExceeded with LimitBits High/Low.
local every = "PT2M";
local quality = {
	"pump1.motorRpm":{
		quality:[ { status: "GoodLocalOverride", start: "PT60S", duration: "PT15S", every: every } ]
	},
	"pump2.motorRpm":{
		sensorMin: 900, sensorMax: 1500,
		quality:[ { status: "UncertainSensorNotAccurate", start: "PT20S", duration: "PT10S", every: every } ]
	},
	"pump3.motorRpm":{
		quality:[ { status: "BadSensorFailure", start: "PT40S", duration: "PT15S", every: every } ]
	},
	"pump4.motorRpm":{
		quality:[
			{ status: "Good", overflow: true, start: "PT80S", duration: "PT10S", every: every },
			{ status: "UncertainLastUsableValue", limit: "constant", start: "PT100S", duration: "PT15S", every: every }
		]
	},
	"pumpManual.motorRpm":{
		quality:[
			{ status: "Good", semanticsChanged: true, start: "PT30S", duration: "PT5S", every: every },
			{ status: "Good", structureChanged: true, start: "PT90S", duration: "PT5S", every: every }
		]
	}
};
(import 'Opc.PlcEmulator.jsonnet') + {
	emulator+:{
		devices: [ d + { tags: [ t + std.get(quality, d.path+"."+t.name, {}) for t in d.tags ] } for d in super.devices ]
	}
}
