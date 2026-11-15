// PLC 4, the Case packer, Packing (docs/demo/spec.md *The plant*):  a case of 24 every 24 seconds, 0.2 % rejects;  starved.
(import 'demo.libsonnet')( 4, {
	cycle: "PT24S", jitter: "PT3S", reject: 0.002,
	stops: [
		{ mean: "PT90M", min: "PT5M", max: "PT20M", code: 2 } //Starved
	],
	slow: []
} )