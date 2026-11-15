// PLC 1, the Filler, Bottling (docs/demo/spec.md *The plant*):  a bottle a second, 1 % rejects;  jams, small stops with no code,
// and a changeover at each shift's start - the shifts run from 06:00, 14:00 and 22:00 in the plant's zone.
(import 'demo.libsonnet')( 1, {
	cycle: "PT1S", jitter: "PT0.2S", reject: 0.01,
	stops: [
		{ mean: "PT40M", min: "PT2M", max: "PT6M", code: 1 }, //Jam
		{ mean: "PT10M", min: "PT20S", max: "PT90S" }, //small stops, uncoded
		{ at: "06:00", "for": "PT20M", code: 10 }, { at: "14:00", "for": "PT20M", code: 10 }, { at: "22:00", "for": "PT20M", code: 10 } //Changeover
	],
	slow: []
} )