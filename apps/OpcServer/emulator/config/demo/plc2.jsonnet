// PLC 2, the Capper, Bottling (docs/demo/spec.md *The plant*):  a bottle a second, 0.5 % rejects;  faults, and a slow half hour at
// 80 % speed each shift - four hours in, the table naming no hour.
(import 'demo.libsonnet')( 2, {
	cycle: "PT1S", jitter: "PT0.2S", reject: 0.005,
	stops: [
		{ mean: "PT2H", min: "PT5M", max: "PT15M", code: 3 } //Fault
	],
	slow: [
		{ at: "10:00", "for": "PT30M", factor: 1.25 }, { at: "18:00", "for": "PT30M", factor: 1.25 }, { at: "02:00", "for": "PT30M", factor: 1.25 }
	]
} )