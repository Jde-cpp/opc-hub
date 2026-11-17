#!/usr/bin/env python3
# The public demo's seed, its half through the hub's GraphQL (docs/demo/plan.md Phases 8 and 9).  seed.sh runs it, as root:  it
# logs in to the loopback hub as the seed's own certificate user, `demo-seed`, with the key `seed.sh certs` left for root alone.
# Every step looks its row up by slug first, so a second run changes nothing and a partial one is finished by the next.
#   seed.py access [--plcs <seconds>]   owner for the seed's user, the bundled server's and the flow server's connections purged, the guest, `visitor`, `demo-plc`,
#                                       every resource enforced;  --plcs waits that long for the five PLCs' first logins
#   seed.py plant [--plcs <seconds>] [--floor <file>]
#                                       the access half, then the plant, once the five PLCs have logged in, --plcs waiting 60 s for them by default:
#                                       the connections, the template and a group a machine, the reasons, the `plant` calendar, the machines, the
#                                       site with its two lines and the floor from floor.json beside this file
#   seed.py grant <email>               owner for the Google user of that address
#   seed.py session                     a session of the seed's user on stdout, for smoke.sh, which ends it
import argparse, base64, json, os, subprocess, sys, time, urllib.error, urllib.request

HUB = os.environ.get( 'JDE_HUB', 'http://127.0.0.1:1967' )
SSL = os.path.join( os.environ.get('JDE_DATA', '/var/lib/Jde-Cpp'), 'DemoSeed', 'ssl' )
SEED = 'demo-seed'
RIGHTS = { 'Create': 1, 'Read': 2, 'Update': 4, 'Delete': 8, 'Purge': 16, 'Administer': 32, 'Subscribe': 64, 'Execute': 128 } #libs/access/config/release.mutation's shifts
#What a visitor reads (docs/demo/spec.md *Guests*):  what the pages read with every resource enforced, and nothing else - no
#users, sessions or logs.  A name that is no resource is skipped and said:  the app's `connections` is gated by none today.
VISITOR = {
	'gateway': [ 'serverConnections', 'histTemplates', 'histTemplateNodes', 'histGroups', 'histGroupNodes', 'oeeCodings', 'oeeFloorShapes',
		'oeeFloors', 'oeeIntervals', 'oeeLineMembers', 'oeeLines', 'oeeMachineSignals', 'oeeMachines', 'oeeReasons', 'oeeStops' ],
	'app': [ 'connections', 'calendarExceptions', 'calendarPatterns', 'calendarShifts', 'calendars' ]
}
#Each PLC's server looks a certificate's user up as its own hub user (libs/opc/server/src/UAAccess.cpp), a read of `users`.
PLC = { 'access': ['users'] }
#The plant (docs/demo/spec.md *The plant*, *The plant's rows*).  PLC N serves machine N on opc.tcp 4841+N as urn:jde:demo:plc<N>
#(apps/OpcServer/emulator/config/demo):  its slug, its name, its line, its ideal cycle in milliseconds, plc<N>.jsonnet's `cycle`,
#and what it counts.
MACHINES = [
	( 'filler', 'Filler', 'bottling', 1000, 'bottles' ),
	( 'capper', 'Capper', 'bottling', 1000, 'bottles' ),
	( 'labeler', 'Labeler', 'bottling', 1000, 'bottles' ),
	( 'case-packer', 'Case packer', 'packing', 24000, 'cases' ),
	( 'palletizer', 'Palletizer', 'packing', 60000, 'layers' )
]
LINES = [ ( 'bottling', 'Bottling' ), ( 'packing', 'Packing' ) ]
#demo.NodeSet2.xml, which every PLC loads:  its namespace, by uri and never by index, its one Machine object, the same node on
#all five, and that object's variables, the template's members.  A machine's signals are four of them.
NSU, MACHINE = 'urn:jde:demo', 5001
VARIABLES = [ 'status', 'totalCount', 'rejectCount', 'reasonCode', 'speed' ]
SIGNALS = [ ( 'State', 'status' ), ( 'Total', 'totalCount' ), ( 'Reject', 'rejectCount' ), ( 'Reason', 'reasonCode' ) ]
SMALL_STOP = 120000 #two minutes:  a stop shorter than it is a small stop, a performance loss
#the reasons' codes are the overlays' stops' (plc<N>.jsonnet `code`)
REASONS = [
	( 'jam', 'Jam', 'Unplanned', 1 ), ( 'starved', 'Starved', 'Unplanned', 2 ), ( 'fault', 'Fault', 'Unplanned', 3 ),
	( 'changeover', 'Changeover', 'Planned', 10 ), ( 'cleaning', 'Cleaning', 'Planned', 11 ),
	( 'no-orders', 'No orders', 'Excluded', 20 )
]
#The calendar:  the plant's zone, the overlays' too (demo.libsonnet `zone`), a production day from 06:00, and three eight-hour
#shifts from it every day, each with half an hour's break four hours in.  A shift's start is minutes after the day's.
ZONE, DAY_START, EVERY_DAY = 'America/New_York', 360, 127
SHIFTS = [ ( 'Day', 0 ), ( 'Evening', 480 ), ( 'Night', 960 ) ]
SHIFT, BREAKS = 480, [ { 'start': 240, 'minutes': 30 } ]

def die( message ):
	print( f'seed: {message}', file=sys.stderr )
	sys.exit( 1 )
def say( message ): print( f'seed: {message}' )
def b64( b ): return base64.urlsafe_b64encode( b ).rstrip( b'=' ).decode()

#The certificate jwt the apps present (libs/app/client/appClient.cpp getJwt):  RS256 over {iat, host, sub, name, slug, x5c}, signed
#by the certificate's key, the certificate carried as x5c and checked against access.trustedCertDirs.  It carries no `exp`, so
#the hub takes it only fresh:  one is minted for each try.
def jwt( cert, key ):
	der = subprocess.check_output( ['openssl', 'x509', '-in', cert, '-outform', 'DER'] )
	head = b64( json.dumps({'alg': 'RS256', 'typ': 'JWT'}, separators=(',', ':')).encode() )
	body = b64( json.dumps({'iat': int(time.time()), 'host': '', 'sub': 0, 'name': SEED, 'slug': SEED, 'x5c': b64(der)}, separators=(',', ':')).encode() )
	signature = subprocess.check_output( ['openssl', 'dgst', '-sha256', '-sign', key, '-binary'], input=f'{head}.{body}'.encode() )
	return f'{head}.{body}.{b64(signature)}'

#The session id comes back in the response's Authorization header.  A hub that is not listening yet, just restarted, is waited for.
def login():
	cert, key = f'{SSL}/certs/{SEED}.pem', f'{SSL}/private/{SEED}.pem'
	if not os.path.isfile( cert ) or not os.access( key, os.R_OK ):
		die( f"no certificate at {cert}, or its key is not readable - `seed.sh certs` makes the pair, and root alone reads the key" )
	for _ in range( 60 ):
		request = urllib.request.Request( HUB+'/login', method='POST', headers={'Authorization': 'Bearer '+jwt(cert, key)} )
		try:
			with urllib.request.urlopen( request, timeout=30 ) as response:
				if session := response.headers.get( 'Authorization' ):
					return session
				die( 'the login answered no session' )
		except urllib.error.HTTPError as e:
			die( f"the hub refused the seed's login, {e.code} {e.read().decode(errors='replace')} - the hub runs the demo's config, and {SSL}/certs is among args/demo's access.trustedCertDirs?" )
		except OSError:
			time.sleep( 1 )
	die( f'nothing answers at {HUB} - systemctl status jde-opchub' )

class Hub:
	def __init__( self ): self.session = login()
	def ql( self, query, **variables ): #the answer's data
		request = urllib.request.Request( HUB+'/graphql', data=json.dumps({'query': query, 'variables': variables}).encode(), headers={'Content-Type': 'application/json', 'Authorization': self.session} )
		try:
			with urllib.request.urlopen( request, timeout=60 ) as response:
				answer = json.loads( response.read() )
		except urllib.error.HTTPError as e:
			die( f"{query} {variables}:  {e.code} {e.read().decode(errors='replace')}" )
		except OSError as e: #a timeout, or the hub gone:  restarted, or stopped
			die( f'{query} {variables}:  {e} - systemctl status jde-opchub' )
		if not isinstance( answer, dict ) or 'errors' in answer:
			die( f'{query} {variables}:  {answer}' )
		return answer['data']
	def logout( self ):
		try: urllib.request.urlopen( urllib.request.Request(HUB+'/logout', method='POST', headers={'Authorization': self.session}), timeout=30 ).close()
		except OSError: pass

	#the row of that slug, deleted or not, restored where it is deleted;  None where there is none
	def find( self, table, slug ):
		rows = self.ql( table+'( slug:$slug ){ id deleted }', slug=slug )[table]
		if rows and rows[0]['deleted']:
			self.ql( f'mutation restore{table[0].upper()}{table[1:-1]}( id:$id )', id=rows[0]['id'] )
			say( f"{table} '{slug}':  restored" )
		return rows[0]['id'] if rows else None
	def ensure( self, table, slug, name, description, **columns ):
		if id := self.find( table, slug ):
			return id
		return self.create( table, f"{table} '{slug}'", slug=slug, name=name, description=description, **columns )['id']
	#a table's rows under a filter, and a row made:  what a table with no slug is looked up and made by
	def rows( self, table, fields, **where ):
		return self.ql( f"{table}( {', '.join(f'{k}:${k}' for k in where)} ){{ {fields} }}", **where )[table]
	def create( self, table, what, **columns ):
		columns = { key: value for key, value in columns.items() if value is not None }
		row = self.ql( f"mutation create{table[0].upper()}{table[1:-1]}( {', '.join(f'{k}:${k}' for k in columns)} ){{ id }}", **columns )
		if what:
			say( f'{what}:  created' )
		return next( iter(row.values()) )
	def member( self, user, userName, role, roleName ):
		if any( r['role'] and r['role']['id']==role for r in self.ql('acl( identityId:$id ){ role{ id } }', id=user)['acl'] ):
			return
		self.ql( 'mutation createAcl( identity:{ id:$user }, role:{ id:$role } )', user=user, role=role )
		say( f"'{userName}' is in '{roleName}'" )
	#Read on each resource named, for those that exist:  addRole refuses a name that is none, 404, which would end the seed.
	def reads( self, role, wanted ):
		resources = { (r['schemaName'], r['slug']) for r in self.ql('resources{ schemaName slug criteria }')['resources'] if not r['criteria'] }
		has = { (p['resource']['schemaName'], p['resource']['slug']) for p in self.ql('roles( slug:$slug ){ permissionRights{ resource{ schemaName slug } } }', slug=role)['roles'][0]['permissionRights'] }
		for schema, slugs in wanted.items():
			for slug in slugs:
				if (schema, slug) not in resources:
					say( f"'{role}':  skipped {schema}/{slug}, no such resource" )
				elif (schema, slug) not in has:
					self.ql( 'mutation addRole( slug:$role, permissionRight:{ allowed:$allowed, denied:0, resource:{ schemaName:$schema, slug:$slug } } )', role=role, allowed=RIGHTS['Read'], schema=schema, slug=slug )
					say( f"'{role}' reads {schema}/{slug}" )

def owner( hub ):
	if id := hub.find( 'roles', 'owner' ):
		return id
	die( "the hub has no `owner` role:  its role seed, access.roles, did not apply - journalctl -u jde-opchub" )

#`demo-plc`, with the PLCs that have logged in in it, waiting that long for all five:  a PLC's user is enrolled by its first
#login, a few seconds into its unit's start.  Answers the PLCs that have not.
def plcs( hub, wait ):
	role = hub.ensure( 'roles', 'demo-plc', 'Demo PLC', "The public demo's PLCs:  Read on users, which each PLC's server looks a certificate's user up with, as its own." )
	hub.reads( 'demo-plc', PLC )
	waited = time.monotonic()+wait
	while time.monotonic()<waited and len( hub.ql('users( slug:$slugs ){ id }', slugs=[f'plc{n}' for n in range(1, 6)])['users'] )<5:
		time.sleep( 1 )
	missing = []
	for n in range( 1, 6 ):
		if plc := hub.find( 'users', f'plc{n}' ):
			hub.member( plc, f'plc{n}', role, 'demo-plc' )
		else:
			missing.append( n )
	return missing

def access( hub, plcWait ):
	#owner first, while every resource is unenforced:  the first grant, which the hub leaves to the first user (apps/OpcHub/README.md, First run).
	#The shipped role holds every right on every resource (libs/access/config/release.roles).
	hub.member( hub.find('users', SEED), SEED, owner(hub), 'owner' )
	#the package's bundled OPC UA server, whose unit the demo leaves disabled, and the flow server, which the package's first start
	#made a row for and the demo's config runs none of (Opc.Hub.Demo.jsonnet):  each connection would be a dead card
	for slug in ( 'OpcServer', 'flow-server' ):
		for connection in hub.ql( 'serverConnections( slug:$slug ){ id }', slug=slug )['serverConnections']:
			hub.ql( 'mutation purgeServerConnection( id:$id )', id=connection['id'] )
			say( f"connection '{slug}':  purged" )
	guest = hub.ensure( 'users', 'guest', 'Guest', "The public demo's visitor:  every session nobody signed in to (access.guest)." )
	visitor = hub.ensure( 'roles', 'visitor', 'Visitor', "The public demo's guest:  reads the plant's pages and changes nothing.  No users, sessions or logs, which Viewer reads." )
	hub.reads( 'visitor', VISITOR )
	hub.member( guest, 'guest', visitor, 'visitor' )
	missing = plcs( hub, plcWait )
	#last:  a resource's row restored is the resource enforced, and from here a call takes a grant
	for resource in hub.ql( 'resources{ id schemaName slug deleted }' )['resources']:
		if resource['deleted']:
			hub.ql( 'mutation restoreResource( id:$id )', id=resource['id'] )
			say( f"{resource['schemaName']}/{resource['slug']}:  enforced" )
	return missing #the PLCs not in `demo-plc`, which have not logged in yet

#A template member's variable, the last name of its browse path, which the column holds as json text.
def memberName( path ):
	path = json.loads( path ) if isinstance( path, str ) else path
	return path[-1]['name'] if path and path[-1].get( 'nsu' )==NSU else None

#A group's members by the template member each was made from.  The collector resolves them on the PLC, so they are there once
#its session is:  a group made before that is asked again.
def members( hub, group, slug, template ):
	waited = time.monotonic()+120
	while True:
		found = { m['histTemplateNodeId']: m['id'] for m in hub.rows('histGroupNodes', 'id histTemplateNodeId', histGroupId=group) }
		if not ( missing := [name for name, id in template.items() if id not in found] ):
			return { name: found[id] for name, id in template.items() }
		if time.monotonic()>=waited:
			die( f"group '{slug}' lacks {', '.join(missing)}:  the hub's collector has no session on its PLC - journalctl -u jde-opchub, journalctl -u 'jde-plc@*'" )
		time.sleep( 2 )
		hub.ql( 'mutation resolveHistory( group:$group ){ nodes{ status } }', group=group )

#The floor, floor.json's (docs/demo/spec.md *The floor*, its export format):  the editor's Import by hand, the shapes of each
#kind in the file's order.  A floor with no shape takes the file's;  one with any is the editor's, and kept, since a shape
#deleted on the page leaves a floor the file would complete.
def floor( hub, line, machines, lines, file ):
	try:
		with open( file ) as f:
			plan = json.load( f )
	except ( OSError, ValueError ) as e:
		die( f'{file}:  {e}' )
	wanted, count = [], {}
	for shape in plan['shapes']:
		kind, named = shape['kind'], {}
		if kind=='Machine':
			named = { 'oeeMachineId': machines.get(shape.get('machine')) }
		elif kind=='Line':
			named = { 'memberLineId': lines.get(shape.get('line')) }
		if None in named.values():
			die( f"{file}:  a {kind} shape names {shape.get('machine', shape.get('line'))!r}, which the plant has none of" )
		sort = count.get( kind, 0 )
		count[kind] = sort+1
		wanted.append( { 'kind': kind, **named, 'points': shape['points'], **({'label': shape['label']} if shape.get('label') else {}), 'sort': sort } )
	floors = hub.rows( 'oeeFloors', 'id', oeeLineId=line )
	id = floors[0]['id'] if floors else hub.create( 'oeeFloors', "the plant's floor", oeeLineId=line, width=plan['width'], height=plan['height'], grid=plan.get('grid', 0.5) )['id']
	if hub.rows( 'oeeFloorShapes', 'id', oeeFloorId=id ):
		say( f"the plant's floor has its shapes:  kept as the editor left it, {os.path.basename(file)} being for a floor with none" )
		return
	for shape in wanted:
		hub.create( 'oeeFloorShapes', None, oeeFloorId=id, **shape )
	say( f"the plant's floor:  {len(wanted)} shapes created" )

def plant( hub, plcWait, floorFile ):
	#The access half first:  the plant is the owner's to make, who holds every right on every resource enforced, and the PLCs are
	#in `demo-plc` before any group is made, the collector's session on a PLC being one that PLC looks up, as its own user.  The
	#plant makes no resource, a group's row being the seed's to add, so none is left for an access half after it to enforce.
	if missing := access( hub, plcWait ):
		die( f"{', '.join(f'plc{n}' for n in missing)} did not log in within {plcWait} s, and the plant takes all five:  systemctl status 'jde-plc@*', journalctl -u jde-plc@{missing[0]}" )
	connections = [ hub.ensure('serverConnections', f'plc{n}', f'PLC {n}', f"The {name}'s PLC, emulated on this box.", url=f'opc.tcp://127.0.0.1:{4841+n}', certificateUri=f'urn:jde:demo:plc{n}', isDefault=n==1)
		for n, ( _, name, *_ ) in enumerate( MACHINES, 1 ) ]
	#One template over the Machine's variables, with no threshold on any:  a member with no ExceptionDeviation stores every change,
	#which a count's must, a band holding its record back into a later interval (docs/oee/spec.md *Machines*).
	template = hub.ensure( 'histTemplates', 'machine', 'Machine', "A demo PLC's machine:  the five variables of its Machine object." )
	member = { memberName(m['path']): m['id'] for m in hub.rows('histTemplateNodes', 'id path', histTemplateId=template) }
	for name in VARIABLES:
		if name not in member:
			member[name] = hub.create( 'histTemplateNodes', f"template 'machine':  {name}", histTemplateId=template, path=json.dumps([{'nsu': NSU, 'name': name}], separators=(',', ':')) )['id']
	for slug, name, category, code in REASONS:
		hub.ensure( 'oeeReasons', slug, name, None, category=category, code=code )
	calendar = hub.ensure( 'calendars', 'plant', 'Plant', "The demo plant's shifts:  three of eight hours from 06:00, every day, each with a half-hour break.", timeZone=ZONE, dayStart=DAY_START )
	patterns = [ p['id'] for p in hub.rows('calendarPatterns', 'id effective deleted', calendarId=calendar) if not p['effective'] and not p['deleted'] ]
	if not patterns:
		die( "calendar 'plant' has no first pattern, which its create makes:  purge the calendar on the AppServer's Calendars tab and run this again" )
	#A pattern with any shift is the owner's, and kept:  the seed's shifts on one whose shifts were renamed or moved would overlap them.
	if not hub.rows( 'calendarShifts', 'id', calendarPatternId=patterns[0] ):
		for name, start in SHIFTS:
			hub.create( 'calendarShifts', f"calendar 'plant':  the {name} shift", calendarPatternId=patterns[0], name=name, days=EVERY_DAY, start=start, minutes=SHIFT, breaks=json.dumps(BREAKS, separators=(',', ':')) )
	#A machine a group, the template on its PLC's Machine.  A new machine's rows start now, `from`, to the second:  the box has
	#no history before its seed.  The group's machine is found by its group, a group having one, whatever its slug is now.
	machines = {}
	for n, ( slug, name, _, cycle, unit ) in enumerate( MACHINES, 1 ):
		group = hub.ensure( 'histGroups', slug, name, f"The {name}'s signals, from PLC {n}.", serverConnectionId=connections[n-1], histTemplateId=template, root=json.dumps({'nsu': NSU, 'i': MACHINE}, separators=(',', ':')) )
		node = members( hub, group, slug, {variable: member[variable] for _, variable in SIGNALS} )
		held = hub.rows( 'oeeMachines', 'slug', histGroupId=group )
		machines[slug] = hub.find( 'oeeMachines', held[0]['slug'] ) if held else hub.ensure( 'oeeMachines', slug, name, f"PLC {n}'s machine.", histGroupId=group, calendarId=calendar,
			idealCycle=cycle, countUnit=unit, smallStop=SMALL_STOP, **{'from': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())} )
		roles = { signal['role'] for signal in hub.rows('oeeMachineSignals', 'id role', oeeMachineId=machines[slug]) }
		for role, variable in SIGNALS: #State first:  a machine with a count and no State wants an idle timeout
			if role not in roles:
				for warning in hub.create( 'oeeMachineSignals', f"machine '{slug}':  {role} is {variable}", oeeMachineId=machines[slug], role=role, histGroupNodeId=node[variable] ).get( 'warnings' ) or []:
					say( f"machine '{slug}', {role}:  {warning}" )
	#The site and its two lines, each on the calendar:  a line with none keeps the UTC day for its shift.
	site = hub.ensure( 'oeeLines', 'plant', 'Plant', "The demo's site:  its two lines.", level='Site', calendarId=calendar )
	lines = { slug: hub.ensure('oeeLines', slug, name, None, level='Line', calendarId=calendar) for slug, name in LINES }
	def join( line, lineName, column, id, sort ):
		if not any( m[column]==id for m in hub.rows('oeeLineMembers', 'id oeeMachineId memberLineId', oeeLineId=line) ):
			hub.create( 'oeeLineMembers', f"line '{lineName}':  member {sort+1}", oeeLineId=line, sort=sort, **{column: id} )
	for sort, ( slug, _ ) in enumerate( LINES ):
		join( site, 'plant', 'memberLineId', lines[slug], sort )
		for at, machine in enumerate( m[0] for m in MACHINES if m[2]==slug ):
			join( lines[slug], slug, 'oeeMachineId', machines[machine], at )
	floor( hub, site, machines, lines, floorFile )

def grant( hub, email ):
	users = [ u for u in hub.ql('users{ id loginName provider deleted }')['users'] if u['provider']=='Google' and (u['loginName'] or '').lower()==email.lower() and not u['deleted'] ]
	if not users:
		die( f"no Google user '{email}':  it is enrolled by its first sign-in, through the tunnel (README.md, The first sign-in)" )
	hub.member( users[0]['id'], email, owner(hub), 'owner' )

if __name__=='__main__':
	parser = argparse.ArgumentParser( prog='seed.py' )
	commands = parser.add_subparsers( dest='command', required=True )
	commands.add_parser( 'access' ).add_argument( '--plcs', type=int, default=0, metavar='seconds' )
	planted = commands.add_parser( 'plant' )
	planted.add_argument( '--plcs', type=int, default=60, metavar='seconds' )
	planted.add_argument( '--floor', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'floor.json'), metavar='file' )
	commands.add_parser( 'grant' ).add_argument( 'email' )
	commands.add_parser( 'session' )
	args = parser.parse_args()
	hub = Hub()
	if args.command=='session':
		print( hub.session )
	else:
		try:
			if args.command=='access':
				for n in access( hub, args.plcs ):
					say( f"plc{n} has not logged in yet:  once jde-plc@{n} runs, `seed.sh access` again puts it in 'demo-plc'" )
			elif args.command=='plant':
				plant( hub, args.plcs, args.floor )
			else:
				grant( hub, args.email )
		finally:
			hub.logout()
		say( f'{args.command}:  done' )