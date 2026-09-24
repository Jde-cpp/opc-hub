//Jde.Web.Tests: the HTTP and websocket client and server - sessions and their two expiries, body and message caps,
//CORS, TLS with a certificate generated into testing.certDir, JWTs, the proto exceptions.  No data source and no args/
//dir, so addJdeTest's `-include=args/sqlite -arg path=:memory:` is inert.  The server listens on 5005 (`/http/port`);
//the timeouts and caps are set small on purpose - each comment below names the test that needs it.
local logsDir = std.extVar("logsDir");
{
	testing:{
		tests:: "SocketTests.*",
		certDir: logsDir+'/web-tests/ssl'
	},
	logging:{
		spd:{
			tags:{
				default: "Information",
				test: "Trace",
				exception: "Trace",
				app: "Trace",
				"http.client.sessions": "Trace",
				"http.client.write": "Trace",
				"http.client.read": "Trace",
				"http.server.write": "Trace",
				"http.server.read": "Trace",
				"socket.client.write": "Trace",
				"socket.client.read": "Trace",
				"socket.server.write": "Trace",
				"socket.server.read": "Trace",
				settings: "Debug"
			},
			sinks:{
				console:{},
				file:{ path: logsDir, md: false }
			}
		},
		memory:{
			tags:{
				default: "Trace",
				locks: "Warning"
			}
		}
	},
	http:{
		address: null,
		port: 5005,
		# Rest session-expiry duration. WebTests.TestTimeout sleeps timeout+1s, so keep it short.
		timeout: "PT3S",
		# Websocket session-expiry duration.  A session is promoted to this the moment a socket
		# connects on it (Sessions.cpp UpdateExpiration), and SocketTests.SocketPromotesSessionTimeout
		# asserts the jump - so keep it far enough above timeout to be unambiguous.
		socketTimeout: "PT30S",
		maxLogLength: 31,
		# Http body cap.  WebTests.BodyLimit needs it below the 10000 default.
		bodyLimit: 8192,
		# Websocket message cap (Streams.cpp read_message_max) - its own setting since web-review3 #14.  Kept small here so
		# SocketTests.OversizeMessageClosesSocket can exceed it cheaply; SocketTests.EchoAttack sizes its echoes under it.
		socketMessageMax: 16384,
		accessControl: {
			allowOrigin: "sameHost",//any port on the host the client reached us by; "*" restores the wide-open default.
			allowMethods: "GET, POST, OPTIONS",
			allowHeaders: "Content-Type, Authorization"
		},
		ssl: {
			certificate:{
				path:: "{ApplicationDataFolder}/ssl/certs/server.pem",
				subjectAltName: "DNS:localhost,IP:127.0.0.1",
				company:: "Jde-Cpp",
				country:: "US",
				commonName: "web-tests"//subject CN - TLS clients match the SAN, not this.
			},
			privateKey:: "{ApplicationDataFolder}/ssl/private/server.pem",
			publicKey:: "{ApplicationDataFolder}/ssl/public/server.pem",
			dh:: "{ApplicationDataFolder}/ssl/dh.pem"
		}
	},
	web:{
		client:{
			# C6: SocketTests.BadTransmissionClient waits on a request the server answers with an unmatchable exception, so the
			# deadline is the only thing that ends it.  Short so the suite does not sit out the 60s default.
			socketRequestTimeout: "PT2S"
		}
	},
	workers:{
		executor: {threads: 2},
		drive: {threads: 1}
	}
}