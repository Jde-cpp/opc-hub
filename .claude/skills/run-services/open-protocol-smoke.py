"""Smoke client for the Open Protocol simulator on loopback (run-services skill, *Open Protocol simulator*):
MID 0001 from revision 6 stepping down on MID 0004 code 97 to a MID 0002, MID 0060 from 7 to a 0005, one tightening
posted at the simulator's HTTP API arriving as MID 0061, acked by 0062, then MID 0003.  `open-protocol-smoke.py [tcp] [http]`."""
import json, socket, sys, urllib.request

tcp = int( sys.argv[1] ) if len( sys.argv )>1 else 4545
http = int( sys.argv[2] ) if len( sys.argv )>2 else 8081

def frame( mid, rev, data=b"" ):
	return f"{20+len(data):04d}{mid:04d}{rev:03d}         ".encode() + data + b"\0"

def recv( s ):
	buf = b""
	while not buf.endswith( b"\0" ):
		chunk = s.recv( 4096 )
		if not chunk: raise SystemExit( "closed" )
		buf += chunk
	text = buf[:-1].decode( "latin-1" )
	mid, rev, data = int( text[4:8] ), int( text[8:11] ), text[20:]
	print( f"<- MID {mid:04d} rev {rev}: {text!r}" )
	return mid, rev, data

def ask( s, mid, rev, want ):
	"""Send mid at rev, stepping down on code 97 until the controller answers want."""
	while True:
		print( f"-> MID {mid:04d} rev {rev}" )
		s.sendall( frame( mid, rev ) )
		got, gotRev, data = recv( s )
		if got==want: return gotRev, data
		code = data[4:7] if got==4 and gotRev==2 else data[4:6]
		if got==4 and int( code )==97 and rev>1: rev -= 1; continue
		raise SystemExit( f"wanted MID {want:04d}, got {got:04d} {data!r}" )

s = socket.create_connection( ("127.0.0.1", tcp), timeout=5 )
rev2, identity = ask( s, 1, 6, 2 )
print( f"MID 0002 at revision {rev2}: {identity!r}" )
ask( s, 60, 7, 5 )
req = urllib.request.Request( f"http://127.0.0.1:{http}/simulate/tightening", data=json.dumps( {"torque": 12.5, "angle": 40.0, "ok": True} ).encode(), headers={"Content-Type": "application/json"} )
print( "POST /simulate/tightening ->", urllib.request.urlopen( req, timeout=5 ).status )
mid, rev61, data = recv( s )
if mid!=61: raise SystemExit( f"wanted MID 0061, got {mid:04d}" )
print( f"MID 0061 at revision {rev61}, {len( data )} data bytes" )
s.sendall( frame( 62, rev61 ) )#MIDs 0060 to 0063 are one family to the simulator:  it refuses a 0062 at a revision its 0060 would not take.
ask( s, 3, 1, 5 )
s.close()
print( "SMOKE PASS" )