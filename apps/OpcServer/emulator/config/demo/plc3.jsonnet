// PLC 3, the Labeler, Bottling (docs/demo/spec.md *The plant*):  a bottle a second, 3 % rejects;  jams, and a stop with no code
// about every three hours - of three to ten minutes, the table naming no length.
(import 'demo.libsonnet')( 3, {
	cycle: "PT1S", jitter: "PT0.2S", reject: 0.03,
	stops: [
		{ mean: "PT1H", min: "PT2M", max: "PT5M", code: 1 }, //Jam
		{ mean: "PT3H", min: "PT3M", max: "PT10M" } //uncoded
	],
	slow: []
} )