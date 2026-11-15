// PLC 5, the Palletizer, Packing (docs/demo/spec.md *The plant*):  a layer a minute, no rejects;  cleaning at the day shift's start,
// and an hour a day with no orders - in the night shift, the table naming no hour.
(import 'demo.libsonnet')( 5, {
	cycle: "PT60S", jitter: "PT5S", reject: 0,
	stops: [
		{ at: "06:00", "for": "PT30M", code: 11 }, //Cleaning
		{ at: "02:00", "for": "PT1H", code: 20 } //No orders
	],
	slow: []
} )